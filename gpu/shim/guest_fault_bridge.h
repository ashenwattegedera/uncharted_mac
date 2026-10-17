#ifndef BB_GPU_GUEST_FAULT_BRIDGE_H
#define BB_GPU_GUEST_FAULT_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
/* Ordinary thread-context dispatch. A translated guest PC is NEVER an ARM
 * ucontext pointer. Keep this bridge independent of Vulkan for contract tests. */
typedef struct {
    void *object;
    int (*read)(void *,uint64_t,size_t);
    int (*write)(void *,uint64_t);
} BbGpuGuestSink;
static inline int bb_gpu_guest_dispatch(const BbGpuGuestSink *sink,uint64_t pc,uint64_t address,size_t size,int write) {
    if(!sink || !sink->object || !pc || !size || size>16384 || size>UINT64_MAX-address || (write!=0 && write!=1)) return 0;
    return write ? (sink->write && sink->write(sink->object,address)) :
        (sink->read && sink->read(sink->object,address,size));
}
#endif
