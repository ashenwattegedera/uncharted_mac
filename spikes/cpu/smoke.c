#include "probe.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    BbCpuProbe *p=NULL;
    assert(bb_probe_open(&p)==BB_PROBE_OK);
    /* mov rax,rdi; add rax,rsi; ret */
    const unsigned char code[]={0x48,0x89,0xf8,0x48,0x01,0xf0,0xc3};
    assert(bb_probe_write(p,BB_PROBE_CODE,code,sizeof(code))==BB_PROBE_OK);
    BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=BB_PROBE_CODE,.args={17,25}};
    assert(bb_probe_invoke(p,&call,NULL)==BB_PROBE_OK && call.result==42);
    bb_probe_close(p);
    puts("PASS: x86-64 guest arithmetic in native arm64 macOS process");
}
