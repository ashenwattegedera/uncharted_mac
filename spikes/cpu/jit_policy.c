/* Public Apple JIT APIs: allocate, write, execute, rewrite, invalidate, execute. */
#include <assert.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
#if !defined(__aarch64__)
#error This JIT policy probe requires arm64.
#endif
    long page=sysconf(_SC_PAGESIZE); assert(page>0);
    uint32_t *code=mmap(NULL,(size_t)page,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANON|MAP_JIT,-1,0);
    assert(code!=MAP_FAILED);
    pthread_jit_write_protect_np(0);
    code[0]=0x52800540; /* mov w0,#42 */
    code[1]=0xd65f03c0; /* ret */
    sys_icache_invalidate(code,8);
    pthread_jit_write_protect_np(1);
    assert(((int (*)(void))code)()==42);
    pthread_jit_write_protect_np(0);
    code[0]=0x52800560; /* mov w0,#43 */
    sys_icache_invalidate(code,8);
    pthread_jit_write_protect_np(1);
    assert(((int (*)(void))code)()==43);
    assert(!munmap(code,(size_t)page));
    printf("PASS: MAP_JIT write/execute transitions and cache invalidation; per-thread protection supported=%d\n",
           pthread_jit_write_protect_supported_np());
}
