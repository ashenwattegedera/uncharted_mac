#define _DARWIN_C_SOURCE
#include "translated_tasks.h"
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { THREADS=64,KEYS=32,LOCKS=64,ATTRS=32,MODULES=8,HANDLERS=256,STACK=262144 };
enum State { EMPTY,READY,WAIT_JOIN,WAIT_MUTEX,WAIT_COND,DONE,IDLE };
enum Event { NONE,EXIT_EVENT,FIBER_RUN_EVENT,FIBER_SWITCH_EVENT,FIBER_RETURN_EVENT };
typedef struct {
    BbProbeContext *cpu;
    enum State state;
    uint64_t id,stack,tls,tcb,module[MODULES],values[KEYS],result,joiner,wait,out,mutex;
    size_t stack_size,tls_size;
    uint64_t worker_mask;
    int detached,joined,persistent,cleaned;
    enum Event event;
    uint64_t event_args[3];
} Thread;
typedef struct { uint64_t owner; int alive; } Mutex;
typedef struct { int alive; } Cond;
typedef struct { int alive,detached; size_t stack; } Attr;
typedef struct { unsigned char *data; size_t filesz,memsz,alignment; } Tls;
typedef struct { uint64_t function,arg,dso; int cxa,called; } Finalizer;
struct BbTasks {
    BbTranslatedRuntime *runtime;
    Thread threads[THREADS],*current;
    size_t thread_count,cursor;
    struct { int alive; uint64_t destructor; } keys[KEYS];
    size_t key_count;
    Mutex mutexes[LOCKS]; Cond conds[LOCKS]; Attr attrs[ATTRS];
    size_t mutex_count,cond_count,attr_count;
    Tls templates[MODULES];
    Finalizer handlers[HANDLERS]; size_t handler_count;
    pthread_mutex_t schedule;
    pthread_cond_t changed;
    unsigned workers,turn;
    size_t slices,budget;
    int running,stop,failed;
    char diagnostic[256];
};
static uint64_t value(const BbAbiValue *v) { uint64_t n; memcpy(&n,v->bytes,8); return n; }
static int32_t error(unsigned n,int posix) { return n ? (int32_t)(posix ? n : UINT32_C(0x80020000)|n) : 0; }
static BbTranslatedStatus fail(BbTasks *t,const char *message) {
    snprintf(t->diagnostic,sizeof(t->diagnostic),"%s",message); return BB_TRANSLATED_EXECUTION;
}
static BbAbiMemory memory(BbTasks *t) { return bb_translated_memory_access(t->runtime); }
static int readable(BbTasks *t,uint64_t p,size_t n) { BbAbiMemory m=memory(t); return p && m.check(m.user,p,n,0); }
static int writable(BbTasks *t,uint64_t p,size_t n) { BbAbiMemory m=memory(t); return p && m.check(m.user,p,n,1); }
static int get(BbTasks *t,uint64_t p,void *out,size_t n) { BbAbiMemory m=memory(t); return readable(t,p,n) && m.read(m.user,p,out,n); }
static int put(BbTasks *t,uint64_t p,const void *in,size_t n) { BbAbiMemory m=memory(t); return writable(t,p,n) && m.write(m.user,p,in,n); }
static int executable(BbTasks *t,uint64_t p) { BbVmView v; return bb_vm_query(bb_translated_memory(t->runtime),p,&v)==BB_VM_OK && (v.guest_permissions&BB_VM_EXEC); }
static int alloc(BbTasks *t,size_t n,uint64_t *out) {
    (void)t; void *p=NULL;
    uintptr_t fn=runtime_memory_resolve_symbol("sceKernelMapFlexibleMemory");
    if(((int32_t (*)(void **,uint64_t,int,int))fn)(&p,n,3,0)) return 0;
    *out=(uintptr_t)p; return 1;
}
static void unmap(uint64_t address,size_t n) {
    if(address) { uintptr_t fn=runtime_memory_resolve_symbol("sceKernelMunmap"); ((int32_t (*)(void *,uint64_t))fn)((void *)(uintptr_t)address,n); }
}
static Thread *find(BbTasks *t,uint64_t id) { return id && id<=t->thread_count && t->threads[id-1].id==id ? &t->threads[id-1] : NULL; }
static int pause_task(BbTasks *t) { return bb_probe_pause(bb_translated_cpu(t->runtime))==BB_PROBE_OK; }
BbTranslatedStatus bb_tasks_create(BbTranslatedRuntime *r,BbTasks **out) {
    if(!r || !out) return BB_TRANSLATED_INVALID;
    *out=NULL; BbTasks *t=calloc(1,sizeof(*t)); if(!t) return BB_TRANSLATED_MEMORY;
    if(pthread_mutex_init(&t->schedule,NULL)) { free(t); return BB_TRANSLATED_MEMORY; }
    if(pthread_cond_init(&t->changed,NULL)) { pthread_mutex_destroy(&t->schedule); free(t); return BB_TRANSLATED_MEMORY; }
    t->runtime=r;
    if(bb_probe_virtual_clock(bb_translated_cpu(r),0,1000)!=BB_PROBE_OK) { bb_tasks_destroy(t); return BB_TRANSLATED_EXECUTION; }
    *out=t; return BB_TRANSLATED_OK;
}
int bb_tasks_busy(BbTasks *t) { return t && t->running; }
void bb_tasks_destroy(BbTasks *t) {
    if(!t || t->running) return;
    for(size_t i=0;i<t->thread_count;++i) { bb_probe_context_free(t->threads[i].cpu); unmap(t->threads[i].stack,t->threads[i].stack_size); unmap(t->threads[i].tls,t->threads[i].tls_size); }
    for(unsigned i=0;i<MODULES;++i) free(t->templates[i].data);
    pthread_cond_destroy(&t->changed); pthread_mutex_destroy(&t->schedule); free(t);
}
BbTranslatedStatus bb_tasks_set_tls(BbTasks *t,unsigned module,const void *data,size_t filesz,size_t memsz,size_t alignment) {
    if(!t || t->thread_count || !module || module>=MODULES || filesz>memsz || memsz>1024*1024 ||
       (filesz && !data) || !alignment || alignment>BB_VM_GUEST_PAGE || (alignment&(alignment-1))) return BB_TRANSLATED_INVALID;
    unsigned char *copy=calloc(memsz ? memsz : 1,1); if(!copy) return BB_TRANSLATED_MEMORY;
    if(filesz) memcpy(copy,data,filesz);
    free(t->templates[module].data); t->templates[module]=(Tls){copy,filesz,memsz,alignment}; return BB_TRANSLATED_OK;
}
static size_t align_up(size_t n,size_t a) { return (n+a-1)&~(a-1); }
static int make_tls(BbTasks *t,Thread *thread) {
    Tls *main=t->templates+1;
    size_t offset=align_up(main->memsz,main->alignment ? main->alignment : 16),total=offset+256;
    for(unsigned i=2;i<MODULES;++i) total=align_up(total,t->templates[i].alignment ? t->templates[i].alignment : 16)+t->templates[i].memsz;
    total=align_up(total,BB_VM_GUEST_PAGE);
    if(!alloc(t,total,&thread->tls)) return 0;
    thread->tls_size=total; thread->tcb=thread->tls+offset; thread->module[1]=thread->tls;
    if(main->filesz && !put(t,thread->tls,main->data,main->filesz)) return 0;
    uint64_t header[3]={thread->tcb,thread->tcb+32,thread->id};
    if(!put(t,thread->tcb,header,sizeof(header))) return 0;
    size_t cursor=offset+256; uint64_t dtv[MODULES*2]={0};
    dtv[2]=thread->tls; dtv[3]=main->memsz;
    for(unsigned i=2;i<MODULES;++i) {
        Tls *m=t->templates+i; cursor=align_up(cursor,m->alignment ? m->alignment : 16);
        if(m->memsz) {
            thread->module[i]=thread->tls+cursor;
            if(m->filesz && !put(t,thread->module[i],m->data,m->filesz)) return 0;
            dtv[2*i]=thread->module[i]; dtv[2*i+1]=m->memsz;
        }
        cursor+=m->memsz;
    }
    return put(t,thread->tcb+32,dtv,sizeof(dtv));
}
static int seed(BbTasks *t,Thread *thread,uint64_t entry,const uint64_t args[3]) {
    if(!executable(t,entry)) return 0;
    bb_probe_context_free(thread->cpu); thread->cpu=NULL;
    uint64_t top=thread->stack+thread->stack_size-BB_VM_GUEST_PAGE;
    if(bb_probe_context_create(bb_translated_cpu(t->runtime),entry,top,args,thread->tcb,&thread->cpu)!=BB_PROBE_OK) return 0;
    thread->state=READY; thread->event=NONE; thread->result=0; return 1;
}
static BbTranslatedStatus spawn(BbTasks *t,uint64_t entry,const uint64_t args[3],size_t stack,int detached,uint64_t *id) {
    if(!t || !args || !id || t->thread_count==THREADS || !executable(t,entry)) return BB_TRANSLATED_INVALID;
    Thread next={.id=t->thread_count+1,.detached=detached};
    next.stack_size=align_up(stack,BB_VM_GUEST_PAGE)+2*BB_VM_GUEST_PAGE;
    if(!alloc(t,next.stack_size,&next.stack) || !make_tls(t,&next)) goto failed;
    BbGuestMemory *vm=bb_translated_memory(t->runtime);
    if(bb_vm_protect(vm,next.stack,BB_VM_GUEST_PAGE,0)!=BB_VM_OK ||
       bb_vm_protect(vm,next.stack+next.stack_size-BB_VM_GUEST_PAGE,BB_VM_GUEST_PAGE,0)!=BB_VM_OK || !seed(t,&next,entry,args)) goto failed;
    t->threads[t->thread_count++]=next; *id=next.id; return BB_TRANSLATED_OK;
failed:
    bb_probe_context_free(next.cpu); unmap(next.stack,next.stack_size); unmap(next.tls,next.tls_size); return BB_TRANSLATED_MEMORY;
}
BbTranslatedStatus bb_tasks_spawn(BbTasks *t,uint64_t entry,const uint64_t args[3],uint64_t *id) { return spawn(t,entry,args,STACK,0,id); }
static int callback(BbTasks *t,Thread *thread,uint64_t function,uint64_t argument) {
    BbGuestCall call={.kind=BB_CALL_VOID_PTR,.target=function,.args={argument}};
    return bb_probe_context_invoke(bb_translated_cpu(t->runtime),thread->cpu,&call)==BB_PROBE_OK;
}
static int cleanup(BbTasks *t,Thread *thread) {
    if(thread->cleaned) return 1;
    /* POSIX destructor iteration bound; clearing before invocation permits rearm. */
    for(unsigned pass=0;pass<4;++pass) for(size_t k=0;k<t->key_count;++k) if(t->keys[k].alive && t->keys[k].destructor && thread->values[k]) {
        uint64_t argument=thread->values[k]; thread->values[k]=0;
        if(!callback(t,thread,t->keys[k].destructor,argument)) return 0;
    }
    memset(thread->values,0,sizeof(thread->values)); thread->cleaned=1; return 1;
}
static int finish(BbTasks *t,Thread *thread,uint64_t result) {
    thread->result=result;
    if(!thread->persistent && !cleanup(t,thread)) return 0;
    thread->state=thread->persistent ? IDLE : DONE;
    if(thread->joiner) {
        Thread *joiner=find(t,thread->joiner);
        if(!joiner || joiner->state!=WAIT_JOIN || (joiner->out && !put(t,joiner->out,&result,8))) return 0;
        joiner->state=READY; thread->joined=1;
    }
    return 1;
}
static int lock_index(BbTasks *t,uint64_t pointer,uint64_t *id) {
    return get(t,pointer,id,8) && *id && *id<=t->mutex_count && t->mutexes[*id-1].alive;
}
static int cond_index(BbTasks *t,uint64_t pointer,uint64_t *id) {
    return get(t,pointer,id,8) && *id && *id<=t->cond_count && t->conds[*id-1].alive;
}
static void release_mutex(BbTasks *t,uint64_t id) {
    Mutex *m=t->mutexes+id-1; m->owner=0;
    for(size_t i=0;i<t->thread_count;++i) if(t->threads[i].state==WAIT_MUTEX && t->threads[i].wait==id) {
        m->owner=t->threads[i].id; t->threads[i].state=READY; break;
    }
}
static void signal_cond(BbTasks *t,uint64_t id,int all) {
    for(size_t i=0;i<t->thread_count;++i) {
        Thread *thread=t->threads+i;
        if(thread->state==WAIT_COND && thread->wait==id) {
            Mutex *m=t->mutexes+thread->mutex-1;
            if(!m->owner) { m->owner=thread->id; thread->state=READY; }
            else { thread->wait=thread->mutex; thread->state=WAIT_MUTEX; }
            if(!all) return;
        }
    }
}
static int finalize(BbTasks *t,uint64_t dso) {
    for(size_t i=t->handler_count;i;) {
        Finalizer *f=t->handlers+--i;
        if(!f->called && (!dso || f->dso==dso)) {
            f->called=1;
            if(!callback(t,t->current,f->function,f->cxa ? f->arg : 0)) return 0;
            i=t->handler_count; /* recursive registration/finalization is safe */
        }
    }
    return 1;
}
static int tick(BbTasks *t);
typedef struct { BbTasks *tasks; unsigned index; } Worker;
static void *worker(void *opaque) {
    Worker *w=opaque; BbTasks *t=w->tasks;
    pthread_mutex_lock(&t->schedule);
    while(!t->stop) {
        while(!t->stop && t->turn!=w->index) pthread_cond_wait(&t->changed,&t->schedule);
        if(t->stop) break;
        if(bb_translated_acquire(t->runtime)!=BB_TRANSLATED_OK) { t->failed=1; t->stop=1; fail(t,"worker could not acquire the exclusive VM lease"); }
        else {
            int more=tick(t);
            if(bb_translated_release(t->runtime)!=BB_TRANSLATED_OK) { t->failed=1; fail(t,"worker could not release the exclusive VM lease"); }
            if(more<=0 || t->failed) t->stop=1;
        }
        t->turn=(t->turn+1)%t->workers; pthread_cond_broadcast(&t->changed);
    }
    pthread_mutex_unlock(&t->schedule); return NULL;
}
BbTranslatedStatus bb_tasks_run(BbTasks *t,unsigned workers,size_t budget) {
    if(!t || t->running || !workers || workers>8 || !budget || !bb_translated_cpu(t->runtime)) return BB_TRANSLATED_INVALID;
    t->running=1; t->stop=t->failed=0; t->turn=0; t->workers=workers; t->slices=0; t->budget=budget; t->diagnostic[0]=0;
    pthread_t hosts[8]; Worker work[8]; unsigned created=0;
    pthread_mutex_lock(&t->schedule);
    if(bb_translated_release(t->runtime)!=BB_TRANSLATED_OK) { t->running=0; pthread_mutex_unlock(&t->schedule); return BB_TRANSLATED_INVALID; }
    for(;created<workers;++created) { work[created]=(Worker){t,created}; if(pthread_create(hosts+created,NULL,worker,work+created)) break; }
    if(created!=workers) { t->stop=1; t->failed=1; fail(t,"could not create requested native workers"); }
    pthread_cond_broadcast(&t->changed); pthread_mutex_unlock(&t->schedule);
    for(unsigned i=0;i<created;++i) pthread_join(hosts[i],NULL);
    int acquired=bb_translated_acquire(t->runtime)==BB_TRANSLATED_OK;
    t->running=0; t->current=NULL;
    return acquired && !t->failed ? BB_TRANSLATED_OK : BB_TRANSLATED_EXECUTION;
}
BbTranslatedStatus bb_tasks_result(BbTasks *t,uint64_t id,uint64_t *result) {
    Thread *thread=t ? find(t,id) : NULL;
    if(!thread || !result || t->running || (thread->state!=DONE && thread->state!=IDLE)) return BB_TRANSLATED_INVALID;
    *result=thread->result; return BB_TRANSLATED_OK;
}
BbTranslatedStatus bb_tasks_call(BbTasks *t,uint64_t entry,const uint64_t args[3],unsigned workers,uint64_t *result) {
    if(!t || !args || !result || t->running) return BB_TRANSLATED_INVALID;
    Thread *main=NULL;
    for(size_t i=0;i<t->thread_count;++i) if(t->threads[i].persistent) main=t->threads+i;
    if(!main) {
        uint64_t id=0; BbTranslatedStatus s=bb_tasks_spawn(t,entry,args,&id); if(s!=BB_TRANSLATED_OK) return s;
        main=find(t,id); main->persistent=1;
    } else if(main->state!=IDLE || !seed(t,main,entry,args)) return BB_TRANSLATED_INVALID;
    BbTranslatedStatus s=bb_tasks_run(t,workers,100000); if(s==BB_TRANSLATED_OK) *result=main->result; return s;
}
BbTranslatedStatus bb_tasks_finalize(BbTasks *t) {
    if(!t || t->running || !bb_translated_cpu(t->runtime)) return BB_TRANSLATED_INVALID;
    for(size_t i=0;i<t->thread_count;++i) if(t->threads[i].persistent && t->threads[i].state==IDLE) {
        t->current=t->threads+i;
        int ok=finalize(t,0) && cleanup(t,t->current); t->current=NULL;
        return ok ? BB_TRANSLATED_OK : fail(t,"process finalizer or main TLS destructor failed");
    }
    return t->handler_count ? BB_TRANSLATED_INVALID : BB_TRANSLATED_OK;
}
uint64_t bb_tasks_worker_mask(BbTasks *t,uint64_t id) { Thread *thread=t && !t->running ? find(t,id) : NULL; return thread ? thread->worker_mask : 0; }
const char *bb_tasks_diagnostic(BbTasks *t) { return t ? t->diagnostic : "no task runtime"; }
BbAbiStatus bb_tasks_import(BbTasks *t,unsigned operation,const BbAbiValue *v,size_t count,BbAbiValue *result) {
    if(!t || !t->current) return BB_ABI_HOST;
    Thread *current=t->current; uint64_t a[7]={0};
    for(unsigned i=0;i<count && i<7;++i) a[i]=value(v+i);
    int posix=operation>=BB_TASK_POSIX; if(posix) operation-=BB_TASK_POSIX;
    unsigned err=0; uint64_t returned=0,id=0;
    switch(operation) {
    case BB_TASK_SELF: returned=current->id; goto wide;
    case BB_TASK_ERRNO: returned=current->tcb+24; goto wide;
    case BB_TASK_CLOCK: returned=bb_probe_clock_value(bb_translated_cpu(t->runtime))/1000; goto wide;
    case BB_TASK_CREATE: {
        if(!writable(t,a[0],8)) { err=22; break; }
        size_t stack=STACK; int detached=0;
        if(a[1]) { if(!get(t,a[1],&id,8) || !id || id>t->attr_count || !t->attrs[id-1].alive) { err=22; break; } stack=t->attrs[id-1].stack; detached=t->attrs[id-1].detached; }
        uint64_t args[3]={a[3],0,0};
        if(spawn(t,a[2],args,stack,detached,&id)!=BB_TRANSLATED_OK) { err=11; break; }
        if(!put(t,a[0],&id,8)) return BB_ABI_MEMORY;
        break;
    }
    case BB_TASK_JOIN: {
        Thread *target=find(t,a[0]);
        if(!target) { err=3; break; }
        if(target==current) { err=35; break; }
        if(target->detached || target->joined || target->joiner || target->persistent) { err=22; break; }
        if(a[1] && !writable(t,a[1],8)) { err=22; break; }
        if(target->state==DONE) { if(a[1] && !put(t,a[1],&target->result,8)) return BB_ABI_MEMORY; target->joined=1; }
        else { if(!pause_task(t)) return BB_ABI_HOST; target->joiner=current->id; current->wait=target->id; current->out=a[1]; current->state=WAIT_JOIN; }
        break;
    }
    case BB_TASK_DETACH: {
        Thread *target=find(t,a[0]); if(!target) err=3; else if(target->detached || target->joined || target->joiner || target->persistent) err=22; else target->detached=1; break;
    }
    case BB_TASK_EXIT: if(!pause_task(t)) return BB_ABI_HOST; current->event=EXIT_EVENT; current->event_args[0]=a[0]; break;
    case BB_TASK_YIELD: if(!pause_task(t)) return BB_ABI_HOST; break;
    case BB_TASK_KEY_CREATE:
        if(!writable(t,a[0],4) || (a[1] && !executable(t,a[1]))) { err=22; break; }
        if(t->key_count==KEYS) { err=11; break; }
        id=t->key_count++; t->keys[id].alive=1; t->keys[id].destructor=a[1];
        { uint32_t key=(uint32_t)id; if(!put(t,a[0],&key,4)) return BB_ABI_MEMORY; } break;
    case BB_TASK_KEY_DELETE: if(a[0]>=t->key_count || !t->keys[a[0]].alive) err=22; else { t->keys[a[0]].alive=0; for(size_t i=0;i<t->thread_count;++i) t->threads[i].values[a[0]]=0; } break;
    case BB_TASK_GETSPECIFIC: returned=a[0]<t->key_count && t->keys[a[0]].alive ? current->values[a[0]] : 0; goto wide;
    case BB_TASK_SETSPECIFIC: if(a[0]>=t->key_count || !t->keys[a[0]].alive) err=22; else current->values[a[0]]=a[1]; break;
    case BB_TASK_TLS: {
        uint64_t index[2]; if(!get(t,a[0],index,sizeof(index)) || !index[0] || index[0]>=MODULES || !current->module[index[0]] || index[1]>=t->templates[index[0]].memsz) return BB_ABI_MEMORY;
        returned=current->module[index[0]]+index[1]; goto wide;
    }
    case BB_TASK_ATTR_INIT:
        if(!writable(t,a[0],8)) { err=22; break; } if(t->attr_count==ATTRS) { err=12; break; }
        id=++t->attr_count; t->attrs[id-1]=(Attr){1,0,STACK}; if(!put(t,a[0],&id,8)) return BB_ABI_MEMORY; break;
    case BB_TASK_ATTR_DESTROY: case BB_TASK_ATTR_STACK: case BB_TASK_ATTR_DETACH:
        if(!get(t,a[0],&id,8) || !id || id>t->attr_count || !t->attrs[id-1].alive) { err=22; break; }
        if(operation==BB_TASK_ATTR_DESTROY) { t->attrs[id-1].alive=0; id=0; if(!put(t,a[0],&id,8)) return BB_ABI_MEMORY; }
        else if(operation==BB_TASK_ATTR_STACK) { if(a[1]<65536 || a[1]>16*1024*1024) err=22; else t->attrs[id-1].stack=(size_t)a[1]; }
        else { if(a[1]>1) err=22; else t->attrs[id-1].detached=(int)a[1]; } break;
    case BB_TASK_MUTEX_INIT:
        if(!writable(t,a[0],8) || a[1]) { err=22; break; } if(t->mutex_count==LOCKS) { err=12; break; }
        id=++t->mutex_count; t->mutexes[id-1]=(Mutex){0,1}; if(!put(t,a[0],&id,8)) return BB_ABI_MEMORY; break;
    case BB_TASK_MUTEX_DESTROY: case BB_TASK_MUTEX_LOCK: case BB_TASK_MUTEX_TRYLOCK: case BB_TASK_MUTEX_UNLOCK:
        if(!lock_index(t,a[0],&id)) { err=22; break; }
        if(operation==BB_TASK_MUTEX_DESTROY) {
            if(t->mutexes[id-1].owner) err=16;
            for(size_t i=0;i<t->thread_count;++i) if((t->threads[i].state==WAIT_MUTEX && t->threads[i].wait==id) ||
                (t->threads[i].state==WAIT_COND && t->threads[i].mutex==id)) err=16;
            if(!err) { t->mutexes[id-1].alive=0; uint64_t zero=0; if(!put(t,a[0],&zero,8)) return BB_ABI_MEMORY; }
        } else if(operation==BB_TASK_MUTEX_UNLOCK) {
            if(t->mutexes[id-1].owner!=current->id) err=1; else release_mutex(t,id);
        } else if(!t->mutexes[id-1].owner) t->mutexes[id-1].owner=current->id;
        else if(operation==BB_TASK_MUTEX_TRYLOCK) err=16;
        else if(t->mutexes[id-1].owner==current->id) err=35;
        else { if(!pause_task(t)) return BB_ABI_HOST; current->state=WAIT_MUTEX; current->wait=id; }
        break;
    case BB_TASK_COND_INIT:
        if(!writable(t,a[0],8) || a[1]) { err=22; break; } if(t->cond_count==LOCKS) { err=12; break; }
        id=++t->cond_count; t->conds[id-1].alive=1; if(!put(t,a[0],&id,8)) return BB_ABI_MEMORY; break;
    case BB_TASK_COND_DESTROY: case BB_TASK_COND_WAIT: case BB_TASK_COND_SIGNAL: case BB_TASK_COND_BROADCAST:
        if(!cond_index(t,a[0],&id)) { err=22; break; }
        if(operation==BB_TASK_COND_DESTROY) {
            for(size_t i=0;i<t->thread_count;++i) if(t->threads[i].state==WAIT_COND && t->threads[i].wait==id) err=16;
            if(!err) { t->conds[id-1].alive=0; uint64_t zero=0; if(!put(t,a[0],&zero,8)) return BB_ABI_MEMORY; }
        } else if(operation==BB_TASK_COND_WAIT) {
            uint64_t mid=0; if(!lock_index(t,a[1],&mid) || t->mutexes[mid-1].owner!=current->id) { err=1; break; }
            if(!pause_task(t)) return BB_ABI_HOST;
            current->state=WAIT_COND; current->wait=id; current->mutex=mid; release_mutex(t,mid);
        } else signal_cond(t,id,operation==BB_TASK_COND_BROADCAST);
        break;
    case BB_TASK_ATEXIT: case BB_TASK_CXA_ATEXIT:
        if(!executable(t,a[0]) || t->handler_count==HANDLERS) { err=12; break; }
        t->handlers[t->handler_count++]=(Finalizer){a[0],a[1],a[2],operation==BB_TASK_CXA_ATEXIT,0}; break;
    case BB_TASK_FINALIZE: if(!finalize(t,a[0])) return BB_ABI_HOST; break;
    default: return BB_ABI_INVALID;
    }
    { int32_t rc=error(err,posix); memcpy(result->bytes,&rc,4); } return BB_ABI_OK;
wide:
    memcpy(result->bytes,&returned,8); return BB_ABI_OK;
}
static int tick(BbTasks *t) {
    Thread *thread=NULL; int waiting=0;
    for(size_t i=0;i<t->thread_count;++i) {
        size_t index=(t->cursor+i)%t->thread_count;
        if(t->threads[index].state==READY) { thread=t->threads+index; t->cursor=(index+1)%t->thread_count; break; }
        if(t->threads[index].state!=DONE && t->threads[index].state!=IDLE) waiting=1;
    }
    if(!thread) { if(waiting) { t->failed=1; fail(t,"guest scheduler deadlock: no runnable thread (join/mutex/condition wait)"); } return 0; }
    if(++t->slices>t->budget) { t->failed=1; fail(t,"guest scheduler slice budget exhausted"); return 0; }
    thread->worker_mask|=UINT64_C(1)<<t->turn;
    t->current=thread; bb_probe_clock_aux(bb_translated_cpu(t->runtime),(uint32_t)thread->id);
    uint64_t result=0; BbProbeFault fault={0};
    BbProbeStatus s=bb_probe_context_resume(bb_translated_cpu(t->runtime),thread->cpu,&result,&fault);
    if(s==BB_PROBE_OK || (s==BB_PROBE_YIELD && thread->event==EXIT_EVENT)) {
        if(thread->event==EXIT_EVENT) result=thread->event_args[0];
        if(!finish(t,thread,result)) { t->failed=1; fail(t,"guest thread cleanup or join output failed"); }
    } else if(s!=BB_PROBE_YIELD) {
        t->failed=1; snprintf(t->diagnostic,sizeof(t->diagnostic),"thread %llu: %s pc=0x%llx address=0x%llx; %.100s",(unsigned long long)thread->id,bb_probe_status_name(s),(unsigned long long)fault.pc,(unsigned long long)fault.address,bb_translated_diagnostic(t->runtime));
    }
    t->current=NULL; return !t->failed;
}
