#include "m3_thread_types.h"
static U tcb(void) { U p; __asm__("movq %%fs:0,%0":"=r"(p)); return p; }
U thread_worker(ThreadJob *job) {
    ThreadShared *s=job->shared; ThreadImports *f=&s->imports;
    U *tls=(U *)(tcb()-16); volatile U index[2]; index[0]=2; index[1]=0; U *module=f->tls((U *)index);
    if(tls[0]!=13 || tls[1] || module[0]!=17 || module[1]) return 1;
    job->tcb=tcb(); tls[0]=job->number; module[0]=job->number+100;
    *f->error()=(int)job->number;
    if(f->setspecific(s->key,job) || f->mutex_lock(&s->mutex)) return 2;
    ++s->started;
    while(!s->go) if(f->cond_wait(&s->cond,&s->mutex)) return 3;
    if(f->mutex_unlock(&s->mutex)) return 4;
    unsigned mxcsr=0x9fc0|((unsigned)job->number<<13); unsigned short cw=(unsigned short)(0x37f|(job->number<<10));
    __asm__ volatile("ldmxcsr %0; fldcw %1"::"m"(mxcsr),"m"(cw));
    U vector[2]={job->number,job->number+1};
    __asm__ volatile("movdqu %0,%%xmm15; fld1"::"m"(vector):"xmm15");
    for(unsigned i=0;i<12;++i) {
        if(f->mutex_lock(&s->mutex)) return 5;
        U old=s->counter;
        if(f->yield()) return 6; /* other threads must block, not block the host CPU */
        s->counter=old+1;
        if(f->mutex_unlock(&s->mutex)) return 7;
        U one=1; __asm__ volatile("lock xaddq %0,%1":"+r"(one),"+m"(s->atomic_counter)::"memory");
        if(f->yield()) return 8;
    }
    U seen[2]; unsigned mxcsr_seen; unsigned short cw_seen; double x87;
    __asm__ volatile("movdqu %%xmm15,%0; stmxcsr %1; fnstcw %2; fstpl %3":"=m"(seen),"=m"(mxcsr_seen),"=m"(cw_seen),"=m"(x87));
    if(seen[0]!=job->number || seen[1]!=job->number+1 || mxcsr_seen!=mxcsr || cw_seen!=cw || ((union { double d; U bits; }){.d=x87}).bits!=0x3ff0000000000000ULL) return 9;
    if(tcb()!=job->tcb || tls[0]!=job->number || module[0]!=job->number+100 || *f->error()!=(int)job->number || f->getspecific(s->key)!=job) return 10;
    job->ok=1;
    if(job->number==3) f->exit((void *)73);
    return 70+job->number;
}
void thread_destructor(ThreadJob *job) {
    ThreadShared *s=job->shared;
    if(tcb()!=job->tcb || s->imports.getspecific(s->key)) { job->ok=0; return; }
    ++job->dtor_calls;
    if(job->number==1 && job->dtor_calls==1) s->imports.setspecific(s->key,job);
}
U thread_main(ThreadShared *s) {
    ThreadImports *f=&s->imports;
    if(f->key_create(&s->key,s->destructor) || f->mutex_init(&s->mutex,0,0) || f->cond_init(&s->cond,0,0)) return 20;
    U lo,hi; __asm__ volatile("rdtsc":"=a"(lo),"=d"(hi)); s->tsc_before=(hi<<32)|(unsigned)lo;
    U handles[3];
    for(unsigned i=0;i<3;++i) if(f->create(handles+i,0,s->worker,s->jobs+i,0)) return 21;
    while(s->started<3) if(f->yield()) return 22;
    if(f->mutex_lock(&s->mutex)) return 23;
    s->go=1;
    if(f->cond_broadcast(&s->cond) || f->mutex_unlock(&s->mutex)) return 24;
    for(unsigned i=0;i<3;++i) { U result=0; if(f->join(handles[i],&result) || result!=71+i) return 25; }
    if((unsigned)f->join(f->self(),0)!=0x80020023 || f->posix_join(999,0)!=3) return 26;
    unsigned aux; __asm__ volatile("rdtscp":"=a"(lo),"=d"(hi),"=c"(aux)); s->tsc_after=(hi<<32)|(unsigned)lo;
    if(aux!=f->self()) return 27;
    return 0;
}
U thread_deadlock(ThreadShared *s) {
    ThreadImports *f=&s->imports;
    if(f->mutex_init(&s->mutex,0,0) || f->cond_init(&s->cond,0,0) || f->mutex_lock(&s->mutex)) return 1;
    f->cond_wait(&s->cond,&s->mutex);
    return 2;
}
U thread_budget(void *ignored) { (void)ignored; for(;;) __asm__ volatile(""); }
U thread_fault(void *ignored) { (void)ignored; *(volatile U *)1=42; return 1; }

U thread_bad_import(ThreadShared *s) { s->imports.tls((U *)1); return 1; }
