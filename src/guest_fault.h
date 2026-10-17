#ifndef BB_GUEST_FAULT_H
#define BB_GUEST_FAULT_H
#include "guest_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t pc,address; size_t size; unsigned access; } BbGuestFault;
typedef enum { BB_FAULT_UNHANDLED, BB_FAULT_RECOVERED, BB_FAULT_ERROR } BbFaultResult;
/* Ordinary thread context only. GPU callback acknowledges every affected 4 KiB
 * subpage before a 16 KiB host page is made writable. Failure retains watches.
 * Callbacks must not reenter the router or change mappings. They may release
 * their own GPU permissions synchronously; acknowledgments must be repeatable. */
typedef struct {
    BbGuestMemory *memory;
    int (*gpu_write)(void *,uint64_t address,uint64_t backing_offset,unsigned watchers);
    int (*demand_map)(void *,const BbGuestFault *);
    void *user;
    int active;
    int (*gpu_read)(void *,uint64_t address,uint64_t backing_offset,unsigned watchers);
} BbFaultRouter;
BbFaultResult bb_guest_fault_route(BbFaultRouter *router,const BbGuestFault *fault);
#ifdef __cplusplus
}
#endif
#endif
