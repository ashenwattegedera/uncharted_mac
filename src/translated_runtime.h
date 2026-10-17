#ifndef BB_TRANSLATED_RUNTIME_H
#define BB_TRANSLATED_RUNTIME_H
#include "execution.h"
#include "guest_abi.h"
#include "guest_memory.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct BbTranslatedRuntime BbTranslatedRuntime;
typedef enum { BB_TRANSLATED_OK, BB_TRANSLATED_INVALID, BB_TRANSLATED_UNSUPPORTED,
    BB_TRANSLATED_MEMORY, BB_TRANSLATED_EXECUTION } BbTranslatedStatus;
/* One explicitly attached, owner-thread runtime; no M3 scheduling/TLS is implied. */
BbTranslatedStatus bb_translated_open(BbTranslatedRuntime **out);
BbTranslatedStatus bb_translated_close(BbTranslatedRuntime *runtime);
BbTranslatedStatus bb_translated_bind(BbTranslatedRuntime *runtime,const char *name,
    BbImportKind kind,uint64_t *guest_address);
typedef enum { BB_RELOC_FUNCTION, BB_RELOC_DATA, BB_RELOC_IMAGE } BbTranslatedRelocKind;
typedef struct { uint64_t offset,value,addend; const char *name; BbTranslatedRelocKind kind; } BbTranslatedReloc;
BbTranslatedStatus bb_translated_relocate(BbTranslatedRuntime *runtime,uint64_t image,size_t size,
    const BbTranslatedReloc *relocations,size_t count);
BbTranslatedStatus bb_translated_invoke(BbTranslatedRuntime *runtime,BbGuestCall *call);
BbGuestMemory *bb_translated_memory(BbTranslatedRuntime *runtime);
BbAbiMemory bb_translated_memory_access(BbTranslatedRuntime *runtime);
const char *bb_translated_diagnostic(BbTranslatedRuntime *runtime);
/* Actual execution.h entry points can be attached to the reference runtime. */
BbTranslatedStatus bb_translated_attach_execution(BbTranslatedRuntime *runtime);
#ifdef __cplusplus
}
#endif
#endif
