#include "memory_probe.h"
#include <stdlib.h>
struct BbMemoryProbe { BbCpuProbe *cpu; BbGuestMemory *vm; BbFaultRouter router; BbGuestFault last; int active,owns_vm; };
static int mirror(BbGuestMemory *vm,BbVmEvent event,uint64_t at,size_t size,void *user) {
    BbMemoryProbe *s=user;
    if(event==BB_VM_UNMAPPING) return bb_probe_unmap(s->cpu,at,size)==BB_PROBE_OK;
    if(event==BB_VM_WRITE_EVENT) return bb_probe_invalidate(s->cpu,at,size)==BB_PROBE_OK;
    for(size_t i=0;i<size;) {
        BbVmView view; if(bb_vm_query(vm,at+i,&view)!=BB_VM_OK) return 0;
        size_t part=view.size<size-i ? view.size : size-i;
        BbProbeStatus status=event==BB_VM_MAPPING ?
            bb_probe_map_host(s->cpu,at+i,part,view.effective_permissions,view.host) :
            bb_probe_protect(s->cpu,at+i,part,view.effective_permissions);
        if(status!=BB_PROBE_OK) return 0;
        i+=part;
    }
    return 1;
}
static int fault(BbCpuProbe *cpu,const BbProbeFault *f,void *user) {
    (void)cpu;
    BbMemoryProbe *s=user;
    unsigned access=0;
    switch(f->access) {
    case UC_MEM_WRITE_PROT: case UC_MEM_WRITE_UNMAPPED: access=BB_VM_WRITE; break;
    case UC_MEM_READ_PROT: case UC_MEM_READ_UNMAPPED: access=BB_VM_READ; break;
    case UC_MEM_FETCH_PROT: case UC_MEM_FETCH_UNMAPPED: access=BB_VM_EXEC; break;
    default: return 0;
    }
    s->last=(BbGuestFault){f->pc,f->address,f->size,access};
    return bb_guest_fault_route(&s->router,&s->last)==BB_FAULT_RECOVERED;
}
static int guest_write(BbCpuProbe *cpu,uint64_t at,size_t size,void *user) {
    (void)cpu;
    BbMemoryProbe *s=user;
    (void)at; (void)size;
    return bb_vm_sync_code(s->vm)==BB_VM_OK;
}
static int code_write(void *user,uint64_t at,size_t size) {
    BbMemoryProbe *s=user; return bb_vm_code_write_needed(s->vm,at,size);
}
static BbProbeStatus attach(BbMemoryProbe **out,BbGuestMemory *vm,int owned) {
    if(!out || !vm || bb_vm_status(vm)!=BB_VM_OK) return BB_PROBE_INVALID;
    *out=NULL; BbMemoryProbe *s=calloc(1,sizeof(*s)); if(!s) return BB_PROBE_ENGINE;
    s->vm=vm; s->owns_vm=owned;
    BbProbeStatus status=bb_probe_open(&s->cpu);
    if(status==BB_PROBE_OK && bb_vm_observe(s->vm,mirror,s)!=BB_VM_OK) status=BB_PROBE_ENGINE;
    if(status==BB_PROBE_OK) status=bb_probe_watch_writes(s->cpu,bb_vm_base(s->vm),bb_vm_base(s->vm)+bb_vm_window_size(s->vm)-1,guest_write,s);
    if(status==BB_PROBE_OK) status=bb_probe_filter_writes(s->cpu,code_write,s);
    if(status!=BB_PROBE_OK) { bb_vm_detach_observer(vm,mirror,s); bb_probe_close(s->cpu); if(owned) bb_vm_close(vm); free(s); return status; }
    s->router.memory=s->vm; bb_probe_set_fault_handler(s->cpu,fault,s);
    *out=s; return BB_PROBE_OK;
}
BbProbeStatus bb_memory_probe_attach(BbMemoryProbe **out,BbGuestMemory *vm) { return attach(out,vm,0); }
BbProbeStatus bb_memory_probe_open(BbMemoryProbe **out,size_t window,size_t backing) {
    if(!out) return BB_PROBE_INVALID;
    *out=NULL; BbGuestMemory *vm=NULL;
    if(bb_vm_create(&vm,window,backing,0)!=BB_VM_OK) return BB_PROBE_ENGINE;
    return attach(out,vm,1);
}
BbProbeStatus bb_memory_probe_close(BbMemoryProbe *s) {
    if(!s) return BB_PROBE_OK;
    if(s->active) return BB_PROBE_INVALID;
    if(bb_vm_status(s->vm)==BB_VM_WRONG_THREAD) return BB_PROBE_INVALID;
    bb_probe_close(s->cpu);
    BbVmStatus status=s->owns_vm ? bb_vm_close(s->vm) : bb_vm_detach_observer(s->vm,mirror,s);
    free(s);
    return status==BB_VM_OK ? BB_PROBE_OK : BB_PROBE_ENGINE;
}
BbGuestMemory *bb_memory_probe_vm(BbMemoryProbe *s) { return s ? s->vm : NULL; }
BbCpuProbe *bb_memory_probe_cpu(BbMemoryProbe *s) { return s ? s->cpu : NULL; }
BbFaultRouter *bb_memory_probe_router(BbMemoryProbe *s) { return s ? &s->router : NULL; }
const BbGuestFault *bb_memory_probe_last_fault(BbMemoryProbe *s) { return s ? &s->last : NULL; }
static int abi_range(void *user,uint64_t at,size_t n,int write) {
    BbMemoryProbe *s=user;
    if(bb_vm_status(s->vm)!=BB_VM_OK || !n || n>UINT64_MAX-at) return 0;
    if(bb_vm_contains(s->vm,at,n)) {
        for(size_t i=0;i<n;) {
            BbVmView view;
            if(bb_vm_query(s->vm,at+i,&view)!=BB_VM_OK ||
               !(view.effective_permissions&(write ? BB_VM_WRITE : BB_VM_READ))) return 0;
            size_t part=view.size<n-i ? view.size : n-i; i+=part;
        }
        return 1;
    }
    return at>=BB_PROBE_STACK && at<BB_PROBE_STACK+8*65536 && n<=BB_PROBE_STACK+8*65536-at;
}
static int abi_read(void *user,uint64_t at,void *out,size_t n) {
    BbMemoryProbe *s=user;
    if(!abi_range(s,at,n,0)) return 0;
    return bb_vm_contains(s->vm,at,n) ? bb_vm_read(s->vm,at,out,n)==BB_VM_OK :
        bb_probe_read(s->cpu,at,out,n)==BB_PROBE_OK;
}
static int abi_write(void *user,uint64_t at,const void *in,size_t n) {
    BbMemoryProbe *s=user;
    if(!abi_range(s,at,n,1)) return 0;
    return bb_vm_contains(s->vm,at,n) ? bb_vm_write(s->vm,at,in,n)==BB_VM_OK :
        bb_probe_write(s->cpu,at,in,n)==BB_PROBE_OK;
}
BbAbiMemory bb_memory_probe_memory(BbMemoryProbe *s) {
    return (BbAbiMemory){abi_range,abi_read,abi_write,s};
}
BbProbeStatus bb_memory_probe_bind_abi(BbMemoryProbe *s,const BbAbiImport *binding,uint64_t *address) {
    if(!s || bb_vm_status(s->vm)!=BB_VM_OK) return BB_PROBE_INVALID;
    BbAbiMemory memory={abi_range,abi_read,abi_write,s};
    return bb_probe_bind_abi(s->cpu,binding,&memory,address);
}
BbProbeStatus bb_memory_probe_invoke(BbMemoryProbe *s,BbGuestCall *call,BbProbeFault *out) {
    if(!s || bb_vm_status(s->vm)!=BB_VM_OK) return BB_PROBE_INVALID;
    ++s->active;
    BbProbeStatus status=bb_probe_invoke(s->cpu,call,out);
    --s->active; return status;
}
