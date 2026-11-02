#ifndef BPNODE_VIDEO_SINK_H
#define BPNODE_VIDEO_SINK_H

#include "bplib.h"

/** Decode borrowed encoded bytes using Inst's pool and append payload BTSD to
 * BPNODE_VIDEO_SINK_PATH via OSAL. Owns/frees a temporary bundle. Enabled builds
 * serialize complete calls with a mutex; write failures can leave a prefix.
 * Returns decoder/reader status, NULL_PTR_ERROR for invalid input, NO_STOR_ERR
 * for allocation failure, or CLA_IO_ERROR for file errors. No decryption,
 * reordering, or persistence ACK is performed. Disabled builds return success.
 */
BPLib_Status_t BPNode_VideoSink_Write(BPLib_Instance_t *Inst,
                                      const void *BundleData, size_t BundleSize);
/** Serialize with writes and close the owned OSAL file; repeated calls are safe.
 * A later write reopens and appends. Rotate the file only with contacts stopped.
 */
void BPNode_VideoSink_Close(void);

#endif
