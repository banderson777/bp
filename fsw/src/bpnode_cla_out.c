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
 *   This file contains the source code for the BPNode CLA Out Child Task(s)
 */

/*
** Include Files
*/

#include "bpnode_app.h"
#ifdef BPNODE_ENABLE_FIVE_NODE_DEMO
#include "bpnode_demo.h"
#endif
#include "bpnode_cla_out.h"

/*
** Function Definitions
*/

CFE_Status_t BPNode_ClaOutCreateTasks(void)
{
    CFE_Status_t Status = CFE_SUCCESS;
    uint32       ContactId;
    char         NameBuff[OS_MAX_API_NAME];
    uint16       TaskPriority;

    /* Create all of the CLA Out task(s) */
    for (ContactId = 0; ContactId < BPLIB_MAX_NUM_CONTACTS; ContactId++)
    {
        /* Set up task data for the child task */
        BPNode_AppData.ClaOutData[ContactId].TaskData.TaskId       = ContactId;
        BPNode_AppData.ClaOutData[ContactId].TaskData.PerfId       = BPNODE_CLA_OUT_PERF_ID_BASE + ContactId;
        BPNode_AppData.ClaOutData[ContactId].TaskData.InitEid      = BPNODE_CLA_OUT_INIT_INF_EID;
        BPNode_AppData.ClaOutData[ContactId].TaskData.NotifErrEid  = BPNODE_CLA_OUT_NOTIF_ERR_EID;
        BPNode_AppData.ClaOutData[ContactId].TaskData.ExitEid      = BPNODE_CLA_OUT_EXIT_CRT_EID;
        BPNode_AppData.ClaOutData[ContactId].TaskData.TaskInitFunc = BPNode_ClaOut_TaskInit;
        BPNode_AppData.ClaOutData[ContactId].TaskData.TaskMainFunc = BPNode_ClaOut_TaskMain;
        BPNode_AppData.ClaOutData[ContactId].BitsEgressed          = 0;
        
        snprintf(BPNode_AppData.ClaOutData[ContactId].TaskData.Name, OS_MAX_API_NAME,
                            "CLA Out %d", ContactId);

        snprintf(NameBuff, OS_MAX_API_NAME, "%s_%d", BPNODE_CLA_OUT_BASE_NAME, ContactId);
        TaskPriority = BPNODE_CLA_OUT_PRIORITY_BASE + ContactId;

        /* Spawn CLA Out child task */
        Status = CFE_ES_CreateChildTask(&BPNode_AppData.ClaOutData[ContactId].TaskData.CfeTaskId,
                                        NameBuff, BPNode_TaskMain, 0,
                                        BPNODE_CLA_OUT_STACK_SIZE, TaskPriority, 0);
        if (Status != CFE_SUCCESS)
        {
            BPLib_EM_SendEvent(BPNODE_CLA_OUT_CREATE_ERR_EID, BPLib_EM_EventType_ERROR,
                                "Failed to create child task for CLA Out #%d. Error = 0x%08X.",
                                ContactId, Status);
            break;
        }
    }

    return Status;
}

CFE_Status_t BPNode_ClaOut_TaskInit(uint32 ContactId)
{
    CFE_Status_t Status = CFE_PSP_SUCCESS;

    /* This should never happen, indicates something is wrong with the function pointers */
    if (ContactId >= BPLIB_MAX_NUM_CONTACTS)
    {
        BPLib_EM_SendEvent(BPNODE_CLA_OUT_INIT_PTR_CRT_EID, BPLib_EM_EventType_CRITICAL,
                            "Invalid contact ID %d passed into BPNode_ClaOut_TaskInit function pointer.",
                            ContactId);

        return CFE_STATUS_RANGE_ERROR;
    }

    #ifdef DEFAULT_UDP_CLA
    /* Get PSP module ID for the socket driver */
    Status = CFE_PSP_IODriver_FindByName(BPNODE_CLA_PSP_DRIVER_NAME,
                                            &BPNode_AppData.ClaOutData[ContactId].PspLocation.PspModuleId);
    #endif /* DEFAULT_UDP_CLA */

    if (Status != CFE_PSP_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_CLA_OUT_FIND_NAME_ERR_EID, BPLib_EM_EventType_ERROR,
                            "[CLA Out #%d]: Couldn't find I/O driver. Error = %d",
                            ContactId,
                            Status);

        return Status;
    }

    #ifdef DEFAULT_UDP_CLA
    BPNode_AppData.ClaOutData[ContactId].PspLocation.SubsystemId = 1 + ContactId + BPLIB_MAX_NUM_CONTACTS;

    /* Set direction to output only */
    Status = CFE_PSP_IODriver_Command(&BPNode_AppData.ClaOutData[ContactId].PspLocation,
                                        CFE_PSP_IODriver_SET_DIRECTION,
                                        CFE_PSP_IODriver_U32ARG(CFE_PSP_IODriver_Direction_OUTPUT_ONLY));

    #endif /* DEFAULT_UDP_CLA */

    if (Status != CFE_PSP_SUCCESS)
    {
        BPLib_EM_SendEvent(BPNODE_CLA_OUT_CFG_DIR_ERR_EID, BPLib_EM_EventType_ERROR,
                            "[CLA Out #%d]: Couldn't set I/O direction to output. Error = %d",
                            ContactId,
                            Status);

        return Status;
    }

    return Status;
}

/* Main loop for CLA Out task(s) */
void BPNode_ClaOut_TaskMain(uint32 ContactId)
{
    BPLib_Status_t              Status;
    BPLib_CLA_ContactRunState_t RunState;
    size_t                      BundleSize = 0;

    /* This should never happen, indicates something is wrong with the function pointers */
    if (ContactId >= BPLIB_MAX_NUM_CONTACTS)
    {
        BPLib_EM_SendEvent(BPNODE_CLA_OUT_MAIN_PTR_CRT_EID, BPLib_EM_EventType_CRITICAL,
                        "Invalid contact ID %d passed into BPNode_ClaOut_TaskMain function pointer.",
                        ContactId);
        return;
    }

    Status = BPLib_CLA_GetContactRunState(ContactId, &RunState);

    /* Ingress bundles only when the contact has been started */
    if (Status == BPLIB_SUCCESS && RunState == BPLIB_CLA_STARTED)
    {
        #ifdef BPNODE_INCLUDE_PERFORMANCE_DEBUGS
        int64_t StartTime = BPLib_TIME_GetMonotonicTime();
        #endif

        while (Status == BPLIB_SUCCESS &&
                (BPNode_AppData.ClaOutData[ContactId].BitsEgressed < 
                BPNode_AppData.BplibInst.ContCtxt[ContactId].Config.EgressBitsPerCycle))
        {
            Status = BPNode_ClaOut_ProcessBundleOutput(ContactId, &BundleSize);
            if (Status == BPLIB_SUCCESS)
            {
                BPNode_AppData.ClaOutData[ContactId].BitsEgressed += (BundleSize * BPNODE_BITS_PER_BYTE);
            }
        }

        #ifdef BPNODE_INCLUDE_PERFORMANCE_DEBUGS
        printf("Egressed %ld bits in %ld msec\n", BPNode_AppData.ClaOutData[ContactId].BitsEgressed, 
                                  BPLib_TIME_GetMonotonicTime() - StartTime);
        #endif

        if (BPNode_AppData.ClaOutData[ContactId].BitsEgressed < BPNode_AppData.BplibInst.ContCtxt[ContactId].Config.EgressBitsPerCycle)
        {
            BPNode_AppData.ClaOutData[ContactId].BitsEgressed = 0;
        }
        else
        {
            BPNode_AppData.ClaOutData[ContactId].BitsEgressed -= BPNode_AppData.BplibInst.ContCtxt[ContactId].Config.EgressBitsPerCycle;
        }
    }

    return;
}


/* Receive bundles from CLA and send egress bundles to network CL */
int32 BPNode_ClaOut_ProcessBundleOutput(uint32 ContId, size_t *MsgSize)
{
    #ifdef DEFAULT_UDP_CLA
    CFE_PSP_IODriver_WritePacketBuffer_t WrBuf;
    #endif /* DEFAULT_UDP_CLA */

    BPLib_Status_t                       Status;

    *MsgSize = 0;

    /* Get next bundle from CLA */
    BPLib_PL_PerfLogExit(BPNode_AppData.ClaOutData[ContId].TaskData.PerfId);

    Status = BPLib_CLA_Egress(&BPNode_AppData.BplibInst,
                                ContId,
                                BPNode_AppData.ClaOutData[ContId].OutBuffer.Payload,
                                MsgSize,
                                BPNODE_CLA_PSP_OUTPUT_BUFFER_SIZE,
                                BPNODE_DATA_TIMEOUT_MSEC);

    BPLib_PL_PerfLogEntry(BPNode_AppData.ClaOutData[ContId].TaskData.PerfId);

    if (Status != BPLIB_SUCCESS && Status != BPLIB_CLA_TIMEOUT)
    {
        BPLib_EM_SendEvent(BPNODE_CLA_OUT_LIB_LOAD_ERR_EID, BPLib_EM_EventType_ERROR,
                            "[CLA Out #%d]: Failed to get bundle for egress. Error = %d",
                            ContId,
                            Status);
    }

    /* Send egress bundle onto CL */
    if (Status == BPLIB_SUCCESS)
    {
        switch (BPNode_AppData.BplibInst.ContCtxt[ContId].Config.CLAType)
        {
            case BPLib_UDP_CLA:
                #ifdef DEFAULT_UDP_CLA
                WrBuf.OutputSize = *MsgSize;
                WrBuf.BufferMem  = BPNode_AppData.ClaOutData[ContId].OutBuffer.Payload;

                BPLib_PL_PerfLogExit(BPNode_AppData.ClaOutData[ContId].TaskData.PerfId);

                /* This does not check return code here, it is "best effort" at this stage.
                * bplib should retry based on custody signals if this does not work. */
                (void) CFE_PSP_IODriver_Command(&BPNode_AppData.ClaOutData[ContId].PspLocation,
                                                        CFE_PSP_IODriver_PACKET_IO_WRITE,
                                                        CFE_PSP_IODriver_VPARG(&WrBuf));

                BPLib_PL_PerfLogEntry(BPNode_AppData.ClaOutData[ContId].TaskData.PerfId);
                #endif /* DEFAULT_UDP_CLA */

                break;
            case BPLib_SB_CLA:
                /* Set the MID for the outbound bundle */
                CFE_MSG_SetMsgId(CFE_MSG_PTR(BPNode_AppData.ClaOutData[ContId].OutBuffer.TelemetryHeader),
                                    CFE_SB_ValueToMsgId(BPNODE_CLA_OUT_BUNDLE_MID));

                /* Set the size of the message */
                CFE_MSG_SetSize(CFE_MSG_PTR(BPNode_AppData.ClaOutData[ContId].OutBuffer.TelemetryHeader),
                                *MsgSize + sizeof(CFE_MSG_TelemetryHeader_t));

                /* Timestamp message before transmitting */
                CFE_SB_TimeStampMsg(CFE_MSG_PTR(BPNode_AppData.ClaOutData[ContId].OutBuffer.TelemetryHeader));

                /* Send the wrapped bundle onto the Software Bus */
                CFE_SB_TransmitMsg(CFE_MSG_PTR(BPNode_AppData.ClaOutData[ContId].OutBuffer.TelemetryHeader), true);

                break;
            case BPLib_LTP_CLA:
                break;
            case BPLib_EPP_CLA:
                break;
            case BPLib_TCP_CLA:
#ifdef BPNODE_ENABLE_FIVE_NODE_DEMO
                Status = BPNode_DemoWire(true, BPNode_AppData.ClaOutData[ContId].OutBuffer.Payload,
                                         BPNODE_CLA_PSP_OUTPUT_BUFFER_SIZE, MsgSize);
                if (Status != BPLIB_SUCCESS) break;
                if (BPNode_DemoDropCcs(BPNode_AppData.ClaOutData[ContId].OutBuffer.Payload, *MsgSize))
                {
                    Status = BPLIB_SUCCESS;
                    break;
                }
#endif
                Status = BPLib_TCPCLA_Send(&BPNode_AppData.ClaInData[ContId].TcpCla,
                                           BPNode_AppData.ClaOutData[ContId].OutBuffer.Payload,
                                           *MsgSize, BPNODE_TCPCLA_IO_TIMEOUT_MSEC);
                break;
            default:
                break;
        }

        CFE_MSG_SetSize(CFE_MSG_PTR(BPNode_AppData.ClaOutData[ContId].OutBuffer.TelemetryHeader), 0);
    }

    return Status;
}


BPLib_Status_t BPNode_ClaOut_Setup(uint32 ContactId)
{
    BPLib_Status_t           Status;
    BPLib_CLA_ContactsSet_t* ContactInfo;

    #ifdef DEFAULT_UDP_CLA
    int32           PspStatus;
    char            Str[100];

    /* Default PSP status to output an error */
    PspStatus = CFE_PSP_ERROR_NOT_IMPLEMENTED;
    #endif /* DEFAULT_UDP_CLA */

    Status      = BPLIB_SUCCESS;
    ContactInfo = &(BPNode_AppData.ConfigPtrs.ContactsConfigPtr->ContactSet[ContactId]);

    switch (BPNode_AppData.BplibInst.ContCtxt[ContactId].Config.CLAType)
    {
        case BPLib_UDP_CLA:
            #ifdef DEFAULT_UDP_CLA
            /* Configure Port Number */
            snprintf(Str, sizeof(Str), "port=%d", ContactInfo->ClaOutPort);
            PspStatus = CFE_PSP_IODriver_Command(&BPNode_AppData.ClaOutData[ContactId].PspLocation,
                                                    CFE_PSP_IODriver_SET_CONFIGURATION,
                                                    CFE_PSP_IODriver_CONST_STR(Str));

            if (PspStatus != CFE_PSP_SUCCESS)
            {
                BPLib_EM_SendEvent(BPNODE_CLA_OUT_CFG_PORT_ERR_EID, BPLib_EM_EventType_ERROR,
                                    "Couldn't configure port number for CLA Out #%d. Error = %d",
                                    ContactId,
                                    PspStatus);

                Status = BPLIB_CLA_IO_ERROR;
            }
            #endif /* DEFAULT_UDP_CLA */

            if (Status == BPLIB_SUCCESS)
            {
                #ifdef DEFAULT_UDP_CLA
                /* Configure IP Address */
                snprintf(Str, sizeof(Str), "IpAddr=%s", ContactInfo->ClaOutAddr);
                PspStatus = CFE_PSP_IODriver_Command(&BPNode_AppData.ClaOutData[ContactId].PspLocation,
                                                        CFE_PSP_IODriver_SET_CONFIGURATION,
                                                        CFE_PSP_IODriver_CONST_STR(Str));

                if (PspStatus != CFE_PSP_SUCCESS)
                {
                    BPLib_EM_SendEvent(BPNODE_CLA_OUT_CFG_IP_ERR_EID, BPLib_EM_EventType_ERROR,
                                        "Couldn't configure IP address for CLA Out #%d. Error = %d",
                                        ContactId,
                                        PspStatus);

                    Status = BPLIB_CLA_IO_ERROR;
                }
                else
                {
                    OS_printf("CLA Out #%d sending on %s:%d\n", ContactId, ContactInfo->ClaOutAddr, ContactInfo->ClaOutPort);
                }
                #endif /* DEFAULT_UDP_CLA */
            }

            break;
        case BPLib_SB_CLA:
            /* No SB-specific operations needed */
            break;
        case BPLib_LTP_CLA:
            break;
        case BPLib_EPP_CLA:
            break;
        case BPLib_TCP_CLA:
            break;
        default:
            break;
    }

    return Status;
}

BPLib_Status_t BPNode_ClaOut_Start(uint32 ContactId)
{
    BPLib_Status_t   Status;

    #ifdef DEFAULT_UDP_CLA
    int32 PspStatus;

    /* Default PSP status to output an error */
    PspStatus = CFE_PSP_ERROR_NOT_IMPLEMENTED;
    #endif /* DEFAULT_UDP_CLA */
    
    Status  = BPLIB_SUCCESS;

    switch (BPNode_AppData.BplibInst.ContCtxt[ContactId].Config.CLAType)
    {
        case BPLib_UDP_CLA:
            #ifdef DEFAULT_UDP_CLA
            /* Set I/O to running */
            PspStatus = CFE_PSP_IODriver_Command(&BPNode_AppData.ClaOutData[ContactId].PspLocation,
                                                    CFE_PSP_IODriver_SET_RUNNING,
                                                    CFE_PSP_IODriver_U32ARG(true));

            if (PspStatus != CFE_PSP_SUCCESS)
            {
                BPLib_EM_SendEvent(BPNODE_CLA_OUT_CFG_SET_RUN_ERR_EID, BPLib_EM_EventType_ERROR,
                                    "Couldn't set I/O state for CLA Out #%d to running. Error = %d",
                                    ContactId,
                                    PspStatus);

                Status = BPLIB_CLA_IO_ERROR;
            }
            #endif /* DEFAULT_UDP_CLA */

            break;
        case BPLib_SB_CLA:
            /* No SB-specific operations needed */
            break;
        case BPLib_LTP_CLA:
            break;
        case BPLib_EPP_CLA:
            break;
        case BPLib_TCP_CLA:
            break;
        default:
            break;
    }

    return Status;
}

BPLib_Status_t BPNode_ClaOut_Stop(uint32 ContactId)
{
    BPLib_Status_t   Status;
    int32            PspStatus;
    
    Status  = BPLIB_SUCCESS;

    /* Default PSP status to output an error */
    PspStatus = CFE_PSP_ERROR_NOT_IMPLEMENTED;

    switch (BPNode_AppData.BplibInst.ContCtxt[ContactId].Config.CLAType)
    {
        case BPLib_UDP_CLA:
            #ifdef DEFAULT_UDP_CLA
            /* Set I/O to stop running */
            PspStatus = CFE_PSP_IODriver_Command(&BPNode_AppData.ClaOutData[ContactId].PspLocation,
                                                    CFE_PSP_IODriver_SET_RUNNING,
                                                    CFE_PSP_IODriver_U32ARG(false));
            #endif /* DEFAULT_UDP_CLA */

            if (PspStatus != CFE_PSP_SUCCESS)
            {
                BPLib_EM_SendEvent(BPNODE_CLA_OUT_CFG_STOP_ERR_EID,
                                    BPLib_EM_EventType_ERROR,
                                    "Couldn't set I/O state to stop for CLA Out #%d. Error = %d",
                                    ContactId,
                                    PspStatus);

                Status = BPLIB_CLA_IO_ERROR;
            }
            break;
        case BPLib_SB_CLA:
            /* No SB-specific operations needed */
            break;
        case BPLib_LTP_CLA:
            break;
        case BPLib_EPP_CLA:
            break;
        case BPLib_TCP_CLA:
            break;
        default:
            break;
    }

    return Status;
}

void BPNode_ClaOut_Teardown(uint32 ContactId)
{
    /*
    ** Disestablish CLA (notify that ACK isn't coming)
    ** Free all CLA resources
    ** Discard output queue
    ** Delete custody timers
    */

    /*
    switch (CLAType)
    {
        case BPLib_UDP_CLA:
            break;
        case BPLib_SB_CLA:
            break;
        case BPLib_LTP_CLA:
            break;
        case BPLib_EPP_CLA:
            break;
        case BPLib_TCP_CLA:
            break;
        default:
            break;
    }
    */

    return;
}
