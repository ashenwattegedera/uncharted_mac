#include "translated_runtime.h"
#include "runtime.h"
#include "gpu/shim/guest_fault_bridge.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned reads,writes;
static uint64_t expected_pc;
static int sink_read(void *p,uint64_t address,size_t size) {
    (void)p; assert(size==4096); ++reads;
    uint64_t value=77; assert(runtime_memory_write_backing(address,&value,8));
    runtime_memory_gpu_protect(address,size,1,0); return 1;
}
static int sink_write(void *p,uint64_t address) {
    (void)p; ++writes; runtime_memory_gpu_protect(address,4096,1,1); return 1;
}
static int renderer(uint64_t pc,uintptr_t address,uint64_t size,int write) {
    assert(pc==expected_pc);
    BbGpuGuestSink sink={(void *)1,sink_read,sink_write};
    return bb_gpu_guest_dispatch(&sink,pc,address,size,write);
}
int main(void) {
    BbTranslatedRuntime *r=NULL; assert(bb_translated_open(&r)==BB_TRANSLATED_OK);
    assert(bb_translated_attach_execution(r)==BB_TRANSLATED_OK);
    const uint64_t page=16384; void *code=NULL;
    uintptr_t flexible=runtime_memory_resolve_symbol("sceKernelMapFlexibleMemory");
    assert(!((int32_t (*)(void **,uint64_t,int,int))flexible)(&code,page,7,0));
    uintptr_t alloc=runtime_memory_resolve_symbol("sceKernelAllocateDirectMemory"),map=runtime_memory_resolve_symbol("sceKernelMapDirectMemory");
    int64_t physical=0;
    assert(!((int32_t (*)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *))alloc)(0,5056LL*1024*1024,page,page,3,&physical));
    void *a=NULL,*b=NULL;
    assert(!((int32_t (*)(void **,uint64_t,int,int,int64_t,uint64_t))map)(&a,page,3,0,physical,page));
    assert(!((int32_t (*)(void **,uint64_t,int,int,int64_t,uint64_t))map)(&b,page,3,0,physical,page));
    BbGuestMemory *vm=bb_translated_memory(r); BbVmView view;
    runtime_memory_gpu_protect((uintptr_t)a,4096,1,0); runtime_memory_gpu_protect((uintptr_t)b,4096,1,0);
    assert(bb_vm_query(vm,(uintptr_t)a,&view)==BB_VM_OK && view.write_watchers==2);
    runtime_memory_gpu_protect((uintptr_t)a,4096,1,1);
    assert(bb_vm_query(vm,(uintptr_t)a,&view)==BB_VM_OK && view.write_watchers==1 && !(view.effective_permissions&2));
    runtime_memory_gpu_protect((uintptr_t)b+8192,4096,0,0);
    const unsigned char read[]={0x48,0x8b,0x07,0xc3};
    assert(bb_vm_write(vm,(uintptr_t)code,read,sizeof(read))==BB_VM_OK); expected_pc=(uintptr_t)code;
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=(uintptr_t)code,.args={(uintptr_t)a+8192},.result=123};
    assert(bb_execution_invoke(&call)==BB_EXEC_INVALID && call.result==123); /* No renderer: fail closed. */
    runtime_memory_set_guest_gpu_fault_handler(renderer);
    assert(bb_execution_invoke(&call)==BB_EXEC_OK && call.result==77 && reads==1 && !writes);
    assert(bb_vm_query(vm,(uintptr_t)b+8192,&view)==BB_VM_OK && !view.read_watchers && view.write_watchers==1);
    const unsigned char store[]={0x48,0x89,0x37,0x48,0x89,0xf0,0xc3};
    assert(bb_vm_write(vm,(uintptr_t)code,store,sizeof(store))==BB_VM_OK);
    call.args[1]=88; assert(bb_execution_invoke(&call)==BB_EXEC_OK && call.result==88 && writes==2);
    uint64_t value=0; assert(bb_vm_read(vm,(uintptr_t)b+8192,&value,8)==BB_VM_OK && value==88);
    assert(bb_vm_query(vm,(uintptr_t)a,&view)==BB_VM_OK && !view.write_watchers);
    assert(bb_vm_protect(vm,(uintptr_t)a,page,BB_VM_READ)==BB_VM_OK);
    assert(bb_execution_invoke(&call)==BB_EXEC_INVALID && writes==2);
    BbGpuGuestSink empty={0}; assert(!bb_gpu_guest_dispatch(&empty,expected_pc,0,8,1));
    assert(!bb_gpu_guest_dispatch(&empty,0,0,8,1));
    assert(bb_translated_close(r)==BB_TRANSLATED_OK);
    puts("PASS: actual GPU protection API, per-alias read/write masks, registered guest-fault bridge and translated restart");
}
