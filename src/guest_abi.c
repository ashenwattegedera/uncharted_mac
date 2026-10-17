#include "guest_abi.h"
#include <string.h>
/* 0=INTEGER, 1=SSE, 2=MEMORY. Aggregates are explicitly classified by
 * metadata, not guessed from a host layout. SSEUP/x87/vector types are absent. */
typedef struct { unsigned size,alignment,count,classes[2]; } Type;
static const Type types[]={
    {0,1,0,{0,0}}, {4,4,1,{0,0}}, {4,4,1,{0,0}}, {8,8,1,{0,0}}, {8,8,1,{0,0}},
    {4,4,1,{1,0}}, {8,8,1,{1,0}}, {16,8,2,{0,0}}, {16,8,2,{1,1}},
    {16,8,2,{0,1}}, {16,8,2,{1,0}}, {24,8,1,{2,0}}
};
size_t bb_abi_type_size(BbAbiType t) { return t>=BB_ABI_VOID && t<=BB_ABI_MEMORY24 ? types[t].size : 0; }
BbAbiStatus bb_abi_validate(const BbAbiSignature *s) {
    if(!s || s->count>BB_ABI_MAX_ARGS || s->fixed_count>s->count ||
       s->result<BB_ABI_VOID || s->result>BB_ABI_MEMORY24 ||
       (s->variadic!=0 && s->variadic!=1) || (!s->variadic && s->fixed_count!=s->count)) return BB_ABI_INVALID;
    for(size_t i=0;i<s->count;++i) {
        if(s->args[i]<=BB_ABI_VOID || s->args[i]>BB_ABI_MEMORY24) return BB_ABI_INVALID;
        /* A variadic float must already be promoted to double at the call site. */
        if(s->variadic && i>=s->fixed_count && s->args[i]==BB_ABI_FLOAT) return BB_ABI_INVALID;
    }
    return BB_ABI_OK;
}
const BbAbiImport *bb_abi_find(const BbAbiImport *table,size_t count,const char *name) {
    if(!table || !name || !count || count>4096) return NULL;
    const BbAbiImport *found=NULL;
    for(size_t i=0;i<count;++i) {
        if(!table[i].name || !table[i].name[0] || !table[i].host || bb_abi_validate(&table[i].signature)!=BB_ABI_OK) return NULL;
        for(size_t j=0;j<i;++j) if(!strcmp(table[j].name,table[i].name)) return NULL;
        if(!strcmp(name,table[i].name)) found=&table[i];
    }
    return found;
}
BbAbiStatus bb_abi_dispatch(const BbAbiImport *b,BbAbiFrame *f,const BbAbiMemory *m) {
    if(!b || !f || !b->host || !m || !m->check || !m->read || !m->write ||
       bb_abi_validate(&b->signature)!=BB_ABI_OK) return BB_ABI_INVALID;
    const BbAbiSignature *s=&b->signature;
    const Type *ret=&types[s->result];
    uint64_t hidden=ret->classes[0]==2 ? f->gp[0] : 0;
    if(ret->classes[0]==2 && !m->check(m->user,hidden,ret->size,1)) return BB_ABI_MEMORY;
    unsigned gp=ret->classes[0]==2 ? 1 : 0,fp=0;
    if(f->rsp>UINT64_MAX-8) return BB_ABI_MEMORY;
    uint64_t stack=f->rsp+8;
    BbAbiValue args[BB_ABI_MAX_ARGS]={{0}},result={{0}};
    for(size_t i=0;i<s->count;++i) {
        const Type *t=&types[s->args[i]];
        unsigned need_gp=0,need_fp=0;
        for(unsigned j=0;j<t->count;++j) { need_gp+=t->classes[j]==0; need_fp+=t->classes[j]==1; }
        if(t->classes[0]==2 || gp+need_gp>6 || fp+need_fp>8) {
            /* Whole-aggregate register assignments are reverted on exhaustion. */
            unsigned align=t->alignment<8 ? 8 : t->alignment;
            if(stack>UINT64_MAX-(align-1)) return BB_ABI_MEMORY;
            stack=(stack+align-1)&~(uint64_t)(align-1);
            unsigned slot=(t->size+7)&~7u;
            if(stack>UINT64_MAX-slot || !m->check(m->user,stack,t->size,0) ||
               !m->read(m->user,stack,args[i].bytes,t->size)) return BB_ABI_MEMORY;
            stack+=slot;
        } else {
            for(unsigned j=0;j<t->count;++j) {
                unsigned part=t->size-8*j; if(part>8) part=8;
                const void *from=t->classes[j]==0 ? (const void *)&f->gp[gp++] : (const void *)f->xmm[fp++];
                memcpy(args[i].bytes+8*j,from,part);
            }
        }
    }
    if(s->variadic && ((f->rax&255)>8 || (f->rax&255)<fp)) return BB_ABI_INVALID;
    BbAbiStatus status=b->host(args,s->count,&result,b->user);
    if(status!=BB_ABI_OK) return status;
    BbAbiFrame next=*f;
    if(ret->classes[0]==2) {
        if(!m->write(m->user,hidden,result.bytes,ret->size)) return BB_ABI_MEMORY;
        next.rax=hidden;
    } else {
        unsigned g=0,x=0;
        for(unsigned j=0;j<ret->count;++j) {
            unsigned part=ret->size-8*j; if(part>8) part=8;
            if(ret->classes[j]==0) {
                uint64_t value=0; memcpy(&value,result.bytes+8*j,part);
                if(!g++) next.rax=value; else next.rdx=value;
            } else {
                memset(next.xmm[x],0,16); memcpy(next.xmm[x++],result.bytes+8*j,part);
            }
        }
    }
    *f=next;
    return BB_ABI_OK;
}
