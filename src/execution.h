#ifndef BB_EXECUTION_H
#define BB_EXECUTION_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Only the call shapes currently used by the C loader/runtime. General PS4 import
 * signature marshalling and virtual CPU/thread/fiber state belong to M1-M3. */
typedef enum {
    BB_CALL_VOID, BB_CALL_VOID_PTR, BB_CALL_PTR_PTR, BB_CALL_VOID_U64_U64,
    BB_CALL_MODULE_INIT, BB_CALL_ENTRY
} BbGuestCallKind;
typedef enum { BB_EXEC_OK, BB_EXEC_UNAVAILABLE, BB_EXEC_INVALID } BbExecutionStatus;
typedef enum { BB_IMPORT_FUNCTION, BB_IMPORT_DATA, BB_IMPORT_MS_HOOK } BbImportKind;
typedef struct {
    BbGuestCallKind kind;
    uintptr_t target;
    uintptr_t args[3];
    void *stack_top; /* optional, entry only; existing low guest stack on Linux */
    uintptr_t result; /* written only on success */
} BbGuestCall;
typedef struct {
    const char *name;
    BbImportKind kind;
    uintptr_t host_address;
} BbHostImport;
typedef struct {
    const char *name;
    int can_execute_guest;
} BbExecutionInfo;

const BbExecutionInfo *bb_execution_info(void);
const char *bb_execution_status_name(BbExecutionStatus status);
BbExecutionStatus bb_execution_invoke(BbGuestCall *call);
BbExecutionStatus bb_execution_bind_import(const BbHostImport *binding,uintptr_t *guest_address);
BbExecutionStatus bb_execution_invalidate(uintptr_t address,size_t size);
/* Loader/runtime convenience entry points: unsupported operations stop explicitly. */
uintptr_t bb_execution_call(BbGuestCallKind kind,uintptr_t target,uintptr_t a0,uintptr_t a1,
                            uintptr_t a2,void *stack_top);
uintptr_t bb_execution_import_address(const char *name,BbImportKind kind,uintptr_t host_address);
#ifdef __cplusplus
}
#endif
#endif
