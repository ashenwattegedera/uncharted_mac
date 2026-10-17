#include "memory_probe.h"
#include "guest_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define OK(x) assert((x)==BB_PROBE_OK)
#define VM(x) assert((x)==BB_VM_OK)
#define G BB_VM_GUEST_PAGE
typedef struct { BbMemoryProbe *session; unsigned gpu_calls,demand_calls,reject; uint64_t base,pc; } State;
static int gpu(void *user,uint64_t at,uint64_t off,unsigned watchers) {
    State *s=user; ++s->gpu_calls;
    assert((at==s->base || at==s->base+4096 || at==s->base+G || at==s->base+G+4096) && off<8192);
    assert(watchers==(off ? 2u : 1u));
    const BbGuestFault *f=bb_memory_probe_last_fault(s->session);
    assert(f->pc==s->pc && f->address==s->base+G+8192 && f->size==8 && f->access==BB_VM_WRITE);
    assert(bb_guest_fault_route(bb_memory_probe_router(s->session),f)==BB_FAULT_ERROR);
    assert(bb_memory_probe_close(s->session)==BB_PROBE_INVALID);
    return !s->reject;
}
static int demand(void *user,const BbGuestFault *fault) {
    State *s=user; ++s->demand_calls;
    assert(fault->address==s->base+5*G && fault->pc==BB_PROBE_CODE && fault->access==BB_VM_READ);
    BbGuestMemory *v=bb_memory_probe_vm(s->session);
    VM(bb_vm_map(v,s->base+5*G,G,3*G,3));
    uint64_t value=42; VM(bb_vm_write(v,s->base+5*G,&value,8)); return 1;
}
static BbProbeStatus run(BbMemoryProbe *s,uint64_t target,uint64_t a,uint64_t b,uint64_t *result,BbProbeFault *fault) {
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=target,.args={a,b},.result=123};
    BbProbeStatus status=bb_memory_probe_invoke(s,&call,fault);
    *result=call.result; return status;
}
static BbAbiStatus pointer_import(const BbAbiValue *args,size_t count,BbAbiValue *result,void *user) {
    assert(count==1); uint64_t pointer,value; memcpy(&pointer,args[0].bytes,8);
    BbGuestMemory *v=user;
    if(bb_vm_read(v,pointer,&value,8)!=BB_VM_OK) return BB_ABI_MEMORY;
    ++value; if(bb_vm_write(v,pointer,&value,8)!=BB_VM_OK) return BB_ABI_MEMORY;
    int32_t error=BB_GUEST_EINVAL; memcpy(result->bytes,&error,4); return BB_ABI_OK;
}
int main(void) {
    BbMemoryProbe *s=NULL; OK(bb_memory_probe_open(&s,8*G,4*G));
    BbGuestMemory *v=bb_memory_probe_vm(s); BbCpuProbe *p=bb_memory_probe_cpu(s);
    uint64_t a=bb_vm_base(v),b=a+G,result=0; BbProbeFault fault={0};
    State state={.session=s,.base=a,.pc=BB_PROBE_CODE};
    *bb_memory_probe_router(s)=(BbFaultRouter){.memory=v,.gpu_write=gpu,.demand_map=demand,.user=&state};
    VM(bb_vm_map(v,a,G,0,3)); VM(bb_vm_map(v,b,G,0,3));
    VM(bb_vm_map(v,a+2*G,G,G,7)); VM(bb_vm_map(v,a+3*G,G,G,7));
    const unsigned char read[]={0x48,0x8b,0x07,0xc3};
    OK(bb_probe_write(p,BB_PROBE_CODE,read,sizeof(read)));
    OK(run(s,BB_PROBE_CODE,a+5*G,0,&result,&fault)); assert(result==42 && state.demand_calls==1);
    assert(bb_memory_probe_last_fault(s)->pc==BB_PROBE_CODE);
    assert(run(s,BB_PROBE_CODE,a+8*G,0,&result,&fault)==BB_PROBE_FAULT && result==123);
    assert(fault.pc==BB_PROBE_CODE && fault.address==a+8*G && state.demand_calls==1);
    uint64_t value=42; VM(bb_vm_write(v,a,&value,8));
    OK(run(s,BB_PROBE_CODE,b,0,&result,&fault)); assert(result==42);
    /* A typed import converts a guest pointer through owned memory APIs. */
    BbAbiImport binding={.name="fixture-pointer-import",.host=pointer_import,.user=v,
        .signature={.result=BB_ABI_I32,.args={BB_ABI_POINTER},.count=1,.fixed_count=1}};
    uint64_t bridge; OK(bb_memory_probe_bind_abi(s,&binding,&bridge));
    const unsigned char call[]={0x48,0x83,0xec,0x08,0xff,0xd6,0x48,0x83,0xc4,0x08,0xc3};
    OK(bb_probe_write(p,BB_PROBE_CODE,call,sizeof(call)));
    BbGuestCall invocation={.kind=BB_CALL_MODULE_INIT,.target=BB_PROBE_CODE,.args={b,bridge}};
    OK(bb_memory_probe_invoke(s,&invocation,&fault)); assert((int64_t)invocation.result==BB_GUEST_EINVAL);
    VM(bb_vm_read(v,a,&value,8)); assert(value==43);
    const unsigned char write[]={0x48,0x89,0x37,0x48,0x89,0xf0,0xc3};
    OK(bb_probe_write(p,BB_PROBE_CODE,write,sizeof(write)));
    VM(bb_vm_watch_write(v,a,4096,1));
    VM(bb_vm_watch_write(v,a+4096,4096,1)); VM(bb_vm_watch_write(v,a+4096,4096,1));
    state.reject=1;
    assert(run(s,BB_PROBE_CODE,b+8192,99,&result,&fault)==BB_PROBE_FAULT && result==123 && state.gpu_calls==1);
    BbVmView view; VM(bb_vm_query(v,a+4096,&view)); assert(view.write_watchers==2);
    state.reject=0; state.gpu_calls=0;
    OK(run(s,BB_PROBE_CODE,b+8192,99,&result,&fault)); assert(result==99 && state.gpu_calls==2);
    VM(bb_vm_read(v,a+8192,&value,8)); assert(value==99);
    VM(bb_vm_query(v,a+4096,&view)); assert(!view.write_watchers && (view.effective_permissions&BB_VM_WRITE));
    VM(bb_vm_protect(v,b,G,BB_VM_READ));
    assert(run(s,BB_PROBE_CODE,b,77,&result,&fault)==BB_PROBE_FAULT && result==123);
    assert(fault.engine_error==UC_ERR_WRITE_PROT && state.gpu_calls==2);
    VM(bb_vm_protect(v,b,G,3));
    /* A recovered atomic instruction must execute exactly once. */
    VM(bb_vm_watch_write(v,a,4096,1)); VM(bb_vm_watch_write(v,a+4096,4096,1)); VM(bb_vm_watch_write(v,a+4096,4096,1));
    const unsigned char atomic[]={0x48,0x89,0xf0,0xf0,0x48,0x0f,0xc1,0x07,0xc3};
    OK(bb_probe_write(p,BB_PROBE_CODE,atomic,sizeof(atomic))); state.pc=BB_PROBE_CODE+3;
    OK(run(s,BB_PROBE_CODE,b+8192,1,&result,&fault)); assert(result==99);
    VM(bb_vm_read(v,a+8192,&value,8)); assert(value==100);
    /* Cached code sees host patches and translated guest stores through aliases. */
    unsigned char code[]={0xb8,1,0,0,0,0xc3};
    VM(bb_vm_write(v,a+2*G,code,sizeof(code)));
    OK(run(s,a+2*G,0,0,&result,&fault)); assert(result==1);
    code[1]=2; VM(bb_vm_write(v,a+3*G,code,sizeof(code)));
    OK(run(s,a+2*G,0,0,&result,&fault)); assert(result==2);
    const unsigned char modify[]={0xc6,0x07,0x03,0xc3};
    OK(bb_probe_write(p,BB_PROBE_CODE,modify,sizeof(modify)));
    OK(run(s,BB_PROBE_CODE,a+3*G+1,0,&result,&fault));
    OK(run(s,a+2*G,0,0,&result,&fault)); assert(result==3);
    VM(bb_vm_protect(v,a+2*G,G,BB_VM_READ));
    assert(run(s,a+2*G,0,0,&result,&fault)==BB_PROBE_FAULT && fault.engine_error==UC_ERR_FETCH_PROT);
    assert(fault.pc==a+2*G);
    VM(bb_vm_protect(v,a+2*G,G,BB_VM_READ|BB_VM_EXEC));
    OK(run(s,a+2*G,0,0,&result,&fault)); assert(result==3);
    VM(bb_vm_unmap(v,a+2*G,G));
    /* Disable demand callback: an absent code page must stay an explicit failure. */
    bb_memory_probe_router(s)->demand_map=NULL;
    assert(run(s,a+2*G,0,0,&result,&fault)==BB_PROBE_FAULT && fault.engine_error==UC_ERR_FETCH_UNMAPPED);
    VM(bb_vm_map(v,a+2*G,G,G,BB_VM_READ|BB_VM_EXEC));
    OK(run(s,a+2*G,0,0,&result,&fault)); assert(result==3);
    OK(bb_memory_probe_close(s));
    puts("PASS: Darwin mappings mirrored into translated execution; attributed faults, GPU watch recovery and alias invalidation");
}
