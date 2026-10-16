#include "runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int calls;
static uintptr_t stack_begin,stack_end;
static ABI void tick(void) { ++calls; }
static ABI void take_ptr(void *value) { assert(value==&calls); ++calls; }
static ABI void *return_ptr(void *value) { ++calls; return value; }
static ABI void take_pair(uint64_t a,uint64_t b) { assert(a==17 && b==29); ++calls; }
static ABI int module_init(uint64_t a,void *b,void *c) {
    assert(a==17 && b==&calls && c==&stack_begin); ++calls; return -7;
}
static ABI void entry(void *arg,GuestCallback callback) {
    assert(arg==&calls);
    if(stack_begin) {
        volatile unsigned char here=0;
        assert((uintptr_t)&here>=stack_begin && (uintptr_t)&here<stack_end);
    }
    callback();
}
static ABI void nested(void) {
    bb_execution_call(BB_CALL_VOID,(uintptr_t)tick,0,0,0,NULL);
}
int main(int argc,char **argv) {
    const BbExecutionInfo *info=bb_execution_info();
    if(argc>1 && !strcmp(argv[1],"execution-unavailable")) {
        bb_execution_call(BB_CALL_VOID,(uintptr_t)tick,0,0,0,NULL); return 99;
    }
    assert(bb_execution_invoke(NULL)==BB_EXEC_INVALID);
    BbGuestCall call={.kind=BB_CALL_VOID,.target=0,.result=123};
    assert(bb_execution_invoke(&call)==BB_EXEC_INVALID && call.result==123);
    call.target=(uintptr_t)tick; call.kind=(BbGuestCallKind)99;
    assert(bb_execution_invoke(&call)==BB_EXEC_INVALID && !calls);
    call.kind=BB_CALL_VOID; call.stack_top=&calls;
    assert(bb_execution_invoke(&call)==BB_EXEC_INVALID && !calls);
    uintptr_t address=123;
    BbHostImport binding={"fixture",BB_IMPORT_FUNCTION,(uintptr_t)tick};
    assert(bb_execution_bind_import(NULL,&address)==BB_EXEC_INVALID);
    assert(bb_execution_bind_import(&binding,NULL)==BB_EXEC_INVALID);
    assert(bb_execution_invalidate(UINTPTR_MAX,2)==BB_EXEC_INVALID);
    assert(bb_execution_invalidate(0,1)==BB_EXEC_INVALID);
    if(!info->can_execute_guest) {
        assert(!strcmp(info->name,"unavailable"));
        for(int kind=BB_CALL_VOID;kind<=BB_CALL_ENTRY;++kind) {
            call=(BbGuestCall){.kind=(BbGuestCallKind)kind,.target=(uintptr_t)tick,.result=123};
            assert(bb_execution_invoke(&call)==BB_EXEC_UNAVAILABLE && call.result==123);
        }
        for(int kind=BB_IMPORT_FUNCTION;kind<=BB_IMPORT_MS_HOOK;++kind) {
            binding.kind=(BbImportKind)kind;
            assert(bb_execution_bind_import(&binding,&address)==BB_EXEC_UNAVAILABLE && address==123);
        }
        assert(bb_execution_invalidate((uintptr_t)tick,1)==BB_EXEC_UNAVAILABLE && !calls);
        puts("PASS: unavailable backend rejects execution, imports and invalidation without calling host addresses");
        return 0;
    }
    assert(!strcmp(info->name,"native-x86"));
    assert(bb_execution_bind_import(&binding,&address)==BB_EXEC_OK && address==(uintptr_t)tick);
    binding=(BbHostImport){"fixture-data",BB_IMPORT_DATA,(uintptr_t)&calls};
    assert(bb_execution_bind_import(&binding,&address)==BB_EXEC_OK && address==(uintptr_t)&calls);
    binding.kind=BB_IMPORT_MS_HOOK;
    assert(bb_execution_bind_import(&binding,&address)==BB_EXEC_OK);
    assert(bb_execution_call(BB_CALL_VOID,(uintptr_t)tick,0,0,0,NULL)==0);
    bb_execution_call(BB_CALL_VOID_PTR,(uintptr_t)take_ptr,(uintptr_t)&calls,0,0,NULL);
    assert(bb_execution_call(BB_CALL_PTR_PTR,(uintptr_t)return_ptr,(uintptr_t)&calls,0,0,NULL)==(uintptr_t)&calls);
    bb_execution_call(BB_CALL_VOID_U64_U64,(uintptr_t)take_pair,17,29,0,NULL);
    assert((intptr_t)bb_execution_call(BB_CALL_MODULE_INIT,(uintptr_t)module_init,17,
        (uintptr_t)&calls,(uintptr_t)&stack_begin,NULL)==-7);
    bb_execution_call(BB_CALL_ENTRY,(uintptr_t)entry,(uintptr_t)&calls,(uintptr_t)tick,0,NULL);
    bb_execution_call(BB_CALL_VOID,(uintptr_t)nested,0,0,0,NULL);
    assert(calls==7);
#ifndef _WIN32
    unsigned char *stack=malloc(128*1024);
    assert(stack);
    stack_begin=(uintptr_t)stack; stack_end=stack_begin+128*1024;
    bb_execution_call(BB_CALL_ENTRY,(uintptr_t)entry,(uintptr_t)&calls,(uintptr_t)tick,0,stack+128*1024-64);
    free(stack); stack_begin=stack_end=0;
    assert(calls==8);
#endif
    assert(bb_execution_invalidate((uintptr_t)tick,1)==BB_EXEC_OK);
    puts("PASS: native x86 entry, imports, callback reentry, arguments, signed results and stack entry");
    return 0;
}
