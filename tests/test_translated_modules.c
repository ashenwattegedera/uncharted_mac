#include "translated_modules.h"
#include "translated_tasks.h"
#include "runtime.h"
#include "m3_modules_text.h"
#include "fixtures/m3_module_types.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define G BB_VM_GUEST_PAGE
#define BASE UINT64_C(0x400000)
#define DATA (BASE+G)
#define ONE (BASE+2*G)
#define TWO (BASE+4*G)
#define IMAGE_SIZE (BASE+6*G)
#define PROFILE (UINT64_C(0x81)<<56)
#define OK(x) assert((x)==BB_TRANSLATED_OK)
typedef struct { unsigned char *data; size_t size,at,relocations; } Package;
static void store(unsigned char *p,U value) { for(unsigned i=0;i<8;++i) p[i]=(unsigned char)(value>>(i*8)); }
static void emit(Package *p,U value) { assert(p->at+8<=p->size); store(p->data+p->at,value); p->at+=8; }
static void relocation(Package *p,U target,U kind,U value) { emit(p,target); emit(p,kind); emit(p,value); emit(p,0); }
static Package package(int failure) {
    Package p={.size=IMAGE_SIZE+4096}; p.data=calloc(p.size,1); assert(p.data);
    memcpy(p.data,"BBPROBE5",8); p.at=8;
    emit(&p,IMAGE_SIZE); emit(&p,BASE+GUEST_MODULE_ENTRY); emit(&p,6); emit(&p,9); emit(&p,5); emit(&p,PROFILE|1);
    emit(&p,DATA); emit(&p,DATA+G-32); emit(&p,8); emit(&p,16); emit(&p,16); emit(&p,2);
    emit(&p,ONE); emit(&p,2*G); emit(&p,ONE+GUEST_MODULE_ONE_INIT); emit(&p,ONE+2*G-32); emit(&p,16); emit(&p,8); emit(&p,2);
    emit(&p,TWO); emit(&p,2*G); emit(&p,TWO+GUEST_MODULE_TWO_INIT); emit(&p,TWO+2*G-32); emit(&p,16); emit(&p,8); emit(&p,3);
    emit(&p,2); emit(&p,3); emit(&p,ONE+GUEST_MODULE_EXPORT); emit(&p,1); emit(&p,4); emit(&p,ONE+G+128); emit(&p,2);
    for(unsigned i=0;i<6;++i) { emit(&p,BASE+i*G); emit(&p,G); emit(&p,i%2 ? 6 : 5); }
    const char *names[]={"snprintf","__tls_get_addr","__cxa_atexit","synthetic_module_export","synthetic_module_data"};
    for(unsigned i=0;i<5;++i) { strcpy((char *)p.data+p.at,names[i]); p.at+=128; }
    p.relocations=p.at;
    for(unsigned i=0;i<5;++i) relocation(&p,DATA+i*8,i==4 ? 2 : 1,i);
    relocation(&p,DATA+offsetof(ModuleShared,destructor_one),0,ONE+GUEST_MODULE_ONE_DESTRUCTOR);
    relocation(&p,DATA+offsetof(ModuleShared,destructor_two),0,TWO+GUEST_MODULE_TWO_DESTRUCTOR);
    relocation(&p,DATA+offsetof(ModuleShared,dso_one),0,ONE+G+256);
    relocation(&p,DATA+offsetof(ModuleShared,dso_two),0,TWO+G+256);
    unsigned char *image=p.data+p.at;
    assert(sizeof(guest_abi_text)<G && sizeof(ModuleShared)<G-32);
    memcpy(image+BASE,guest_abi_text,sizeof(guest_abi_text)); memcpy(image+ONE,guest_abi_text,sizeof(guest_abi_text)); memcpy(image+TWO,guest_abi_text,sizeof(guest_abi_text));
    ModuleShared shared={.fail_second=(U)failure,.number=3.5}; strcpy(shared.format,"sum=%u %.1f"); memcpy(image+DATA,&shared,sizeof(shared));
    store(image+DATA+G-32,13); store(image+ONE+2*G-32,17); store(image+TWO+2*G-32,19); store(image+ONE+G+128,77);
    p.at+=IMAGE_SIZE; p.size=p.at; return p;
}
static void rejected(BbTranslatedRuntime *r,Package *p,size_t n,const char *expected) {
    BbTranslatedImage *image=(void *)1; char diagnostic[256]={0};
    BbTranslatedStatus s=bb_modules_load(r,p->data,n,&image,diagnostic,sizeof(diagnostic));
    if(s==BB_TRANSLATED_OK || !strstr(diagnostic,expected)) fprintf(stderr,"unexpected load result=%d diagnostic=%s\n",s,diagnostic);
    assert(s!=BB_TRANSLATED_OK && image==NULL && strstr(diagnostic,expected));
    BbVmView view; assert(bb_vm_query(bb_translated_memory(r),BASE,&view)==BB_VM_INVALID);
}
int main(int argc,char **argv) {
    if(argc==3 && !strcmp(argv[1],"--write-image")) {
        Package generated=package(0); FILE *file=fopen(argv[2],"wb"); assert(file);
        assert(fwrite(generated.data,1,generated.size,file)==generated.size); assert(!fclose(file));
        free(generated.data); return 0;
    }
    int failure=argc==2 && !strcmp(argv[1],"init-failure");
    BbTranslatedRuntime *r=NULL; OK(bb_translated_open(&r)); Package p=package(failure);
    store(p.data+48,1); rejected(r,&p,p.size,"profile"); store(p.data+48,PROFILE|1);
    store(p.data+24,UINT64_MAX); rejected(r,&p,p.size,"header"); store(p.data+24,6);
    rejected(r,&p,p.size-1,"truncated");
    store(p.data+72,UINT64_MAX); rejected(r,&p,p.size,"TLS"); store(p.data+72,8);
    U first=DATA; store(p.data+p.relocations+32,first+4); rejected(r,&p,p.size,"overlapping relocation"); store(p.data+p.relocations+32,DATA+8);
    /* Unresolved names are diagnosed before mappings or TLS are committed. */
    size_t names=p.relocations-5*128; char saved[128]; memcpy(saved,p.data+names,128);
    memset(p.data+names,0,128); strcpy((char *)p.data+names,"missing_named_fixture_import"); rejected(r,&p,p.size,"missing_named_fixture_import"); memcpy(p.data+names,saved,128);
    BbTranslatedImage *image=NULL; char diagnostic[256]={0};
    BbTranslatedStatus loaded=bb_modules_load(r,p.data,p.size,&image,diagnostic,sizeof(diagnostic));
    if(loaded!=BB_TRANSLATED_OK) fprintf(stderr,"load: %s\n",diagnostic); assert(loaded==BB_TRANSLATED_OK);
    BbVmView view; assert(bb_vm_query(bb_translated_memory(r),BASE,&view)==BB_VM_OK && (U)(uintptr_t)view.host!=BASE && !(view.guest_permissions&2));
    U patch=BASE+GUEST_MODULE_PATCH_TARGET; BbGuestCall call={.kind=BB_CALL_PTR_PTR,.target=patch};
    OK(bb_translated_invoke(r,&call)); assert(call.result==31);
    unsigned char bad=33,before=31,after=32;
    assert(bb_modules_patch(image,patch+1,&bad,&after,1)==BB_TRANSLATED_INVALID);
    OK(bb_modules_patch(image,patch+1,&before,&after,1)); OK(bb_translated_invoke(r,&call)); assert(call.result==32);
    U result=999; BbTranslatedStatus boot=bb_modules_boot(image,3,&result);
    if(boot!=BB_TRANSLATED_OK && !failure) fprintf(stderr,"boot: %s\n",bb_modules_diagnostic(image));
    ModuleShared shared; assert(bb_vm_read(bb_translated_memory(r),DATA,&shared,sizeof(shared))==BB_VM_OK);
    if(failure) { assert(boot==BB_TRANSLATED_EXECUTION && result==999 && shared.order==125 && strstr(bb_modules_diagnostic(image),"module 1 initialization failed")); }
    else {
        if(result!=42 || shared.order!=12345) fprintf(stderr,"entry=%llu order=%llu\n",(unsigned long long)result,(unsigned long long)shared.order);
        assert(boot==BB_TRANSLATED_OK && result==42 && shared.order==12345 && !strcmp(shared.output,"sum=42 3.5"));
    }
    assert(bb_modules_boot(image,3,&result)==BB_TRANSLATED_INVALID);
    OK(bb_modules_close(image)); assert(bb_vm_query(bb_translated_memory(r),BASE,&view)==BB_VM_INVALID);
    OK(bb_translated_close(r)); free(p.data);
    puts("PASS: strict backend-versioned linked image, low guest mappings, named imports/fallback exports, persistent main/module TLS and FP, ordered init/cleanup, checked code patches, malformed inputs and failed-init unwind");
}
