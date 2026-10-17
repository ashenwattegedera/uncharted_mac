#ifndef BB_CPU_PROBE_H
#define BB_CPU_PROBE_H
#include "execution.h"
#include "guest_abi.h"
#include <unicorn/unicorn.h>

/* Experimental, software-mapped x86 address space. Not the PS4 runtime backend.
 * One engine is serialized across callers; callbacks execute outside uc_emu_start.
 * Nested calls preserve CPU state and use separate stack slices. */
typedef struct BbCpuProbe BbCpuProbe;
typedef enum { BB_PROBE_OK, BB_PROBE_INVALID, BB_PROBE_ENGINE, BB_PROBE_FAULT, BB_PROBE_LIMIT,
               BB_PROBE_UNSUPPORTED } BbProbeStatus;
typedef struct {
    uint64_t pc,address;
    size_t size;
    int access;
    uc_err engine_error;
} BbProbeFault;
typedef BbProbeStatus (*BbProbeHostCall)(BbCpuProbe *,const uint64_t args[6],uint64_t *result,void *user);
typedef int (*BbProbeFaultHandler)(BbCpuProbe *,const BbProbeFault *,void *user);

#define BB_PROBE_CODE UINT64_C(0x100000)
#define BB_PROBE_DATA UINT64_C(0x400000)
#define BB_PROBE_STACK UINT64_C(0x800000)
#define BB_PROBE_RETURN UINT64_C(0x20fff0)

BbProbeStatus bb_probe_open(BbCpuProbe **out);
void bb_probe_close(BbCpuProbe *probe);
BbProbeStatus bb_probe_map(BbCpuProbe *probe,uint64_t address,size_t size,unsigned permissions);
BbProbeStatus bb_probe_map_host(BbCpuProbe *probe,uint64_t address,size_t size,unsigned permissions,void *host);
BbProbeStatus bb_probe_unmap(BbCpuProbe *probe,uint64_t address,size_t size);
BbProbeStatus bb_probe_invalidate(BbCpuProbe *probe,uint64_t address,size_t size);
typedef int (*BbProbeWriteObserver)(BbCpuProbe *,uint64_t,size_t,void *);
BbProbeStatus bb_probe_watch_writes(BbCpuProbe *probe,uint64_t start,uint64_t end,BbProbeWriteObserver observer,void *user);
BbProbeStatus bb_probe_write(BbCpuProbe *probe,uint64_t address,const void *data,size_t size);
BbProbeStatus bb_probe_read(BbCpuProbe *probe,uint64_t address,void *data,size_t size);
BbProbeStatus bb_probe_protect(BbCpuProbe *probe,uint64_t address,size_t size,unsigned permissions);
BbProbeStatus bb_probe_bind(BbCpuProbe *probe,BbProbeHostCall call,void *user,uint64_t *guest_address);
/* Copies metadata; binding names, host user state and optional memory user state
 * must outlive the probe. NULL memory uses checked Unicorn guest mappings. */
BbProbeStatus bb_probe_bind_abi(BbCpuProbe *probe,const BbAbiImport *binding,
                               const BbAbiMemory *memory,uint64_t *guest_address);
BbProbeStatus bb_probe_invoke(BbCpuProbe *probe,BbGuestCall *call,BbProbeFault *fault);
void bb_probe_set_fault_handler(BbCpuProbe *probe,BbProbeFaultHandler handler,void *user);
const char *bb_probe_status_name(BbProbeStatus status);
#endif
