#ifndef M3_FIBER_TYPES_H
#define M3_FIBER_TYPES_H
#include "m3_thread_types.h"
typedef struct {
    int (*initialize)(void *,const char *,U,U,void *,U,void *,unsigned);
    int (*finalize)(void *);
    int (*run)(void *,U,U *);
    int (*switch_to)(void *,U,U *);
    int (*return_to_thread)(U,U *);
    int (*self)(void **);
    int (*attach_run)(void *,void *,U,U,U *);
    int (*info)(void *,void *);
    int (*option)(void *);
    int (*start_check)(unsigned);
    int (*stop_check)(void);
} FiberImports;
struct FiberShared;
typedef struct { struct FiberShared *shared; U number,tcb,ok; } FiberJob;
typedef struct FiberShared {
    ThreadImports threads;
    FiberImports fibers;
    U worker,entry,entry_b,stacks[2],turn,attached,visits[2],errors,order,option,active,busy;
    char name[32];
    U records[2][32];
    U info[16];
    FiberJob jobs[2];
} FiberShared;
#endif
