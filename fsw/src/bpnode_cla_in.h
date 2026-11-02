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
 *   Header file for CLA Input.
 */

#ifndef BPNODE_CLA_IN_H
#define BPNODE_CLA_IN_H

/*
** Include Files
*/

#include "cfe.h"
#include "bplib.h"
#include "bpnode_platform_cfg.h"
#include "bpnode_task.h"
#include "bplib_tcp_cla.h"

#ifdef DEFAULT_UDP_CLA
#include "iodriver_base.h"
#include "iodriver_packet_io.h"
#endif /* DEFAULT_UDP_CLA */

/*
** Macro Definitions
*/

#define BPNODE_CLA_IN_BASE_NAME              "BPNODE.CLA_RX"        /** \brief Task base name */
#define BPNODE_CLA_PSP_INPUT_BUFFER_SIZE     (BPLIB_MAX_BUNDLE_LEN) /** \brief IODriver buffer size*/
#define BPNODE_CLA_INGRESS_PIPE_DEPTH        (32u)                  /** \brief CLA In SB pipe depth */
#define BPNODE_CLA_INGRESS_MSG_LIMIT         (32u)                  /** \brief CLA In SB message limit */
/*
** Type Definitions
*/

/**
** \brief CLA In Task Data
*/
typedef struct
{
    BPNode_TaskData_t TaskData;

    size_t BitsIngressed;

    #ifdef DEFAULT_UDP_CLA
    /* IODriver usock_intf related */
    CFE_PSP_IODriver_Direction_t Dir;
    CFE_PSP_IODriver_Location_t  PspLocation;
    #endif /* DEFAULT_UDP_CLA */

    /* CFE_SB_ReceiveBuffer related */
    CFE_SB_PipeId_t IngressPipe;

    /* CLA In bundle/packet */
    uint8 PSP_Buffer[BPNODE_CLA_PSP_INPUT_BUFFER_SIZE];
    void* SB_Buffer;

    bool   ClearPipe;

    /* TCPCLv4 uses one bidirectional stream per contact. */
    BPLib_TCPCLA_t TcpCla;
    bool TcpInitialized;
    
} BPNode_ClaInData_t;

/*
** Exported Functions
*/

/**
 * \brief     Create all CLA In tasks
 * \return    Execution status
 * \retval    CFE_SUCCESS: Successful execution
 * \retval    CFE errors from CFE_ES_CreateChildTask
 */
CFE_Status_t BPNode_ClaInCreateTasks(void);

/**
 * \brief     Initialize a CLA In task
 * 
 *  \par      Description
 *            Initialize provided CLA In task. This function is called as a function 
 *            pointer from BPNode_TaskInit
 * 
 * \param[in] ContactId (uint32) Index into the various contact info tracking
 *                                 arrays that corresponds to that contact's info
 * \return    Execution status
 * \retval    CFE_SUCCESS: Successful execution
 * \retval    PSP errors from CFE_PSP_IODriver_FindByName
 * \retval    PSP errors from CFE_PSP_IODriver_Command
 */
CFE_Status_t BPNode_ClaIn_TaskInit(uint32 ContactId);

/**
 * \brief CLA In Main Task
 *
 *  \par Description
 *       CLA In main task operations. This function is called as a function pointer from
 *       BPNode_TaskMain
 *
 *  \par Assumptions, External Events, and Notes:
 *       None
 */
void BPNode_ClaIn_TaskMain(uint32 ContactId);

/**
 * \brief Process Bundle Input from CLA
 *
 *  \par Description
 *       Receive and process candidate bundle (bundle or control message) from CLA
 *       and pass the bundle to Bundle Interface.
 *
 *  \par Assumptions, External Events, and Notes:
 *       None
 *
 *  \param[in] ContId Contact ID
 *  \param[out] BundleSize Size of bundle ingested
 *
 *  \return Execution status, see \ref CFEReturnCodes
 *  \retval #CFE_SUCCESS \copybrief CFE_SUCCESS
 */
int32 BPNode_ClaIn_ProcessBundleInput(uint32 ContId, size_t *BundleSize);

/**
  * \brief     Set up a CLA In task
  * \param[in] ContactId Index into the various contact info tracking arrays that
  *                      corresponds to that contact's info
  * \return    Execution status
  * \retval    BPLIB_SUCCESS: Successful execution
  * \retval    BPLIB_CLA_IO_ERROR: A I/O driver API call failed operation
  */
BPLib_Status_t BPNode_ClaIn_Setup(uint32 ContactId);

/**
  * \brief     Start up a CLA In task
  * \note      Create a CLA In child tasks and signal to the main task that
  *            the task is running
  * \param[in] ContactId (uint32) Index into the various contact info tracking
  *                               arrays that corresponds to that contact's info
  * \return    Execution status
  * \retval    BPLIB_SUCCESS: Successful execution
  * \retval    BPLIB_CLA_IO_ERROR: UDP connection couldn't be set to running
  */
BPLib_Status_t BPNode_ClaIn_Start(uint32 ContactId);

/**
  * \brief     Stop a CLA In task
  * \param[in] ContactId (uint32) Index into the various contact info tracking
  *                               arrays that corresponds to that contact's info
  * \return    Execution status
  * \retval    BPLIB_SUCCESS: Successful execution
  * \retval    BPLIB_CLA_IO_ERROR: Something went wrong while running CFE_PSP_IODriver_Command
  */
BPLib_Status_t BPNode_ClaIn_Stop(uint32 ContactId);

/**
  * \brief     Teardown a CLA In task
  * \note      Nothing is implemented as of right now
  * \param[in] ContactId (uint32) Index into the various contact info tracking
  *                               arrays that corresponds to that contact's info
  * \return    void
  */
void BPNode_ClaIn_Teardown(uint32 ContactId);

#endif /* BPNODE_CLA_IN_H */
