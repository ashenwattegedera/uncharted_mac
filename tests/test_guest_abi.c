#include "guest_abi.h"
#include "guest_layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef struct { unsigned char bytes[512]; unsigned calls; } Memory;
static int valid(void *u,uint64_t at,size_t n,int write) {
    (void)u; (void)write; return at<512 && n<=512-at;
}
static int read_mem(void *u,uint64_t at,void *out,size_t n) {
    if(!valid(u,at,n,0)) return 0; memcpy(out,((Memory *)u)->bytes+at,n); return 1;
}
static int write_mem(void *u,uint64_t at,const void *in,size_t n) {
    if(!valid(u,at,n,1)) return 0; memcpy(((Memory *)u)->bytes+at,in,n); return 1;
}
static uint64_t integer(const BbAbiValue *v) { uint64_t n; memcpy(&n,v->bytes,8); return n; }
static double real(const BbAbiValue *v) { double n; memcpy(&n,v->bytes,8); return n; }
static BbAbiStatus sum8(const BbAbiValue *v,size_t n,BbAbiValue *out,void *u) {
    assert(n==8); ++((Memory *)u)->calls;
    uint64_t result=0; for(size_t i=0;i<n;++i) { assert(integer(&v[i])==i+1); result+=integer(&v[i]); }
    memcpy(out->bytes,&result,8); return BB_ABI_OK;
}
static BbAbiStatus aggregate(const BbAbiValue *v,size_t n,BbAbiValue *out,void *u) {
    assert(n==7); ++((Memory *)u)->calls;
    for(unsigned i=0;i<5;++i) assert(integer(&v[i])==i+1);
    uint64_t pair[2]; memcpy(pair,v[5].bytes,16);
    assert(pair[0]==123 && pair[1]==456 && integer(&v[6])==77);
    uint64_t tag=123; double d=4.5;
    memcpy(out->bytes,&tag,8); memcpy(out->bytes+8,&d,8); return BB_ABI_OK;
}
static BbAbiStatus doubles(const BbAbiValue *v,size_t n,BbAbiValue *out,void *u) {
    assert(n==9); ++((Memory *)u)->calls;
    double sum=0; for(unsigned i=0;i<9;++i) { assert(real(&v[i])==(double)i+1); sum+=real(&v[i]); }
    memcpy(out->bytes,&sum,8); return BB_ABI_OK;
}
typedef struct { uint64_t x,y,z; } Triple;
static Triple native_triple(uint64_t add,Triple t) { t.x+=add; t.y+=add; t.z+=add; return t; }
static BbAbiStatus triple(const BbAbiValue *v,size_t n,BbAbiValue *out,void *u) {
    assert(n==2); ++((Memory *)u)->calls;
    Triple t; memcpy(&t,v[1].bytes,24); t=native_triple(integer(v),t);
    memcpy(out->bytes,&t,24); return BB_ABI_OK;
}
static BbAbiStatus variadic(const BbAbiValue *v,size_t n,BbAbiValue *out,void *u) {
    assert(n==2 && integer(v)==1 && real(v+1)==2.5); ++((Memory *)u)->calls;
    double answer=real(v+1); memcpy(out->bytes,&answer,8); return BB_ABI_OK;
}
static BbAbiStatus signed_error(const BbAbiValue *v,size_t n,BbAbiValue *out,void *u) {
    (void)v; assert(!n); ++((Memory *)u)->calls;
    int32_t code=BB_GUEST_EINVAL; memcpy(out->bytes,&code,4); return BB_ABI_OK;
}
int main(void) {
    Memory memory={0}; BbAbiMemory m={valid,read_mem,write_mem,&memory};
    BbAbiFrame f={.gp={1,2,3,4,5,6},.rsp=120};
    uint64_t tail[]={7,8}; memcpy(memory.bytes+128,tail,16);
    BbAbiImport b={.name="sum8",.host=sum8,.user=&memory,
        .signature={.result=BB_ABI_U64,.args={BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64},.count=8,.fixed_count=8}};
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_OK && f.rax==36 && memory.calls==1);
    b.host=aggregate; b.signature=(BbAbiSignature){.result=BB_ABI_IS,
        .args={BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_U64,BB_ABI_II,BB_ABI_U64},.count=7,.fixed_count=7};
    f=(BbAbiFrame){.gp={1,2,3,4,5,77},.rsp=120};
    uint64_t pair[]={123,456}; memcpy(memory.bytes+128,pair,16);
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_OK && f.rax==123);
    double d; memcpy(&d,f.xmm[0],8); assert(d==4.5);
    b.host=doubles; b.signature=(BbAbiSignature){.result=BB_ABI_DOUBLE,.count=9,.fixed_count=9};
    f=(BbAbiFrame){.rsp=120};
    for(unsigned i=0;i<9;++i) {
        b.signature.args[i]=BB_ABI_DOUBLE; d=i+1;
        if(i<8) memcpy(f.xmm[i],&d,8); else memcpy(memory.bytes+128,&d,8);
    }
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_OK); memcpy(&d,f.xmm[0],8); assert(d==45);
    b.host=triple; b.signature=(BbAbiSignature){.result=BB_ABI_MEMORY24,
        .args={BB_ABI_U64,BB_ABI_MEMORY24},.count=2,.fixed_count=2};
    f=(BbAbiFrame){.gp={256,10},.rsp=120}; Triple t={1,2,3}; memcpy(memory.bytes+128,&t,24);
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_OK && f.rax==256);
    memcpy(&t,memory.bytes+256,24); assert(t.x==11 && t.y==12 && t.z==13);
    b.host=variadic; b.signature=(BbAbiSignature){.result=BB_ABI_DOUBLE,
        .args={BB_ABI_U32,BB_ABI_DOUBLE},.count=2,.fixed_count=1,.variadic=1};
    f=(BbAbiFrame){.gp={1},.rax=1,.rsp=120}; d=2.5; memcpy(f.xmm[0],&d,8);
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_OK);
    unsigned calls=memory.calls; f.rax=0; BbAbiFrame saved=f;
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_INVALID && !memcmp(&f,&saved,sizeof(f)) && calls==memory.calls);
    b.signature.args[1]=BB_ABI_FLOAT; assert(bb_abi_validate(&b.signature)==BB_ABI_INVALID);
    b.host=signed_error; b.signature=(BbAbiSignature){.result=BB_ABI_I32};
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_OK && (int32_t)f.rax==BB_GUEST_EINVAL);
    BbAbiImport table[]={b,b};
    assert(bb_abi_find(table,1,b.name)==table && !bb_abi_find(table,1,"unknown"));
    assert(!bb_abi_find(table,2,b.name));
    b.signature=(BbAbiSignature){.result=BB_ABI_MEMORY24}; f.gp[0]=UINT64_MAX;
    saved=f; calls=memory.calls;
    assert(bb_abi_dispatch(&b,&f,&m)==BB_ABI_MEMORY && !memcmp(&f,&saved,sizeof(f)) && calls==memory.calls);
    b.signature.result=(BbAbiType)99; assert(bb_abi_validate(&b.signature)==BB_ABI_INVALID);
    puts("PASS: SysV registers, stack, aggregate rollback/sret, declared varargs and failure contracts");
}
