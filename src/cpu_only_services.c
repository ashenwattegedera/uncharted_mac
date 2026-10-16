/* CPU-only loader boundary. No renderer, networking, input or Vulkan is registered.
 * An explicit request to initialize graphics fails instead of reporting readiness. */
#include "runtime.h"
#include "gpu/bbgpu.h"
#include <stdio.h>

void bbgpu_register_kernel(void) {}
void bbnet_configure(const char *app0,const char *serial,const char *title,const char *version) {
    (void)app0; (void)serial; (void)title; (void)version;
}
uintptr_t bbgpu_resolve(const char *name) { (void)name; return 0; }
unsigned bbgpu_symbol_count(void) { return 0; }
int bbgpu_init(const BbGpuConfig *config) {
    (void)config;
    fputs("ERROR: CPU-only build has no GPU provider; use --cpu-only\n",stderr);
    return -1;
}
int vulkan_smoke(void) {
    fputs("ERROR: Vulkan is unavailable in the CPU-only build\n",stderr);
    return 1;
}
int bbgpu_handle_fault(void *context,void *address) { (void)context; (void)address; return 0; }
void bbgpu_dump_guest_writes(void *context) { (void)context; }
int bbgpu_text_input_begin(const char *text,const char *prompt) { (void)text; (void)prompt; return 0; }
int bbgpu_text_input_poll(char *out,uint64_t size) { (void)out; (void)size; return 0; }
const RuntimeGuestCallback *runtime_guest_callbacks(size_t *count) { *count=0; return NULL; }
