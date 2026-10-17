#include "probe.h"
#include "guest_layout.h"
#include "guest_abi_text.h"
#include "../../tests/fixtures/guest_abi_types.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
_Static_assert(sizeof(Output)==112,"shared fixture layout");
_Static_assert(offsetof(Output,triple)==88,"shared triple offset");
#define OK(x) assert((x)==BB_PROBE_OK)
typedef struct { int kind; BbCpuProbe *probe; uint64_t nested_bridge; } Host;
static U64 scalar_native(U64 a,U64 b,U64 c,U64 d,U64 e,U64 f,U64 g,U64 h) { return a+b+c+d+e+f+g+h; }
static double fp_native(double a,double b,double c,double d,double e,double f,double g,double h,double i) { return a+b+c+d+e+f+g+h+i; }
static BbProbeStatus enter(BbCpuProbe *p,unsigned entry,uint64_t fn,uint64_t data) {
    BbGuestCall call={.kind=BB_CALL_VOID,.target=BB_PROBE_CODE+entry,.args={fn,data}};
    return bb_probe_invoke(p,&call,NULL);
}
static int va_check(void *p,uint64_t at,size_t n,int write) {
    (void)p;
    return !write && at>=BB_PROBE_STACK && at<BB_PROBE_STACK+8*65536 && n<=BB_PROBE_STACK+8*65536-at;
}
static int va_read(void *p,uint64_t at,void *out,size_t n) {
    return va_check(p,at,n,0) && bb_probe_read(p,at,out,n)==BB_PROBE_OK;
}
static BbAbiStatus host_call(const BbAbiValue *a,size_t n,BbAbiValue *r,void *user) {
    Host *h=user;
    switch(h->kind) {
    case 0: {
        assert(n==8); U64 v[8]; for(unsigned i=0;i<8;++i) memcpy(&v[i],a[i].bytes,8);
        U64 value=scalar_native(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]); assert(value==36);
        if(h->nested_bridge) {
            OK(enter(h->probe,GUEST_INTEGERS,h->nested_bridge,BB_PROBE_DATA+256));
            Output nested; OK(bb_probe_read(h->probe,BB_PROBE_DATA+256,&nested,sizeof(nested)));
            assert(nested.integer==36); value+=nested.integer;
        }
        memcpy(r->bytes,&value,8); break;
    }
    case 1: {
        assert(n==9); double v[9]; for(unsigned i=0;i<9;++i) memcpy(v+i,a[i].bytes,8);
        double value=fp_native(v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],v[8]);
        memcpy(r->bytes,&value,8); break;
    }
    case 2: { assert(n==1); float value; memcpy(&value,a[0].bytes,4); value+=1; memcpy(r->bytes,&value,4); break; }
    case 3: { assert(n==1); II v; memcpy(&v,a[0].bytes,16); II answer={v.b,v.a}; memcpy(r->bytes,&answer,16); break; }
    case 4: { assert(n==1); SS v; memcpy(&v,a[0].bytes,16); SS answer={v.b+1,v.a+2}; memcpy(r->bytes,&answer,16); break; }
    case 5: { assert(n==1); IS v; memcpy(&v,a[0].bytes,16); IS answer={v.a+1,v.b+2}; memcpy(r->bytes,&answer,16); break; }
    case 6: { assert(n==1); SI v; memcpy(&v,a[0].bytes,16); SI answer={v.a+1,v.b+2}; memcpy(r->bytes,&answer,16); break; }
    case 7: {
        assert(n==7); II v; memcpy(&v,a[5].bytes,16); U64 last; memcpy(&last,a[6].bytes,8); assert(last==77);
        v.a+=last; memcpy(r->bytes,&v,16); break;
    }
    case 8: {
        assert(n==2); U64 add; Triple t; memcpy(&add,a[0].bytes,8); memcpy(&t,a[1].bytes,24);
        t.a+=add; t.b+=add; t.c+=add; memcpy(r->bytes,&t,24); break;
    }
    case 9: { assert(n==2); unsigned tag; double v; memcpy(&tag,a[0].bytes,4); assert(tag==1);
        memcpy(&v,a[1].bytes,8); v+=1; memcpy(r->bytes,&v,8); break; }
    case 10: {
        assert(n==1); uint64_t at; memcpy(&at,a[0].bytes,8);
        BbGuestVaList state; assert(va_read(h->probe,at,&state,sizeof(state)));
        BbAbiMemory m={.check=va_check,.read=va_read,.user=h->probe};
        BbAbiValue value; double sum=0,d; int32_t tag;
        assert(bb_abi_va_read(&state,BB_ABI_I32,&value,&m)==BB_ABI_OK); memcpy(&tag,value.bytes,4); assert(tag==17); sum+=tag;
        assert(bb_abi_va_read(&state,BB_ABI_DOUBLE,&value,&m)==BB_ABI_OK); memcpy(&d,value.bytes,8); sum+=d;
        for(unsigned j=25;j<=31;++j) {
            uint64_t v; assert(bb_abi_va_read(&state,BB_ABI_U64,&value,&m)==BB_ABI_OK);
            memcpy(&v,value.bytes,8); assert(v==j); sum+=(double)v;
        }
        memcpy(r->bytes,&sum,8); break;
    }
    default: return BB_ABI_HOST;
    }
    return BB_ABI_OK;
}
void bb_test_abi(BbCpuProbe *p) {
    OK(bb_probe_write(p,BB_PROBE_CODE,guest_abi_text,sizeof(guest_abi_text)));
    const unsigned entries[]={GUEST_INTEGERS,GUEST_FLOATING,GUEST_SMALL_FLOAT,GUEST_PAIR_INTEGER,
        GUEST_PAIR_SSE,GUEST_PAIR_MIXED,GUEST_PAIR_REVERSE,GUEST_ROLLBACK,GUEST_MEMORY_RETURN,GUEST_VARARGS};
    const BbAbiSignature signatures[]={
        {.result=BB_ABI_U64,.args={BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64},.count=8,.fixed_count=8},
        {.result=BB_ABI_DOUBLE,.args={BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE,BB_ABI_DOUBLE},.count=9,.fixed_count=9},
        {.result=BB_ABI_FLOAT,.args={BB_ABI_FLOAT},.count=1,.fixed_count=1},
        {.result=BB_ABI_II,.args={BB_ABI_II},.count=1,.fixed_count=1},
        {.result=BB_ABI_SS,.args={BB_ABI_SS},.count=1,.fixed_count=1},
        {.result=BB_ABI_IS,.args={BB_ABI_IS},.count=1,.fixed_count=1},
        {.result=BB_ABI_SI,.args={BB_ABI_SI},.count=1,.fixed_count=1},
        {.result=BB_ABI_II,.args={BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_II,BB_ABI_U64},.count=7,.fixed_count=7},
        {.result=BB_ABI_MEMORY24,.args={BB_ABI_U64,BB_ABI_MEMORY24},.count=2,.fixed_count=2},
        {.result=BB_ABI_DOUBLE,.args={BB_ABI_U32,BB_ABI_DOUBLE},.count=2,.fixed_count=1,.variadic=1}
    };
    const char *names[]={"sum8","sum9fp","float","II","SS","IS","SI","rollback","sret","varargs"};
    Host hosts[11]; uint64_t bridges[11];
    for(unsigned i=0;i<11;++i) {
        unsigned kind=i==10 ? 0 : i; hosts[i]=(Host){kind,p,0};
        BbAbiImport b={.name=i==10 ? "nested-sum8" : names[i],.signature=signatures[kind],.host=host_call,.user=hosts+i};
        OK(bb_probe_bind_abi(p,&b,NULL,bridges+i));
        assert(bridges[i]!=(uintptr_t)host_call);
        uint64_t sentinel=123;
        assert(bb_probe_bind_abi(p,&b,NULL,&sentinel)==BB_PROBE_INVALID && sentinel==123);
    }
    hosts[0].nested_bridge=bridges[10];
    for(unsigned i=0;i<10;++i) {
        Output out={.integer=2}; OK(bb_probe_write(p,BB_PROBE_DATA,&out,sizeof(out)));
        OK(enter(p,entries[i],bridges[i],BB_PROBE_DATA));
        OK(bb_probe_read(p,BB_PROBE_DATA,&out,sizeof(out)));
        switch(i) {
        case 0: assert(out.integer==72); break;
        case 1: assert(out.real==18); break;
        case 2: assert(out.small==3); break;
        case 3: assert(out.ii.a==25 && out.ii.b==17); break;
        case 4: assert(out.ss.a==3 && out.ss.b==4); break;
        case 5: assert(out.is.a==18 && out.is.b==4); break;
        case 6: assert(out.si.a==3 && out.si.b==27); break;
        case 7: assert(out.ii.a==79 && out.ii.b==3); break;
        case 8: assert(out.triple.a==12 && out.triple.b==13 && out.triple.c==14); break;
        case 9: assert(out.real==3); break;
        }
    }
    Host list_host={10,p,0};
    BbAbiImport list_import={.name="guest-va-list",.host=host_call,.user=&list_host,
        .signature={.result=BB_ABI_DOUBLE,.args={BB_ABI_POINTER},.count=1,.fixed_count=1}};
    uint64_t list_bridge; OK(bb_probe_bind_abi(p,&list_import,NULL,&list_bridge));
    Output list_out={.integer=2}; OK(bb_probe_write(p,BB_PROBE_DATA,&list_out,sizeof(list_out)));
    OK(enter(p,GUEST_VARARG_LIST,list_bridge,BB_PROBE_DATA));
    OK(bb_probe_read(p,BB_PROBE_DATA,&list_out,sizeof(list_out))); assert(list_out.real==215);
    U64 exported=42; OK(bb_probe_write(p,BB_PROBE_DATA+8192,&exported,8));
    OK(enter(p,GUEST_DATA_EXPORT,BB_PROBE_DATA+8192,BB_PROBE_DATA));
    Output out; OK(bb_probe_read(p,BB_PROBE_DATA,&out,sizeof(out))); assert(out.integer==42);
    puts("PASS: compiler-generated x86 SysV calls, typed bridges, nested callbacks and guest data export");
}
int main(void) {
    BbCpuProbe *p=NULL; OK(bb_probe_open(&p));
    OK(bb_probe_map(p,BB_PROBE_DATA,16384,UC_PROT_READ|UC_PROT_WRITE));
    bb_test_abi(p); bb_probe_close(p);
}
