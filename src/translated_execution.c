/* Opt-in implementation of the same loader-facing boundary as native-x86. */
#include "translated_runtime.h"
#include <stdio.h>
#include <stdlib.h>
static _Thread_local BbTranslatedRuntime *attached;
static const BbExecutionInfo ready={"unicorn-reference",1},unavailable={"unicorn-reference-unattached",0};
BbTranslatedStatus bb_translated_attach_execution(BbTranslatedRuntime *r) {
    if(r && bb_vm_status(bb_translated_memory(r))!=BB_VM_OK) return BB_TRANSLATED_INVALID;
    attached=r; return BB_TRANSLATED_OK;
}
const BbExecutionInfo *bb_execution_info(void) { return attached ? &ready : &unavailable; }
const char *bb_execution_status_name(BbExecutionStatus s) {
    switch(s) { case BB_EXEC_OK: return "ok"; case BB_EXEC_UNAVAILABLE: return "reference backend unavailable or unsupported import";
    case BB_EXEC_INVALID: return "invalid request or translated execution failure"; }
    return "unknown execution status";
}
BbExecutionStatus bb_execution_invoke(BbGuestCall *call) {
    if(!call || !call->target) return BB_EXEC_INVALID;
    if(!attached) return BB_EXEC_UNAVAILABLE;
    return bb_translated_invoke(attached,call)==BB_TRANSLATED_OK ? BB_EXEC_OK : BB_EXEC_INVALID;
}
BbExecutionStatus bb_execution_bind_import(const BbHostImport *binding,uintptr_t *address) {
    if(!binding || !binding->name || !address) return BB_EXEC_INVALID;
    if(!attached) return BB_EXEC_UNAVAILABLE;
    uint64_t guest=0;
    /* host_address is deliberately not used to manufacture a guest target. */
    BbTranslatedStatus s=bb_translated_bind(attached,binding->name,binding->kind,&guest);
    if(s!=BB_TRANSLATED_OK) return s==BB_TRANSLATED_UNSUPPORTED ? BB_EXEC_UNAVAILABLE : BB_EXEC_INVALID;
    *address=(uintptr_t)guest; return BB_EXEC_OK;
}
BbExecutionStatus bb_execution_invalidate(uintptr_t address,size_t size) {
    if(!address || size>UINTPTR_MAX-address) return BB_EXEC_INVALID;
    if(!attached) return BB_EXEC_UNAVAILABLE;
    BbGuestMemory *vm=bb_translated_memory(attached);
    if(size && !bb_vm_contains(vm,address,size)) return BB_EXEC_INVALID;
    return bb_vm_sync_code(vm)==BB_VM_OK ? BB_EXEC_OK : BB_EXEC_INVALID;
}
static void fatal(BbExecutionStatus status) {
    fprintf(stderr,"STOP: %s (%s)\n",bb_execution_status_name(status),bb_translated_diagnostic(attached)); exit(25);
}
uintptr_t bb_execution_call(BbGuestCallKind kind,uintptr_t target,uintptr_t a,uintptr_t b,uintptr_t c,void *top) {
    BbGuestCall call={.kind=kind,.target=target,.args={a,b,c},.stack_top=top};
    BbExecutionStatus s=bb_execution_invoke(&call); if(s!=BB_EXEC_OK) fatal(s); return call.result;
}
uintptr_t bb_execution_import_address(const char *name,BbImportKind kind,uintptr_t host) {
    BbHostImport binding={name,kind,host}; uintptr_t address=0;
    BbExecutionStatus s=bb_execution_bind_import(&binding,&address); if(s!=BB_EXEC_OK) fatal(s); return address;
}
