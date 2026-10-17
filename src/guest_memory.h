#ifndef BB_GUEST_MEMORY_H
#define BB_GUEST_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Owned Darwin address window. All operations, observers and teardown belong to
 * the creating host thread. No API here is async-signal-safe. */
typedef struct BbGuestMemory BbGuestMemory;
typedef enum { BB_VM_OK, BB_VM_INVALID, BB_VM_BUSY, BB_VM_ACCESS, BB_VM_SYSTEM,
               BB_VM_WRONG_THREAD, BB_VM_POISONED } BbVmStatus;
enum { BB_VM_READ=1, BB_VM_WRITE=2, BB_VM_EXEC=4, BB_VM_GUEST_PAGE=16384,
       BB_VM_TRACKER_PAGE=4096 };
typedef enum { BB_VM_MAPPING, BB_VM_UNMAPPING, BB_VM_PROTECTION, BB_VM_WRITE_EVENT } BbVmEvent;
typedef struct {
    uint64_t address,backing_offset;
    size_t size;
    unsigned guest_permissions,effective_permissions,write_watchers;
    void *host;
    unsigned read_watchers,gpu_denied;
} BbVmView;
/* A failed observer poisons further operations: partially mirrored state cannot
 * be executed. Close is still permitted. Observers may query, never mutate. */
typedef int (*BbVmObserver)(BbGuestMemory *,BbVmEvent,uint64_t,size_t,void *);
BbVmStatus bb_vm_create(BbGuestMemory **out,size_t window_size,size_t backing_size,uint64_t preferred);
/* Logical guest addresses need not equal the private host reservation. */
BbVmStatus bb_vm_create_address_space(BbGuestMemory **out,size_t window_size,size_t backing_size,uint64_t guest_base);
BbVmStatus bb_vm_close(BbGuestMemory *vm);
uint64_t bb_vm_base(const BbGuestMemory *vm);
/* Service-owned physical allocations can outlive their last virtual alias. */
BbVmStatus bb_vm_preserve_backing(BbGuestMemory *vm);
void *bb_vm_backing_view(BbGuestMemory *vm,size_t offset,size_t size);
BbVmStatus bb_vm_discard_backing(BbGuestMemory *vm,size_t offset,size_t size);
size_t bb_vm_window_size(const BbGuestMemory *vm);
size_t bb_vm_host_page(const BbGuestMemory *vm);
BbVmStatus bb_vm_observe(BbGuestMemory *vm,BbVmObserver observer,void *user);
BbVmStatus bb_vm_detach_observer(BbGuestMemory *vm,BbVmObserver observer,void *user);
BbVmStatus bb_vm_map(BbGuestMemory *vm,uint64_t address,size_t size,size_t backing_offset,unsigned permissions);
BbVmStatus bb_vm_unmap(BbGuestMemory *vm,uint64_t address,size_t size);
BbVmStatus bb_vm_protect(BbGuestMemory *vm,uint64_t address,size_t size,unsigned permissions);
BbVmStatus bb_vm_set_gpu_permissions(BbGuestMemory *vm,uint64_t address,size_t size,int read,int write);
BbVmStatus bb_vm_watch_read(BbGuestMemory *vm,uint64_t address,size_t size,int delta);
BbVmStatus bb_vm_clear_read_watches(BbGuestMemory *vm,uint64_t address,size_t size);
BbVmStatus bb_vm_watch_write(BbGuestMemory *vm,uint64_t address,size_t size,int delta);
BbVmStatus bb_vm_clear_write_watches(BbGuestMemory *vm,uint64_t address,size_t size);
BbVmStatus bb_vm_status(BbGuestMemory *vm);
int bb_vm_contains(BbGuestMemory *vm,uint64_t address,size_t size);
BbVmStatus bb_vm_query(BbGuestMemory *vm,uint64_t address,BbVmView *view);
int bb_vm_code_write_needed(BbGuestMemory *vm,uint64_t address,size_t size);
BbVmStatus bb_vm_sync_code(BbGuestMemory *vm);
BbVmStatus bb_vm_read(BbGuestMemory *vm,uint64_t address,void *out,size_t size);
BbVmStatus bb_vm_write(BbGuestMemory *vm,uint64_t address,const void *data,size_t size);
#ifdef __cplusplus
}
#endif
#endif
