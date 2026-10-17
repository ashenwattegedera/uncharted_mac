#ifndef BB_GUEST_LAYOUT_H
#define BB_GUEST_LAYOUT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
#define BB_GUEST_ASSERT static_assert
#else
#define BB_GUEST_ASSERT _Static_assert
#endif
/* Wire layouts use guest integers, never Darwin pointers or native va_list. */
typedef struct { uint64_t start,end,offset; int32_t protection,memory_type;
    uint32_t flags; char name[32]; } BbGuestVirtualQuery;
typedef struct { uint64_t start,offset,length; int8_t protection,type; int16_t reserved;
    int32_t operation; } BbGuestBatchEntry;
typedef struct { uint32_t gp_offset,fp_offset; uint64_t overflow_arg_area,reg_save_area; } BbGuestVaList;
BB_GUEST_ASSERT(sizeof(BbGuestVirtualQuery)==72,"guest query size");
BB_GUEST_ASSERT(offsetof(BbGuestVirtualQuery,name)==36,"guest query name offset");
BB_GUEST_ASSERT(sizeof(BbGuestBatchEntry)==32,"guest batch size");
BB_GUEST_ASSERT(offsetof(BbGuestBatchEntry,operation)==28,"guest batch operation offset");
BB_GUEST_ASSERT(sizeof(BbGuestVaList)==24,"SysV guest va_list size");
#undef BB_GUEST_ASSERT
#define BB_GUEST_EINVAL INT32_C(-2147352554)
#endif
