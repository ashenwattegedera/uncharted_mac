#include "probe.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_BINDINGS=16, MAX_DEPTH=8, STACK_SLICE=65536, MAX_RESUMES=64 };
#define BRIDGE_BASE UINT64_C(0x200000)
struct BbCpuProbe {
    uc_engine *engine;
    pthread_mutex_t lock;
    uc_hook bridge_hook,fault_hook;
    struct { BbProbeHostCall call; void *user; } bindings[MAX_BINDINGS];
    unsigned count,depth;
    int pending;
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
static bool fault_hook(uc_engine *engine,uc_mem_type access,uint64_t address,int size,int64_t value,void *user) {
    (void)size; (void)value;
    BbCpuProbe *p=user;
    p->fault.address=address; p->fault.access=access;
    uc_reg_read(engine,UC_X86_REG_RIP,&p->fault.pc);
    return p->fault_handler && p->fault_handler(p,&p->fault,p->fault_user);
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
    if(e==UC_ERR_OK) e=uc_hook_add(p->engine,&p->bridge_hook,UC_HOOK_CODE,(void *)bridge_hook,p,BRIDGE_BASE,BRIDGE_BASE+16*MAX_BINDINGS-1);
    if(e==UC_ERR_OK) e=uc_hook_add(p->engine,&p->fault_hook,UC_HOOK_MEM_INVALID,(void *)fault_hook,p,1,0);
    if(e!=UC_ERR_OK) { bb_probe_close(p); return BB_PROBE_ENGINE; }
    *out=p;
    return BB_PROBE_OK;
}
void bb_probe_close(BbCpuProbe *p) {
    if(!p) return;
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
void bb_probe_set_fault_handler(BbCpuProbe *p,BbProbeFaultHandler handler,void *user) {
    pthread_mutex_lock(&p->lock);
    p->fault_handler=handler; p->fault_user=user;
    pthread_mutex_unlock(&p->lock);
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
    BbProbeStatus status=BB_PROBE_LIMIT;
    memset(&p->fault,0,sizeof(p->fault));
    for(unsigned resume=0;e==UC_ERR_OK && resume<MAX_RESUMES;++resume) {
        p->pending=-1;
        e=uc_emu_start(p->engine,pc,BB_PROBE_RETURN,1000000,5000000);
        uc_reg_read(p->engine,UC_X86_REG_RIP,&pc);
        if(e!=UC_ERR_OK) { p->fault.pc=pc; p->fault.engine_error=e; status=BB_PROBE_FAULT; break; }
        if(pc==BB_PROBE_RETURN) { status=BB_PROBE_OK; break; }
        int i=p->pending;
        if(i<0 || (unsigned)i>=p->count || pc!=BRIDGE_BASE+16*(unsigned)i) break;
        uint64_t args[6]={0};
        const int abi_regs[]={UC_X86_REG_RDI,UC_X86_REG_RSI,UC_X86_REG_RDX,UC_X86_REG_RCX,UC_X86_REG_R8,UC_X86_REG_R9};
        for(unsigned a=0;a<6 && e==UC_ERR_OK;++a) e=uc_reg_read(p->engine,abi_regs[a],&args[a]);
        if(e!=UC_ERR_OK) break;
        status=p->bindings[i].call(p,args,&result,p->bindings[i].user);
        if(status!=BB_PROBE_OK) break;
        status=BB_PROBE_LIMIT;
        e=uc_reg_write(p->engine,UC_X86_REG_RAX,&result);
        if(e==UC_ERR_OK) e=uc_reg_read(p->engine,UC_X86_REG_RSP,&sp);
        if(e==UC_ERR_OK) e=uc_mem_read(p->engine,sp,&pc,sizeof(pc));
        sp+=8;
        if(e==UC_ERR_OK) e=uc_reg_write(p->engine,UC_X86_REG_RSP,&sp);
        if(e==UC_ERR_OK && pc==BB_PROBE_RETURN) { status=BB_PROBE_OK; break; }
    }
    if(status==BB_PROBE_OK) {
        e=uc_reg_read(p->engine,UC_X86_REG_RAX,&result);
        if(e!=UC_ERR_OK) status=BB_PROBE_ENGINE;
    } else if(e!=UC_ERR_OK && status!=BB_PROBE_FAULT) status=BB_PROBE_ENGINE;
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
    }
    return "unknown status";
}
