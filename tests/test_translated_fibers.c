#include "translated_tasks.h"
#include "runtime.h"
#include "m3_fibers_text.h"
#include "fixtures/m3_fiber_types.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define OK(x) assert((x)==BB_TRANSLATED_OK)
#define G BB_VM_GUEST_PAGE
static U allocate(U size,int permissions) {
    void *p=NULL; uintptr_t map=runtime_memory_resolve_symbol("sceKernelMapFlexibleMemory");
    assert(!((int (*)(void **,U,int,int))map)(&p,size,permissions,0)); return (uintptr_t)p;
}
int main(int argc,char **argv) {
    int switching=argc==2 && !strcmp(argv[1],"switch");
    int guard=argc==2 && !strcmp(argv[1],"guard");
    BbTranslatedRuntime *r=NULL; OK(bb_translated_open(&r)); BbTasks *tasks=bb_translated_tasks(r);
    U initial=13; OK(bb_tasks_set_tls(tasks,1,&initial,8,16,16));
    U base=allocate(2*G,7); BbGuestMemory *vm=bb_translated_memory(r);
    assert(sizeof(guest_abi_text)<G && sizeof(FiberShared)<G);
    assert(bb_vm_write(vm,base,guest_abi_text,sizeof(guest_abi_text))==BB_VM_OK);
    FiberShared data={.worker=base+GUEST_FIBER_WORKER,.entry=base+(switching ? GUEST_FIBER_A : GUEST_FIBER_ENTRY),.entry_b=base+GUEST_FIBER_B};
    strcpy(data.name,"synthetic-fiber");
    const char *threads[]={"scePthreadCreate","scePthreadJoin","scePthreadSelf","scePthreadYield"};
    for(size_t i=0;i<4;++i) { U address=0; OK(bb_translated_bind(r,threads[i],BB_IMPORT_FUNCTION,&address)); memcpy((char *)&data.threads+i*8,&address,8); }
    const char *fibers[]={"_sceFiberInitializeImpl","sceFiberFinalize","sceFiberRun","sceFiberSwitch","sceFiberReturnToThread","sceFiberGetSelf","_sceFiberAttachContextAndRun","sceFiberGetInfo","sceFiberOptParamInitialize","sceFiberStartContextSizeCheck","sceFiberStopContextSizeCheck"};
    for(size_t i=0;i<sizeof(fibers)/sizeof(*fibers);++i) { U address=0; OK(bb_translated_bind(r,fibers[i],BB_IMPORT_FUNCTION,&address)); memcpy((char *)&data.fibers+i*8,&address,8); }
    for(unsigned i=0;i<2;++i) {
        data.stacks[i]=allocate(65536,3); data.jobs[i].number=i+1; data.jobs[i].shared=(void *)(uintptr_t)(base+G);
    }
    assert(bb_vm_write(vm,base+G,&data,sizeof(data))==BB_VM_OK); assert(bb_vm_protect(vm,base,G,5)==BB_VM_OK);
    U args[3]={base+G,0,0},result=999;
    BbTranslatedStatus s=bb_tasks_call(tasks,base+(guard ? GUEST_FIBER_BAD_STACK : switching ? GUEST_FIBER_SWITCH_MAIN : GUEST_FIBER_MAIN),args,3,&result);
    if(s!=BB_TRANSLATED_OK) fprintf(stderr,"%s\n",bb_tasks_diagnostic(tasks));
    if(result) fprintf(stderr,"guest fiber result=%llu\n",(unsigned long long)result);
    assert(s==BB_TRANSLATED_OK && !result);
    assert(bb_vm_read(vm,base+G,&data,sizeof(data))==BB_VM_OK && !data.errors);
    if(!switching && !guard) {
        assert(data.visits[0]==12 && data.visits[1]==12 && data.jobs[0].ok && data.jobs[1].ok && data.busy);
        U record=base+G+offsetof(FiberShared,records);
        assert(bb_tasks_fiber_worker_mask(tasks,record)==7);
        OK(bb_tasks_call(tasks,base+GUEST_FIBER_QUERY,args,2,&result)); assert(!result);
        assert(bb_vm_read(vm,base+G,&data,sizeof(data))==BB_VM_OK && data.info[4]==65536 && data.info[9]<65536);
    } else if(switching) assert(data.order==123);
    OK(bb_tasks_finalize(tasks)); OK(bb_translated_close(r));
    puts("PASS: virtual fiber stacks, complete saved CPU state, cross-thread/worker migration with current-thread FS, FP isolation, return/switch arguments, attach/info/options and stack checks");
}
