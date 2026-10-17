#include "memory_probe.h"
#include <assert.h>
#include <stdio.h>
#define G BB_VM_GUEST_PAGE
int main(void) {
    BbGuestMemory *vm=NULL; BbMemoryProbe *session=NULL;
    const uint64_t low=0x400000,limit=UINT64_C(0x10000000000),last=limit-G;
    assert(bb_vm_create_address_space(&vm,limit-low,2*G,low)==BB_VM_OK);
    assert(bb_memory_probe_attach(&session,vm)==BB_PROBE_OK);
    /* Insert out of order and exercise the sparse vector's first/last entries. */
    assert(bb_vm_map(vm,last,G,0,7)==BB_VM_OK);
    assert(bb_vm_map(vm,low,G,0,7)==BB_VM_OK);
    BbVmView view; assert(bb_vm_query(vm,low,&view)==BB_VM_OK && (uintptr_t)view.host!=low);
    const unsigned char code[]={0xb8,42,0,0,0,0xc3};
    assert(bb_vm_write(vm,low,code,sizeof(code))==BB_VM_OK);
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=last};
    assert(bb_memory_probe_invoke(session,&call,NULL)==BB_PROBE_OK && call.result==42);
    assert(bb_vm_map(vm,low+2*G,G,G,3)==BB_VM_OK);
    assert(bb_vm_map(vm,low+G,G,G,3)==BB_VM_OK);
    assert(bb_vm_unmap(vm,low+G,2*G)==BB_VM_OK);
    assert(bb_vm_query(vm,low+G,&view)==BB_VM_INVALID);
    assert(bb_vm_map(vm,limit,G,0,3)==BB_VM_INVALID);
    assert(bb_vm_unmap(vm,low,G)==BB_VM_OK);
    call.result=0; assert(bb_memory_probe_invoke(session,&call,NULL)==BB_PROBE_OK && call.result==42);
    assert(bb_memory_probe_close(session)==BB_PROBE_OK);
    assert(bb_vm_close(vm)==BB_VM_OK);
    puts("PASS: sparse 1 TiB logical window, low image and high alias execution with separate host addresses");
}
