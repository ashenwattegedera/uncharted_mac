#ifndef BB_GUEST_FORMAT_H
#define BB_GUEST_FORMAT_H
#include "guest_abi.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { BB_FORMAT_TEXT=4096, BB_FORMAT_PARTS=128, BB_FORMAT_LIMIT=1048576 };
typedef enum { BB_FORMAT_OK, BB_FORMAT_INVALID, BB_FORMAT_UNSUPPORTED, BB_FORMAT_MEMORY,
               BB_FORMAT_LIMITED, BB_FORMAT_SYSTEM } BbFormatStatus;
typedef struct {
    unsigned begin,end,arg,width_arg,precision_arg;
    int width,precision;
    unsigned length;
    char flags[6],conversion;
} BbFormatPart;
typedef struct {
    char text[BB_FORMAT_TEXT]; BbFormatPart parts[BB_FORMAT_PARTS];
    BbAbiType types[BB_ABI_MAX_ARGS]; unsigned count,argument_count;
} BbFormatPlan;
BbFormatStatus bb_format_plan(const char *text,BbFormatPlan *plan);
BbFormatStatus bb_guest_string(const BbAbiMemory *memory,uint64_t address,char *text,size_t capacity);
/* Values are indexed in positional order. Validate/render completely before
 * committing destination or %n writes. snprintf size=0 never dereferences dst. */
BbFormatStatus bb_format_write(const BbFormatPlan *plan,const BbAbiValue *values,
    const BbAbiMemory *memory,uint64_t destination,uint64_t capacity,int32_t *result);
#ifdef __cplusplus
}
#endif
#endif
