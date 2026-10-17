#define _DARWIN_C_SOURCE
#include "guest_memory.h"
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#define OK(x) assert((x)==BB_VM_OK)
#define G BB_VM_GUEST_PAGE
static void expect_fault(uint64_t address,int write) {
    pid_t child=fork(); assert(child>=0);
    if(!child) {
        struct rlimit limit={0,0}; setrlimit(RLIMIT_CORE,&limit);
        volatile unsigned char *p=(void *)(uintptr_t)address;
        if(write) *p=99; else { volatile unsigned char value=*p; (void)value; }
        _exit(0);
    }
    int status=0; assert(waitpid(child,&status,0)==child);
    assert(WIFSIGNALED(status) && (WTERMSIG(status)==SIGBUS || WTERMSIG(status)==SIGSEGV));
}
static void *wrong_thread(void *arg) {
    BbGuestMemory *v=arg; BbVmView view;
    assert(bb_vm_query(v,bb_vm_base(v),&view)==BB_VM_WRONG_THREAD);
    assert(bb_vm_close(v)==BB_VM_WRONG_THREAD);
    return NULL;
}
static int poison(BbGuestMemory *v,BbVmEvent event,uint64_t at,size_t n,void *user) {
    (void)event; (void)n; (void)user;
    BbVmView view; OK(bb_vm_query(v,at,&view));
    assert(bb_vm_protect(v,at,G,BB_VM_READ)==BB_VM_BUSY);
    assert(bb_vm_close(v)==BB_VM_BUSY);
    return 0;
}
int main(void) {
    BbGuestMemory *v=NULL,*other=NULL;
    assert(bb_vm_create(&v,1,G,0)==BB_VM_INVALID && !v);
    OK(bb_vm_create(&v,8*G,4*G,0));
    uint64_t a=bb_vm_base(v),b=a+G;
    assert(a>=UINT64_C(0x100000000) && a+8*G<UINT64_C(0x10000000000));
    printf("host=%zu guest=%d tracker=%d base=0x%llx\n",bb_vm_host_page(v),G,BB_VM_TRACKER_PAGE,(unsigned long long)a);
    /* A second reservation cannot replace even a PROT_NONE owned window. */
    assert(bb_vm_create(&other,G,G,a)==BB_VM_SYSTEM && !other);
    assert(bb_vm_map(v,a-G,G,0,3)==BB_VM_INVALID);
    assert(bb_vm_map(v,a+8*G,G,0,3)==BB_VM_INVALID);
    assert(bb_vm_map(v,UINT64_MAX,G,0,3)==BB_VM_INVALID);
    assert(bb_vm_map(v,a,4096,0,3)==BB_VM_INVALID);
    OK(bb_vm_map(v,a,G,0,3)); OK(bb_vm_map(v,b,G,0,3));
    assert(bb_vm_map(v,a,G,0,3)==BB_VM_BUSY);
    uint64_t value=42,read=0;
    OK(bb_vm_write(v,a,&value,8)); OK(bb_vm_read(v,b,&read,8)); assert(read==42);
    assert(*(uint64_t *)(uintptr_t)b==42); /* Actual Darwin aliases, not software copies. */
    OK(bb_vm_watch_write(v,a,4096,1)); OK(bb_vm_watch_write(v,a+4096,4096,1));
    OK(bb_vm_watch_write(v,a+4096,4096,1));
    BbVmView view; OK(bb_vm_query(v,b+4096,&view)); assert(view.write_watchers==2 && !(view.effective_permissions&BB_VM_WRITE));
    assert(bb_vm_write(v,b,&value,8)==BB_VM_ACCESS);
    expect_fault(b,1);
    OK(bb_vm_watch_write(v,a,4096,-1));
    OK(bb_vm_query(v,a,&view)); assert(!view.write_watchers && !(view.effective_permissions&BB_VM_WRITE));
    OK(bb_vm_watch_write(v,b+4096,4096,-1));
    OK(bb_vm_query(v,a+4096,&view)); assert(view.write_watchers==1 && !(view.effective_permissions&BB_VM_WRITE));
    OK(bb_vm_watch_write(v,a+4096,4096,-1));
    assert(bb_vm_watch_write(v,a,4096,-1)==BB_VM_INVALID);
    /* Two aliases in one range must not cause watcher underflow on a failed removal. */
    OK(bb_vm_watch_write(v,a,4096,1));
    assert(bb_vm_watch_write(v,a,2*G,-1)==BB_VM_INVALID);
    OK(bb_vm_query(v,a,&view)); assert(view.write_watchers==1);
    OK(bb_vm_watch_write(v,a,4096,-1));
    OK(bb_vm_write(v,b,&value,8));
    OK(bb_vm_protect(v,a,G,BB_VM_READ));
    assert(bb_vm_write(v,a,&value,8)==BB_VM_ACCESS);
    OK(bb_vm_write(v,b,&value,8)); /* Per-alias guest permissions stay independent. */
    OK(bb_vm_unmap(v,a,G)); expect_fault(a,0);
    OK(bb_vm_map(v,a,G,0,3)); OK(bb_vm_read(v,a,&read,8)); assert(read==42);
    OK(bb_vm_unmap(v,a,2*G)); OK(bb_vm_map(v,a,G,0,3));
    OK(bb_vm_read(v,a,&read,8)); assert(read==0); /* Last alias released: reuse is zeroed. */
    pthread_t thread; assert(!pthread_create(&thread,NULL,wrong_thread,v)); assert(!pthread_join(thread,NULL));
    OK(bb_vm_close(v));
    OK(bb_vm_create(&v,G,G,a)); OK(bb_vm_observe(v,poison,NULL));
    assert(bb_vm_map(v,a,G,0,3)==BB_VM_POISONED);
    assert(bb_vm_read(v,a,&read,8)==BB_VM_POISONED);
    OK(bb_vm_close(v));
    puts("PASS: owned Darwin mappings, aliases, subpage watches, protection, reuse and teardown");
}
