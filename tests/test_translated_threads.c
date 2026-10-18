#include "translated_tasks.h"
#include "runtime.h"
#include "m3_threads_text.h"
#include "fixtures/m3_thread_types.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define OK(x) assert((x)==BB_TRANSLATED_OK)
#define G BB_VM_GUEST_PAGE
int main(int argc,char **argv) {
    BbTranslatedRuntime *r=NULL; OK(bb_translated_open(&r));
    BbTasks *tasks=bb_translated_tasks(r); U initial=13,mod=17;
    OK(bb_tasks_set_tls(tasks,1,&initial,8,16,16)); OK(bb_tasks_set_tls(tasks,2,&mod,8,16,16));
    void *image=NULL; uintptr_t map=runtime_memory_resolve_symbol("sceKernelMapFlexibleMemory");
    assert(!((int (*)(void **,U,int,int))map)(&image,2*G,7,0));
    U base=(uintptr_t)image; BbGuestMemory *vm=bb_translated_memory(r);
    assert(sizeof(guest_abi_text)<G && sizeof(ThreadShared)<G);
    assert(bb_vm_write(vm,base,guest_abi_text,sizeof(guest_abi_text))==BB_VM_OK);
    ThreadShared data={.worker=base+GUEST_THREAD_WORKER,.destructor=base+GUEST_THREAD_DESTRUCTOR};
    const char *names[]={"scePthreadCreate","scePthreadJoin","scePthreadSelf","scePthreadYield","scePthreadExit",
        "scePthreadKeyCreate","scePthreadSetspecific","scePthreadGetspecific","__tls_get_addr","__error",
        "scePthreadMutexInit","scePthreadMutexLock","scePthreadMutexUnlock","scePthreadCondInit","scePthreadCondWait","scePthreadCondBroadcast","pthread_join"};
    _Static_assert(sizeof(ThreadImports)==sizeof(names)/sizeof(*names)*8,"fixture import layout");
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i) {
        U address=0; OK(bb_translated_bind(r,names[i],BB_IMPORT_FUNCTION,&address)); memcpy((char *)&data.imports+i*8,&address,8);
    }
    for(unsigned i=0;i<3;++i) { data.jobs[i].number=i+1; data.jobs[i].shared=(void *)(uintptr_t)(base+G); }
    assert(bb_vm_write(vm,base+G,&data,sizeof(data))==BB_VM_OK);
    assert(bb_vm_protect(vm,base,G,5)==BB_VM_OK);
    U args[3]={base+G,0,0},result=999;
    if(argc==2) {
        U target=!strcmp(argv[1],"deadlock") ? GUEST_THREAD_DEADLOCK : !strcmp(argv[1],"budget") ? GUEST_THREAD_BUDGET : !strcmp(argv[1],"bad-import") ? GUEST_THREAD_BAD_IMPORT : GUEST_THREAD_FAULT;
        U id=0; OK(bb_tasks_spawn(tasks,base+target,args,&id));
        assert(bb_tasks_run(tasks,2,10)==BB_TRANSLATED_EXECUTION);
        assert(strstr(bb_tasks_diagnostic(tasks),!strcmp(argv[1],"deadlock") ? "deadlock" : !strcmp(argv[1],"budget") ? "budget" : !strcmp(argv[1],"bad-import") ? "__tls_get_addr" : "guest fault"));
        OK(bb_translated_close(r)); puts("PASS: scheduler failure is bounded and diagnostic"); return 0;
    }
    BbTranslatedStatus s=bb_tasks_call(tasks,base+GUEST_THREAD_MAIN,args,3,&result);
    if(s!=BB_TRANSLATED_OK) fprintf(stderr,"%s\n",bb_tasks_diagnostic(tasks));
    assert(s==BB_TRANSLATED_OK); if(result) fprintf(stderr,"guest status=%llu\n",(unsigned long long)result); assert(!result);
    assert(bb_vm_read(vm,base+G,&data,sizeof(data))==BB_VM_OK);
    assert(data.counter==36 && data.atomic_counter==36 && data.tsc_before==1000 && data.tsc_after==2000);
    for(unsigned i=0;i<3;++i) assert(data.jobs[i].ok && data.jobs[i].dtor_calls==(i ? 1 : 2));
    for(unsigned i=0;i<3;++i) {
        U guard=(data.jobs[i].stack_pointer+G-1)&~(U)(G-1); uint32_t permissions=99;
        uintptr_t query=runtime_memory_resolve_symbol("sceKernelQueryMemoryProtection");
        assert(!((int32_t (*)(void *,void **,void **,uint32_t *))query)((void *)(uintptr_t)guard,NULL,NULL,&permissions) && !permissions);
        BbVmView view; assert(bb_vm_query(vm,guard,&view)==BB_VM_OK && !view.guest_permissions);
    }
    for(U id=2;id<=4;++id) assert(bb_tasks_worker_mask(tasks,id)==7);
    assert(data.jobs[0].tcb!=data.jobs[1].tcb && data.jobs[1].tcb!=data.jobs[2].tcb);
    OK(bb_tasks_finalize(tasks)); OK(bb_translated_close(r));
    puts("PASS: native worker handoff, translated create/join/exit, FS/static/module TLS, errno, repeated key destructors, mutex/condition contention, atomic xadd, FP/XMM/x87 state and deterministic guest TSC");
}
