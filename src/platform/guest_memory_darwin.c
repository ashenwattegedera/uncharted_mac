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

typedef struct { size_t index,offset; unsigned permissions; } Page;
struct BbGuestMemory {
    void *base,*backing;
    size_t size,backing_size,page,granule,page_count,page_capacity;
    uint64_t guest_base;
    int fd,poisoned,notifying,preserve_backing;
    pthread_t owner;
    Page *pages;
    unsigned *references,*watchers,*executable,*read_watchers;
    unsigned char *gpu_denied;
    BbVmObserver observer;
    void *observer_user;
};
static BbVmStatus check(BbGuestMemory *v) {
    if(!v) return BB_VM_INVALID;
    if(!pthread_equal(v->owner,pthread_self())) return BB_VM_WRONG_THREAD;
    return v->poisoned ? BB_VM_POISONED : BB_VM_OK;
}
static int range(BbGuestMemory *v,uint64_t at,size_t n,size_t align) {
    uint64_t base=v->guest_base;
    return n && at>=base && at-base<v->size && n<=v->size-(at-base) &&
        !(at%align) && !(n%align);
}
/* Sorted live mappings keep metadata proportional to committed guest pages. */
static size_t lower_bound(BbGuestMemory *v,size_t index) {
    size_t a=0,b=v->page_count;
    while(a<b) { size_t m=a+(b-a)/2; if(v->pages[m].index<index) a=m+1; else b=m; }
    return a;
}
static Page *page_at(BbGuestMemory *v,size_t index) {
    size_t i=lower_bound(v,index);
    return i<v->page_count && v->pages[i].index==index ? &v->pages[i] : NULL;
}
static void *host_at(BbGuestMemory *v,uint64_t address) {
    return (char *)v->base+(address-v->guest_base);
}
static unsigned watches(BbGuestMemory *v,size_t off,const unsigned *counts) {
    unsigned total=0;
    for(size_t j=off/BB_VM_TRACKER_PAGE;j<(off+v->granule)/BB_VM_TRACKER_PAGE;++j)
        if(counts[j]) total=1;
    return total;
}
static unsigned effective(BbGuestMemory *v,const Page *p) {
    unsigned permissions=p->permissions;
    if(watches(v,p->offset,v->watchers)) permissions&=~BB_VM_WRITE;
    /* Darwin cannot enforce read-denied/write-allowed pages. A read watch
     * therefore blocks all host accesses until synchronization completes. */
    if(watches(v,p->offset,v->read_watchers)) permissions=0;
    return permissions;
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
    for(size_t i=0;i<v->page_count;++i) {
        if(mprotect((char *)v->base+v->pages[i].index*v->granule,v->granule,host_permissions(effective(v,&v->pages[i])))) {
            v->poisoned=1; return BB_VM_POISONED;
        }
        BbVmStatus s=notify(v,BB_VM_PROTECTION,v->guest_base+v->pages[i].index*v->granule,v->granule);
        if(s!=BB_VM_OK) return s;
    }
    return BB_VM_OK;
}
static BbVmStatus destroy(BbGuestMemory *v);
static BbVmStatus create(BbGuestMemory **out,size_t n,size_t backing_size,uint64_t preferred,int separate) {
    if(!out) return BB_VM_INVALID;
    *out=NULL;
    long host_page=sysconf(_SC_PAGESIZE);
    if(host_page<=0) return BB_VM_SYSTEM;
    size_t granule=(size_t)host_page>BB_VM_GUEST_PAGE ? (size_t)host_page : BB_VM_GUEST_PAGE;
    const uint64_t limit=UINT64_C(0x10000000000);
    if(!n || !backing_size || n>limit || backing_size>UINT64_C(32)*1024*1024*1024 ||
       n%granule || backing_size%granule || preferred%granule ||
       (separate ? (preferred>=limit || n>limit-preferred) :
        (n>UINT64_C(64)*1024*1024*1024 || (preferred &&
         (preferred<UINT64_C(0x100000000) || preferred>=limit-n))))) return BB_VM_INVALID;
    BbGuestMemory *v=calloc(1,sizeof(*v));
    if(!v) return BB_VM_SYSTEM;
    v->fd=-1; v->base=MAP_FAILED; v->backing=MAP_FAILED;
    v->owner=pthread_self(); v->size=n; v->backing_size=backing_size;
    v->page=(size_t)host_page; v->granule=granule;
    v->references=calloc(backing_size/granule,sizeof(unsigned));
    v->watchers=calloc(backing_size/BB_VM_TRACKER_PAGE,sizeof(unsigned));
    v->executable=calloc(backing_size/granule,sizeof(unsigned));
    v->read_watchers=calloc(backing_size/BB_VM_TRACKER_PAGE,sizeof(unsigned));
    v->gpu_denied=calloc(n/BB_VM_TRACKER_PAGE,1);
    if(!v->references || !v->watchers || !v->executable || !v->read_watchers || !v->gpu_denied) goto fail;
    for(unsigned i=0;i<(separate || preferred ? 1u : 60u);++i) {
        mach_vm_address_t candidate=preferred ? preferred : UINT64_C(0x1000000000)+(uint64_t)i*UINT64_C(0x400000000);
        /* FIXED without OVERWRITE claims only an empty range. Unlike mmap's
         * advisory hint this also works when ASan interposes mmap allocation. */
        kern_return_t result=mach_vm_allocate(mach_task_self(),&candidate,n,separate ? VM_FLAGS_ANYWHERE : VM_FLAGS_FIXED);
        if(result!=KERN_SUCCESS) continue;
        if(mach_vm_protect(mach_task_self(),candidate,n,FALSE,VM_PROT_NONE)!=KERN_SUCCESS) {
            mach_vm_deallocate(mach_task_self(),candidate,n); continue;
        }
        v->base=(void *)(uintptr_t)candidate; break;
    }
    if(v->base==MAP_FAILED) goto fail;
    v->guest_base=separate ? preferred : (uintptr_t)v->base;
    char path[]="/tmp/bb-guest-memory-XXXXXX";
    v->fd=mkstemp(path);
    if(v->fd<0) goto fail;
    if(unlink(path)) goto fail;
    if(fcntl(v->fd,F_SETFD,FD_CLOEXEC)<0 || ftruncate(v->fd,(off_t)backing_size)) goto fail;
    v->backing=mmap(NULL,backing_size,PROT_READ|PROT_WRITE,MAP_SHARED,v->fd,0);
    if(v->backing==MAP_FAILED) goto fail;
    *out=v; return BB_VM_OK;
fail:
    destroy(v); return BB_VM_SYSTEM;
}
BbVmStatus bb_vm_create(BbGuestMemory **out,size_t n,size_t backing,uint64_t preferred) {
    return create(out,n,backing,preferred,0);
}
BbVmStatus bb_vm_create_address_space(BbGuestMemory **out,size_t n,size_t backing,uint64_t guest_base) {
    return create(out,n,backing,guest_base,1);
}
BbVmStatus bb_vm_close(BbGuestMemory *v) {
    if(!v) return BB_VM_OK;
    if(!pthread_equal(v->owner,pthread_self())) return BB_VM_WRONG_THREAD;
    if(v->notifying) return BB_VM_BUSY;
    return destroy(v);
}
static BbVmStatus destroy(BbGuestMemory *v) {
    int failed=0;
    if(v->base!=MAP_FAILED) failed|=munmap(v->base,v->size)!=0;
    if(v->backing!=MAP_FAILED) failed|=munmap(v->backing,v->backing_size)!=0;
    if(v->fd>=0) failed|=close(v->fd)!=0;
    free(v->pages); free(v->references); free(v->watchers); free(v->executable); free(v->read_watchers); free(v->gpu_denied); free(v);
    return failed ? BB_VM_SYSTEM : BB_VM_OK;
}
uint64_t bb_vm_base(const BbGuestMemory *v) { return v ? v->guest_base : 0; }
size_t bb_vm_window_size(const BbGuestMemory *v) { return v ? v->size : 0; }
BbVmStatus bb_vm_preserve_backing(BbGuestMemory *v) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(v->page_count) return BB_VM_BUSY;
    v->preserve_backing=1; return BB_VM_OK;
}
void *bb_vm_backing_view(BbGuestMemory *v,size_t off,size_t size) {
    if(check(v)!=BB_VM_OK || !size || off>=v->backing_size || size>v->backing_size-off) return NULL;
    return (char *)v->backing+off;
}
BbVmStatus bb_vm_discard_backing(BbGuestMemory *v,size_t off,size_t size) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!size || off%v->granule || size%v->granule || off>=v->backing_size || size>v->backing_size-off) return BB_VM_INVALID;
    for(size_t i=off/v->granule;i<(off+size)/v->granule;++i) if(v->references[i]) return BB_VM_BUSY;
    memset((char *)v->backing+off,0,size); return BB_VM_OK;
}
size_t bb_vm_host_page(const BbGuestMemory *v) { return v ? v->page : 0; }
BbVmStatus bb_vm_observe(BbGuestMemory *v,BbVmObserver observer,void *user) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    /* Attach before mappings; replacing a live mirror would miss history. */
    if(v->page_count) return BB_VM_BUSY;
    v->observer=observer; v->observer_user=user; return BB_VM_OK;
}
BbVmStatus bb_vm_detach_observer(BbGuestMemory *v,BbVmObserver observer,void *user) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK && s!=BB_VM_POISONED) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(v->observer!=observer || v->observer_user!=user) return BB_VM_INVALID;
    v->observer=NULL; v->observer_user=NULL; return BB_VM_OK;
}
BbVmStatus bb_vm_map(BbGuestMemory *v,uint64_t at,size_t n,size_t off,unsigned perm) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!range(v,at,n,v->granule) || off%v->granule || off>=v->backing_size ||
       n>v->backing_size-off || perm>7) return BB_VM_INVALID;
    size_t first=(at-v->guest_base)/v->granule;
    size_t count=n/v->granule,slot=lower_bound(v,first);
    if(slot<v->page_count && v->pages[slot].index<first+count) return BB_VM_BUSY;
    if(count>SIZE_MAX/sizeof(Page)-v->page_count) return BB_VM_SYSTEM;
    size_t needed=v->page_count+count;
    if(needed>v->page_capacity) {
        size_t capacity=needed>v->page_capacity*2 ? needed : v->page_capacity*2;
        Page *pages=realloc(v->pages,capacity*sizeof(Page));
        if(!pages) return BB_VM_SYSTEM;
        v->pages=pages; v->page_capacity=capacity;
    }
    /* Clear storage only when it has no live aliases. */
    for(size_t i=0;i<n/v->granule;++i) if(!v->preserve_backing && !v->references[off/v->granule+i])
        memset((char *)v->backing+off+i*v->granule,0,v->granule);
    void *p=mmap(host_at(v,at),n,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,v->fd,(off_t)off);
    if(p==MAP_FAILED) { v->poisoned=1; return BB_VM_POISONED; }
    memmove(v->pages+slot+count,v->pages+slot,(v->page_count-slot)*sizeof(Page));
    v->page_count+=count;
    for(size_t i=0;i<count;++i) {
        Page *page=&v->pages[slot+i];
        *page=(Page){first+i,off+i*v->granule,perm};
        ++v->references[off/v->granule+i];
        if(perm&BB_VM_EXEC) ++v->executable[off/v->granule+i];
        if(mprotect((char *)p+i*v->granule,v->granule,host_permissions(effective(v,page)))) {
            v->poisoned=1; return BB_VM_POISONED;
        }
    }
    return notify(v,BB_VM_MAPPING,at,n);
}
static int mapped(BbGuestMemory *v,uint64_t at,size_t n) {
    if(!range(v,at,n,1)) return 0;
    size_t a=(at-v->guest_base)/v->granule,b=(at-v->guest_base+n-1)/v->granule;
    size_t slot=lower_bound(v,a),count=b-a+1;
    if(count>v->page_count-slot) return 0;
    for(size_t i=0;i<count;++i) if(v->pages[slot+i].index!=a+i) return 0;
    return 1;
}
BbVmStatus bb_vm_unmap(BbGuestMemory *v,uint64_t at,size_t n) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!range(v,at,n,v->granule) || !mapped(v,at,n)) return BB_VM_INVALID;
    if(mmap(host_at(v,at),n,PROT_NONE,MAP_PRIVATE|MAP_ANON|MAP_FIXED,-1,0)==MAP_FAILED) {
        v->poisoned=1; return BB_VM_POISONED;
    }
    size_t a=lower_bound(v,(at-v->guest_base)/v->granule);
    for(size_t i=0;i<n/v->granule;++i) {
        Page *p=&v->pages[a+i];
        for(size_t j=0;j<v->granule/BB_VM_TRACKER_PAGE;++j) {
            unsigned char mask=v->gpu_denied[p->index*v->granule/BB_VM_TRACKER_PAGE+j];
            size_t physical=p->offset/BB_VM_TRACKER_PAGE+j;
            if(((mask&BB_VM_READ) && !v->read_watchers[physical]) || ((mask&BB_VM_WRITE) && !v->watchers[physical])) { v->poisoned=1; return BB_VM_POISONED; }
            if(mask&BB_VM_READ) --v->read_watchers[physical];
            if(mask&BB_VM_WRITE) --v->watchers[physical];
            v->gpu_denied[p->index*v->granule/BB_VM_TRACKER_PAGE+j]=0;
        }
        if(p->permissions&BB_VM_EXEC) --v->executable[p->offset/v->granule];
        if(!--v->references[p->offset/v->granule]) {
            memset(v->watchers+p->offset/BB_VM_TRACKER_PAGE,0,v->granule/BB_VM_TRACKER_PAGE*sizeof(unsigned));
            memset(v->read_watchers+p->offset/BB_VM_TRACKER_PAGE,0,v->granule/BB_VM_TRACKER_PAGE*sizeof(unsigned));
        }
    }
    size_t count=n/v->granule;
    memmove(v->pages+a,v->pages+a+count,(v->page_count-a-count)*sizeof(Page));
    v->page_count-=count;
    BbVmStatus result=notify(v,BB_VM_UNMAPPING,at,n);
    return result==BB_VM_OK ? update_aliases(v) : result;
}
BbVmStatus bb_vm_protect(BbGuestMemory *v,uint64_t at,size_t n,unsigned perm) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(perm>7 || !range(v,at,n,v->granule) || !mapped(v,at,n)) return BB_VM_INVALID;
    size_t a=lower_bound(v,(at-v->guest_base)/v->granule);
    for(size_t i=0;i<n/v->granule;++i) {
        Page *p=&v->pages[a+i];
        if((p->permissions&BB_VM_EXEC) && !(perm&BB_VM_EXEC)) --v->executable[p->offset/v->granule];
        if(!(p->permissions&BB_VM_EXEC) && (perm&BB_VM_EXEC)) ++v->executable[p->offset/v->granule];
        p->permissions=perm;
    }
    return update_aliases(v);
}
static BbVmStatus watch(BbGuestMemory *v,uint64_t at,size_t n,int delta,int read) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if((delta!=1 && delta!=-1) || !range(v,at,n,BB_VM_TRACKER_PAGE) || !mapped(v,at,n)) return BB_VM_INVALID;
    size_t bytes=v->backing_size/BB_VM_TRACKER_PAGE*sizeof(unsigned);
    unsigned *next=malloc(bytes);
    if(!next) return BB_VM_SYSTEM;
    unsigned *counts=read ? v->read_watchers : v->watchers;
    memcpy(next,counts,bytes);
    for(size_t i=0;i<n;i+=BB_VM_TRACKER_PAGE) {
        size_t relative=(at-v->guest_base)+i;
        Page *p=page_at(v,relative/v->granule);
        unsigned *count=&next[(p->offset+relative%v->granule)/BB_VM_TRACKER_PAGE];
        if((delta<0 && !*count) || (delta>0 && *count==UINT_MAX)) { free(next); return BB_VM_INVALID; }
        if(delta>0) ++*count; else --*count;
    }
    memcpy(counts,next,bytes); free(next);
    return update_aliases(v);
}
BbVmStatus bb_vm_watch_write(BbGuestMemory *v,uint64_t at,size_t n,int delta) { return watch(v,at,n,delta,0); }
BbVmStatus bb_vm_watch_read(BbGuestMemory *v,uint64_t at,size_t n,int delta) { return watch(v,at,n,delta,1); }
typedef struct { size_t physical,slot; unsigned old_mask,new_mask; } GpuChange;
static int change_order(const void *a,const void *b) {
    const GpuChange *left=a,*right=b;
    return left->physical<right->physical ? -1 : left->physical>right->physical;
}
BbVmStatus bb_vm_set_gpu_permissions(BbGuestMemory *v,uint64_t at,size_t n,int read,int write) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if((read!=0 && read!=1) || (write!=0 && write!=1) || !range(v,at,n,BB_VM_TRACKER_PAGE) || !mapped(v,at,n)) return BB_VM_INVALID;
    size_t count=n/BB_VM_TRACKER_PAGE;
    GpuChange *changes=calloc(count,sizeof(*changes)); if(!changes) return BB_VM_SYSTEM;
    unsigned mask=(read ? 0 : BB_VM_READ)|(write ? 0 : BB_VM_WRITE);
    for(size_t i=0;i<count;++i) {
        size_t relative=at-v->guest_base+i*BB_VM_TRACKER_PAGE;
        Page *page=page_at(v,relative/v->granule);
        size_t physical=(page->offset+relative%v->granule)/BB_VM_TRACKER_PAGE,slot=relative/BB_VM_TRACKER_PAGE;
        changes[i]=(GpuChange){physical,slot,v->gpu_denied[slot],mask};
    }
    qsort(changes,count,sizeof(*changes),change_order);
    /* Aggregate aliases before validation: repeated virtual views may refer to
     * one physical subpage. No partial count update on overflow/underflow. */
    for(unsigned apply=0;apply<2;++apply) for(size_t i=0;i<count;) {
        size_t first=i,physical=changes[i].physical; int64_t rd=0,wr=0;
        for(;i<count && changes[i].physical==physical;++i) {
            rd+=!!(changes[i].new_mask&BB_VM_READ)-!!(changes[i].old_mask&BB_VM_READ);
            wr+=!!(changes[i].new_mask&BB_VM_WRITE)-!!(changes[i].old_mask&BB_VM_WRITE);
        }
        int64_t next_read=(int64_t)v->read_watchers[physical]+rd,next_write=(int64_t)v->watchers[physical]+wr;
        if(next_read<0 || next_read>UINT_MAX || next_write<0 || next_write>UINT_MAX) { free(changes); return BB_VM_INVALID; }
        if(apply) {
            v->read_watchers[physical]=(unsigned)next_read; v->watchers[physical]=(unsigned)next_write;
            for(size_t j=first;j<i;++j) v->gpu_denied[changes[j].slot]=(unsigned char)changes[j].new_mask;
        }
    }
    free(changes); return update_aliases(v);
}
BbVmStatus bb_vm_status(BbGuestMemory *v) { return check(v); }
int bb_vm_contains(BbGuestMemory *v,uint64_t at,size_t n) {
    return check(v)==BB_VM_OK && range(v,at,n,1);
}
static BbVmStatus clear_watches(BbGuestMemory *v,uint64_t at,size_t n,int read) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(v->notifying) return BB_VM_BUSY;
    if(!range(v,at,n,v->granule) || !mapped(v,at,n)) return BB_VM_INVALID;
    for(size_t i=0;i<n;i+=v->granule) {
        Page *p=page_at(v,(at-v->guest_base+i)/v->granule);
        unsigned *counts=read ? v->read_watchers : v->watchers;
        memset(counts+p->offset/BB_VM_TRACKER_PAGE,0,v->granule/BB_VM_TRACKER_PAGE*sizeof(unsigned));
        for(size_t j=0;j<v->page_count;++j) if(v->pages[j].offset==p->offset)
            for(size_t k=0;k<v->granule/BB_VM_TRACKER_PAGE;++k)
                v->gpu_denied[v->pages[j].index*v->granule/BB_VM_TRACKER_PAGE+k]&=(unsigned char)~(read ? BB_VM_READ : BB_VM_WRITE);
    }
    return update_aliases(v);
}
BbVmStatus bb_vm_clear_write_watches(BbGuestMemory *v,uint64_t at,size_t n) { return clear_watches(v,at,n,0); }
BbVmStatus bb_vm_clear_read_watches(BbGuestMemory *v,uint64_t at,size_t n) { return clear_watches(v,at,n,1); }
BbVmStatus bb_vm_query(BbGuestMemory *v,uint64_t at,BbVmView *out) {
    BbVmStatus s=check(v); if(s!=BB_VM_OK) return s;
    if(!out || !mapped(v,at,1)) return BB_VM_INVALID;
    size_t rel=at-v->guest_base,index=rel/v->granule,within=rel%v->granule;
    Page *p=page_at(v,index);
    *out=(BbVmView){at,p->offset+within,v->granule-within,p->permissions,effective(v,p),
        v->watchers[(p->offset+within)/BB_VM_TRACKER_PAGE],host_at(v,at),
        v->read_watchers[(p->offset+within)/BB_VM_TRACKER_PAGE],v->gpu_denied[rel/BB_VM_TRACKER_PAGE]};
    return BB_VM_OK;
}
static BbVmStatus written(BbGuestMemory *v) {
    for(size_t i=0;i<v->page_count;++i) {
        BbVmStatus s=notify(v,BB_VM_WRITE_EVENT,v->guest_base+v->pages[i].index*v->granule,v->granule);
        if(s!=BB_VM_OK) return s;
    }
    return BB_VM_OK;
}
int bb_vm_code_write_needed(BbGuestMemory *v,uint64_t at,size_t n) {
    if(check(v)!=BB_VM_OK) return -1;
    if(!range(v,at,n,1)) return 0;
    size_t first=(at-v->guest_base)/v->granule,last=(at-v->guest_base+n-1)/v->granule;
    for(size_t i=lower_bound(v,first);i<v->page_count && v->pages[i].index<=last;++i)
        if(v->executable[v->pages[i].offset/v->granule]) return 1;
    return 0;
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
        BbVmView view;
        s=bb_vm_query(v,at+i,&view); if(s!=BB_VM_OK) return s;
        if(!(view.effective_permissions & (write ? BB_VM_WRITE : BB_VM_READ))) return BB_VM_ACCESS;
        size_t part=view.size<n-i ? view.size : n-i; i+=part;
    }
    for(size_t i=0;i<n;) {
        BbVmView view;
        s=bb_vm_query(v,at+i,&view); if(s!=BB_VM_OK) return s;
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
