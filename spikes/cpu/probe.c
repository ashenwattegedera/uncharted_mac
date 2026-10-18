#include "probe.h"
#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

enum { MAX_BINDINGS=BB_PROBE_MAX_BINDINGS, MAX_DEPTH=8, STACK_SLICE=65536, MAX_RESUMES=64 };
#define BRIDGE_BASE UINT64_C(0x200000)
struct BbCpuProbe {
    uc_engine *engine;
    uc_context *initial;
    int resumable,pause_requested,clock_pending;
    uint64_t tsc,tsc_step;
    uint32_t tsc_aux;
    pthread_mutex_t lock;
    uc_hook bridge_hook,fault_hook,syscall_hook,write_hook,write_block_hook;
    BbProbeWriteObserver write_observer; void *write_user; int external_error;
    BbProbeWriteFilter write_filter; void *filter_user;
    int memory_fault;
    int dirty_write,write_stopped;
    uint64_t write_start,write_end,write_resume;
    struct { BbProbeHostCall call; void *user;
        BbAbiImport abi; BbAbiMemory memory; } bindings[MAX_BINDINGS];
    unsigned count,depth;
    int pending;
    int unsupported;
    BbProbeFault fault;
    BbProbeFaultHandler fault_handler;
    void *fault_user;
};
static int range_valid(uint64_t address,size_t size) { return size && size<=UINT64_MAX-address; }
static BbProbeStatus engine_status(uc_err error) { return error==UC_ERR_OK ? BB_PROBE_OK : BB_PROBE_ENGINE; }
static void bridge_hook(uc_engine *engine,uint64_t address,uint32_t size,void *user) {
    (void)size;
    BbCpuProbe *p=user;
    p->pending=(int)((address-BRIDGE_BASE)/16);
    uc_emu_stop(engine);
}
static void syscall_hook(uc_engine *engine,void *user) {
    BbCpuProbe *p=user;
    p->unsupported=1;
    uc_emu_stop(engine);
}
static bool fault_hook(uc_engine *engine,uc_mem_type access,uint64_t address,int size,int64_t value,void *user) {
    (void)value;
    BbCpuProbe *p=user;
    p->fault.address=address; p->fault.access=access; p->fault.size=size>0 ? (size_t)size : 0;
    uc_reg_read(engine,UC_X86_REG_RIP,&p->fault.pc);
    p->memory_fault=1;
    return false; /* Recovery mutates mappings only after uc_emu_start returns. */
}
BbProbeStatus bb_probe_open(BbCpuProbe **out) {
    if(!out) return BB_PROBE_INVALID;
    *out=NULL;
    BbCpuProbe *p=calloc(1,sizeof(*p));
    if(!p) return BB_PROBE_ENGINE;
    pthread_mutexattr_t attr;
    if(pthread_mutexattr_init(&attr)) { free(p); return BB_PROBE_ENGINE; }
    int error=pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
    if(!error) error=pthread_mutex_init(&p->lock,&attr);
    pthread_mutexattr_destroy(&attr);
    if(error) { free(p); return BB_PROBE_ENGINE; }
    uc_err e=uc_open(UC_ARCH_X86,UC_MODE_64,&p->engine);
    if(e==UC_ERR_OK) e=uc_ctl_set_cpu_model(p->engine,UC_CPU_X86_HASWELL);
    if(e==UC_ERR_OK) e=uc_mem_map(p->engine,BB_PROBE_CODE,65536,UC_PROT_ALL);
    if(e==UC_ERR_OK) e=uc_mem_map(p->engine,BRIDGE_BASE,65536,UC_PROT_READ|UC_PROT_EXEC);
    if(e==UC_ERR_OK) e=uc_mem_map(p->engine,BB_PROBE_STACK,MAX_DEPTH*STACK_SLICE,UC_PROT_READ|UC_PROT_WRITE);
    /* Each bridge is entered by a call/jump and starts its own translated block.
     * A block hook avoids instrumenting every guest instruction for host calls. */
    if(e==UC_ERR_OK) e=uc_hook_add(p->engine,&p->bridge_hook,UC_HOOK_BLOCK,(void *)bridge_hook,p,BRIDGE_BASE,BRIDGE_BASE+16*MAX_BINDINGS-1);
    if(e==UC_ERR_OK) e=uc_hook_add(p->engine,&p->fault_hook,UC_HOOK_MEM_INVALID,(void *)fault_hook,p,1,0);
    if(e==UC_ERR_OK) e=uc_hook_add(p->engine,&p->syscall_hook,UC_HOOK_INSN,(void *)syscall_hook,p,1,0,UC_X86_INS_SYSCALL);
    if(e==UC_ERR_OK) e=uc_context_alloc(p->engine,&p->initial);
    if(e==UC_ERR_OK) e=uc_context_save(p->engine,p->initial);
    if(e!=UC_ERR_OK) { bb_probe_close(p); return BB_PROBE_ENGINE; }
    *out=p;
    return BB_PROBE_OK;
}
void bb_probe_close(BbCpuProbe *p) {
    if(!p) return;
    if(p->initial) uc_context_free(p->initial);
    if(p->engine) uc_close(p->engine);
    pthread_mutex_destroy(&p->lock);
    free(p);
}
BbProbeStatus bb_probe_map(BbCpuProbe *p,uint64_t address,size_t size,unsigned permissions) {
    if(!p || !range_valid(address,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_map(p->engine,address,size,permissions);
    pthread_mutex_unlock(&p->lock);
    return engine_status(e);
}
BbProbeStatus bb_probe_map_host(BbCpuProbe *p,uint64_t address,size_t size,unsigned permissions,void *host) {
    if(!p || !host || !range_valid(address,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_map_ptr(p->engine,address,size,permissions,host);
    pthread_mutex_unlock(&p->lock);
    return engine_status(e);
}
BbProbeStatus bb_probe_invalidate(BbCpuProbe *p,uint64_t at,size_t size) {
    if(!p || !range_valid(at,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_ctl_remove_cache(p->engine,at,at+size);
    pthread_mutex_unlock(&p->lock); return engine_status(e);
}
BbProbeStatus bb_probe_unmap(BbCpuProbe *p,uint64_t at,size_t size) {
    if(!p || !range_valid(at,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_unmap(p->engine,at,size);
    if(e==UC_ERR_OK) e=uc_ctl_remove_cache(p->engine,at,at+size);
    pthread_mutex_unlock(&p->lock); return engine_status(e);
}
static void write_hook(uc_engine *engine,uc_mem_type type,uint64_t at,int size,int64_t value,void *user) {
    (void)type; (void)value;
    BbCpuProbe *p=user;
    if(size<=0 || !range_valid(at,(size_t)size)) { p->external_error=1; uc_emu_stop(engine); return; }
    if(p->write_filter) {
        int needed=p->write_filter(p->filter_user,at,(size_t)size);
        if(needed<0) { p->external_error=1; uc_emu_stop(engine); return; }
        if(!needed) return;
    }
    if(!p->dirty_write) { p->write_start=at; p->write_end=at+(size_t)size; }
    else { if(at<p->write_start) p->write_start=at; if(at+(size_t)size>p->write_end) p->write_end=at+(size_t)size; }
    p->dirty_write=1;
}
static void write_block_hook(uc_engine *engine,uint64_t pc,uint32_t size,void *user) {
    (void)size;
    BbCpuProbe *p=user;
    if(p->dirty_write) {
        p->write_stopped=1; p->write_resume=pc;
        uc_emu_stop(engine); /* Store completed; flush before the next instruction executes. */
        return;
    }
    if(p->tsc_step && size>=2) {
        unsigned char bytes[3]={0};
        if(uc_mem_read(engine,pc,bytes,size>=3 ? 3 : 2)==UC_ERR_OK && bytes[0]==0x0f &&
           (bytes[1]==0x31 || (size>=3 && bytes[1]==0x01 && bytes[2]==0xf9))) {
            p->clock_pending=bytes[1]==0x31 ? 2 : 3; uc_emu_stop(engine);
        }
    }
}
BbProbeStatus bb_probe_filter_writes(BbCpuProbe *p,BbProbeWriteFilter filter,void *user) {
    if(!p || !filter) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock); p->write_filter=filter; p->filter_user=user;
    pthread_mutex_unlock(&p->lock); return BB_PROBE_OK;
}
BbProbeStatus bb_probe_watch_writes(BbCpuProbe *p,uint64_t start,uint64_t end,BbProbeWriteObserver observer,void *user) {
    if(!p || !observer || !start || end<start) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    if(p->write_observer) { pthread_mutex_unlock(&p->lock); return BB_PROBE_INVALID; }
    uc_err e=uc_hook_add(p->engine,&p->write_hook,UC_HOOK_MEM_WRITE,(void *)write_hook,p,start,end);
    if(e==UC_ERR_OK) {
        e=uc_hook_add(p->engine,&p->write_block_hook,UC_HOOK_CODE,(void *)write_block_hook,p,1,0);
        if(e!=UC_ERR_OK) uc_hook_del(p->engine,p->write_hook);
    }
    if(e==UC_ERR_OK) { p->write_observer=observer; p->write_user=user; }
    pthread_mutex_unlock(&p->lock); return engine_status(e);
}
BbProbeStatus bb_probe_write(BbCpuProbe *p,uint64_t address,const void *data,size_t size) {
    if(!p || !data || !range_valid(address,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_write(p->engine,address,data,size);
    if(e==UC_ERR_OK) e=uc_ctl_remove_cache(p->engine,address,address+size);
    pthread_mutex_unlock(&p->lock);
    return engine_status(e);
}
BbProbeStatus bb_probe_read(BbCpuProbe *p,uint64_t address,void *data,size_t size) {
    if(!p || !data || !range_valid(address,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_read(p->engine,address,data,size);
    pthread_mutex_unlock(&p->lock);
    return engine_status(e);
}
BbProbeStatus bb_probe_protect(BbCpuProbe *p,uint64_t address,size_t size,unsigned permissions) {
    if(!p || !range_valid(address,size)) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_protect(p->engine,address,size,permissions);
    if(e==UC_ERR_OK) e=uc_ctl_remove_cache(p->engine,address,address+size);
    pthread_mutex_unlock(&p->lock);
    return engine_status(e);
}
BbProbeStatus bb_probe_bind(BbCpuProbe *p,BbProbeHostCall call,void *user,uint64_t *address) {
    if(!p || !call || !address) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    BbProbeStatus status=BB_PROBE_LIMIT;
    if(p->count<MAX_BINDINGS) {
        unsigned i=p->count++;
        p->bindings[i].call=call; p->bindings[i].user=user;
        *address=BRIDGE_BASE+16*i; status=BB_PROBE_OK;
    }
    pthread_mutex_unlock(&p->lock);
    return status;
}
static int abi_check(void *user,uint64_t at,size_t size,int write) {
    BbCpuProbe *p=user;
    if(!range_valid(at,size)) return 0;
    uc_mem_region *regions=NULL; uint32_t count=0;
    if(uc_mem_regions(p->engine,&regions,&count)!=UC_ERR_OK) return 0;
    uint64_t cursor=at,end=at+size;
    while(cursor<end) {
        uint64_t next=cursor;
        for(uint32_t i=0;i<count;++i) if(regions[i].begin<=cursor && cursor<=regions[i].end &&
            (regions[i].perms & (write ? UC_PROT_WRITE : UC_PROT_READ))) {
            next=regions[i].end>=end-1 ? end : regions[i].end+1; break;
        }
        if(next==cursor) break;
        cursor=next;
    }
    uc_free(regions); return cursor==end;
}
static int abi_read(void *p,uint64_t at,void *out,size_t size) {
    return abi_check(p,at,size,0) && bb_probe_read(p,at,out,size)==BB_PROBE_OK;
}
static int abi_write(void *p,uint64_t at,const void *in,size_t size) {
    return abi_check(p,at,size,1) && bb_probe_write(p,at,in,size)==BB_PROBE_OK;
}
static BbProbeStatus abi_host(BbCpuProbe *p,const uint64_t args[6],uint64_t *result,void *user) {
    (void)args;
    unsigned i=(unsigned)(uintptr_t)user;
    BbAbiFrame frame={0};
    const int regs[]={UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,UC_X86_REG_RCX,UC_X86_REG_R8,UC_X86_REG_R9};
    uc_err e=UC_ERR_OK;
    for(unsigned j=0;j<6 && e==UC_ERR_OK;++j) e=uc_reg_read(p->engine,regs[j],&frame.gp[j]);
    for(unsigned j=0;j<8 && e==UC_ERR_OK;++j) e=uc_reg_read(p->engine,UC_X86_REG_XMM0+j,frame.xmm[j]);
    if(e==UC_ERR_OK) e=uc_reg_read(p->engine,UC_X86_REG_RSP,&frame.rsp);
    if(e==UC_ERR_OK) e=uc_reg_read(p->engine,UC_X86_REG_RAX,&frame.rax);
    frame.rdx=frame.gp[2];
    if(e!=UC_ERR_OK) return BB_PROBE_ENGINE;
    BbAbiStatus status=bb_abi_dispatch(&p->bindings[i].abi,&frame,&p->bindings[i].memory);
    if(status!=BB_ABI_OK) return status==BB_ABI_INVALID ? BB_PROBE_INVALID : BB_PROBE_FAULT;
    e=uc_reg_write(p->engine,UC_X86_REG_RDX,&frame.rdx);
    for(unsigned j=0;j<2 && e==UC_ERR_OK;++j) e=uc_reg_write(p->engine,UC_X86_REG_XMM0+j,frame.xmm[j]);
    *result=frame.rax;
    return engine_status(e);
}
BbProbeStatus bb_probe_bind_abi(BbCpuProbe *p,const BbAbiImport *binding,
                               const BbAbiMemory *memory,uint64_t *address) {
    if(!p || !binding || !binding->name || !binding->host || !address ||
       bb_abi_validate(&binding->signature)!=BB_ABI_OK ||
       (memory && (!memory->check || !memory->read || !memory->write))) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    /* Guest names are unique across signature-aware bindings. */
    for(unsigned j=0;j<p->count;++j) if(p->bindings[j].abi.name &&
        !strcmp(binding->name,p->bindings[j].abi.name)) {
        pthread_mutex_unlock(&p->lock); return BB_PROBE_INVALID;
    }
    unsigned i=p->count;
    BbProbeStatus status=bb_probe_bind(p,abi_host,(void *)(uintptr_t)i,address);
    if(status==BB_PROBE_OK) {
        p->bindings[i].abi=*binding;
        p->bindings[i].memory=memory ? *memory : (BbAbiMemory){abi_check,abi_read,abi_write,p};
    }
    pthread_mutex_unlock(&p->lock); return status;
}
void bb_probe_set_fault_handler(BbCpuProbe *p,BbProbeFaultHandler handler,void *user) {
    if(!p) return;
    pthread_mutex_lock(&p->lock);
    p->fault_handler=handler; p->fault_user=user;
    pthread_mutex_unlock(&p->lock);
}
static BbProbeStatus execute(BbCpuProbe *p,uint64_t pc,uint64_t *result) {
    BbProbeStatus status=BB_PROBE_LIMIT;
    uc_err e=UC_ERR_OK; uint64_t sp=0;
    memset(&p->fault,0,sizeof(p->fault));
    unsigned resume=0;
    for(;e==UC_ERR_OK && resume<MAX_RESUMES;++resume) {
        p->pending=-1;
        p->unsupported=0;
        p->external_error=0;
        p->memory_fault=0;
        p->dirty_write=0; p->write_stopped=0; p->clock_pending=0;
        e=uc_emu_start(p->engine,pc,BB_PROBE_RETURN,1000000,p->resumable ? 10000 : 5000000);
        uc_reg_read(p->engine,UC_X86_REG_RIP,&pc);
        if(p->dirty_write && !p->write_observer(p,p->write_start,(size_t)(p->write_end-p->write_start),p->write_user)) p->external_error=1;
        if(p->external_error) { status=BB_PROBE_ENGINE; break; }
        if(p->write_stopped && e==UC_ERR_OK) { pc=p->write_resume; continue; }
        if(p->clock_pending && e==UC_ERR_OK) {
            if(p->tsc>UINT64_MAX-p->tsc_step) { status=BB_PROBE_LIMIT; break; }
            p->tsc+=p->tsc_step;
            uint64_t lo=(uint32_t)p->tsc,hi=p->tsc>>32,aux=p->tsc_aux;
            e=uc_reg_write(p->engine,UC_X86_REG_RAX,&lo);
            if(e==UC_ERR_OK) e=uc_reg_write(p->engine,UC_X86_REG_RDX,&hi);
            if(e==UC_ERR_OK && p->clock_pending==3) e=uc_reg_write(p->engine,UC_X86_REG_RCX,&aux);
            pc+=(unsigned)p->clock_pending;
            if(e==UC_ERR_OK) e=uc_reg_write(p->engine,UC_X86_REG_RIP,&pc);
            continue;
        }
        if(p->unsupported) { p->fault.pc=pc; status=BB_PROBE_UNSUPPORTED; break; }
        if(e!=UC_ERR_OK) {
            if(p->memory_fault) {
                BbProbeFault stopped=p->fault; stopped.engine_error=e;
                if(p->fault_handler && p->fault_handler(p,&stopped,p->fault_user)) {
                    p->fault=stopped; pc=stopped.pc; e=UC_ERR_OK;
                    continue; /* Explicit retry of the faulting instruction. */
                }
                p->fault=stopped;
            } else { p->fault.pc=pc; p->fault.engine_error=e; }
            status=BB_PROBE_FAULT; break;
        }
        if(pc==BB_PROBE_RETURN) { status=BB_PROBE_OK; break; }
        int i=p->pending;
        if(i<0 || (unsigned)i>=p->count || pc!=BRIDGE_BASE+16*(unsigned)i) {
            if(p->resumable) status=BB_PROBE_YIELD;
            break;
        }
        uint64_t args[6]={0};
        const int abi_regs[]={UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,UC_X86_REG_RCX,UC_X86_REG_R8,UC_X86_REG_R9};
        for(unsigned a=0;a<6 && e==UC_ERR_OK;++a) e=uc_reg_read(p->engine,abi_regs[a],&args[a]);
        if(e!=UC_ERR_OK) break;
        status=p->bindings[i].call(p,args,result,p->bindings[i].user);
        if(status!=BB_PROBE_OK) {
            p->fault.pc=pc;
            snprintf(p->fault.import_name,sizeof(p->fault.import_name),"%s",p->bindings[i].abi.name ? p->bindings[i].abi.name : "untyped-host-bridge");
            break;
        }
        status=BB_PROBE_LIMIT;
        e=uc_reg_write(p->engine,UC_X86_REG_RAX,result);
        if(e==UC_ERR_OK) e=uc_reg_read(p->engine,UC_X86_REG_RSP,&sp);
        if(e==UC_ERR_OK) e=uc_mem_read(p->engine,sp,&pc,sizeof(pc));
        sp+=8;
        if(e==UC_ERR_OK) e=uc_reg_write(p->engine,UC_X86_REG_RSP,&sp);
        if(e==UC_ERR_OK) e=uc_reg_write(p->engine,UC_X86_REG_RIP,&pc);
        if(e==UC_ERR_OK && p->pause_requested) { status=BB_PROBE_YIELD; break; }
        if(e==UC_ERR_OK && pc==BB_PROBE_RETURN) { status=BB_PROBE_OK; break; }
    }
    if(status==BB_PROBE_LIMIT && p->resumable && e==UC_ERR_OK && resume==MAX_RESUMES) status=BB_PROBE_YIELD;
    if(status==BB_PROBE_OK) {
        e=uc_reg_read(p->engine,UC_X86_REG_RAX,result);
        if(e!=UC_ERR_OK) status=BB_PROBE_ENGINE;
    } else if(e!=UC_ERR_OK && status!=BB_PROBE_FAULT) status=BB_PROBE_ENGINE;
    return status;
}
BbProbeStatus bb_probe_invoke(BbCpuProbe *p,BbGuestCall *call,BbProbeFault *fault) {
    if(!p || !call || !call->target || call->stack_top ||
       call->kind<BB_CALL_VOID || call->kind>BB_CALL_ENTRY) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    if(p->depth==MAX_DEPTH) { pthread_mutex_unlock(&p->lock); return BB_PROBE_LIMIT; }
    uc_context *saved=NULL;
    uc_err e=uc_context_alloc(p->engine,&saved);
    if(e==UC_ERR_OK) e=uc_context_save(p->engine,saved);
    if(e!=UC_ERR_OK) {
        if(saved) uc_context_free(saved);
        pthread_mutex_unlock(&p->lock); return BB_PROBE_ENGINE;
    }
    uint64_t sp=BB_PROBE_STACK+(++p->depth)*STACK_SLICE-8,stop=BB_PROBE_RETURN;
    const int regs[]={UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX};
    e=uc_reg_write(p->engine,UC_X86_REG_RSP,&sp);
    if(e==UC_ERR_OK) e=uc_mem_write(p->engine,sp,&stop,sizeof(stop));
    for(unsigned i=0;i<3 && e==UC_ERR_OK;++i) {
        uint64_t arg=call->args[i]; e=uc_reg_write(p->engine,regs[i],&arg);
    }
    uint64_t pc=call->target,result=0;
    int previous_resumable=p->resumable,previous_pause=p->pause_requested;
    p->resumable=0; p->pause_requested=0;
    BbProbeStatus status=e==UC_ERR_OK ? execute(p,pc,&result) : BB_PROBE_ENGINE;
    p->resumable=previous_resumable; p->pause_requested=previous_pause;
    if(fault) *fault=p->fault;
    uc_err restored=uc_context_restore(p->engine,saved);
    uc_context_free(saved); --p->depth;
    if(restored!=UC_ERR_OK) status=BB_PROBE_ENGINE;
    if(status==BB_PROBE_OK) {
        if(call->kind==BB_CALL_MODULE_INIT) result=(uint64_t)(int64_t)(int32_t)result;
        if(call->kind==BB_CALL_VOID || call->kind==BB_CALL_VOID_PTR ||
           call->kind==BB_CALL_VOID_U64_U64 || call->kind==BB_CALL_ENTRY) result=0;
        call->result=(uintptr_t)result;
    }
    pthread_mutex_unlock(&p->lock);
    return status;
}
const char *bb_probe_status_name(BbProbeStatus status) {
    switch(status) {
    case BB_PROBE_OK: return "ok";
    case BB_PROBE_INVALID: return "invalid request";
    case BB_PROBE_ENGINE: return "engine API failure";
    case BB_PROBE_FAULT: return "guest fault";
    case BB_PROBE_LIMIT: return "execution/depth/binding limit";
    case BB_PROBE_YIELD: return "suspended continuation";
    case BB_PROBE_UNSUPPORTED: return "guest syscall is not provided";
    }
    return "unknown status";
}

struct BbProbeContext { BbCpuProbe *owner; uc_context *registers; int done; };
BbProbeStatus bb_probe_context_create(BbCpuProbe *p,uint64_t entry,uint64_t top,
    const uint64_t args[3],uint64_t fs,BbProbeContext **out) {
    if(!p || !out || !args || !entry || top<16 || top%16) return BB_PROBE_INVALID;
    *out=NULL; BbProbeContext *c=calloc(1,sizeof(*c)); if(!c) return BB_PROBE_ENGINE;
    pthread_mutex_lock(&p->lock);
    uc_context *saved=NULL; uc_err e=uc_context_alloc(p->engine,&saved);
    if(e==UC_ERR_OK) e=uc_context_save(p->engine,saved);
    if(e==UC_ERR_OK) e=uc_context_restore(p->engine,p->initial);
    if(e==UC_ERR_OK) e=uc_context_alloc(p->engine,&c->registers);
    if(e==UC_ERR_OK) e=uc_context_save(p->engine,c->registers);
    uint64_t sp=top-8,stop=BB_PROBE_RETURN,zero=0; uint32_t mxcsr=0x9fc0; uint16_t fpcw=0x37f;
    if(e==UC_ERR_OK) e=uc_mem_write(p->engine,sp,&stop,8);
    const int regs[]={UC_X86_REG_RIP,UC_X86_REG_RSP,UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,UC_X86_REG_FS_BASE,UC_X86_REG_GS_BASE,UC_X86_REG_MXCSR,UC_X86_REG_FPCW};
    const void *values[]={&entry,&sp,args,args+1,args+2,&fs,&zero,&mxcsr,&fpcw};
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs) && e==UC_ERR_OK;++i) e=uc_context_reg_write(c->registers,regs[i],values[i]);
    if(saved) { if(uc_context_restore(p->engine,saved)!=UC_ERR_OK) e=UC_ERR_EXCEPTION; uc_context_free(saved); }
    pthread_mutex_unlock(&p->lock);
    if(e!=UC_ERR_OK) { bb_probe_context_free(c); return BB_PROBE_ENGINE; }
    c->owner=p; *out=c; return BB_PROBE_OK;
}
BbProbeStatus bb_probe_context_restart(BbCpuProbe *p,BbProbeContext *c,uint64_t entry,uint64_t top,const uint64_t args[3]) {
    if(!p || !c || c->owner!=p || !entry || top<16 || top%16 || !args || p->depth) return BB_PROBE_INVALID;
    uint64_t sp=top-8,stop=BB_PROBE_RETURN,zero=0;
    const int regs[]={UC_X86_REG_RIP,UC_X86_REG_RSP,UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,UC_X86_REG_RAX};
    const void *values[]={&entry,&sp,args,args+1,args+2,&zero};
    pthread_mutex_lock(&p->lock);
    uc_err e=uc_mem_write(p->engine,sp,&stop,8);
    for(unsigned i=0;i<sizeof(regs)/sizeof(*regs) && e==UC_ERR_OK;++i) e=uc_context_reg_write(c->registers,regs[i],values[i]);
    if(e==UC_ERR_OK) c->done=0;
    pthread_mutex_unlock(&p->lock); return engine_status(e);
}
void bb_probe_context_free(BbProbeContext *c) { if(c) { if(c->registers) uc_context_free(c->registers); free(c); } }
BbProbeStatus bb_probe_context_invoke(BbCpuProbe *p,BbProbeContext *c,BbGuestCall *call) {
    if(!p || !c || c->owner!=p || !call) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    uc_context *saved=NULL; uc_err e=uc_context_alloc(p->engine,&saved);
    if(e==UC_ERR_OK) e=uc_context_save(p->engine,saved);
    if(e==UC_ERR_OK) e=uc_context_restore(p->engine,c->registers);
    BbProbeStatus status=e==UC_ERR_OK ? bb_probe_invoke(p,call,NULL) : BB_PROBE_ENGINE;
    if(saved) { if(uc_context_restore(p->engine,saved)!=UC_ERR_OK) status=BB_PROBE_ENGINE; uc_context_free(saved); }
    pthread_mutex_unlock(&p->lock); return status;
}
BbProbeStatus bb_probe_context_read(BbProbeContext *c,int reg,void *value) {
    return c && value ? engine_status(uc_context_reg_read(c->registers,reg,value)) : BB_PROBE_INVALID;
}
BbProbeStatus bb_probe_context_write(BbProbeContext *c,int reg,const void *value) {
    return c && value && !c->done ? engine_status(uc_context_reg_write(c->registers,reg,value)) : BB_PROBE_INVALID;
}
int bb_probe_idle(BbCpuProbe *p) {
    if(!p) return 0;
    pthread_mutex_lock(&p->lock); int idle=!p->depth; pthread_mutex_unlock(&p->lock); return idle;
}
BbProbeStatus bb_probe_pause(BbCpuProbe *p) {
    if(!p || !p->resumable || p->depth!=1) return BB_PROBE_INVALID;
    p->pause_requested=1; return BB_PROBE_OK;
}
BbProbeStatus bb_probe_context_resume(BbCpuProbe *p,BbProbeContext *c,uint64_t *result,BbProbeFault *fault) {
    if(!p || !c || c->owner!=p || !result || c->done) return BB_PROBE_INVALID;
    pthread_mutex_lock(&p->lock);
    if(p->depth) { pthread_mutex_unlock(&p->lock); return BB_PROBE_INVALID; }
    uc_context *saved=NULL; uc_err e=uc_context_alloc(p->engine,&saved);
    if(e==UC_ERR_OK) e=uc_context_save(p->engine,saved);
    if(e==UC_ERR_OK) e=uc_context_restore(p->engine,c->registers);
    uint64_t pc=0,value=0; if(e==UC_ERR_OK) e=uc_reg_read(p->engine,UC_X86_REG_RIP,&pc);
    p->depth=1; p->resumable=1; p->pause_requested=0;
    BbProbeStatus status=e==UC_ERR_OK ? execute(p,pc,&value) : BB_PROBE_ENGINE;
    if(status==BB_PROBE_OK || status==BB_PROBE_YIELD) {
        if(uc_context_save(p->engine,c->registers)!=UC_ERR_OK) status=BB_PROBE_ENGINE;
        if(status==BB_PROBE_OK) { c->done=1; *result=value; }
    }
    if(fault) *fault=p->fault;
    p->depth=0; p->resumable=0; p->pause_requested=0;
    if(saved) { if(uc_context_restore(p->engine,saved)!=UC_ERR_OK) status=BB_PROBE_ENGINE; uc_context_free(saved); }
    pthread_mutex_unlock(&p->lock); return status;
}
BbProbeStatus bb_probe_virtual_clock(BbCpuProbe *p,uint64_t initial,uint64_t step) {
    if(!p || !step || p->depth || !p->write_observer) return BB_PROBE_INVALID;
    p->tsc=initial; p->tsc_step=step; return BB_PROBE_OK;
}
uint64_t bb_probe_clock_value(BbCpuProbe *p) { return p ? p->tsc : 0; }
void bb_probe_clock_aux(BbCpuProbe *p,uint32_t aux) { if(p) p->tsc_aux=aux; }
