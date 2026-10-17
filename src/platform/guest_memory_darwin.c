#define _DARWIN_C_SOURCE
#include "guest_memory.h"
#include <fcntl.h>
#include <limits.h>
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct { size_t offset; unsigned permissions; int mapped; } Page;
struct BbGuestMemory {
    void *base,*backing;
    size_t size,backing_size,page,granule;
    int fd,poisoned,notifying;
    pthread_t owner;
    Page *pages;
    unsigned *references,*watchers;
    BbVmObserver observer;
    void *observer_user;
};
static BbVmStatus check(BbGuestMemory *v) {
    if(!v) return BB_VM_INVALID;
    if(!pthread_equal(v->owner,pthread_self())) return BB_VM_WRONG_THREAD;
    return v->poisoned ? BB_VM_POISONED : BB_VM_OK;
}
static int range(BbGuestMemory *v,uint64_t at,size_t n,size_t align) {
    uint64_t base=(uintptr_t)v->base;
    return n && at>=base && at-base<v->size && n<=v->size-(at-base) &&
        !(at%align) && !(n%align);
}
static unsigned watches(BbGuestMemory *v,size_t off) {
    unsigned total=0;
    for(size_t j=off/BB_VM_TRACKER_PAGE;j<(off+v->granule)/BB_VM_TRACKER_PAGE;++j)
        if(v->watchers[j]) total=1;
    return total;
}
static unsigned effective(BbGuestMemory *v,const Page *p) {
    return p->permissions & (watches(v,p->offset) ? ~BB_VM_WRITE : UINT_MAX);
}
static int host_permissions(unsigned permissions) {
    /* Guest execute is a translator permission; never mark x86 data host executable. */
    return ((permissions & (BB_VM_READ|BB_VM_EXEC)) ? PROT_READ : 0) |
        ((permissions & BB_VM_WRITE) ? PROT_WRITE : 0);
}
static BbVmStatus notify(BbGuestMemory *v,BbVmEvent event,uint64_t a,size_t n) {
    if(!v->observer) return BB_VM_OK;
    v->notifying=1;
    int ok=v->observer(v,event,a,n,v->observer_user);
    v->notifying=0;
    if(!ok) { v->poisoned=1; return BB_VM_POISONED; }
    return BB_VM_OK;
}
static BbVmStatus update_aliases(BbGuestMemory *v) {
    for(size_t i=0;i<v->size/v->granule;++i) if(v->pages[i].mapped) {
        if(mprotect((char *)v->base+i*v->granule,v->granule,host_permissions(effective(v,&v->pages[i])))) {
            v->poisoned=1; return BB_VM_POISONED;
        }
        BbVmStatus s=notify(v,BB_VM_PROTECTION,(uintptr_t)v->base+i*v->granule,v->granule);
        if(s!=BB_VM_OK) return s;
    }
    return BB_VM_OK;
}
BbVmStatus bb_vm_create(BbGuestMemory **out,size_t n,size_t backing_size,uint64_t preferred) {
    if(!out) return BB_VM_INVALID;
    *out=NULL;
    long host_page=sysconf(_SC_PAGESIZE);
    if(host_page<=0) return BB_VM_SYSTEM;
    size_t granule=(size_t)host_page>BB_VM_GUEST_PAGE ? (size_t)host_page : BB_VM_GUEST_PAGE;
    /* Bounded foundation, not a reservation of the complete retail PS4 range. */
    if(!n || !backing_size || n>256u*1024*1024 || backing_size>256u*1024*1024 ||
       n%granule || backing_size%granule || (preferred &&
       (preferred%granule || preferred<UINT64_C(0x100000000) || preferred>=UINT64_C(0x10000000000)-n)))
        return BB_VM_INVALID;
    BbGuestMemory *v=calloc(1,sizeof(*v));
    if(!v) return BB_VM_SYSTEM;
    v->fd=-1; v->base=MAP_FAILED; v->backing=MAP_FAILED;
    v->owner=pthread_self(); v->size=n; v->backing_size=backing_size;
    v->page=(size_t)host_page; v->granule=granule;
    v->pages=calloc(n/granule,sizeof(Page));
    v->references=calloc(backing_size/granule,sizeof(unsigned));
    v->watchers=calloc(backing_size/BB_VM_TRACKER_PAGE,sizeof(unsigned));
    if(!v->pages || !v->references || !v->watchers) goto fail;
    for(unsigned i=0;i<(preferred ? 1u : 60u);++i) {
        mach_vm_address_t candidate=preferred ? preferred : UINT64_C(0x1000000000)+(uint64_t)i*UINT64_C(0x400000000);
        /* FIXED without OVERWRITE claims only an empty range. Unlike mmap's
         * advisory hint this also works when ASan interposes mmap allocation. */
        kern_return_t result=mach_vm_allocate(mach_task_self(),&candidate,n,VM_FLAGS_FIXED);
        if(result!=KERN_SUCCESS) continue;
        if(mach_vm_protect(mach_task_self(),candidate,n,FALSE,VM_PROT_NONE)!=KERN_SUCCESS) {
            mach_vm_deallocate(mach_task_self(),candidate,n); continue;
        }
        v->base=(void *)(uintptr_t)candidate; break;
    }
    if(v->base==MAP_FAILED) goto fail;
    char path[]="/tmp/bb-guest-memory-XXXXXX";
    v->fd=mkstemp(path);
    if(v->fd<0) goto fail;
    if(unlink(path)) goto fail;
    if(fcntl(v->fd,F_SETFD,FD_CLOEXEC)<0 || ftruncate(v->fd,(off_t)backing_size)) goto fail;
    v->backing=mmap(NULL,backing_size,PROT_READ|PROT_WRITE,MAP_SHARED,v->fd,0);
    if(v->backing==MAP_FAILED) goto fail;
    *out=v; return BB_VM_OK;
fail:
    bb_vm_close(v); return BB_VM_SYSTEM;
}
BbVmStatus bb_vm_close(BbGuestMemory *v) {
    if(!v) return BB_VM_OK;
    if(!pthread_equal(v->owner,pthread_self())) return BB_VM_WRONG_THREAD;
    if(v->notifying) return BB_VM_BUSY;
    int failed=0;
    if(v->base!=MAP_FAILED) failed|=munmap(v->base,v->size)!=0;
    if(v->backing!=MAP_FAILED) failed|=munmap(v->backing,v->backing_size)!=0;
    if(v->fd>=0) failed|=close(v->fd)!=0;
    free(v->pages); free(v->references); free(v->watchers); free(v);
    return failed ? BB_VM_SYSTEM : BB_VM_OK;
}
uint64_t bb_vm_base(const BbGuestMemory *v) { return v ? (uintptr_t)v->base : 0; }
size_t bb_vm_host_page(const BbGuestMemory *v) { return v ? v->page : 0; }
BbVmStatus bb_vm_observe(BbGuestMemory *v,BbVmObserver observer,void *user) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    /* Attach before mappings; replacing a live mirror would miss history. */
    for(size_t i=0;i<v->size/v->granule;++i) if(v->pages[i].mapped) return BB_VM_BUSY;
    v->observer=observer; v->observer_user=user; return BB_VM_OK;
}
BbVmStatus bb_vm_map(BbGuestMemory *v,uint64_t at,size_t n,size_t off,unsigned perm) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!range(v,at,n,v->granule) || off%v->granule || off>=v->backing_size ||
       n>v->backing_size-off || perm>7) return BB_VM_INVALID;
    size_t first=(at-(uintptr_t)v->base)/v->granule;
    for(size_t i=0;i<n/v->granule;++i) if(v->pages[first+i].mapped) return BB_VM_BUSY;
    /* Clear storage only when it has no live aliases. */
    for(size_t i=0;i<n/v->granule;++i) if(!v->references[off/v->granule+i])
        memset((char *)v->backing+off+i*v->granule,0,v->granule);
    void *p=mmap((void *)(uintptr_t)at,n,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,v->fd,(off_t)off);
    if(p==MAP_FAILED) { v->poisoned=1; return BB_VM_POISONED; }
    for(size_t i=0;i<n/v->granule;++i) {
        Page *page=&v->pages[first+i];
        *page=(Page){off+i*v->granule,perm,1};
        ++v->references[off/v->granule+i];
        if(mprotect((char *)p+i*v->granule,v->granule,host_permissions(effective(v,page)))) {
            v->poisoned=1; return BB_VM_POISONED;
        }
    }
    return notify(v,BB_VM_MAPPING,at,n);
}
static int mapped(BbGuestMemory *v,uint64_t at,size_t n) {
    if(!range(v,at,n,1)) return 0;
    size_t a=(at-(uintptr_t)v->base)/v->granule,b=(at-(uintptr_t)v->base+n-1)/v->granule;
    for(size_t i=a;i<=b;++i) if(!v->pages[i].mapped) return 0;
    return 1;
}
BbVmStatus bb_vm_unmap(BbGuestMemory *v,uint64_t at,size_t n) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!range(v,at,n,v->granule) || !mapped(v,at,n)) return BB_VM_INVALID;
    if(mmap((void *)(uintptr_t)at,n,PROT_NONE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0)==MAP_FAILED) {
        v->poisoned=1; return BB_VM_POISONED;
    }
    size_t a=(at-(uintptr_t)v->base)/v->granule;
    for(size_t i=0;i<n/v->granule;++i) {
        Page *p=&v->pages[a+i];
        if(!--v->references[p->offset/v->granule])
            memset(v->watchers+p->offset/BB_VM_TRACKER_PAGE,0,v->granule/BB_VM_TRACKER_PAGE*sizeof(unsigned));
        *p=(Page){0};
    }
    return notify(v,BB_VM_UNMAPPING,at,n);
}
BbVmStatus bb_vm_protect(BbGuestMemory *v,uint64_t at,size_t n,unsigned perm) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(perm>7 || !range(v,at,n,v->granule) || !mapped(v,at,n)) return BB_VM_INVALID;
    size_t a=(at-(uintptr_t)v->base)/v->granule;
    for(size_t i=0;i<n/v->granule;++i) v->pages[a+i].permissions=perm;
    return update_aliases(v);
}
BbVmStatus bb_vm_watch_write(BbGuestMemory *v,uint64_t at,size_t n,int delta) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if((delta!=1 && delta!=-1) || !range(v,at,n,BB_VM_TRACKER_PAGE) || !mapped(v,at,n)) return BB_VM_INVALID;
    size_t bytes=v->backing_size/BB_VM_TRACKER_PAGE*sizeof(unsigned);
    unsigned *next=malloc(bytes);
    if(!next) return BB_VM_SYSTEM;
    memcpy(next,v->watchers,bytes);
    for(size_t i=0;i<n;i+=BB_VM_TRACKER_PAGE) {
        size_t relative=(at-(uintptr_t)v->base)+i;
        Page *p=&v->pages[relative/v->granule];
        unsigned *count=&next[(p->offset+relative%v->granule)/BB_VM_TRACKER_PAGE];
        if((delta<0 && !*count) || (delta>0 && *count==UINT_MAX)) { free(next); return BB_VM_INVALID; }
        if(delta>0) ++*count; else --*count;
    }
    memcpy(v->watchers,next,bytes); free(next);
    return update_aliases(v);
}
BbVmStatus bb_vm_status(BbGuestMemory *v) { return check(v); }
int bb_vm_contains(BbGuestMemory *v,uint64_t at,size_t n) {
    return check(v)==BB_VM_OK && range(v,at,n,1);
}
BbVmStatus bb_vm_clear_write_watches(BbGuestMemory *v,uint64_t at,size_t n) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!range(v,at,n,v->granule) || !mapped(v,at,n)) return BB_VM_INVALID;
    for(size_t i=0;i<n;i+=v->granule) {
        Page *p=&v->pages[(at-(uintptr_t)v->base+i)/v->granule];
        memset(v->watchers+p->offset/BB_VM_TRACKER_PAGE,0,v->granule/BB_VM_TRACKER_PAGE*sizeof(unsigned));
    }
    return update_aliases(v);
}
BbVmStatus bb_vm_query(BbGuestMemory *v,uint64_t at,BbVmView *out) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(!out || !mapped(v,at,1)) return BB_VM_INVALID;
    size_t rel=at-(uintptr_t)v->base,index=rel/v->granule,within=rel%v->granule;
    Page *p=&v->pages[index];
    *out=(BbVmView){at,p->offset+within,v->granule-within,p->permissions,effective(v,p),
        v->watchers[(p->offset+within)/BB_VM_TRACKER_PAGE],(void *)(uintptr_t)at};
    return BB_VM_OK;
}
static BbVmStatus written(BbGuestMemory *v) {
    for(size_t i=0;i<v->size/v->granule;++i) if(v->pages[i].mapped) {
        BbVmStatus s=notify(v,BB_VM_WRITE_EVENT,(uintptr_t)v->base+i*v->granule,v->granule);
        if(s!=BB_VM_OK) return s;
    }
    return BB_VM_OK;
}
BbVmStatus bb_vm_sync_code(BbGuestMemory *v) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    return v->notifying ? BB_VM_BUSY : written(v);
}
static BbVmStatus transfer(BbGuestMemory *v,uint64_t at,void *data,size_t n,int write) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!data || !mapped(v,at,n)) return BB_VM_INVALID;
    /* Validate the entire range before copying any bytes. */
    for(size_t i=0;i<n;) {
        BbVmView view; bb_vm_query(v,at+i,&view);
        if(!(view.effective_permissions & (write ? BB_VM_WRITE : BB_VM_READ))) return BB_VM_ACCESS;
        size_t part=view.size<n-i ? view.size : n-i; i+=part;
    }
    for(size_t i=0;i<n;) {
        BbVmView view; bb_vm_query(v,at+i,&view);
        size_t part=view.size<n-i ? view.size : n-i;
        void *backing=(char *)v->backing+view.backing_offset;
        if(write) memcpy(backing,(char *)data+i,part); else memcpy((char *)data+i,backing,part);
        i+=part;
    }
    /* Invalidate every alias: the written range may contain translated code. */
    return write ? written(v) : BB_VM_OK;
}
BbVmStatus bb_vm_read(BbGuestMemory *v,uint64_t at,void *out,size_t n) { return transfer(v,at,out,n,0); }
BbVmStatus bb_vm_write(BbGuestMemory *v,uint64_t at,const void *data,size_t n) { return transfer(v,at,(void *)data,n,1); }
