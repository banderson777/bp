/* Local integration modifications (banderson777, 2026); see LOCAL_CHANGES.md. */
/*
 * NASA Docket No. GSC-19,559-1, and identified as "Delay/Disruption Tolerant Networking 
 * (DTN) Bundle Protocol (BP) v7 Core Flight System (cFS) Application Build 7.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may not use this 
 * file except in compliance with the License. You may obtain a copy of the License at 
 *
 * http://www.apache.org/licenses/LICENSE-2.0 
 *
 * Unless required by applicable law or agreed to in writing, software distributed under 
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF 
 * ANY KIND, either express or implied. See the License for the specific language 
 * governing permissions and limitations under the License. The copyright notice to be 
 * included in the software is as follows: 
 *
 * Copyright 2025 United States Government as represented by the Administrator of the 
 * National Aeronautics and Space Administration. All Rights Reserved.
 *
 */

/**
 * \file
 *   This file contains the source code for the BPNode App.
 */

/*
** Include Files
*/

#include "bpnode_app.h"
#ifdef BPNODE_ENABLE_FIVE_NODE_DEMO
#include "bpnode_demo.h"
#endif
#include "bpnode_eventids.h"
#include "bpnode_tbl.h"
#include "bpnode_version.h"

#include "bplib.h"
#include "fwp.h"

/*
** Global Data
*/

BPNode_AppData_t BPNode_AppData;


/*
** Function Definitions
*/

/* Application entry point and main process loop */
void BPNode_AppMain(void)
{
    CFE_Status_t     Status;
    CFE_SB_Buffer_t *BufPtr;

    /* Create the first Performance Log entry */
    BPLib_PL_PerfLogEntry(BPNODE_PERF_ID);

    /*
    ** Perform application-specific initialization
    ** If the initialization fails, set the RunStatus to
    ** CFE_ES_RunStatus_APP_ERROR and the app will not enter the run loop
    */
    Status = BPNode_AppInit();
#ifdef BPNODE_ENABLE_FIVE_NODE_DEMO
    if (Status == CFE_SUCCESS) Status = BPNode_DemoStart();
#endif
    if (Status != CFE_SUCCESS)
    {
        BPNode_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
    }

    /* BPNode run loop */
    while (CFE_ES_RunLoop(&BPNode_AppData.RunStatus) == true)
    {
        /* Performance Log Exit Stamp */
        BPLib_PL_PerfLogExit(BPNODE_PERF_ID);

        /* Pend on receipt of wakeup message */
        Status = CFE_SB_ReceiveBuffer(&BufPtr, BPNode_AppData.WakeupPipe,
                                                BPNODE_WAKEUP_PIPE_TIMEOUT);

        /* Performance Log Entry Stamp */
        BPLib_PL_PerfLogEntry(BPNODE_PERF_ID);

        if (Status == CFE_SUCCESS)
        {
            /* Process wakeup tasks */
            Status = BPNode_WakeupProcess();
        }

        /* Exit upon pipe read error */
        if (Status != CFE_SUCCESS && Status != CFE_SB_TIME_OUT && Status != CFE_SB_NO_MESSAGE)
        {
            BPLib_EM_SendEvent(BPNODE_PIPE_ERR_EID, BPLib_EM_EventType_ERROR,
                              "SB Pipe Read Error, App Will Exit");

            BPNode_AppData.RunStatus = CFE_ES_RunStatus_APP_ERROR;
        }
    }

    CFE_ES_ExitApp(BPNode_AppData.RunStatus);
}


/* Perform wakeup processing */
CFE_Status_t BPNode_WakeupProcess(void)
{
    CFE_Status_t     Status;
    BPLib_Status_t   BpStatus;
    CFE_SB_Buffer_t *BufPtr = NULL;

    /* Call NC to update configurations */
    BpStatus = BPLib_NC_ConfigUpdate(&BPNode_AppData.BplibInst);
    if (BpStatus != BPLIB_SUCCESS && BpStatus != BPLIB_TBL_UPDATED)
    {
        BPLib_EM_SendEvent(BPNODE_NC_CFG_UPDATE_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Error managing configurations on wakeup, Status=0x%08X",
                            BpStatus);
    }

    /* Update the ADUP configuration individually since it's owned by BPNode */
    BpStatus = BPA_TABLEP_TableUpdate(BPLIB_ADU_PROXY, (void**) &BPNode_AppData.AduProxyTablePtr);
    if (BpStatus != BPLIB_SUCCESS && BpStatus != BPLIB_TBL_UPDATED)
    {
        BPLib_EM_SendEvent(BPNODE_TBL_ADDR_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Error managing the configuration: ADUProxyTable on wakeup, Status=0x%08X",
                            BpStatus);
    }

    /* Check for pending commands */
    do
    {
        Status = CFE_SB_ReceiveBuffer(&BufPtr, BPNode_AppData.CommandPipe, CFE_SB_POLL);

        if (Status == CFE_SUCCESS && BufPtr != NULL)
        {
            BPA_DP_TaskPipe(BufPtr);
        }
    } while (Status == CFE_SUCCESS);

    /* Not an error case */
    if (Status == CFE_SB_NO_MESSAGE)
    {
        Status = CFE_SUCCESS;
    }

#ifdef BPNODE_ENABLE_FIVE_NODE_DEMO
    BPNode_DemoTick();
#endif
    /* Tell child tasks to start processing */
    BPNode_NotifSet(&BPNode_AppData.ChildStartWorkNotif);

    return Status;
}

/* App initialization activities */
CFE_Status_t BPNode_AppInit(void)
{
    CFE_Status_t   Status;
    BPLib_Status_t BpStatus;
    int32          OsStatus;
    uint8          i;
    CFE_SB_Qos_t   PipeQOS = {0, 0};

    BPLib_FWP_ProxyCallbacks_t Callbacks = {
        /* Time Proxy */
        .BPA_TIMEP_GetMonotonicTime          = BPA_TIMEP_GetMonotonicTime,
        .BPA_TIMEP_GetHostEpoch              = BPA_TIMEP_GetHostEpoch,
        .BPA_TIMEP_GetHostClockState         = BPA_TIMEP_GetHostClockState,
        .BPA_TIMEP_GetHostTime               = BPA_TIMEP_GetHostTime,
        /* Perf Log Proxy */
        .BPA_PERFLOGP_Entry                  = BPA_PERFLOGP_Entry,
        .BPA_PERFLOGP_Exit                   = BPA_PERFLOGP_Exit,
        /* Table Proxy */
        .BPA_TABLEP_TableInit                = BPA_TABLEP_TableInit,
        .BPA_TABLEP_TableUpdate              = BPA_TABLEP_TableUpdate,
        /* Event Proxy */
        .BPA_EVP_Init                        = BPA_EVP_Init,
        .BPA_EVP_SendEvent                   = BPA_EVP_SendEvent,
        /* ADU Proxy */
        .BPA_ADUP_AddApplication             = BPA_ADUP_AddApplication,
        .BPA_ADUP_StartApplication           = BPA_ADUP_StartApplication,
        .BPA_ADUP_StopApplication            = BPA_ADUP_StopApplication,
        .BPA_ADUP_RemoveApplication          = BPA_ADUP_RemoveApplication,
        /* Telemetry Proxy */
        .BPA_TLMP_SendNodeMibConfigPkt       = BPA_TLMP_SendNodeMibConfigPkt,
        .BPA_TLMP_SendPerSourceMibConfigPkt  = BPA_TLMP_SendPerSourceMibConfigPkt,
        .BPA_TLMP_SendNodeMibCounterPkt      = BPA_TLMP_SendNodeMibCounterPkt,
        .BPA_TLMP_SendPerSourceMibCounterPkt = BPA_TLMP_SendPerSourceMibCounterPkt,
        .BPA_TLMP_SendNodeMibReportsPkt      = BPA_TLMP_SendNodeMibReportsPkt,
        .BPA_TLMP_SendChannelContactPkt      = BPA_TLMP_SendChannelContactPkt,
        .BPA_TLMP_SendStoragePkt             = BPA_TLMP_SendStoragePkt,
        /* CLA Proxy */
        .BPA_CLAP_ContactSetup               = BPA_CLAP_ContactSetup,
        .BPA_CLAP_ContactStart               = BPA_CLAP_ContactStart,
        .BPA_CLAP_ContactStop                = BPA_CLAP_ContactStop,
        .BPA_CLAP_ContactTeardown            = BPA_CLAP_ContactTeardown,
    };

    /* Zero out the global data structure */
    CFE_PSP_MemSet(&BPNode_AppData, 0, sizeof(BPNode_AppData));

    BPNode_AppData.MemPool = malloc(BPNODE_MEM_POOL_LEN);

    if (BPNode_AppData.MemPool == NULL)
    {
        CFE_ES_WriteToSysLog("Failed to malloc the memory pool\n");

        return BPLIB_NULL_PTR_ERROR;
    }

    /* Initialize configurations and counters */
    BpStatus = BPLib_NC_Init(&BPNode_AppData.ConfigPtrs, &Callbacks, &BPNode_AppData.BplibInst, (uint16) BPNODE_MAX_UNSORTED_JOBS,
                             BPNode_AppData.MemPool, BPNODE_MEM_POOL_LEN);

    if (BpStatus != BPLIB_SUCCESS)
    {
        if (BpStatus == BPLIB_NC_FWP_INIT_ERR || BpStatus == BPLIB_NC_EM_INIT_ERR)
        {
            CFE_ES_WriteToSysLog("Error initializing BPLib, RC = %d\n",
                                    BpStatus);
        }
        else
        {
            BPLib_EM_SendEvent(BPNODE_BPLIB_INIT_ERR_EID, BPLib_EM_EventType_ERROR,
                                "Error initializing BPLib, RC = %d",
                                BpStatus);
        }

        return BpStatus;
    }

    /* Create command pipe */
    Status = CFE_SB_CreatePipe(&BPNode_AppData.CommandPipe, BPNODE_CMD_PIPE_DEPTH,
                                "BPNODE_CMD_PIPE");

    if (Status != CFE_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_CR_CMD_PIPE_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Error creating SB Command Pipe, RC = 0x%08lX", (unsigned long)Status);

        return Status;
    }

    /* Create wakeup pipe */
    Status = CFE_SB_CreatePipe(&BPNode_AppData.WakeupPipe, BPNODE_WAKEUP_PIPE_DEPTH,
                                "BPNODE_WAKEUP_PIPE");

    if (Status != CFE_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_CR_WKP_PIPE_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Error creating SB Wakeup Pipe, RC = 0x%08lX", (unsigned long)Status);

        return Status;
    }

    /* Subscribe to ground command packets */
    Status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(BPNODE_CMD_MID), BPNode_AppData.CommandPipe,
            PipeQOS, BPNODE_CMD_MID_DEPTH);
    if (Status != CFE_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_SUB_CMD_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Error subscribing to Commands, RC = 0x%08lX", (unsigned long)Status);

        return Status;
    }

    /* Subscribe to wakeup messages on the wakeup pipe */
    Status = CFE_SB_SubscribeEx(CFE_SB_ValueToMsgId(BPNODE_WAKEUP_MID),
                    BPNode_AppData.WakeupPipe, CFE_SB_DEFAULT_QOS, BPNODE_WAKEUP_PIPE_LIM);

    if (Status != CFE_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_SUB_WKP_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Error subscribing to wakeup messages, RC = 0x%08lX",
                            (unsigned long)Status);

        return Status;
    }

    /* Call Telemetry Proxy Init Function */
    BPA_TLMP_Init();

    /* Create Child Task Notifications */

    OsStatus = BPNode_NotifInit(&BPNode_AppData.ChildStartWorkNotif, 
                                    BPNODE_CHILD_STRTWORKNOTIF_NAME);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_INIT_WORK_NOTIF_ERR_EID, BPLib_EM_EventType_ERROR,
                    "Error creating child task start work notification, RC = %d",
                    OsStatus);
        return OsStatus;
    }

    OsStatus = BPNode_NotifInit(&BPNode_AppData.ChildTaskInitNotif, 
                                    BPNODE_CHILD_INIT_NOTIF_NAME);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_INIT_INIT_NOTIF_ERR_EID, BPLib_EM_EventType_ERROR,
                    "Error creating child task init notification, RC = %d",
                    OsStatus);
        return OsStatus;
    }

    OsStatus = BPNode_NotifInit(&BPNode_AppData.ChildTaskExitNotif, 
                                    BPNODE_CHILD_EXIT_NOTIF_NAME);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_INIT_EXIT_NOTIF_ERR_EID, BPLib_EM_EventType_ERROR,
                    "Error creating child task exit notification, RC = %d",
                    OsStatus);
        return OsStatus;
    }

    OsStatus = BPNode_NotifInit(&BPNode_AppData.ChildTaskCleanStorNotif, 
                                    BPNODE_CHILD_STOR_NOTIF_NAME);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_INIT_STOR_NOTIF_ERR_EID, BPLib_EM_EventType_ERROR,
                    "Error creating child task storage notification, RC = %d",
                    OsStatus);
        return OsStatus;
    }

    /* Create ADU In child tasks */
    Status = BPNode_AduInCreateTasks();

    if (Status != CFE_SUCCESS)
    {
        /* Event message handled in task creation function */
        return Status;
    }

    /* Create ADU Out child tasks */
    Status = BPNode_AduOutCreateTasks();

    if (Status != CFE_SUCCESS)
    {
        /* Event message handled in task creation function */
        return Status;
    }

    /* Create CLA In child tasks */
    Status = BPNode_ClaInCreateTasks();

    if (Status != CFE_SUCCESS)
    {
        /* Event message handled in task creation function */
        return Status;
    }

    /* Create CLA Out child tasks */
    Status = BPNode_ClaOutCreateTasks();

    if (Status != CFE_SUCCESS)
    {
        /* Event message handled in task creation function */
        return Status;
    }

    /* Create Generic Worker child tasks */
    Status = BPNode_GenWorkerCreateTasks();

    if (Status != CFE_SUCCESS)
    {
        /* Event message handled in task creation function */
        return Status;
    }

    Status = BPNode_MaintCreateTask();

    if (Status != CFE_SUCCESS)
    {
        /* Event message handled in task creation function */
        return Status;
    }
    
    OsStatus = BPNode_NotifWaitExact(&BPNode_AppData.ChildTaskInitNotif, BPNODE_TOTAL_NUM_CHILD_TASKS,
                                         BPNODE_CHILD_INIT_WAIT_MSEC);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_INIT_NOTIF_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Only %d child tasks detected, expected %d. Error = %d.", 
                            BPNode_NotifGetCount(&BPNode_AppData.ChildTaskInitNotif),
                            BPNODE_TOTAL_NUM_CHILD_TASKS, OsStatus);

        return OsStatus;
    }

    /* Register delete handler for graceful app shutdowns */
    OsStatus = OS_TaskInstallDeleteHandler(&BPNode_AppExit);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_DEL_HANDLER_ERR_EID, BPLib_EM_EventType_ERROR,
                            "Failed to install delete handler. Error = 0x%08X", OsStatus);

        return OsStatus;
    }

    /* Add and start all applications set to be loaded at startup */
    for (i = 0; i < BPLIB_MAX_NUM_CHANNELS; i++)
    {
        if (BPNode_AppData.ConfigPtrs.ChanConfigPtr->Configs[i].AddAutomatically == true)
        {
            /* Ignore return value, no failure conditions are possible here */
            (void) BPLib_PI_AddApplication(&BPNode_AppData.BplibInst, i);

            BpStatus = BPLib_PI_StartApplication(i);

            if (BpStatus != BPLIB_SUCCESS)
            {
                /* Error event message handled by ADU Proxy */
                return BpStatus;
            }
            else
            {
                BPLib_EM_SendEvent(BPNODE_AUTO_ADD_APP_INF_EID, BPLib_EM_EventType_INFORMATION,
                                    "Automatically added app configurations for ChanId=%d", i);
            }
        }
    }

    /* App has initialized properly */
    BPNode_AppData.RunStatus = CFE_ES_RunStatus_APP_RUN;

    #ifdef DEFAULT_UDP_CLA
    BPLib_EM_SendEvent(BPNODE_INIT_INF_EID, BPLib_EM_EventType_INFORMATION,
                        "BPNode Initialized. Version %d.%d.%d.%d",
                        BPNODE_MAJOR_VERSION,
                        BPNODE_MINOR_VERSION,
                        BPNODE_REVISION,
                        BPNODE_MISSION_REV);
    #else
    BPLib_EM_SendEvent(BPNODE_INIT_INF_EID, BPLib_EM_EventType_INFORMATION,
                        "BPNode Initialized without default UDP CLA. Version %d.%d.%d.%d",
                        BPNODE_MAJOR_VERSION,
                        BPNODE_MINOR_VERSION,
                        BPNODE_REVISION,
                        BPNODE_MISSION_REV);
    #endif /* DEFAULT_UDP_CLA */

    return CFE_SUCCESS;
}

/* Exit app */
void BPNode_AppExit(void)
{
    uint32 ChanId;
    uint32 ContactId;
    uint32 WorkerId;
    int32  OsStatus;

    BPLib_EM_SendEvent(BPNODE_EXIT_CRIT_EID, BPLib_EM_EventType_CRITICAL,
                        "App terminating, error = %d", BPNode_AppData.RunStatus);

    CFE_ES_WriteToSysLog("BPNode app terminating, error = %d", BPNode_AppData.RunStatus);

    /* Signal to maintenance task to exit */
    BPNode_AppData.MaintData.TaskData.RunStatus = CFE_ES_RunStatus_APP_EXIT;

    /* Signal for the children to stop work - This needs to be done here because
    ** cFS sends the child terminate signal from another thread, which
    ** creates the potential for a deadlock waiting on the child semaphores in the
    ** logic below.
    */

    /* Signal to ADU child tasks to exit */
    for (ChanId = 0; ChanId < BPLIB_MAX_NUM_CHANNELS; ChanId++)
    {
        /* Stop and remove the application */
        (void) BPLib_PI_StopApplication(ChanId);
        (void) BPLib_PI_RemoveApplication(&BPNode_AppData.BplibInst, ChanId);

        BPNode_AppData.AduOutData[ChanId].TaskData.RunStatus = CFE_ES_RunStatus_APP_EXIT;
        BPNode_AppData.AduInData[ChanId].TaskData.RunStatus = CFE_ES_RunStatus_APP_EXIT;
    }

    /* Signal to CLA child tasks to exit */
    for (ContactId = 0; ContactId < BPLIB_MAX_NUM_CONTACTS; ContactId++)
    {
        /* Change the BPLib contact state and clean up the contacts */
        (void) BPLib_CLA_ContactStop(&BPNode_AppData.BplibInst, ContactId);
        (void) BPLib_CLA_ContactTeardown(&BPNode_AppData.BplibInst, ContactId);

        BPNode_AppData.ClaOutData[ContactId].TaskData.RunStatus = CFE_ES_RunStatus_APP_EXIT;
        BPNode_AppData.ClaInData[ContactId].TaskData.RunStatus = CFE_ES_RunStatus_APP_EXIT;        
    }

    /* Signal to generic worker tasks to exit */
    for (WorkerId = 0; WorkerId < BPNODE_NUM_GEN_WRKR_TASKS; WorkerId++)
    {
        BPNode_AppData.GenWorkerData[WorkerId].TaskData.RunStatus = CFE_ES_RunStatus_APP_EXIT;
    }

    /* Verify that all child tasks have shut down */
    BPLib_PL_PerfLogExit(BPNODE_PERF_ID);
    OsStatus = BPNode_NotifWaitExact(&BPNode_AppData.ChildTaskExitNotif, BPNODE_TOTAL_NUM_CHILD_TASKS,
                                         BPNODE_CHILD_EXIT_WAIT_MSEC);
    BPLib_PL_PerfLogEntry(BPNODE_PERF_ID);
    if (OsStatus != OS_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_EXIT_NOTIF_CRT_EID, BPLib_EM_EventType_CRITICAL,
                            "Only %d child tasks have exited, expected %d. Error = %d.", 
                            BPNODE_TOTAL_NUM_CHILD_TASKS, 
                            BPNode_NotifGetCount(&BPNode_AppData.ChildTaskExitNotif),
                            BPNODE_TOTAL_NUM_CHILD_TASKS, 
                            OsStatus);
    }

    /* Cleanup Notification */
    BPNode_NotifDestroy(&BPNode_AppData.ChildStartWorkNotif);
    BPNode_NotifDestroy(&BPNode_AppData.ChildTaskInitNotif);
    BPNode_NotifDestroy(&BPNode_AppData.ChildTaskExitNotif);
    BPNode_NotifDestroy(&BPNode_AppData.ChildTaskCleanStorNotif);

    /* Cleanup QM and MEM */
    BPLib_QM_QueueTableDestroy(&BPNode_AppData.BplibInst);
    BPLib_MEM_PoolDestroy(&BPNode_AppData.BplibInst.pool);
    free(BPNode_AppData.MemPool);

    /* Performance Log Exit Stamp */
    BPLib_PL_PerfLogExit(BPNODE_PERF_ID);
}
