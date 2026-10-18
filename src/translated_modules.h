#ifndef BB_TRANSLATED_MODULES_H
#define BB_TRANSLATED_MODULES_H
#include "translated_runtime.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct BbTranslatedImage BbTranslatedImage;
/* Strict BBPROBE5 / translated profile-v1 reader. External descriptors are
 * copied/validated; no host structure is overlaid on the serialized input. */
BbTranslatedStatus bb_modules_load(BbTranslatedRuntime *,const void *bytes,size_t size,
    BbTranslatedImage **out,char *diagnostic,size_t diagnostic_size);
BbTranslatedStatus bb_modules_boot(BbTranslatedImage *,unsigned workers,uint64_t *result);
/* Expected-byte guest patches are backend-aware and invalidate translations. */
BbTranslatedStatus bb_modules_patch(BbTranslatedImage *,uint64_t address,const void *expected,const void *replacement,size_t size);
BbTranslatedStatus bb_modules_close(BbTranslatedImage *);
const char *bb_modules_diagnostic(BbTranslatedImage *);
#ifdef __cplusplus
}
#endif
#endif
