#ifndef BB_TRANSLATED_TASKS_H
#define BB_TRANSLATED_TASKS_H
#include "translated_runtime.h"
#include "probe.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct BbTasks BbTasks;
enum {
    BB_TASK_SELF=40,BB_TASK_CREATE,BB_TASK_JOIN,BB_TASK_DETACH,BB_TASK_EXIT,BB_TASK_YIELD,
    BB_TASK_KEY_CREATE,BB_TASK_KEY_DELETE,BB_TASK_GETSPECIFIC,BB_TASK_SETSPECIFIC,
    BB_TASK_TLS,BB_TASK_ERRNO,BB_TASK_MUTEX_INIT,BB_TASK_MUTEX_DESTROY,BB_TASK_MUTEX_LOCK,
    BB_TASK_MUTEX_TRYLOCK,BB_TASK_MUTEX_UNLOCK,BB_TASK_COND_INIT,BB_TASK_COND_DESTROY,
    BB_TASK_COND_WAIT,BB_TASK_COND_SIGNAL,BB_TASK_COND_BROADCAST,BB_TASK_ATEXIT,
    BB_TASK_CXA_ATEXIT,BB_TASK_FINALIZE,BB_TASK_CLOCK,BB_TASK_ATTR_INIT,BB_TASK_ATTR_DESTROY,
    BB_TASK_ATTR_STACK,BB_TASK_ATTR_DETACH,
    BB_TASK_FIBER_INIT=80,BB_TASK_FIBER_FINALIZE,BB_TASK_FIBER_RUN,BB_TASK_FIBER_SWITCH,
    BB_TASK_FIBER_RETURN,BB_TASK_FIBER_SELF,BB_TASK_FIBER_ATTACH_RUN,BB_TASK_FIBER_ATTACH_SWITCH,
    BB_TASK_FIBER_INFO,BB_TASK_FIBER_RENAME,BB_TASK_FIBER_OPT,BB_TASK_FIBER_SIZE_START,BB_TASK_FIBER_SIZE_STOP,
    BB_TASK_POSIX=1000
};
BbTranslatedStatus bb_tasks_create(BbTranslatedRuntime *,BbTasks **);
void bb_tasks_destroy(BbTasks *);
int bb_tasks_busy(BbTasks *);
BbTasks *bb_translated_tasks(BbTranslatedRuntime *);
/* Configure copied TLS templates before creating any guest execution state. */
BbTranslatedStatus bb_tasks_set_tls(BbTasks *,unsigned module,const void *data,size_t filesz,size_t memsz,size_t alignment);
BbTranslatedStatus bb_tasks_spawn(BbTasks *,uint64_t entry,const uint64_t args[3],uint64_t *id);
BbTranslatedStatus bb_tasks_run(BbTasks *,unsigned workers,size_t max_slices);
BbTranslatedStatus bb_tasks_result(BbTasks *,uint64_t id,uint64_t *result);
/* Repeated module calls reuse the process main thread's TLS and keys. */
BbTranslatedStatus bb_tasks_call(BbTasks *,uint64_t entry,const uint64_t args[3],unsigned workers,uint64_t *result);
BbTranslatedStatus bb_tasks_finalize(BbTasks *);
uint64_t bb_tasks_fiber_worker_mask(BbTasks *,uint64_t address);
uint64_t bb_tasks_worker_mask(BbTasks *,uint64_t id);
const char *bb_tasks_diagnostic(BbTasks *);
BbAbiStatus bb_tasks_import(BbTasks *,unsigned operation,const BbAbiValue *,size_t count,BbAbiValue *);
#ifdef __cplusplus
}
#endif
#endif
