#include "translated_runtime.h"
#include "guest_format.h"
#include "runtime.h"
#include "memory_probe.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { MAX_IMPORTS=128 };
typedef struct { const char *name; BbAbiSignature signature; unsigned operation; } Import;
typedef struct { BbTranslatedRuntime *runtime; const Import *entry; } Handler;
struct BbTranslatedRuntime {
    BbMemoryProbe *session;
    Handler handlers[MAX_IMPORTS];
    uint64_t addresses[MAX_IMPORTS],canary,libc_marker;
    char diagnostic[256];
    int active;
};
/* Authoritative supported signatures, shared by binding and inventory tests. */
#define U BB_ABI_U64
#define I BB_ABI_U32
#define P BB_ABI_POINTER
#define SIG(ret,...) {.result=ret,.args={__VA_ARGS__}}
static const Import imports[]={
    {"sceKernelGetDirectMemorySize",{.result=U},0},
    {"sceKernelAllocateDirectMemory",{.result=BB_ABI_I32,.args={U,U,U,U,I,P},.count=6,.fixed_count=6},1},
    {"sceKernelMapDirectMemory",{.result=BB_ABI_I32,.args={P,U,I,I,U,U},.count=6,.fixed_count=6},2},
    {"sceKernelMapNamedDirectMemory",{.result=BB_ABI_I32,.args={P,U,I,I,U,U,P},.count=7,.fixed_count=7},3},
    {"sceKernelReleaseDirectMemory",{.result=BB_ABI_I32,.args={U,U},.count=2,.fixed_count=2},4},
    {"sceKernelMunmap",{.result=BB_ABI_I32,.args={P,U},.count=2,.fixed_count=2},5},
    {"munmap",{.result=BB_ABI_I32,.args={P,U},.count=2,.fixed_count=2},5},
    {"sceKernelMapFlexibleMemory",{.result=BB_ABI_I32,.args={P,U,I,I},.count=4,.fixed_count=4},6},
    {"sceKernelMapNamedFlexibleMemory",{.result=BB_ABI_I32,.args={P,U,I,I,P},.count=5,.fixed_count=5},7},
    {"sceKernelReleaseFlexibleMemory",{.result=BB_ABI_I32,.args={P,U},.count=2,.fixed_count=2},5},
    {"sceKernelReserveVirtualRange",{.result=BB_ABI_I32,.args={P,U,I,U},.count=4,.fixed_count=4},8},
    {"sceKernelMprotect",{.result=BB_ABI_I32,.args={P,U,I},.count=3,.fixed_count=3},9},
    {"sceKernelQueryMemoryProtection",{.result=BB_ABI_I32,.args={P,P,P,P},.count=4,.fixed_count=4},10},
    {"sceKernelVirtualQuery",{.result=BB_ABI_I32,.args={P,I,P,U},.count=4,.fixed_count=4},11},
    {"sceKernelGetDirectMemoryType",{.result=BB_ABI_I32,.args={U,P,P,P},.count=4,.fixed_count=4},12},
    {"sceKernelBatchMap",{.result=BB_ABI_I32,.args={P,I,P},.count=3,.fixed_count=3},13},
    {"sceKernelBatchMap2",{.result=BB_ABI_I32,.args={P,I,P,I},.count=4,.fixed_count=4},14},
    {"memcpy",{.result=P,.args={P,P,U},.count=3,.fixed_count=3},20},
    {"memmove",{.result=P,.args={P,P,U},.count=3,.fixed_count=3},21},
    {"memset",{.result=P,.args={P,I,U},.count=3,.fixed_count=3},22},
    {"memcmp",{.result=BB_ABI_I32,.args={P,P,U},.count=3,.fixed_count=3},23},
    {"strlen",{.result=U,.args={P},.count=1,.fixed_count=1},24},
    {"strcmp",{.result=BB_ABI_I32,.args={P,P},.count=2,.fixed_count=2},25},
    {"strncmp",{.result=BB_ABI_I32,.args={P,P,U},.count=3,.fixed_count=3},26},
    {"strcpy",{.result=P,.args={P,P},.count=2,.fixed_count=2},27},
    {"strncpy",{.result=P,.args={P,P,U},.count=3,.fixed_count=3},28},
    {"snprintf",{.result=BB_ABI_I32,.args={P,U,P},.count=3,.fixed_count=3,.variadic=1},30},
    {"vsnprintf",{.result=BB_ABI_I32,.args={P,U,P,P},.count=4,.fixed_count=4},31},
    {"sprintf",{.result=BB_ABI_I32,.args={P,P},.count=2,.fixed_count=2,.variadic=1},32},
};
#undef SIG
#undef P
#undef I
#undef U
static uint64_t integer(const BbAbiValue *v) { uint64_t value; memcpy(&value,v->bytes,8); return value; }
static int range_access(BbAbiMemory *m,uint64_t at,size_t n,int write) { return !n || m->check(m->user,at,n,write); }
static BbAbiStatus failure(BbTranslatedRuntime *r,const char *message) {
    snprintf(r->diagnostic,sizeof(r->diagnostic),"%s",message); return BB_ABI_MEMORY;
}
static int put(BbAbiMemory *m,uint64_t at,const void *value,size_t size) { return !at || (range_access(m,at,size,1) && m->write(m->user,at,value,size)); }
static BbAbiStatus memory_call(BbTranslatedRuntime *r,const Import *entry,const BbAbiValue *values,BbAbiValue *result) {
    uint64_t a[7]={0}; for(size_t i=0;i<entry->signature.count;++i) a[i]=integer(values+i);
    BbAbiMemory m=bb_memory_probe_memory(r->session);
    uintptr_t target=runtime_memory_resolve_symbol(entry->name); if(!target) return BB_ABI_HOST;
    int32_t rc=0; uint64_t out=0; void *pointer=NULL;
    switch(entry->operation) {
    case 0: out=((uint64_t (*)(void))target)(); memcpy(result->bytes,&out,8); return BB_ABI_OK;
    case 1: {
        if(!a[5] || !range_access(&m,a[5],8,1)) return failure(r,"allocate output is not writable guest memory");
        int64_t physical=0;
        rc=((int32_t (*)(int64_t,int64_t,uint64_t,uint64_t,int,int64_t *))target)((int64_t)a[0],(int64_t)a[1],a[2],a[3],(int)a[4],&physical);
        if(!rc && !put(&m,a[5],&physical,8)) return failure(r,"allocate output mapping changed");
        break;
    }
    case 2: case 3: case 6: case 7: case 8:
        if(!a[0] || !range_access(&m,a[0],8,1) || !m.read(m.user,a[0],&out,8)) return failure(r,"map output is not readable/writable guest memory");
        pointer=(void *)(uintptr_t)out;
        if(entry->operation==2 || entry->operation==3) {
            if(entry->operation==2) rc=((int32_t (*)(void **,uint64_t,int,int,int64_t,uint64_t))target)(&pointer,a[1],(int)a[2],(int)a[3],(int64_t)a[4],a[5]);
            else rc=((int32_t (*)(void **,uint64_t,int,int,int64_t,uint64_t,const char *))target)(&pointer,a[1],(int)a[2],(int)a[3],(int64_t)a[4],a[5],NULL);
        } else if(entry->operation==6) rc=((int32_t (*)(void **,uint64_t,int,int))target)(&pointer,a[1],(int)a[2],(int)a[3]);
        else if(entry->operation==7) rc=((int32_t (*)(void **,uint64_t,int,int,const char *))target)(&pointer,a[1],(int)a[2],(int)a[3],NULL);
        else rc=((int32_t (*)(void **,uint64_t,int,uint64_t))target)(&pointer,a[1],(int)a[2],a[3]);
        out=(uintptr_t)pointer;
        if(!rc && !put(&m,a[0],&out,8)) return failure(r,"map changed its output mapping");
        break;
    case 4: rc=((int32_t (*)(uint64_t,uint64_t))target)(a[0],a[1]);
        break;
    case 5: rc=((int32_t (*)(void *,uint64_t))target)((void *)(uintptr_t)a[0],a[1]);
        break;
    case 9: rc=((int32_t (*)(const void *,uint64_t,int))target)((const void *)(uintptr_t)a[0],a[1],(int)a[2]);
        break;
    case 10: case 12: {
        if(!range_access(&m,a[1],entry->operation==10 ? 8 : 4,1) || !range_access(&m,a[2],8,1) || !range_access(&m,a[3],entry->operation==10 ? 4 : 8,1)) {
            /* NULL optional outputs are allowed by these PS4 contracts. */
            if((a[1] && !range_access(&m,a[1],entry->operation==10 ? 8 : 4,1)) || (a[2] && !range_access(&m,a[2],8,1)) || (a[3] && !range_access(&m,a[3],entry->operation==10 ? 4 : 8,1))) return failure(r,"query output is not writable");
        }
        void *start=NULL,*end=NULL; uint32_t protection=0; int type=0;
        if(entry->operation==10) rc=((int32_t (*)(void *,void **,void **,uint32_t *))target)((void *)(uintptr_t)a[0],&start,&end,&protection);
        else rc=((int32_t (*)(uint64_t,int *,void **,void **))target)(a[0],&type,&start,&end);
        uint64_t lo=(uintptr_t)start,hi=(uintptr_t)end;
        if(!rc && (entry->operation==10 ? (!put(&m,a[1],&lo,8)||!put(&m,a[2],&hi,8)||!put(&m,a[3],&protection,4)) :
            (!put(&m,a[1],&type,4)||!put(&m,a[2],&lo,8)||!put(&m,a[3],&hi,8)))) return failure(r,"query output commit failed");
        break;
    }
    case 11: {
        if(!a[2] || !range_access(&m,a[2],sizeof(BbGuestVirtualQuery),1)) return failure(r,"virtual query output is not writable");
        BbGuestVirtualQuery info={0};
        rc=((int32_t (*)(const void *,int,BbGuestVirtualQuery *,uint64_t))target)((void *)(uintptr_t)a[0],(int)a[1],&info,a[3]);
        if(!rc && !put(&m,a[2],&info,sizeof(info))) return failure(r,"virtual query output commit failed");
        break;
    }
    case 13: case 14: {
        int count=(int32_t)a[1],done=0;
        if(count<0 || count>4096) { rc=BB_GUEST_EINVAL; break; }
        size_t size=(size_t)count*sizeof(BbGuestBatchEntry);
        if(!range_access(&m,a[0],size,1) || !range_access(&m,a[0],size,0) || (a[2] && !range_access(&m,a[2],4,1))) return failure(r,"batch buffer is not accessible");
        BbGuestBatchEntry *entries=calloc((size_t)count+1,sizeof(*entries)); if(!entries) return BB_ABI_HOST;
        if(size && !m.read(m.user,a[0],entries,size)) { free(entries); return failure(r,"batch read failed"); }
        if(entry->operation==13) rc=((int32_t (*)(BbGuestBatchEntry *,int,int *))target)(entries,count,&done);
        else rc=((int32_t (*)(BbGuestBatchEntry *,int,int *,int))target)(entries,count,&done,(int)a[3]);
        int ok=(!size || m.write(m.user,a[0],entries,size)) && put(&m,a[2],&done,4); free(entries);
        if(!ok) return failure(r,"batch output mapping changed");
        break;
    }
    default: return BB_ABI_INVALID;
    }
    memcpy(result->bytes,&rc,4); return BB_ABI_OK;
}
static BbAbiStatus libc_call(BbTranslatedRuntime *r,const Import *entry,const BbAbiValue *v,BbAbiValue *out) {
    BbAbiMemory m=bb_memory_probe_memory(r->session);
    uint64_t a=integer(v),b=entry->signature.count>1 ? integer(v+1) : 0,n=entry->signature.count>2 ? integer(v+2) : 0;
    uint64_t result=a; unsigned char buffer[4096],other[4096];
    if(entry->operation<=23) {
        if(n>SIZE_MAX || (n && (a>UINT64_MAX-n || (entry->operation!=22 && b>UINT64_MAX-n)))) return failure(r,"libc range overflow");
        if(!range_access(&m,a,(size_t)n,entry->operation!=23) || (entry->operation!=22 && !range_access(&m,b,(size_t)n,0))) return failure(r,"libc range is inaccessible");
        if(entry->operation==22) memset(buffer,(int)(uint8_t)b,sizeof(buffer));
        if(entry->operation==23) result=0;
        int backwards=entry->operation==21 && a>b && a-b<n;
        for(uint64_t done=0;done<n;) {
            size_t part=n-done<sizeof(buffer) ? (size_t)(n-done) : sizeof(buffer);
            uint64_t offset=backwards ? n-done-part : done;
            if(entry->operation!=22 && !m.read(m.user,b+offset,buffer,part)) return failure(r,"libc source read failed");
            if(entry->operation==23) {
                if(!m.read(m.user,a+offset,other,part)) return failure(r,"memcmp source read failed");
                int difference=memcmp(other,buffer,part); if(difference) { result=(uint32_t)difference; break; }
            } else if(!m.write(m.user,a+offset,buffer,part)) return failure(r,"libc destination write failed");
            done+=part;
        }
    } else {
        if(entry->operation==26 || entry->operation==28) { if(n>BB_FORMAT_LIMIT) return failure(r,"bounded string operation exceeds limit"); }
        size_t limit=(entry->operation==26 || entry->operation==28) ? (size_t)n : BB_FORMAT_LIMIT;
        result=entry->operation==25 || entry->operation==26 ? 0 : a;
        if(entry->operation==24) b=a;
        if(entry->operation==27 || entry->operation==28) {
            char *copy=calloc(limit+1,1); if(!copy) return BB_ABI_HOST;
            size_t i=0;
            for(;i<limit;++i) { if(b>UINT64_MAX-i || !m.check(m.user,b+i,1,0) || !m.read(m.user,b+i,copy+i,1)) { free(copy); return failure(r,"string source is inaccessible"); } if(!copy[i]) break; }
            if(i==limit && entry->operation==27) { free(copy); return failure(r,"unterminated string exceeds limit"); }
            size_t size=entry->operation==28 ? limit : i+1;
            int ok=range_access(&m,a,size,1) && (!size || m.write(m.user,a,copy,size)); free(copy);
            if(!ok) return failure(r,"string destination is inaccessible");
        } else for(size_t i=0;i<limit;++i) {
            unsigned char right,left=0;
            if(b>UINT64_MAX-i || !m.check(m.user,b+i,1,0) || !m.read(m.user,b+i,&right,1)) return failure(r,"string source is inaccessible");
            if(entry->operation==24) { if(!right) { result=i; break; } }
            else {
                if(a>UINT64_MAX-i || !m.check(m.user,a+i,1,0) || !m.read(m.user,a+i,&left,1)) return failure(r,"string source is inaccessible");
                if(left!=right || !left) { result=(uint32_t)((int)left-(int)right);
        break; }
            }
            if(i+1==limit && entry->operation!=26) return failure(r,"unterminated string exceeds limit");
        }
    }
    memcpy(out->bytes,&result,8); return BB_ABI_OK;
}
static BbAbiStatus prepare_format(const BbAbiFrame *frame,const BbAbiMemory *memory,BbAbiSignature *signature,void *user) {
    (void)user; unsigned fixed=(unsigned)signature->fixed_count;
    uint64_t address=frame->gp[fixed-1]; char text[BB_FORMAT_TEXT]; BbFormatPlan plan;
    if(bb_guest_string(memory,address,text,sizeof(text))!=BB_FORMAT_OK) return BB_ABI_MEMORY;
    if(bb_format_plan(text,&plan)!=BB_FORMAT_OK || fixed+plan.argument_count>BB_ABI_MAX_ARGS) return BB_ABI_INVALID;
    for(unsigned i=0;i<plan.argument_count;++i) signature->args[fixed+i]=plan.types[i];
    signature->count=fixed+plan.argument_count; return BB_ABI_OK;
}
static BbAbiStatus formatted_call(BbTranslatedRuntime *r,const Import *entry,const BbAbiValue *v,size_t count,BbAbiValue *out) {
    BbAbiMemory memory=bb_memory_probe_memory(r->session);
    uint64_t dst=integer(v),capacity=entry->operation==32 ? BB_FORMAT_LIMIT : integer(v+1);
    unsigned fmt_index=entry->operation==32 ? 1 : 2;
    char text[BB_FORMAT_TEXT]; BbFormatPlan plan;
    if(bb_guest_string(&memory,integer(v+fmt_index),text,sizeof(text))!=BB_FORMAT_OK) return failure(r,"format string is inaccessible");
    BbFormatStatus status=bb_format_plan(text,&plan); if(status!=BB_FORMAT_OK) { snprintf(r->diagnostic,sizeof(r->diagnostic),"unsupported/invalid guest format (%d)",status); return BB_ABI_INVALID; }
    BbAbiValue arguments[BB_ABI_MAX_ARGS]={0}; BbGuestVaList state={0};
    if(entry->operation==31) {
        uint64_t address=integer(v+3);
        if(!memory.check(memory.user,address,sizeof(state),1) || !memory.read(memory.user,address,&state,sizeof(state))) return failure(r,"guest va_list is inaccessible");
        for(unsigned i=0;i<plan.argument_count;++i) if(bb_abi_va_read(&state,plan.types[i],arguments+i,&memory)!=BB_ABI_OK) return failure(r,"guest va_list operand is inaccessible");
    } else {
        unsigned fixed=(unsigned)entry->signature.fixed_count;
        if(count!=fixed+plan.argument_count) return BB_ABI_INVALID;
        memcpy(arguments,v+fixed,plan.argument_count*sizeof(*arguments));
    }
    int32_t result=0;
    status=bb_format_write(&plan,arguments,&memory,dst,capacity,&result);
    if(status!=BB_FORMAT_OK) { snprintf(r->diagnostic,sizeof(r->diagnostic),"guest format failed (%d)",status); return status==BB_FORMAT_MEMORY ? BB_ABI_MEMORY : BB_ABI_INVALID; }
    if(entry->operation==31 && !memory.write(memory.user,integer(v+3),&state,sizeof(state))) return failure(r,"guest va_list output failed");
    memcpy(out->bytes,&result,4); return BB_ABI_OK;
}
static BbAbiStatus host_call(const BbAbiValue *values,size_t count,BbAbiValue *result,void *user) {
    /* The handler object is owned by its runtime for the entire binding lifetime. */
    Handler *h=user; BbAbiStatus status;
    if(h->entry->operation<20) status=memory_call(h->runtime,h->entry,values,result);
    else if(h->entry->operation<30) status=libc_call(h->runtime,h->entry,values,result);
    else status=formatted_call(h->runtime,h->entry,values,count,result);
    if(status==BB_ABI_OK && bb_vm_sync_code(runtime_memory_darwin_vm())!=BB_VM_OK) return BB_ABI_HOST;
    return status;
}
static BbTranslatedRuntime *process_runtime;
BbTranslatedStatus bb_translated_open(BbTranslatedRuntime **out) {
    if(!out) return BB_TRANSLATED_INVALID;
    *out=NULL;
    if(process_runtime) return BB_TRANSLATED_INVALID;
    BbTranslatedRuntime *r=calloc(1,sizeof(*r)); if(!r) return BB_TRANSLATED_MEMORY;
    BbGuestMemory *vm=runtime_memory_darwin_vm();
    if(!vm || bb_memory_probe_attach(&r->session,vm)!=BB_PROBE_OK) { free(r); return BB_TRANSLATED_MEMORY; }
    process_runtime=r;
    for(size_t i=0;i<sizeof(imports)/sizeof(*imports);++i) {
        r->handlers[i].runtime=r; r->handlers[i].entry=imports+i;
        BbAbiImport binding={.name=imports[i].name,.signature=imports[i].signature,.host=host_call,.user=r->handlers+i,
            .prepare=imports[i].signature.variadic ? prepare_format : NULL};
        if(bb_memory_probe_bind_abi(r->session,&binding,&r->addresses[i])!=BB_PROBE_OK) { bb_translated_close(r); return BB_TRANSLATED_EXECUTION; }
    }
    void *page=NULL;
    uintptr_t map=runtime_memory_resolve_symbol("sceKernelMapFlexibleMemory");
    if(((int32_t (*)(void **,uint64_t,int,int))map)(&page,BB_VM_GUEST_PAGE,3,0)) { bb_translated_close(r); return BB_TRANSLATED_MEMORY; }
    runtime_start(1);
    uintptr_t canary=runtime_resolve("f7uOxY9mM1U#p#J",1);
    r->canary=(uintptr_t)page; r->libc_marker=r->canary+16;
    int32_t marker=1;
    if(!canary || bb_vm_write(vm,r->canary,(const void *)canary,8)!=BB_VM_OK || bb_vm_write(vm,r->libc_marker,&marker,4)!=BB_VM_OK) {
        bb_translated_close(r); return BB_TRANSLATED_MEMORY;
    }
    *out=r; return BB_TRANSLATED_OK;
}
BbTranslatedStatus bb_translated_close(BbTranslatedRuntime *r) {
    if(!r || r!=process_runtime || r->active) return BB_TRANSLATED_INVALID;
    if(bb_memory_probe_close(r->session)!=BB_PROBE_OK) return BB_TRANSLATED_EXECUTION;
    if(runtime_memory_darwin_shutdown()) return BB_TRANSLATED_MEMORY;
    bb_translated_attach_execution(NULL);
    process_runtime=NULL; free(r); return BB_TRANSLATED_OK;
}
BbGuestMemory *bb_translated_memory(BbTranslatedRuntime *r) { return r && r==process_runtime ? runtime_memory_darwin_vm() : NULL; }
BbAbiMemory bb_translated_memory_access(BbTranslatedRuntime *r) { return bb_memory_probe_memory(r->session); }
const char *bb_translated_diagnostic(BbTranslatedRuntime *r) { return r ? r->diagnostic : "no translated runtime"; }
BbTranslatedStatus bb_translated_bind(BbTranslatedRuntime *r,const char *name,BbImportKind kind,uint64_t *address) {
    if(!r || r!=process_runtime || !name || !address || bb_vm_status(bb_translated_memory(r))!=BB_VM_OK) return BB_TRANSLATED_INVALID;
    const char *symbol=runtime_symbol(name); if(!symbol) symbol=name;
    if(kind==BB_IMPORT_DATA) {
        if(!strcmp(symbol,"__stack_chk_guard")) { *address=r->canary; return BB_TRANSLATED_OK; }
        if(!strcmp(symbol,"Need_sceLibcInternal")) { *address=r->libc_marker; return BB_TRANSLATED_OK; }
    }
    if(kind!=BB_IMPORT_FUNCTION) { snprintf(r->diagnostic,sizeof(r->diagnostic),"unregistered data/hook import: %s",name); return BB_TRANSLATED_UNSUPPORTED; }
    for(size_t i=0;i<sizeof(imports)/sizeof(*imports);++i) if(!strcmp(symbol,imports[i].name)) { *address=r->addresses[i]; return BB_TRANSLATED_OK; }
    snprintf(r->diagnostic,sizeof(r->diagnostic),"unregistered guest signature: %s (%s)",name,symbol); return BB_TRANSLATED_UNSUPPORTED;
}
BbTranslatedStatus bb_translated_invoke(BbTranslatedRuntime *r,BbGuestCall *call) {
    if(!r || r!=process_runtime || !call || bb_vm_status(bb_memory_probe_vm(r->session))!=BB_VM_OK) return BB_TRANSLATED_INVALID;
    r->diagnostic[0]=0;
    ++r->active; BbProbeFault fault={0}; BbProbeStatus status=bb_memory_probe_invoke(r->session,call,&fault); --r->active;
    if(status!=BB_PROBE_OK) {
        char detail[128]; snprintf(detail,sizeof(detail),"%.127s",r->diagnostic);
        snprintf(r->diagnostic,sizeof(r->diagnostic),"%s: execution %s pc=0x%" PRIx64 " address=0x%" PRIx64,detail,bb_probe_status_name(status),fault.pc,fault.address);
        return BB_TRANSLATED_EXECUTION;
    }
    return BB_TRANSLATED_OK;
}

typedef struct { uint64_t address,value; } Resolved;
static int relocation_order(const void *a,const void *b) {
    const Resolved *left=a,*right=b; return left->address<right->address ? -1 : left->address>right->address;
}
BbTranslatedStatus bb_translated_relocate(BbTranslatedRuntime *r,uint64_t image,size_t size,
    const BbTranslatedReloc *relocations,size_t count) {
    if(!r || r!=process_runtime || (!relocations && count) || !size || size>UINT64_MAX-image || count>65536) return BB_TRANSLATED_INVALID;
    BbAbiMemory memory=bb_translated_memory_access(r);
    Resolved *resolved=calloc(count+1,sizeof(*resolved)); if(!resolved) return BB_TRANSLATED_MEMORY;
    BbTranslatedStatus status=BB_TRANSLATED_INVALID;
    for(size_t i=0;i<count;++i) {
        status=BB_TRANSLATED_INVALID;
        const BbTranslatedReloc *entry=relocations+i;
        if(entry->offset>size || size-entry->offset<8 || !memory.check(memory.user,image+entry->offset,8,1)) goto done;
        uint64_t value=0;
        if(entry->kind==BB_RELOC_IMAGE) {
            if(entry->value>=size || entry->addend>=size-entry->value) goto done;
            value=image+entry->value+entry->addend;
        } else {
            if(entry->kind!=BB_RELOC_FUNCTION && entry->kind!=BB_RELOC_DATA) goto done;
            status=bb_translated_bind(r,entry->name,entry->kind==BB_RELOC_DATA ? BB_IMPORT_DATA : BB_IMPORT_FUNCTION,&value);
            if(status!=BB_TRANSLATED_OK) goto done;
            const char *symbol=runtime_symbol(entry->name); if(!symbol) symbol=entry->name;
            size_t export_size=!strcmp(symbol,"__stack_chk_guard") ? 8 : 4;
            if((entry->kind==BB_RELOC_FUNCTION && entry->addend) || (entry->kind==BB_RELOC_DATA && entry->addend>=export_size)) { status=BB_TRANSLATED_INVALID; goto done; }
            value+=entry->addend;
        }
        resolved[i]=(Resolved){image+entry->offset,value};
    }
    qsort(resolved,count,sizeof(*resolved),relocation_order);
    for(size_t i=1;i<count;++i) if(resolved[i].address-resolved[i-1].address<8) { status=BB_TRANSLATED_INVALID; goto done; }
    for(size_t i=0;i<count;++i) if(!memory.write(memory.user,resolved[i].address,&resolved[i].value,8)) { status=BB_TRANSLATED_MEMORY; goto done; }
    status=BB_TRANSLATED_OK;
done:
    free(resolved); return status;
}
