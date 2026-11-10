#ifndef BPNODE_DEMO_H
#define BPNODE_DEMO_H
#include "bpnode_app.h"
/* Optional Linux lab mode, enabled only in the dedicated demonstration build. */
CFE_Status_t BPNode_DemoConfigure(void);
CFE_Status_t BPNode_DemoStart(void);
/** Feed the next source ADUs when its contact is ready; other nodes do nothing.
 * Call from the BPNode scheduler task. A failed record stays at the head of
 * the queue for a later tick. */
void BPNode_DemoTick(void);
const char *BPNode_DemoNodeEid(void);
BPLib_Status_t BPNode_DemoWire(bool outgoing, void *data, size_t capacity, size_t *size);
bool BPNode_DemoDropCcs(const void *data, size_t size);
#endif
