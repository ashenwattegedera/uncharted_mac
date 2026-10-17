#include "translated_runtime.h"
#include "runtime.h"
#include "m2_runtime_text.h"
#include "fixtures/m2_runtime_types.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define G BB_VM_GUEST_PAGE
#define OK(x) assert((x)==BB_TRANSLATED_OK)
static BbTranslatedReloc function(size_t offset,const char *name) { return (BbTranslatedReloc){.offset=G+offset,.name=name,.kind=BB_RELOC_FUNCTION}; }
int main(void) {
    BbTranslatedRuntime *r=NULL; OK(bb_translated_open(&r));
    assert(!bb_execution_info()->can_execute_guest); OK(bb_translated_attach_execution(r)); assert(bb_execution_info()->can_execute_guest);
    BbGuestMemory *vm=bb_translated_memory(r);
    void *image=NULL; uintptr_t map=runtime_memory_resolve_symbol("sceKernelMapFlexibleMemory");
    assert(!((int32_t (*)(void **,uint64_t,int,int))map)(&image,3*G,7,0));
    uint64_t base=(uintptr_t)image;
    assert(sizeof(guest_abi_text)<G && sizeof(ModuleOutput)<G);
    assert(bb_vm_write(vm,base,guest_abi_text,sizeof(guest_abi_text))==BB_VM_OK);
    const char *format="id=%d %.2f %s %#llx"; const char *input="guest";
    assert(bb_vm_write(vm,base+G+1024,format,strlen(format)+1)==BB_VM_OK);
    assert(bb_vm_write(vm,base+G+2048,input,strlen(input)+1)==BB_VM_OK);
    BbTranslatedReloc relocations[]={
        function(offsetof(Imports,size),"pO96TwzOm5E#p#J"),function(offsetof(Imports,allocate),"rTXw65xmLIA#p#J"),
        function(offsetof(Imports,map),"L-Q3LEjIbgA#p#J"),function(offsetof(Imports,release),"MBuItvba6z8#p#J"),
        function(offsetof(Imports,format),"snprintf"),function(offsetof(Imports,vformat),"Q2V+iqvjgC0#q#q"),
        function(offsetof(Imports,move),"memmove"),function(offsetof(Imports,length),"strlen"),
        {.offset=G+offsetof(Imports,canary),.name="f7uOxY9mM1U#p#J",.kind=BB_RELOC_DATA},
        {.offset=G+offsetof(Imports,marker),.name="ZT4ODD2Ts9o#libSceLibcInternal",.kind=BB_RELOC_DATA},
        {.offset=G+offsetof(Imports,format_text),.value=G+1024,.kind=BB_RELOC_IMAGE},
        {.offset=G+offsetof(Imports,input),.value=G+2048,.kind=BB_RELOC_IMAGE},
    };
    size_t count=sizeof(relocations)/sizeof(*relocations);
    OK(bb_translated_relocate(r,base,3*G,relocations,count));
    Imports bindings; assert(bb_vm_read(vm,base+G,&bindings,sizeof(bindings))==BB_VM_OK);
    assert(bindings.size!=runtime_memory_resolve_symbol("sceKernelGetDirectMemorySize"));
    unsigned char snapshot[sizeof(bindings)]; memcpy(snapshot,&bindings,sizeof(bindings));
    BbTranslatedReloc invalid[]={relocations[0],function(offsetof(Imports,format),"unknown-import")};
    assert(bb_translated_relocate(r,base,3*G,invalid,2)==BB_TRANSLATED_UNSUPPORTED);
    assert(bb_vm_read(vm,base+G,&bindings,sizeof(bindings))==BB_VM_OK && !memcmp(snapshot,&bindings,sizeof(bindings)));
    BbHostImport unsafe={"unknown-import",BB_IMPORT_FUNCTION,UINTPTR_MAX}; uintptr_t sentinel=123;
    assert(bb_execution_bind_import(&unsafe,&sentinel)==BB_EXEC_UNAVAILABLE && sentinel==123);
    ModuleOutput output={.number=3.75}; assert(bb_vm_write(vm,base+2*G,&output,sizeof(output))==BB_VM_OK);
    assert(bb_vm_protect(vm,base,G,BB_VM_READ|BB_VM_EXEC)==BB_VM_OK);
    BbGuestCall call={.kind=BB_CALL_VOID,.target=base+GUEST_MODULE,.args={base+G,base+2*G},.result=123};
    BbExecutionStatus status=bb_execution_invoke(&call);
    if(status!=BB_EXEC_OK) fprintf(stderr,"%s\n",bb_translated_diagnostic(r));
    assert(status==BB_EXEC_OK && !call.result);
    assert(bb_vm_read(vm,base+2*G,&output,sizeof(output))==BB_VM_OK);
    assert(output.size==5056ULL*1024*1024 && !output.allocated && !output.mapped && !output.released);
    assert(output.length==5 && !strcmp(output.copied,"guest") && output.moved==base+2*G+offsetof(ModuleOutput,copied));
    const char *expected="id=7 3.75 guest 0x2a";
    assert(output.formatted==(int)strlen(expected) && output.vformatted==output.formatted);
    assert(!strcmp(output.text,expected) && !strcmp(output.vtext,expected));
    assert(output.marker==1 && (output.canary&255)==0 && !runtime_memory_is_mapped(output.mapping,G));
    OK(bb_translated_close(r)); assert(!bb_execution_info()->can_execute_guest);
    puts("PASS: loader-facing execution API, typed NID relocations, actual PS4 memory services, data exports and compiled x86 snprintf/vsnprintf");
}
