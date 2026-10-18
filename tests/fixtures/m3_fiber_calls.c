#include "m3_fiber_types.h"
static U tcb(void) { U p; __asm__("movq %%fs:0,%0":"=r"(p)); return p; }
void fiber_entry(FiberShared *s,U run) {
    unsigned mxcsr=0; __asm__ volatile("stmxcsr %0":"=m"(mxcsr));
    if(mxcsr!=0x9fc0) ++s->errors;
    mxcsr=0xbfc0; __asm__ volatile("ldmxcsr %0"::"m"(mxcsr));
    volatile U vector[2]; vector[0]=42; vector[1]=43; __asm__ volatile("movdqu %0,%%xmm15; fld1"::"m"(vector):"xmm15");
    for(unsigned i=0;i<24;++i) {
        U seen[2]; unsigned control=0; void *self=0;
        __asm__ volatile("movdqu %%xmm15,%0; stmxcsr %1":"=m"(seen),"=m"(control));
        if(run<1 || run>2 || seen[0]!=42 || seen[1]!=43 || control!=mxcsr) ++s->errors;
        if(s->fibers.self(&self) || self!=s->records[0] || s->threads.self()!=run+1 || *(U *)(tcb()-16)!=run || tcb()!=s->jobs[run-1].tcb) ++s->errors;
        s->active=1; s->threads.yield(); s->active=0;
        ++s->visits[run-1];
        if(s->fibers.return_to_thread(100+i,&run)) ++s->errors;
    }
    for(;;) s->fibers.return_to_thread(999,0);
}
U fiber_worker(FiberJob *job) {
    FiberShared *s=job->shared; *(U *)(tcb()-16)=job->number; job->tcb=tcb();
    unsigned control=0x9fc0|((unsigned)job->number<<13); __asm__ volatile("ldmxcsr %0"::"m"(control));
    unsigned checked=0;
    for(unsigned i=0;i<12;++i) {
        while(s->turn!=job->number) {
            if(s->active && !checked) {
                U output=123;
                if((unsigned)s->fibers.run(s->records[0],0,&output)!=0x80590006 || output!=123 || (unsigned)s->fibers.finalize(s->records[0])!=0x80590006) return 2;
                ++s->busy; checked=1;
            }
            s->threads.yield();
        }
        U result=0; int status;
        if(!s->attached) { status=s->fibers.attach_run(s->records[0],(void *)s->stacks[0],65536,job->number,&result); s->attached=1; }
        else status=s->fibers.run(s->records[0],job->number,&result);
        unsigned seen=0; __asm__ volatile("stmxcsr %0":"=m"(seen));
        if(status || result!=100+2*i+job->number-1 || seen!=control) return 1;
        s->turn=3-job->number; s->threads.yield();
    }
    job->ok=1; return 0;
}
U fiber_main(FiberShared *s) {
    if(s->fibers.start_check(0) || s->fibers.option(&s->option) ||
       s->fibers.initialize(s->records[0],s->name,s->entry,(U)s,0,0,&s->option,0x03500000)) return 10;
    U result=123; void *self=(void *)123;
    if((unsigned)s->fibers.run(s->records[0],0,&result)!=0x80590004 || result!=123 ||
       (unsigned)s->fibers.self(&self)!=0x80590005 || self!=(void *)123) return 11;
    s->turn=1; U threads[2];
    for(unsigned i=0;i<2;++i) if(s->threads.create(threads+i,0,s->worker,s->jobs+i,0)) return 12;
    for(unsigned i=0;i<2;++i) { U value=999; if(s->threads.join(threads[i],&value) || value) return 13; }
    return 0;
}
U fiber_query(FiberShared *s) {
    s->info[0]=128;
    if(s->fibers.info(s->records[0],s->info) || s->fibers.stop_check()) return 1;
    if(s->fibers.finalize(s->records[0]) || (unsigned)s->fibers.finalize(s->records[0])!=0x80590006) return 2;
    return 0;
}
void fiber_a(FiberShared *s,U run) {
    (void)run; s->order=1; U next=0;
    if(s->fibers.switch_to(s->records[1],17,&next) || next!=18) ++s->errors;
    s->order=s->order*10+3;
    for(;;) s->fibers.return_to_thread(100,0);
}
void fiber_b(FiberShared *s,U run) {
    if(run!=17) ++s->errors;
    s->order=s->order*10+2; U next=0;
    if(s->fibers.return_to_thread(99,&next) || next!=19) ++s->errors;
    for(;;) s->fibers.return_to_thread(1000,0);
}
U fiber_switch_main(FiberShared *s) {
    for(unsigned i=0;i<2;++i) if(s->fibers.initialize(s->records[i],s->name,i ? s->entry_b : s->entry,(U)s,(void *)s->stacks[i],65536,0,0x03500000)) return 1;
    U result=0;
    if(s->fibers.run(s->records[0],0,&result) || result!=99) return 2;
    if(s->fibers.run(s->records[0],18,&result) || result!=100) return 3;
    if(s->fibers.run(s->records[1],19,&result) || result!=1000) return 4;
    return s->errors || s->order!=123;
}

U fiber_bad_stack(FiberShared *s) {
    if(s->fibers.initialize(s->records[0],s->name,s->entry,(U)s,(void *)s->stacks[0],65536,0,0x03500000)) return 1;
    *(U *)s->stacks[0]=0; U output=123;
    return (unsigned)s->fibers.run(s->records[0],0,&output)!=0x80590006 || output!=123;
}
