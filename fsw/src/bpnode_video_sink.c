#include "bpnode_video_sink.h"

#ifdef BPNODE_VIDEO_SINK_ENABLE

#include "bplib_cbor.h"
#include "bplib_mem.h"
#include "osapi-file.h"
#include "osapi-constants.h"
#include "osapi-error.h"

#ifndef BPNODE_VIDEO_SINK_PATH
#define BPNODE_VIDEO_SINK_PATH "/cf/moc-video.bin"
#endif

static osal_id_t VideoFile = OS_OBJECT_ID_UNDEFINED;
static pthread_mutex_t VideoLock = PTHREAD_MUTEX_INITIALIZER;

static BPLib_Status_t open_sink(void)
{
    int32 status;
    if (VideoFile != OS_OBJECT_ID_UNDEFINED)
        return BPLIB_SUCCESS;
    status = OS_OpenCreate(&VideoFile, BPNODE_VIDEO_SINK_PATH,
                           OS_FILE_FLAG_CREATE,
                           OS_WRITE_ONLY);
    if (status == OS_SUCCESS && OS_lseek(VideoFile, 0, OS_SEEK_END) < 0)
    {
        OS_close(VideoFile);
        VideoFile = OS_OBJECT_ID_UNDEFINED;
        return BPLIB_CLA_IO_ERROR;
    }
    return status == OS_SUCCESS ? BPLIB_SUCCESS : BPLIB_CLA_IO_ERROR;
}

BPLib_Status_t BPNode_VideoSink_Write(BPLib_Instance_t *inst,
                                      const void *bundle_data, size_t bundle_size)
{
    BPLib_Bundle_t *bundle;
    BPLib_BlockReader_t reader;
    uint8_t payload[1024];
    size_t got;
    int32 written;
    BPLib_Status_t status;

    if (inst == NULL || bundle_data == NULL || bundle_size == 0)
        return BPLIB_NULL_PTR_ERROR;
    pthread_mutex_lock(&VideoLock);
    status = open_sink();
    if (status != BPLIB_SUCCESS)
        goto done;

    bundle = BPLib_MEM_BundleAlloc(&inst->pool, bundle_data, bundle_size);
    if (bundle == NULL)
    {
        status = BPLIB_NO_STOR_ERR;
        goto done;
    }
    status = BPLib_CBOR_DecodeBundle(inst, bundle_data, bundle_size, bundle);
    if (status == BPLIB_SUCCESS)
    {
        status = BPLib_BlockReaderOpen(bundle, bundle->blocks.PayloadHeader.BlockNum, &reader);
        while (status == BPLIB_SUCCESS && reader.remaining != 0)
        {
            status = BPLib_BlockReaderRead(&reader, payload, sizeof(payload), &got);
            if (status != BPLIB_SUCCESS)
                break;
            size_t offset = 0;
            while (offset < got)
            {
                written = OS_write(VideoFile, payload + offset, got - offset);
                if (written <= 0) { status = BPLIB_CLA_IO_ERROR; break; }
                offset += (size_t)written;
            }
        }
    }
    BPLib_MEM_BundleFree(&inst->pool, bundle);
done:
    pthread_mutex_unlock(&VideoLock);
    return status;
}

void BPNode_VideoSink_Close(void)
{
    pthread_mutex_lock(&VideoLock);
    if (VideoFile != OS_OBJECT_ID_UNDEFINED)
    {
        OS_close(VideoFile);
        VideoFile = OS_OBJECT_ID_UNDEFINED;
    }
    pthread_mutex_unlock(&VideoLock);
}

#else

BPLib_Status_t BPNode_VideoSink_Write(BPLib_Instance_t *inst,
                                      const void *bundle_data, size_t bundle_size)
{
    (void)inst; (void)bundle_data; (void)bundle_size;
    return BPLIB_SUCCESS;
}

void BPNode_VideoSink_Close(void) {}

#endif
