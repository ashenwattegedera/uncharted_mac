#define _DARWIN_C_SOURCE
#include "runtime.h"
#include "guest_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define GET(type,name) ((type)runtime_memory_resolve_symbol(name))
typedef int32_t (*Allocate)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *);
typedef int32_t (*Map)(void **,uint64_t,int,int,int64_t,uint64_t);
typedef int32_t (*Release)(uint64_t,uint64_t);
typedef int32_t (*Unmap)(void *,uint64_t);
typedef int32_t (*Flexible)(void **,uint64_t,int,int);
typedef int32_t (*Reserve)(void **,uint64_t,int,uint64_t);
typedef int32_t (*Protect)(const void *,uint64_t,int);
typedef int32_t (*Query)(const void *,int,BbGuestVirtualQuery *,uint64_t);
typedef int32_t (*Batch)(BbGuestBatchEntry *,int,int *,int);
int main(void) {
    const uint64_t page=16384;
    runtime_memory_reserve(); BbGuestMemory *vm=runtime_memory_darwin_vm(); assert(vm);
    uint64_t total=GET(uint64_t (*)(void),"sceKernelGetDirectMemorySize")(); assert(total==5056ULL*1024*1024);
    Allocate alloc=GET(Allocate,"sceKernelAllocateDirectMemory"); Map map=GET(Map,"sceKernelMapDirectMemory");
    Release release=GET(Release,"sceKernelReleaseDirectMemory"); Unmap unmap=GET(Unmap,"sceKernelMunmap");
    int64_t physical=-1; assert(!alloc(0,(int64_t)total,3*page,page,3,&physical));
    void *a=NULL,*b=NULL; assert(!map(&a,page,3,0,physical,page)); assert(!map(&b,page,3,0,physical,page));
    BbVmView host; uint64_t value=42;
    assert(bb_vm_query(vm,(uintptr_t)a,&host)==BB_VM_OK && host.host!=a);
    assert(bb_vm_write(vm,(uintptr_t)a,&value,8)==BB_VM_OK);
    assert(bb_vm_read(vm,(uintptr_t)b,&value,8)==BB_VM_OK && value==42);
    assert(runtime_memory_read_backing((uintptr_t)b,&value,8) && value==42);
    assert(!runtime_memory_read_backing(UINTPTR_MAX,&value,8));
    assert(!unmap(a,page)); a=NULL; assert(!map(&a,page,3,0,physical,page)); assert(bb_vm_read(vm,(uintptr_t)a,&value,8)==BB_VM_OK && value==42);
    void *outside=(void *)0x100000000ULL;
    assert((uint32_t)map(&outside,page,3,0x10,physical,page)==0x8002000c && bb_vm_read(vm,(uintptr_t)a,&value,8)==BB_VM_OK && value==42);
    Protect protect=GET(Protect,"sceKernelMprotect"); assert(!protect(a,page,1));
    BbVmView view; assert(bb_vm_query(vm,(uintptr_t)a,&view)==BB_VM_OK && !(view.effective_permissions&BB_VM_WRITE));
    assert(!protect(a,page,3));
    BbGuestVirtualQuery info; assert(!GET(Query,"sceKernelVirtualQuery")(a,0,&info,sizeof(info)));
    assert(info.offset==(uint64_t)physical && info.flags==18 && info.memory_type==3);
    assert(!release(physical,page)); assert(!runtime_memory_is_mapped((uintptr_t)a,page));
    assert(!runtime_memory_is_mapped((uintptr_t)b,page));
    int64_t reused=-1; assert(!alloc(0,(int64_t)total,page,page,3,&reused)); assert(reused==physical);
    a=NULL; assert(!map(&a,page,3,0,reused,page)); assert(bb_vm_read(vm,(uintptr_t)a,&value,8)==BB_VM_OK && !value);
    void *reservation=NULL; assert(!GET(Reserve,"sceKernelReserveVirtualRange")(&reservation,2*page,0,page));
    void *flex=NULL; assert(!GET(Flexible,"sceKernelMapFlexibleMemory")(&flex,2*page,3,0));
    unsigned char bytes[32768]; memset(bytes,0x5a,sizeof(bytes));
    assert(bb_vm_write(vm,(uintptr_t)flex,bytes,sizeof(bytes))==BB_VM_OK);
    BbGuestBatchEntry entries[]={ {.start=(uintptr_t)flex,.length=page,.protection=1,.operation=2},
        {.start=(uintptr_t)flex+page,.length=page,.operation=1}, {.start=(uintptr_t)flex,.length=page,.operation=99}};
    int processed=-1; assert((uint32_t)GET(Batch,"sceKernelBatchMap2")(entries,3,&processed,0x10)==0x80020016 && processed==2);
    assert(bb_vm_query(vm,(uintptr_t)flex,&view)==BB_VM_OK && !(view.effective_permissions&2));
    assert(!runtime_memory_is_mapped((uintptr_t)flex+page,page));
    assert(!unmap(flex,page)); assert(!unmap(reservation,2*page));
    assert(!release(reused,page)); assert(!release(physical+page,2*page));
    assert((uint32_t)unmap((void *)(UINTPTR_MAX-page+1),page)==0x80020016);
    assert(runtime_memory_darwin_shutdown()==0);
    runtime_memory_reserve(); assert(runtime_memory_darwin_shutdown()==0);
    puts("PASS: actual PS4 memory exports on owned Darwin multi-GiB backing, aliases, reservation, flexible/batch/protect/query/release");
}
