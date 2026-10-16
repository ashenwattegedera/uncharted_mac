/* Native reference adapter and a fail-closed ARM harness backend. No translation yet. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>

#if defined(__x86_64__) && !defined(BB_EXECUTION_UNAVAILABLE)
#define BB_HAS_NATIVE_EXECUTION 1
static const BbExecutionInfo backend={"native-x86",1};
#ifndef _WIN32
/* Preserve the original loader's stack-entry convention. Mach-O prefixes symbols. */
#ifdef __APPLE__
#define BB_ASM_PREFIX "_"
#else
#define BB_ASM_PREFIX ""
#endif
void bb_native_enter_on_stack(void *entry,void *arg0,void *arg1,void *top);
__asm__(".text\n.globl " BB_ASM_PREFIX "bb_native_enter_on_stack\n"
        BB_ASM_PREFIX "bb_native_enter_on_stack:\n"
        " push %rbp\n mov %rsp,%rbp\n and $-16,%rcx\n mov %rcx,%rsp\n"
        " mov %rdi,%rax\n mov %rsi,%rdi\n mov %rdx,%rsi\n call *%rax\n"
        " mov %rbp,%rsp\n pop %rbp\n ret\n");
#endif
#else
static const BbExecutionInfo backend={"unavailable",0};
#endif

const BbExecutionInfo *bb_execution_info(void) { return &backend; }
const char *bb_execution_status_name(BbExecutionStatus status) {
    switch(status) {
    case BB_EXEC_OK: return "ok";
    case BB_EXEC_UNAVAILABLE: return "guest execution backend unavailable";
    case BB_EXEC_INVALID: return "invalid execution request";
    }
    return "unknown execution status";
}
BbExecutionStatus bb_execution_invoke(BbGuestCall *call) {
    if(!call || !call->target || call->kind<BB_CALL_VOID || call->kind>BB_CALL_ENTRY ||
       (call->stack_top && call->kind!=BB_CALL_ENTRY)) return BB_EXEC_INVALID;
#ifdef BB_HAS_NATIVE_EXECUTION
    uintptr_t result=0;
    switch(call->kind) {
    case BB_CALL_VOID:
        ((void (ABI *)(void))call->target)(); break;
    case BB_CALL_VOID_PTR:
        ((void (ABI *)(void *))call->target)((void *)call->args[0]); break;
    case BB_CALL_PTR_PTR:
        result=(uintptr_t)((void *(ABI *)(void *))call->target)((void *)call->args[0]); break;
    case BB_CALL_VOID_U64_U64:
        ((void (ABI *)(uint64_t,uint64_t))call->target)(call->args[0],call->args[1]); break;
    case BB_CALL_MODULE_INIT:
        result=(uintptr_t)(intptr_t)((int (ABI *)(uint64_t,void *,void *))call->target)(
            call->args[0],(void *)call->args[1],(void *)call->args[2]); break;
    case BB_CALL_ENTRY:
#ifndef _WIN32
        if(call->stack_top) {
            bb_native_enter_on_stack((void *)call->target,(void *)call->args[0],
                                     (void *)call->args[1],call->stack_top);
            break;
        }
#else
        if(call->stack_top) return BB_EXEC_INVALID;
#endif
        ((void (ABI *)(void *,GuestCallback))call->target)((void *)call->args[0],(GuestCallback)call->args[1]);
        break;
    }
    call->result=result;
    return BB_EXEC_OK;
#else
    return BB_EXEC_UNAVAILABLE;
#endif
}
BbExecutionStatus bb_execution_bind_import(const BbHostImport *binding,uintptr_t *guest_address) {
    if(!binding || !guest_address || !binding->name || !binding->host_address ||
       binding->kind<BB_IMPORT_FUNCTION || binding->kind>BB_IMPORT_MS_HOOK) return BB_EXEC_INVALID;
#ifdef BB_HAS_NATIVE_EXECUTION
    /* Native x86 shares host addresses; translators must supply actual bridge entries. */
    *guest_address=binding->host_address;
    return BB_EXEC_OK;
#else
    return BB_EXEC_UNAVAILABLE;
#endif
}
BbExecutionStatus bb_execution_invalidate(uintptr_t address,size_t size) {
    if(!address || size>UINTPTR_MAX-address) return BB_EXEC_INVALID;
#ifdef BB_HAS_NATIVE_EXECUTION
    __builtin___clear_cache((char *)address,(char *)(address+size));
    return BB_EXEC_OK;
#else
    return BB_EXEC_UNAVAILABLE;
#endif
}
static void execution_fail(BbExecutionStatus status) {
    fprintf(stderr,"STOP: %s (backend %s)\n",bb_execution_status_name(status),backend.name);
    exit(25);
}
uintptr_t bb_execution_call(BbGuestCallKind kind,uintptr_t target,uintptr_t a0,uintptr_t a1,
                            uintptr_t a2,void *stack_top) {
    BbGuestCall call={.kind=kind,.target=target,.args={a0,a1,a2},.stack_top=stack_top};
    BbExecutionStatus status=bb_execution_invoke(&call);
    if(status!=BB_EXEC_OK) execution_fail(status);
    return call.result;
}
uintptr_t bb_execution_import_address(const char *name,BbImportKind kind,uintptr_t host_address) {
    const BbHostImport binding={name,kind,host_address};
    uintptr_t guest_address=0;
    BbExecutionStatus status=bb_execution_bind_import(&binding,&guest_address);
    if(status!=BB_EXEC_OK) execution_fail(status);
    return guest_address;
}
