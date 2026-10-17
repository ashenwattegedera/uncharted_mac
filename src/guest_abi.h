#ifndef BB_GUEST_ABI_H
#define BB_GUEST_ABI_H
#include <stddef.h>
#include <stdint.h>
/* Explicit supported SysV classes. No host va_list, function pointer, or C struct
 * is reinterpreted as a guest object. Unlisted classes must fail closed. */
typedef enum { BB_ABI_VOID, BB_ABI_U32, BB_ABI_I32, BB_ABI_U64, BB_ABI_POINTER,
    BB_ABI_FLOAT, BB_ABI_DOUBLE, BB_ABI_II, BB_ABI_SS, BB_ABI_IS, BB_ABI_SI,
    BB_ABI_MEMORY24 } BbAbiType;
typedef struct { unsigned char bytes[24]; } BbAbiValue;
typedef struct { uint64_t gp[6],rsp,rax,rdx; unsigned char xmm[8][16]; } BbAbiFrame;
enum { BB_ABI_MAX_ARGS=24 };
typedef struct { BbAbiType result,args[BB_ABI_MAX_ARGS]; size_t count,fixed_count; int variadic; } BbAbiSignature;
typedef enum { BB_ABI_OK, BB_ABI_INVALID, BB_ABI_MEMORY, BB_ABI_HOST } BbAbiStatus;
typedef struct {
    int (*check)(void *,uint64_t,size_t,int write);
    int (*read)(void *,uint64_t,void *,size_t);
    int (*write)(void *,uint64_t,const void *,size_t);
    void *user;
} BbAbiMemory;
typedef BbAbiStatus (*BbAbiHost)(const BbAbiValue *,size_t,BbAbiValue *,void *);
typedef struct { const char *name; BbAbiSignature signature; BbAbiHost host; void *user; } BbAbiImport;
BbAbiStatus bb_abi_validate(const BbAbiSignature *signature);
BbAbiStatus bb_abi_dispatch(const BbAbiImport *binding,BbAbiFrame *frame,const BbAbiMemory *memory);
/* Duplicate names or malformed entries invalidate the entire registry. */
const BbAbiImport *bb_abi_find(const BbAbiImport *table,size_t count,const char *name);
size_t bb_abi_type_size(BbAbiType type);
#endif
