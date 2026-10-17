/* Hand-auditable synthetic x86 fixtures. No game executables or assets. */
#include "probe.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysctl.h>
#include <time.h>
#include <unistd.h>

#if !defined(__aarch64__)
#error M1 acceptance requires an arm64 executable.
#endif
#define OK(expr) do { BbProbeStatus s_=(expr); if(s_!=BB_PROBE_OK) { \
    fprintf(stderr,"%s:%d: %s: %s\n",__FILE__,__LINE__,#expr,bb_probe_status_name(s_)); abort(); } } while(0)
static BbCpuProbe *create(void) { BbCpuProbe *p=NULL; OK(bb_probe_open(&p)); return p; }
static uint64_t invoke(BbCpuProbe *p,uint64_t at,uint64_t a,uint64_t b) {
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=at,.args={a,b},.result=UINTPTR_MAX};
    BbProbeFault fault={0};
    BbProbeStatus status=bb_probe_invoke(p,&call,&fault);
    if(status!=BB_PROBE_OK) {
        fprintf(stderr,"invoke: %s pc=0x%llx address=0x%llx engine=%s\n",bb_probe_status_name(status),
            (unsigned long long)fault.pc,(unsigned long long)fault.address,uc_strerror(fault.engine_error));
        abort();
    }
    return call.result;
}
static void put(BbCpuProbe *p,uint64_t at,const unsigned char *code,size_t size) { OK(bb_probe_write(p,at,code,size)); }
static void scalar(void) {
    BbCpuProbe *p=create();
    /* push rbx; mov rax,rdi; test rsi,rsi; jz pop; imul rax,rsi; pop rbx; ret */
    const unsigned char branch[]={0x53,0x48,0x89,0xf8,0x48,0x85,0xf6,0x74,0x04,0x48,0x0f,0xaf,0xc6,0x5b,0xc3};
    put(p,BB_PROBE_CODE,branch,sizeof(branch));
    assert(invoke(p,BB_PROBE_CODE,7,6)==42 && invoke(p,BB_PROBE_CODE,7,0)==7);
    const unsigned char indirect[]={0x48,0x83,0xec,0x08,0x48,0x89,0xf0,0xff,0xd0,0x48,0x83,0xc4,0x08,0xc3};
    const unsigned char increment[]={0x48,0x8d,0x47,0x01,0xc3};
    put(p,BB_PROBE_CODE,indirect,sizeof(indirect));
    put(p,BB_PROBE_CODE+256,increment,sizeof(increment));
    assert(invoke(p,BB_PROBE_CODE,41,BB_PROBE_CODE+256)==42);
    const unsigned char signed_result[]={0xb8,0xf9,0xff,0xff,0xff,0xc3};
    put(p,BB_PROBE_CODE,signed_result,sizeof(signed_result));
    BbGuestCall call={.kind=BB_CALL_MODULE_INIT,.target=BB_PROBE_CODE};
    OK(bb_probe_invoke(p,&call,NULL)); assert((intptr_t)call.result==-7);
    call.kind=BB_CALL_VOID_U64_U64; call.result=123;
    OK(bb_probe_invoke(p,&call,NULL)); assert(call.result==0);
    bb_probe_close(p);
}
static void simd(void) {
    BbCpuProbe *p=create();
    /* movq xmm0,rdi; movq xmm1,rsi; paddq xmm0,xmm1; movq rax,xmm0; ret */
    const unsigned char code[]={0x66,0x48,0x0f,0x6e,0xc7,0x66,0x48,0x0f,0x6e,0xce,
        0x66,0x0f,0xd4,0xc1,0x66,0x48,0x0f,0x7e,0xc0,0xc3};
    put(p,BB_PROBE_CODE,code,sizeof(code));
    assert(invoke(p,BB_PROBE_CODE,0x100000000ULL,42)==0x10000002aULL);
    OK(bb_probe_map(p,BB_PROBE_DATA,4096,UC_PROT_READ|UC_PROT_WRITE));
    const double input[]={1.5,2.25};
    OK(bb_probe_write(p,BB_PROBE_DATA,input,sizeof(input)));
    /* movsd xmm0,[rdi]; addsd xmm0,[rdi+8]; movq rax,xmm0; ret */
    const unsigned char fp[]={0xf2,0x0f,0x10,0x07,0xf2,0x0f,0x58,0x47,0x08,0x66,0x48,0x0f,0x7e,0xc0,0xc3};
    put(p,BB_PROBE_CODE,fp,sizeof(fp));
    uint64_t bits=invoke(p,BB_PROBE_CODE,BB_PROBE_DATA,0);
    double result; memcpy(&result,&bits,sizeof(result)); assert(result==3.75);
    uint32_t state[]={0,0x3f80,0};
    OK(bb_probe_write(p,BB_PROBE_DATA,state,sizeof(state)));
    /* save MXCSR; load directed-rounding value; read it back; restore original */
    const unsigned char mxcsr[]={0x0f,0xae,0x1f,0x0f,0xae,0x57,0x04,0x0f,0xae,0x5f,0x08,
        0x0f,0xae,0x17,0x8b,0x47,0x08,0xc3};
    put(p,BB_PROBE_CODE,mxcsr,sizeof(mxcsr));
    assert(invoke(p,BB_PROBE_CODE,BB_PROBE_DATA,0)==0x3f80);
    bb_probe_close(p);
}
static void x87(void) {
    BbCpuProbe *p=create();
    OK(bb_probe_map(p,BB_PROBE_DATA,4096,UC_PROT_READ|UC_PROT_WRITE));
    const double input[]={1.5,2.25,0};
    OK(bb_probe_write(p,BB_PROBE_DATA,input,sizeof(input)));
    /* fld qword [rdi]; fadd qword [rdi+8]; fstp qword [rdi+16]; mov rax,[rdi+16]; ret */
    const unsigned char code[]={0xdd,0x07,0xdc,0x47,0x08,0xdd,0x5f,0x10,0x48,0x8b,0x47,0x10,0xc3};
    put(p,BB_PROBE_CODE,code,sizeof(code));
    uint64_t bits=invoke(p,BB_PROBE_CODE,BB_PROBE_DATA,0);
    double result; memcpy(&result,&bits,sizeof(result)); assert(result==3.75);
    bb_probe_close(p);
}
static BbProbeStatus nested_host(BbCpuProbe *p,const uint64_t args[6],uint64_t *result,void *user) {
    (void)user; assert(args[0]==7);
    *result=invoke(p,BB_PROBE_CODE+256,0,0)+1;
    return BB_PROBE_OK;
}
static void callbacks(void) {
    BbCpuProbe *p=create();
    uint64_t bridge=0;
    OK(bb_probe_bind(p,nested_host,NULL,&bridge));
    assert(bridge!=(uintptr_t)nested_host);
    /* Save rbx and xmm2=7; set ZF; call bridge; check restored flags/xmm2/rbx. */
    unsigned char outer[]={0x53,0x48,0x89,0xfb,0x66,0x48,0x0f,0x6e,0xd3,
        0x48,0x83,0xec,0x08,0x48,0x39,0xff,0x48,0xb8,
        0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x8d,0x64,0x24,0x08,
        0x0f,0x94,0xc2,0x0f,0xb6,0xd2,0x66,0x48,0x0f,0x7e,0xd1,
        0x48,0x01,0xd0,0x48,0x01,0xd8,0x48,0x01,0xc8,0x5b,0xc3};
    memcpy(outer+18,&bridge,8);
    /* Deliberately clobber virtual callee-saved state in the nested context. */
    const unsigned char inner[]={0xbb,0x78,0x56,0x34,0x12,0x66,0x0f,0xef,0xd2,0xb8,0x63,0,0,0,0x83,0xc8,1,0xc3};
    put(p,BB_PROBE_CODE,outer,sizeof(outer)); put(p,BB_PROBE_CODE+256,inner,sizeof(inner));
    assert(invoke(p,BB_PROBE_CODE,7,0)==115);
    bb_probe_close(p);
}
typedef struct { unsigned count; uint64_t pc; } Recovery;
static int recover(BbCpuProbe *p,const BbProbeFault *fault,void *user) {
    Recovery *r=user; ++r->count; r->pc=fault->pc;
    assert(fault->access==UC_MEM_READ_UNMAPPED && fault->address==BB_PROBE_DATA);
    OK(bb_probe_map(p,BB_PROBE_DATA,4096,UC_PROT_READ|UC_PROT_WRITE));
    const uint64_t value=42;
    OK(bb_probe_write(p,BB_PROBE_DATA,&value,sizeof(value)));
    return 1;
}
static void faults(void) {
    BbCpuProbe *p=create();
    const unsigned char read[]={0x48,0x8b,0x07,0xc3};
    put(p,BB_PROBE_CODE,read,sizeof(read));
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=BB_PROBE_CODE,.args={BB_PROBE_DATA},.result=123};
    BbProbeFault fault={0};
    assert(bb_probe_invoke(p,&call,&fault)==BB_PROBE_FAULT && call.result==123);
    assert(fault.pc==BB_PROBE_CODE && fault.address==BB_PROBE_DATA && fault.engine_error==UC_ERR_READ_UNMAPPED);
    Recovery recovery={0};
    bb_probe_set_fault_handler(p,recover,&recovery);
    assert(invoke(p,BB_PROBE_CODE,BB_PROBE_DATA,0)==42);
    assert(recovery.count==1 && recovery.pc==BB_PROBE_CODE);
    bb_probe_set_fault_handler(p,NULL,NULL);
    const unsigned char write[]={0x48,0x89,0x37,0x48,0x89,0xf0,0xc3};
    put(p,BB_PROBE_CODE,write,sizeof(write));
    OK(bb_probe_protect(p,BB_PROBE_DATA,4096,UC_PROT_READ));
    call.args[1]=99;
    assert(bb_probe_invoke(p,&call,&fault)==BB_PROBE_FAULT && fault.engine_error==UC_ERR_WRITE_PROT);
    assert(fault.pc==BB_PROBE_CODE && fault.address==BB_PROBE_DATA);
    OK(bb_probe_protect(p,BB_PROBE_DATA,4096,UC_PROT_READ|UC_PROT_WRITE));
    assert(invoke(p,BB_PROBE_CODE,BB_PROBE_DATA,99)==99);
    OK(bb_probe_protect(p,BB_PROBE_CODE,65536,UC_PROT_READ));
    assert(bb_probe_invoke(p,&call,&fault)==BB_PROBE_FAULT && fault.engine_error==UC_ERR_FETCH_PROT);
    assert(fault.pc==BB_PROBE_CODE);
    bb_probe_close(p);
}
static void invalidation(void) {
    BbCpuProbe *p=create();
    const unsigned char value[]={0xb8,1,0,0,0,0xc3};
    put(p,BB_PROBE_CODE,value,sizeof(value));
    assert(invoke(p,BB_PROBE_CODE,0,0)==1);
    unsigned char replacement=2;
    OK(bb_probe_write(p,BB_PROBE_CODE+1,&replacement,1));
    assert(invoke(p,BB_PROBE_CODE,0,0)==2);
    /* A guest write changes another translated function's immediate to 3. */
    const unsigned char modify[]={0xc6,0x07,0x03,0xc3};
    put(p,BB_PROBE_CODE+256,modify,sizeof(modify));
    invoke(p,BB_PROBE_CODE+256,BB_PROBE_CODE+1,0);
    assert(invoke(p,BB_PROBE_CODE,0,0)==3);
    bb_probe_close(p);
}
typedef struct { BbCpuProbe *p; unsigned count; uint64_t sum,address; } Worker;
static void *atomic_worker(void *arg) {
    Worker *w=arg;
    for(unsigned i=0;i<w->count;++i) w->sum+=invoke(w->p,BB_PROBE_CODE,w->address,0);
    return NULL;
}
static void serialized_threads(void) {
    BbCpuProbe *p=create();
    void *backing=NULL; assert(!posix_memalign(&backing,16384,16384)); memset(backing,0,16384);
    const uint64_t high=UINT64_C(0x1000000000);
    OK(bb_probe_map_host(p,BB_PROBE_DATA,16384,UC_PROT_READ|UC_PROT_WRITE,backing));
    OK(bb_probe_map_host(p,high,16384,UC_PROT_READ|UC_PROT_WRITE,backing));
    /* mov eax,1; lock xadd qword [rdi],rax; ret */
    const unsigned char code[]={0xb8,1,0,0,0,0xf0,0x48,0x0f,0xc1,0x07,0xc3};
    put(p,BB_PROBE_CODE,code,sizeof(code));
    pthread_t threads[4]; Worker workers[4];
    for(unsigned i=0;i<4;++i) {
        workers[i]=(Worker){p,100,0,i&1 ? high : BB_PROBE_DATA};
        assert(!pthread_create(&threads[i],NULL,atomic_worker,&workers[i]));
    }
    uint64_t sum=0;
    for(unsigned i=0;i<4;++i) { assert(!pthread_join(threads[i],NULL)); sum+=workers[i].sum; }
    assert(*(uint64_t *)backing==400 && sum==400*399/2);
    bb_probe_close(p); free(backing);
}
static BbProbeStatus recurse(BbCpuProbe *p,const uint64_t args[6],uint64_t *result,void *user) {
    (void)args; (void)result;
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=*(uint64_t *)user};
    return bb_probe_invoke(p,&call,NULL);
}
static BbProbeStatus count_bridge(BbCpuProbe *p,const uint64_t args[6],uint64_t *result,void *user) {
    (void)p; (void)args;
    ++*(unsigned *)user; *result=0;
    return BB_PROBE_OK;
}
static void limits(void) {
    BbCpuProbe *p=create();
    const unsigned char illegal[]={0x0f,0x0b};
    put(p,BB_PROBE_CODE,illegal,sizeof(illegal));
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=BB_PROBE_CODE,.result=123};
    BbProbeFault fault={0};
    assert(bb_probe_invoke(p,&call,&fault)==BB_PROBE_FAULT && fault.engine_error==UC_ERR_INSN_INVALID);
    assert(fault.pc==BB_PROBE_CODE && call.result==123);
    const unsigned char syscall[]={0x0f,0x05,0xc3};
    put(p,BB_PROBE_CODE,syscall,sizeof(syscall));
    assert(bb_probe_invoke(p,&call,&fault)==BB_PROBE_UNSUPPORTED && call.result==123);
    const unsigned char loop[]={0xeb,0xfe};
    put(p,BB_PROBE_CODE,loop,sizeof(loop));
    assert(bb_probe_invoke(p,&call,&fault)==BB_PROBE_LIMIT && call.result==123);
    uint64_t bridge=0;
    OK(bb_probe_bind(p,recurse,&bridge,&bridge)); call.target=bridge;
    assert(bb_probe_invoke(p,&call,NULL)==BB_PROBE_LIMIT && call.result==123);
    for(unsigned i=1;i<16;++i) { uint64_t ignored; OK(bb_probe_bind(p,recurse,&bridge,&ignored)); }
    uint64_t sentinel=123;
    assert(bb_probe_bind(p,recurse,&bridge,&sentinel)==BB_PROBE_LIMIT && sentinel==123);
    assert(bb_probe_write(p,UINT64_MAX,loop,2)==BB_PROBE_INVALID);
    call.stack_top=&call;
    assert(bb_probe_invoke(p,&call,NULL)==BB_PROBE_INVALID);
    bb_probe_close(p);
    /* Repeated nonrecursive bridges must also terminate at the resume bound. */
    p=create();
    unsigned calls=0;
    OK(bb_probe_bind(p,count_bridge,&calls,&bridge));
    unsigned char repeat[]={0x48,0xb8,0,0,0,0,0,0,0,0,0x48,0x83,0xec,0x08,
        0xff,0xd0,0x48,0x83,0xc4,0x08,0xeb,0xea};
    memcpy(repeat+2,&bridge,8); put(p,BB_PROBE_CODE,repeat,sizeof(repeat));
    call=(BbGuestCall){.kind=BB_CALL_PTR_PTR,.target=BB_PROBE_CODE,.result=123};
    assert(bb_probe_invoke(p,&call,NULL)==BB_PROBE_LIMIT && call.result==123 && calls==64);
    bb_probe_close(p);
}
static uint64_t now_ns(void) {
    struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;
}
static BbProbeStatus benchmark_host(BbCpuProbe *p,const uint64_t args[6],uint64_t *result,void *user) {
    (void)p; (void)user; *result=args[0]+1; return BB_PROBE_OK;
}
static void benchmark(void) {
    BbCpuProbe *p=create();
    /* mov rcx,rdi; xor eax,eax; loop: add rax,3; dec rcx; jnz loop; ret */
    const unsigned char loop[]={0x48,0x89,0xf9,0x31,0xc0,0x48,0x83,0xc0,3,0x48,0xff,0xc9,0x75,0xf7,0xc3};
    put(p,BB_PROBE_CODE,loop,sizeof(loop));
    uint64_t start=now_ns(); assert(invoke(p,BB_PROBE_CODE,100000,0)==300000);
    uint64_t cold=now_ns()-start;
    start=now_ns();
    for(unsigned i=0;i<10;++i) assert(invoke(p,BB_PROBE_CODE,100000,0)==300000);
    uint64_t warm=now_ns()-start;
    const unsigned char ret[]={0xc3}; put(p,BB_PROBE_CODE,ret,sizeof(ret));
    start=now_ns(); for(unsigned i=0;i<100;++i) invoke(p,BB_PROBE_CODE,0,0);
    uint64_t entry=now_ns()-start;
    uint64_t bridge=0; OK(bb_probe_bind(p,benchmark_host,NULL,&bridge));
    unsigned char hostcall[]={0x48,0xb8,0,0,0,0,0,0,0,0,0x48,0x83,0xec,0x08,0xff,0xd0,
        0x48,0x83,0xc4,0x08,0xc3};
    memcpy(hostcall+2,&bridge,8); put(p,BB_PROBE_CODE,hostcall,sizeof(hostcall));
    assert(invoke(p,BB_PROBE_CODE,41,0)==42);
    start=now_ns(); for(unsigned i=0;i<100;++i) assert(invoke(p,BB_PROBE_CODE,41,0)==42);
    uint64_t bridges=now_ns()-start;
    printf("{\"engine\":\"unicorn-2.1.4\",\"host\":\"arm64-darwin\",\"cold_loop_ns\":%llu,"
        "\"warm_10_loops_ns\":%llu,\"guest_instructions_per_loop\":300003,\"entry_100_calls_ns\":%llu,"
        "\"bridge_100_calls_ns\":%llu}\n",(unsigned long long)cold,(unsigned long long)warm,
        (unsigned long long)entry,(unsigned long long)bridges);
    bb_probe_close(p);
}
int main(int argc,char **argv) {
    int translated=0; size_t size=sizeof(translated);
    int queried=sysctlbyname("sysctl.proc_translated",&translated,&size,NULL,0);
    assert((!queried && !translated) || (queried && errno==ENOENT));
    const struct { const char *name; void (*run)(void); } cases[]={
        {"scalar",scalar},{"simd",simd},{"x87",x87},{"callbacks",callbacks},
        {"faults",faults},{"invalidation",invalidation},{"serialized-threads",serialized_threads},{"limits",limits}
    };
    const char *name=argc==2 ? argv[1] : "all";
    if(!strcmp(name,"benchmark")) { benchmark(); return 0; }
    int ran=0;
    for(size_t i=0;i<sizeof(cases)/sizeof(*cases);++i) if(!strcmp(name,"all") || !strcmp(name,cases[i].name)) {
        cases[i].run(); printf("PASS: %s\n",cases[i].name); ++ran;
    }
    if(!ran) { fprintf(stderr,"Unknown fixture: %s\n",name); return 2; }
    return 0;
}
