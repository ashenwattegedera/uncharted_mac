#ifndef BB_MEMORY_PROBE_H
#define BB_MEMORY_PROBE_H
#include "probe.h"
#include "guest_fault.h"
typedef struct BbMemoryProbe BbMemoryProbe;
BbProbeStatus bb_memory_probe_open(BbMemoryProbe **out,size_t window,size_t backing);
/* Attach before mappings exist. The external owner closes VM after this session. */
BbProbeStatus bb_memory_probe_attach(BbMemoryProbe **out,BbGuestMemory *vm);
BbAbiMemory bb_memory_probe_memory(BbMemoryProbe *session);
BbProbeStatus bb_memory_probe_close(BbMemoryProbe *session);
BbGuestMemory *bb_memory_probe_vm(BbMemoryProbe *session);
BbCpuProbe *bb_memory_probe_cpu(BbMemoryProbe *session);
BbFaultRouter *bb_memory_probe_router(BbMemoryProbe *session);
const BbGuestFault *bb_memory_probe_last_fault(BbMemoryProbe *session);
BbProbeStatus bb_memory_probe_bind_abi(BbMemoryProbe *session,const BbAbiImport *binding,uint64_t *guest_address);
BbProbeStatus bb_memory_probe_invoke(BbMemoryProbe *session,BbGuestCall *call,BbProbeFault *fault);
#endif
