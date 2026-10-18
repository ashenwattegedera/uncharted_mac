#include "translated_tasks.h"
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
static void *claim_poisoned(void *p) {
    BbTranslatedRuntime *r=p;
    assert(bb_translated_close(r)==BB_TRANSLATED_INVALID);
    assert(bb_translated_acquire(r)==BB_TRANSLATED_MEMORY);
    assert(!bb_translated_cpu(r));
    assert(bb_translated_release(r)==BB_TRANSLATED_OK); return NULL;
}
int main(void) {
    BbTranslatedRuntime *r=NULL; assert(bb_translated_open(&r)==BB_TRANSLATED_OK);
    BbGuestMemory *vm=bb_translated_memory(r); uint64_t address=bb_vm_base(vm);
    BbCpuProbe *cpu=bb_translated_cpu(r);
    /* Deliberately desynchronize the CPU mirror to exercise the fail-closed
     * observer path. This is fault injection, never a normal service operation. */
    assert(bb_vm_map(vm,address,BB_VM_GUEST_PAGE,0,3)==BB_VM_OK);
    assert(bb_probe_unmap(cpu,address,BB_VM_GUEST_PAGE)==BB_PROBE_OK);
    assert(bb_vm_protect(vm,address,BB_VM_GUEST_PAGE,1)==BB_VM_POISONED);
    assert(bb_translated_release(r)==BB_TRANSLATED_OK);
    pthread_t thread; assert(!pthread_create(&thread,NULL,claim_poisoned,r)); assert(!pthread_join(thread,NULL));
    assert(bb_translated_acquire(r)==BB_TRANSLATED_MEMORY);
    assert(bb_translated_close(r)==BB_TRANSLATED_OK);
    puts("PASS: poisoned VM lease handoff remains teardown-only and closes on the final owner");
}
