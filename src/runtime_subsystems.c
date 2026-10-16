/* Build-time provider profiles. The default preserves the complete game runtime. */
#include "runtime.h"
#if !defined(BB_RUNTIME_CORE_ONLY)
#include "gpu/bbgpu.h"
#endif

static uintptr_t (*const resolvers[])(const char *) = {
#if !defined(BB_RUNTIME_CORE_ONLY)
    runtime_mutex_resolve, runtime_thread_resolve,
#endif
    runtime_content_resolve, runtime_time_resolve,
#if !defined(BB_RUNTIME_CORE_ONLY)
    runtime_sema_resolve, runtime_rwlock_resolve, runtime_memory_resolve,
    runtime_kernel_resolve, runtime_services_resolve,
#if !defined(BB_RUNTIME_CPU_ONLY)
    runtime_ajm_resolve, runtime_audio_resolve, runtime_pad_resolve,
#endif
#endif
    runtime_rtc_resolve,
#if !defined(BB_RUNTIME_CORE_ONLY)
    runtime_savedata_resolve, runtime_file_resolve, runtime_fiber_resolve, bbgpu_resolve,
#endif
};

void runtime_subsystems_report(void) {
#if !defined(BB_RUNTIME_CORE_ONLY)
    runtime_thread_report();
    runtime_mutex_report();
    runtime_rwlock_report();
    runtime_sema_report();
#endif
    runtime_content_report();
#if !defined(BB_RUNTIME_CORE_ONLY)
    runtime_memory_report();
#if !defined(BB_RUNTIME_CPU_ONLY)
    runtime_ajm_report();
    runtime_audio_report();
    runtime_pad_report();
#endif
    runtime_savedata_report();
    runtime_file_report();
    runtime_fiber_report();
#endif
}

uintptr_t runtime_subsystems_resolve(const char *name) {
    for (size_t i=0;i<sizeof(resolvers)/sizeof(*resolvers);++i) {
        uintptr_t result=resolvers[i](name);
        if (result) return result;
    }
    return 0;
}
