#ifndef M3_THREAD_TYPES_H
#define M3_THREAD_TYPES_H
#include <stdint.h>
typedef uint64_t U;
typedef struct {
    int (*create)(U *,U *,U,void *,const char *);
    int (*join)(U,U *);
    U (*self)(void);
    int (*yield)(void);
    void (*exit)(void *);
    int (*key_create)(unsigned *,U);
    int (*setspecific)(unsigned,void *);
    void *(*getspecific)(unsigned);
    void *(*tls)(U *);
    int *(*error)(void);
    int (*mutex_init)(U *,void *,const char *);
    int (*mutex_lock)(U *);
    int (*mutex_unlock)(U *);
    int (*cond_init)(U *,void *,const char *);
    int (*cond_wait)(U *,U *);
    int (*cond_broadcast)(U *);
    int (*posix_join)(U,U *);
} ThreadImports;
struct ThreadShared;
typedef struct { struct ThreadShared *shared; U number,tcb,dtor_calls,ok,stack_pointer; } ThreadJob;
typedef struct ThreadShared {
    ThreadImports imports;
    U worker,destructor,mutex,cond,started,go,counter,atomic_counter;
    U tsc_before,tsc_after;
    unsigned key;
    int status;
    ThreadJob jobs[3];
} ThreadShared;
#endif
