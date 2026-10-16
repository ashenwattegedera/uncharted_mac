/* Real portable runtime contracts, called with the host ABI on the ARM harness. */
#include "runtime.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

typedef int (ABI *Register)(GuestCallback);
typedef int (ABI *RegisterCxa)(void (ABI *)(void *),void *,void *);
typedef int (ABI *Acquire)(uint64_t *);
typedef void (ABI *Guard)(uint64_t *);
typedef void *(ABI *TlsAddress)(const uint64_t *);
#define GET(type,name) ((type)runtime_resolve(name,0))
static int order[4],count;
static ABI void first(void) { order[count++]=1; }
static ABI void with_arg(void *arg) { order[count++]=(int)(uintptr_t)arg; }
static void *tls_worker(void *unused) {
    (void)unused;
    const uint64_t index[]={2,0};
    unsigned char *p=GET(TlsAddress,"vNe1w4diLCs#p#J")(index);
    assert(p[0]==42 && p[7]==0);
    p[0]=99;
    return NULL;
}
int main(int argc,char **argv) {
    runtime_start(0);
    assert(!runtime_resolve("bzQExy189ZI#q#q",0));
    runtime_start(1);
    assert(!runtime_resolve("unknown",0));
    assert(!runtime_resolve("8zTFvBIAIN8#q#q",1));
    assert(!runtime_resolve("bzQExy189ZI#wrong#module",0));
    /* These providers are intentionally absent from the portable core. */
    assert(!runtime_resolve("rTXw65xmLIA#p#J",0));
    assert(!runtime_resolve("188x57JYp0g#p#J",0));
    if(argc>1 && !strcmp(argv[1],"stack-failure")) {
        GET(GuestCallback,"Ou3iL1abvng#p#J")(); return 99;
    }
    static const unsigned char data[]={42,17};
    runtime_set_libc_tls(data,sizeof(data),8);
    const uint64_t index[]={2,0},bad[]={2,8};
    if(argc>1 && !strcmp(argv[1],"bad-tls")) {
        GET(TlsAddress,"vNe1w4diLCs#p#J")(bad); return 99;
    }
    unsigned char *p=GET(TlsAddress,"vNe1w4diLCs#p#J")(index);
    p[0]=23;
    pthread_t worker;
    assert(!pthread_create(&worker,NULL,tls_worker,NULL));
    assert(!pthread_join(worker,NULL));
    assert(p[0]==23 && p[1]==17 && p[7]==0 && data[0]==42);
    uint64_t state=0;
    Acquire acquire=GET(Acquire,"3GPpjQdAMTw#q#q");
    Guard release=GET(Guard,"9rAeANT2tyE#q#q"),abort_guard=GET(Guard,"2emaaluWzUw#q#q");
    assert(acquire(&state)==1);
    if(argc>1 && !strcmp(argv[1],"guard-recursion")) { acquire(&state); return 99; }
    abort_guard(&state); assert(state==0);
    assert(acquire(&state)==1); release(&state);
    assert(state==1 && acquire(&state)==0);
    Register plain=GET(Register,"8G2LB+A3rzg#q#q");
    RegisterCxa cxa=GET(RegisterCxa,"tsvEmnenz48#q#q");
    assert(!plain(first) && !cxa(with_arg,(void *)2,(void *)11));
    runtime_finalize((void *)11); runtime_finalize((void *)11);
    runtime_finalize(NULL); runtime_finalize(NULL);
    assert(count==2 && order[0]==2 && order[1]==1);
    typedef void *(ABI *Copy)(void *,const void *,size_t);
    char dst[8]={0};
    assert(GET(Copy,"Q3VBxCXhUHs#q#q")(dst,"fixture",8)==dst && !strcmp(dst,"fixture"));
    uint64_t *canary=(uint64_t *)runtime_resolve("f7uOxY9mM1U#p#J",1);
    assert(canary && !(*canary&255));
    runtime_report();
    puts("PASS: host runtime contracts (no guest instruction execution)");
    return 0;
}
