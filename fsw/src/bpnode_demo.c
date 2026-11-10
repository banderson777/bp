#define _GNU_SOURCE
#include "bpnode_demo.h"
#include "bplib_bsl_security.h"
#include "bplib_ct_db.h"
#include "bplib_crc.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>

static unsigned node;
static char node_eid[32];
static uint8_t session[16];
static uint32_t next_record;
static uint32_t acknowledged_files;
static bool feed_done;
static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;
static CFE_ES_TaskId_t link_tasks[2];
static uint64_t sequence_id;
static unsigned feed_batch = 8;
static unsigned feed_delay_ms;
static uint64_t next_feed_ms;
static bool dropped_ccs;
static bool tampered_ciphertext;

static uint32_t get32(const uint8_t *p);

static BPLib_EID_t eid(unsigned number, unsigned service)
{
    BPLib_EID_t value = {0};
    value.Scheme = BPLIB_EID_SCHEME_IPN;
    value.IpnSspFormat = BPLIB_EID_IPN_SSP_FORMAT_TWO_DIGIT;
    value.Node = number; value.Service = service;
    return value;
}
static BPLib_EID_Pattern_t pattern(unsigned low, unsigned high, unsigned min_service, unsigned max_service)
{
    BPLib_EID_Pattern_t value = {0};
    value.Scheme = BPLIB_EID_SCHEME_IPN;
    value.IpnSspFormat = BPLIB_EID_IPN_SSP_FORMAT_TWO_DIGIT;
    value.MinNode = low; value.MaxNode = high;
    value.MinService = min_service; value.MaxService = max_service;
    return value;
}
static int read_file(const char *path, void *buffer, size_t capacity, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    *length = fread(buffer, 1, capacity, file);
    int result = ferror(file) ? -1 : 0;
    /* The EOF probe can itself raise an I/O error after an exact-fit read. */
    if (!result && (fgetc(file) != EOF || ferror(file))) result = -1;
    if (fclose(file)) result = -1;
    return result;
}
static int sync_dir(const char *path)
{
    int fd = open(path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) return -1;
    int result = fsync(fd);
    if (close(fd)) result = -1;
    return result;
}
static int write_file(const char *path, const void *data, size_t length, const char *directory)
{
    char temporary[256];
    if (snprintf(temporary, sizeof(temporary), "%s.tmp", path) >= (int)sizeof(temporary)) return -1;
    FILE *file = fopen(temporary, "wb");
    if (!file) return -1;
    int result = fwrite(data, 1, length, file) != length || fflush(file) || fsync(fileno(file)) ? -1 : 0;
    if (fclose(file)) result = -1;
    if (!result && rename(temporary, path)) result = -1;
    if (!result) result = sync_dir(directory);
    return result;
}

static BPLib_Status_t allocate_sequence(uint64_t *id)
{
    uint8_t encoded[8];
    if (sequence_id == UINT64_MAX) return BPLIB_ERROR;
    uint64_t next = sequence_id + 1;
    if (next == BPLIB_CT_SEQ_ID_ROLLOVER_VALUE) next++;
    for (unsigned i = 0; i < 8; ++i) encoded[7 - i] = (uint8_t)(next >> (i * 8));
    /* Reserve before publishing the identifier to a contact. A crash can waste
     * an identifier but cannot cause an old CCS to match a new transmission. */
    if (write_file("custody-sequence", encoded, sizeof(encoded), ".")) return BPLIB_ERROR;
    *id = sequence_id = next;
    return BPLIB_SUCCESS;
}

static int recover_custody(void)
{
    BPLib_Instance_t *instance = &BPNode_AppData.BplibInst;
    uint8_t encoded[8]; size_t length;
    if (!access("custody-sequence", F_OK))
    {
        if (read_file("custody-sequence", encoded, sizeof(encoded), &length) || length != sizeof(encoded)) return -1;
        for (unsigned i = 0; i < 8; ++i) sequence_id = (sequence_id << 8) | encoded[i];
    }
    else if (errno != ENOENT) return -1;
    instance->Ct.AllocateSequenceId = allocate_sequence;
    sqlite3_stmt *query = NULL;
    if (sqlite3_prepare_v2(instance->BundleStorage.db,
            "SELECT bundle_id FROM bundle_data WHERE is_custodial=1 AND egress_attempted=0",
            -1, &query, NULL) != SQLITE_OK) return -1;
    int result;
    while ((result = sqlite3_step(query)) == SQLITE_ROW)
    {
        uint32_t id = (uint32_t)sqlite3_column_int64(query, 0);
        BPLib_CT_DbEntry_t *entry = NULL;
        if (BPLib_CT_InitEntry(instance, id) != BPLIB_SUCCESS ||
            BPLib_CT_GetEntryFromCtdbWithId(&instance->Ct, id, &entry) != BPLIB_SUCCESS)
        { result = SQLITE_ERROR; break; }
        entry->State = BPLib_CT_InCustody;
        instance->Ct.BundleCountInCustody++;
    }
    sqlite3_finalize(query);
    if (result != SQLITE_DONE) return -1;
    if (sqlite3_exec(instance->BundleStorage.db,
            "UPDATE bundle_data SET retransmit_timestamp=0 WHERE is_custodial=1 AND egress_attempted=0",
            NULL, NULL, NULL) != SQLITE_OK) return -1;
    printf("DEMO node=%u recovered %u custody records\n", node, (unsigned)instance->Ct.BundleCountInCustody);
    return 0;
}

CFE_Status_t BPNode_DemoConfigure(void)
{
    const char *value = getenv("BPNODE_DEMO_NODE");
    if (!value || strlen(value) != 1 || value[0] < '1' || value[0] > '5') return CFE_ES_BAD_ARGUMENT;
    node = (unsigned)(value[0] - '0');
    snprintf(node_eid, sizeof(node_eid), "ipn:%u.0", node * 10);
    size_t length;
    if (read_file("session", session, sizeof(session), &length) || length != sizeof(session)) return CFE_ES_BAD_ARGUMENT;
    if (node == 1 || node == 5)
    {
        uint8_t key[32];
        if (read_file("video.key", key, sizeof(key), &length) || length != sizeof(key)) return CFE_ES_BAD_ARGUMENT;
        int status = BPLib_BSL_SecurityInit(key);
        memset(key, 0, sizeof(key));
        if (status) return CFE_ES_BAD_ARGUMENT;
    }
    /* Lab tables are configured once during TABLEP initialization, before NC
     * publishes them to workers. This mode does not accept live table updates. */
    BPLib_NC_ConfigPtrs_t *config = &BPNode_AppData.ConfigPtrs;
    config->MibPnConfigPtr->InstanceEID = eid(node * 10, 0);
    for (unsigned i = 0; i < BPLIB_MAX_NUM_CHANNELS; ++i)
    {
        config->ChanConfigPtr->Configs[i].AddAutomatically = (i == 0 && (node == 1 || node == 5));
        config->ChanConfigPtr->Configs[i].LocalServiceNumber = i + 1;
    }
    BPLib_PI_Config_t *channel = &config->ChanConfigPtr->Configs[0];
    channel->DestEID = eid(50, 1);
    channel->ReportToEID = eid(10, 0);
    channel->CustodyTransferBlkConfig.IncludeBlock = true;
    channel->AduWrapping = true;
    channel->AduUnwrapping = false;
    channel->Lifetime = 3600000;
    BPLib_EID_Pattern_t all = pattern(10, 50, 0, 1);
    for (size_t i = 0; i < sizeof(config->AuthConfigPtr->AuthSrcEIDs)/sizeof(all); ++i)
        config->AuthConfigPtr->AuthSrcEIDs[i] = all;
    for (size_t i = 0; i < sizeof(config->CustodyConfigPtr->AuthCustodySrc)/sizeof(all); ++i)
        config->CustodyConfigPtr->AuthCustodySrc[i] = pattern(10, 10, 1, 1);
    for (size_t i = 0; i < sizeof(config->CustodianConfigPtr->AuthCustodians)/sizeof(all); ++i)
        config->CustodianConfigPtr->AuthCustodians[i] = pattern(10, 50, 0, 0);
    for (size_t i = 0; i < sizeof(config->MibPsConfigPtr->Sources)/sizeof(config->MibPsConfigPtr->Sources[0]); ++i)
    {
        config->MibPsConfigPtr->Sources[i].SrcEIDs[0] = all;
        config->MibPsConfigPtr->Sources[i].Configs[0] = 3600000;
    }
    config->StorConfigPtr->StorageSet[0].SrcEIDs[0] = all;
    config->StorConfigPtr->StorageSet[0].StorageSize = 256 * 1024 * 1024;
    const char *batch = getenv("BPNODE_DEMO_FEED_BATCH");
    if (batch)
    {
        char *end;
        unsigned long parsed = strtoul(batch, &end, 10);
        if (!*batch || *end || parsed < 1 || parsed > 8) return CFE_ES_BAD_ARGUMENT;
        feed_batch = (unsigned)parsed;
    }
    const char *delay = getenv("BPNODE_DEMO_FEED_DELAY_MS");
    if (delay)
    {
        char *end;
        unsigned long parsed = strtoul(delay, &end, 10);
        if (!*delay || *end || parsed > 60000) return CFE_ES_BAD_ARGUMENT;
        feed_delay_ms = (unsigned)parsed;
    }
    unsigned base = 4600;
    const char *port_base = getenv("BPNODE_DEMO_PORT_BASE");
    if (port_base)
    {
        char *end;
        unsigned long parsed = strtoul(port_base, &end, 10);
        if (*end || parsed < 1024 || parsed > 65530) return CFE_ES_BAD_ARGUMENT;
        base = (unsigned)parsed;
    }
    for (unsigned contact = 0; contact < 2; ++contact)
    {
        BPLib_CLA_ContactsSet_t *entry = &config->ContactsConfigPtr->ContactSet[contact];
        memset(entry, 0, sizeof(*entry));
        entry->CLAType = BPLib_TCP_CLA;
        entry->RetransmitTimeout = 3000;
        entry->CSTimeTrigger = 1000;
        entry->CSSizeTrigger = BPLIB_MINIMUM_ENCODED_CCS_LEN + 10;
        entry->IngressBitsPerCycle = entry->EgressBitsPerCycle = 20000000;
        if (contact == 0 && node > 1)
        {
            strcpy(entry->ClaInAddr, "127.0.0.1"); entry->ClaInPort = base + node;
            /* Each hop sends its custody report back to its immediate peer. */
            entry->DestEIDs[0] = pattern((node - 1) * 10, (node - 1) * 10, 0, 0);
        }
        else
        {
            strcpy(entry->ClaOutAddr, "127.0.0.1"); entry->ClaOutPort = base + node + 1;
            entry->DestEIDs[0] = pattern(50, 50, 1, 1);
        }
    }
    printf("DEMO node=%u eid=%s configured; payload key=%s\n", node, node_eid,
           (node == 1 || node == 5) ? "loaded" : "absent");
    if (getenv("BPNODE_DEMO_DROP_CCS"))
        printf("DEMO node=%u one-shot CCS drop hook enabled\n", node);
    return CFE_SUCCESS;
}
const char *BPNode_DemoNodeEid(void) { return node_eid; }

static void link_loop(unsigned contact)
{
    BPLib_Instance_t *instance = &BPNode_AppData.BplibInst;
    const char *disabled = getenv("BPNODE_DEMO_DISABLE_CONTACT");
    if (disabled && strlen(disabled) == 1 && disabled[0] == '0' + contact)
    {
        printf("DEMO node=%u contact=%u disabled by fault injection\n", node, contact);
        for (;;) OS_TaskDelay(200);
    }
    BPLib_Status_t setup_status = BPLib_CLA_ContactSetup(instance, contact);
    printf("DEMO node=%u contact=%u setup=%d\n", node, contact, setup_status);
    if (setup_status != BPLIB_SUCCESS) goto failed;
    for (;;)
    {
        /* The lab does not depend on SCH_LAB. Generate the same wakeup message
         * locally so the five-process mission has one less app/table failure
         * surface and remains deterministic in isolated runtime directories. */
        if (contact == 0)
        {
            BPNode_DemoTick();
            /* The standalone lab omits SCH_LAB. Drive the same child work
             * notification directly from its contact timer. */
            BPNode_NotifSet(&BPNode_AppData.ChildStartWorkNotif);
        }
        BPLib_CLA_ContactRunState_t state;
        if (BPLib_CLA_GetContactRunState(contact, &state) != BPLIB_SUCCESS) goto failed;
        if (state == BPLIB_CLA_STARTED)
        {
            /* Socket liveness is observed under the transport lock. */
            BPLib_TCPCLA_t *cla = &BPNode_AppData.ClaInData[contact].TcpCla;
            pthread_mutex_lock(&cla->IoLock);
            bool closed = cla->SocketFd == BPLIB_INVALID_SOCKET;
            pthread_mutex_unlock(&cla->IoLock);
            if (closed) (void)BPLib_CLA_ContactStop(instance, contact);
        }
        else
        {
            BPLib_Status_t start_status = BPLib_CLA_ContactStart(instance, contact);
            if (start_status == BPLIB_SUCCESS)
                printf("DEMO node=%u contact=%u started\n", node, contact);
            else
                printf("DEMO node=%u contact=%u start=%d state=%d\n", node, contact, start_status, state);
        }
        OS_TaskDelay(200);
    }
failed:
    fprintf(stderr, "DEMO node=%u contact=%u setup failed\n", node, contact);
    CFE_ES_ExitChildTask();
}
static void link_zero(void) { link_loop(0); }
static void link_one(void) { link_loop(1); }
CFE_Status_t BPNode_DemoStart(void)
{
    if (mkdir("receipts", 0700) && errno != EEXIST) return CFE_ES_BAD_ARGUMENT;
    if (mkdir("output", 0700) && errno != EEXIST) return CFE_ES_BAD_ARGUMENT;
    if (sync_dir(".")) return CFE_ES_BAD_ARGUMENT;
    if (getenv("BPNODE_DEMO_STORAGE_FAIL"))
    {
        const char *fault_sql = "CREATE TRIGGER IF NOT EXISTS demo_storage_full "
            "BEFORE INSERT ON bundle_data BEGIN SELECT RAISE(FAIL, 'injected storage full'); END;";
        if (sqlite3_exec(BPNode_AppData.BplibInst.BundleStorage.db, fault_sql, NULL, NULL, NULL) != SQLITE_OK)
            return CFE_ES_BAD_ARGUMENT;
        printf("DEMO node=%u storage-full fault hook enabled\n", node);
    }
    if (recover_custody()) return CFE_ES_BAD_ARGUMENT;
    CFE_Status_t status = CFE_ES_CreateChildTask(&link_tasks[0], "DEMO_LINK0", link_zero, 0, 32768, 100, 0);
    printf("DEMO node=%u link0 create=%ld\n", node, (long)status);
    if (status == CFE_SUCCESS && node > 1 && node < 5)
    {
        status = CFE_ES_CreateChildTask(&link_tasks[1], "DEMO_LINK1", link_one, 0, 32768, 101, 0);
        printf("DEMO node=%u link1 create=%ld\n", node, (long)status);
    }
    return status;
}
void BPNode_DemoTick(void)
{
    if (node != 1 || feed_done) return;
    if (getenv("BPNODE_DEMO_FEED_GATE") && access("input/release", F_OK)) return;
    if (feed_delay_ms)
    {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now)) return;
        uint64_t now_ms = (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
        if (now_ms < next_feed_ms) return;
        next_feed_ms = now_ms + feed_delay_ms;
    }
    BPLib_CLA_ContactRunState_t state;
    if (BPLib_CLA_GetContactRunState(0, &state) != BPLIB_SUCCESS || state != BPLIB_CLA_STARTED) return;
    for (unsigned batch = 0; batch < feed_batch; ++batch)
    {
        char path[128]; uint8_t data[8192]; size_t length;
        snprintf(path, sizeof(path), "input/%08u.adu", next_record);
        if (access(path, F_OK))
        {
            if (errno == ENOENT)
            {
                if (access("input/stream", F_OK) || !access("input/eof", F_OK))
                {
                    feed_done = true;
                    printf("DEMO source queued %u records\n", next_record);
                }
            }
            else fprintf(stderr, "DEMO source input access failed\n");
            return;
        }
        if (read_file(path, data, sizeof(data), &length)) return;
        if (length == 24 && !memcmp(data, "BPF1", 4))
        {
            uint32_t file_number = get32(data + 20);
            if (memcmp(data + 4, session, 16) || file_number != acknowledged_files + 1)
            {
                fprintf(stderr, "DEMO source file marker invalid at record=%u\n", next_record);
                return;
            }
            if (write_file("input/file-ack", data + 20, 4, "input")) return;
            acknowledged_files = file_number;
            next_record++;
            printf("DEMO source accepted file=%u\n", file_number);
            continue;
        }
        BPLib_Status_t ingress_status = BPLib_PI_Ingress(&BPNode_AppData.BplibInst, 0, data, length);
        if (ingress_status != BPLIB_SUCCESS)
        {
            printf("DEMO source ingress=%d record=%u length=%lu\n", ingress_status, next_record, (unsigned long)length);
            return;
        }
        next_record++;
    }
}

static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

/* Shared receipt validation and segment publication for data and EOF replay.
 * Use the caller's scratch buffer rather than nesting another chunk-sized frame. */
static int persist_segment(uint32_t seg, uint32_t count, uint32_t segments,
                           uint8_t *previous, size_t capacity, bool require_all)
{
    char path[128];
    size_t previous_length;
    for (uint32_t i = 0; i < count; ++i)
    {
        snprintf(path, sizeof(path), "receipts/%06u-%05u.adu", seg, i);
        if (access(path, R_OK)) return errno == ENOENT && !require_all ? 0 : -1;
    }
    char output[128];
    char temporary[sizeof(output) + sizeof(".tmp") - 1];
    snprintf(output, sizeof(output), "output/ground-%06u.ts", seg);
    snprintf(temporary, sizeof(temporary), "%s.tmp", output);
    /* A replay must validate the published bytes without replacing the file.
     * Missing output is rebuilt from the durable authenticated receipts. */
    FILE *file = fopen(output, "rb");
    bool published = file != NULL;
    if (!published)
    {
        if (errno != ENOENT) return -1;
        file = fopen(temporary, "wb");
    }
    if (!file) return -1;
    int result = 0;
    for (uint32_t i = 0; i < count && !result; ++i)
    {
        snprintf(path, sizeof(path), "receipts/%06u-%05u.adu", seg, i);
        if (read_file(path, previous, capacity, &previous_length) || previous_length < 40 ||
            memcmp(previous, "BPD1", 4) || memcmp(previous + 4, session, 16) ||
            get32(previous + 20) != seg || get32(previous + 24) != i ||
            get32(previous + 28) != count ||
            (get32(previous + 32) != segments && get32(previous + 32) != 0) ||
            get32(previous + 36) != previous_length - 40) result = -1;
        else if (published)
        {
            /* stdio buffers this comparison; avoid another chunk-sized stack buffer. */
            for (size_t offset = 40; offset < previous_length && !result; ++offset)
                if (fgetc(file) != previous[offset]) result = -1;
        }
        else if (fwrite(previous + 40, 1, previous_length - 40, file) != previous_length - 40) result = -1;
    }
    if (published)
    {
        if (fgetc(file) != EOF || ferror(file)) result = -1;
    }
    else if (fflush(file) || fsync(fileno(file))) result = -1;
    if (fclose(file)) result = -1;
    if (!published && !result && rename(temporary, output)) result = -1;
    /* Existing output also needs this boundary: an earlier rename can survive
     * an fsync error even though the directory entry is not yet durable. */
    if (!result) result = sync_dir("output");
    if (!published && !result) printf("DEMO segment=%u persisted\n", seg);
    return result;
}

static int archive_video(uint32_t file_number, uint32_t first_segment, uint32_t count)
{
    char output[128];
    char temporary[sizeof(output) + sizeof(".tmp") - 1];
    uint8_t buffer[8192];
    if (!file_number || !count || first_segment >= 100000 || count > 100000 - first_segment) return -1;
    if (mkdir("output", 0700) && errno != EEXIST) return -1;
    if (mkdir("output/videos", 0700) && errno != EEXIST) return -1;
    if (sync_dir("output")) return -1;
    snprintf(output, sizeof(output), "output/videos/video-%06u.ts", file_number);
    snprintf(temporary, sizeof(temporary), "%s.tmp", output);

    FILE *archive = fopen(output, "rb");
    bool published = archive != NULL;
    if (!published)
    {
        if (errno != ENOENT) return -1;
        archive = fopen(temporary, "wb");
    }
    if (!archive) return -1;

    int result = 0;
    for (uint32_t index = 0; index < count && !result; ++index)
    {
        char segment_path[128];
        snprintf(segment_path, sizeof(segment_path), "output/ground-%06u.ts", first_segment + index);
        FILE *segment = fopen(segment_path, "rb");
        if (!segment)
        {
            result = -1;
            break;
        }
        size_t length;
        while (!result && (length = fread(buffer, 1, sizeof(buffer), segment)) > 0)
        {
            if (published)
            {
                for (size_t offset = 0; offset < length; ++offset)
                    if (fgetc(archive) != buffer[offset]) { result = -1; break; }
            }
            else if (fwrite(buffer, 1, length, archive) != length) result = -1;
        }
        if (ferror(segment)) result = -1;
        if (fclose(segment)) result = -1;
    }
    if (published)
    {
        if (fgetc(archive) != EOF || ferror(archive)) result = -1;
    }
    else if (fflush(archive) || fsync(fileno(archive))) result = -1;
    if (fclose(archive)) result = -1;
    if (!published && !result && rename(temporary, output)) result = -1;
    if (!result) result = sync_dir("output/videos");
    if (!published && !result) printf("DEMO video=%u archived segments=%u\n", file_number, count);
    return result;
}

/* Files contain the authenticated complete chunk record, retained for replay.
 * A successful return means receipts and any completed output have been synced. */
static int deliver(const uint8_t *data, size_t length)
{
    if (length < 20 || memcmp(data + 4, session, 16)) return -1;
    if (!memcmp(data, "BPE2", 4))
    {
        if (length != 32 || archive_video(get32(data + 20), get32(data + 24), get32(data + 28))) return -1;
        return 0;
    }
    if (length < 40) return -1;
    uint32_t seg = get32(data + 20), part = get32(data + 24), count = get32(data + 28);
    uint32_t segments = get32(data + 32), bytes = get32(data + 36);
    if (bytes != length - 40 || segments > 100000) return -1;
    char path[128];
    uint8_t previous[8192]; size_t previous_length;
    if (!memcmp(data, "BPE1", 4))
    {
        if (seg != segments || part || count || bytes) return -1;
        if (sync_dir("receipts")) return -1;
        /* Readability alone cannot establish completion. Validate every output
         * byte against consistent retained receipts, rebuilding missing output. */
        for (uint32_t i = 0; i < segments; ++i)
        {
            snprintf(path, sizeof(path), "receipts/%06u-%05u.adu", i, 0u);
            if (read_file(path, previous, sizeof(previous), &previous_length) || previous_length < 40 ||
                memcmp(previous, "BPD1", 4) || memcmp(previous + 4, session, 16) ||
                get32(previous + 20) != i || get32(previous + 24) != 0 ||
                (get32(previous + 32) != segments && get32(previous + 32) != 0) ||
                get32(previous + 36) != previous_length - 40)
                return -1;
            uint32_t parts = get32(previous + 28);
            if (!parts || parts > 65536 ||
                persist_segment(i, parts, segments, previous, sizeof(previous), true)) return -1;
        }
        if (!access("output/complete", F_OK))
        {
            if (read_file("output/complete", previous, sizeof(previous), &previous_length) ||
                previous_length != length || memcmp(previous, data, length)) return -1;
            /* A previous rename may have succeeded before directory sync failed. */
            return sync_dir("output");
        }
        if (write_file("output/complete", data, length, "output")) return -1;
        printf("DEMO complete: %u authenticated segments persisted\n", segments);
        return 0;
    }
    if (memcmp(data, "BPD1", 4) || seg >= 100000 || (segments && seg >= segments) ||
        !count || count > 65536 || part >= count) return -1;
    snprintf(path, sizeof(path), "receipts/%06u-%05u.adu", seg, part);
    if (!access(path, F_OK))
    {
        if (read_file(path, previous, sizeof(previous), &previous_length) || previous_length != length ||
            memcmp(data, previous, length)) return -1;
        /* Retry the durability boundary even when this receipt already exists. */
        if (sync_dir("receipts")) return -1;
    }
    else if (errno != ENOENT || write_file(path, data, length, "receipts")) return -1;
    return persist_segment(seg, count, segments, previous, sizeof(previous), false);
}

BPLib_Status_t BPNode_DemoWire(bool outgoing, void *data, size_t capacity, size_t *size)
{
    if ((outgoing && node != 1) || (!outgoing && node != 5)) return BPLIB_SUCCESS;
    BPLib_Instance_t *instance = &BPNode_AppData.BplibInst;
    BPLib_Bundle_t *bundle;
    BPLib_Status_t status;
    bundle = BPLib_MEM_BundleAlloc(&instance->pool, data, *size);
    if (!bundle) return BPLIB_NO_STOR_ERR;
    status = BPLib_CBOR_DecodeBundle(instance, data, *size, bundle);
    bool admin = status == BPLIB_SUCCESS && (bundle->blocks.PrimaryBlock.BundleProcFlags & 2);
    BPLib_MEM_BundleFree(&instance->pool, bundle);
    if ((outgoing && status != BPLIB_SUCCESS) || admin) return status;
    if (outgoing && getenv("BPNODE_DEMO_OMIT_BCB"))
    {
        printf("DEMO fault injection: omitted BCB at node=1\n");
        return BPLIB_SUCCESS;
    }
    uint8_t result[BPLIB_MAX_BUNDLE_LEN]; size_t length;
    if (BPLib_BSL_SecurityTransform(outgoing, data, *size, result, sizeof(result), &length) || length > capacity)
    {
        fprintf(stderr, "DEMO security rejected bundle at node=%u\n", node);
        return BPLIB_CLA_IO_ERROR;
    }
    if (outgoing && getenv("BPNODE_DEMO_TAMPER_CIPHERTEXT") && !tampered_ciphertext)
    {
        bundle = BPLib_MEM_BundleAlloc(&instance->pool, result, length);
        if (!bundle) return BPLIB_NO_STOR_ERR;
        status = BPLib_CBOR_DecodeBundle(instance, result, length, bundle);
        if (status != BPLIB_SUCCESS)
        {
            printf("DEMO fault injection: could not decode protected bundle status=%d\n", status);
            BPLib_MEM_BundleFree(&instance->pool, bundle);
            return BPLIB_CLA_IO_ERROR;
        }
        BPLib_CanBlockHeader_t *payload = &bundle->blocks.PayloadHeader;
        size_t block_size = payload->BlockOffsetEnd - payload->BlockOffsetStart + 1;
        size_t crc_size = payload->CrcType == BPLib_CRC_Type_CRC16 ? 2 :
                          payload->CrcType == BPLib_CRC_Type_CRC32C ? 4 : 0;
        if (payload->DataSize == 0 || !crc_size || block_size < crc_size)
        {
            printf("DEMO fault injection: invalid protected payload data=%lu crc=%lu block=%lu\n",
                   (unsigned long)payload->DataSize, (unsigned long)payload->CrcType, (unsigned long)block_size);
            BPLib_MEM_BundleFree(&instance->pool, bundle);
            return BPLIB_CLA_IO_ERROR;
        }
        result[payload->DataOffsetStart] ^= 1; /* Flip ciphertext, retaining a valid BP block CRC. */
        size_t crc_offset = payload->BlockOffsetEnd + 1 - crc_size;
        memset(result + crc_offset, 0, crc_size);
        BPLib_CRC_Val_t crc = BPLib_CRC_Calculate(result + payload->BlockOffsetStart, block_size,
                                                   (BPLib_CRC_Type_t)payload->CrcType);
        for (size_t i = 0; i < crc_size; ++i)
            result[crc_offset + i] = (uint8_t)(crc >> (8 * (crc_size - i - 1)));
        BPLib_MEM_BundleFree(&instance->pool, bundle);
        tampered_ciphertext = true;
        printf("DEMO fault injection: altered ciphertext with valid BP payload CRC at node=1\n");
    }
    if (!outgoing)
    {
        bundle = BPLib_MEM_BundleAlloc(&instance->pool, result, length);
        if (!bundle) return BPLIB_NO_STOR_ERR;
        status = BPLib_CBOR_DecodeBundle(instance, result, length, bundle);
        uint8_t payload[8192];
        if (status == BPLIB_SUCCESS)
            status = BPLib_MEM_CopyOutFromOffset(bundle, bundle->blocks.PayloadHeader.DataOffsetStart,
                                               bundle->blocks.PayloadHeader.DataSize, payload, sizeof(payload));
        if (status == BPLIB_SUCCESS)
        {
            pthread_mutex_lock(&file_lock);
            if (deliver(payload, bundle->blocks.PayloadHeader.DataSize)) status = BPLIB_CLA_IO_ERROR;
            pthread_mutex_unlock(&file_lock);
        }
        BPLib_MEM_BundleFree(&instance->pool, bundle);
        if (status != BPLIB_SUCCESS) return status;
    }
    memcpy(data, result, length); *size = length;
    return BPLIB_SUCCESS;
}

bool BPNode_DemoDropCcs(const void *data, size_t size)
{
    const char *hold_until = getenv("BPNODE_DEMO_HOLD_CCS_UNTIL");
    bool holding = hold_until != NULL && access(hold_until, F_OK) != 0;
    if (!data || !size || (!holding && (dropped_ccs || !getenv("BPNODE_DEMO_DROP_CCS")))) return false;
    BPLib_Instance_t *instance = &BPNode_AppData.BplibInst;
    BPLib_Bundle_t *bundle = BPLib_MEM_BundleAlloc(&instance->pool, (void *)data, size);
    if (!bundle) return false;
    BPLib_Status_t status = BPLib_CBOR_DecodeBundle(instance, data, size, bundle);
    bool is_ccs = status == BPLIB_SUCCESS && (bundle->blocks.PrimaryBlock.BundleProcFlags & 2);
    BPLib_MEM_BundleFree(&instance->pool, bundle);
    if (is_ccs)
    {
        if (holding)
            printf("DEMO fault injection: held CCS at node=%u\n", node);
        else
        {
            dropped_ccs = true;
            printf("DEMO fault injection: dropped one CCS at node=%u\n", node);
        }
        return true;
    }
    return false;
}
