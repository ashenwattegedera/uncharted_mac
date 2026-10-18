#include "translated_modules.h"
#include "translated_tasks.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PROFILE (UINT64_C(0x81)<<56)
enum { MAX_SEGMENTS=64,MAX_MODULES=6,MAX_IMPORTS=100000,MAX_RELOCS=1000000 };
typedef struct { uint64_t address,size,flags,mapped; } Segment;
typedef struct { uint64_t base,size,init,tls,memsz,filesz,module; } Module;
typedef struct { uint64_t index,address,kind; } Binding;
typedef struct { uint64_t target,kind,value,addend,resolved; } Relocation;
struct BbTranslatedImage {
    BbTranslatedRuntime *runtime;
    Segment *segments; Module modules[MAX_MODULES];
    size_t segment_count,module_count;
    uint64_t entry,procparam;
    int booted;
    char diagnostic[256];
};
typedef struct { const unsigned char *bytes; size_t size,at; int error; } Reader;
static const void *take(Reader *r,size_t n) {
    if(r->error || n>r->size-r->at) { r->error=1; return NULL; }
    const void *p=r->bytes+r->at; r->at+=n; return p;
}
static uint64_t word(Reader *r) {
    const unsigned char *p=take(r,8); uint64_t value=0;
    if(p) for(unsigned i=0;i<8;++i) value|=(uint64_t)p[i]<<(i*8);
    return value;
}
static int within(uint64_t at,uint64_t n,uint64_t base,uint64_t size) { return n && at>=base && at-base<size && n<=size-(at-base); }
static int mapped(BbTranslatedImage *image,uint64_t at,uint64_t n,unsigned flags) {
    for(size_t i=0;i<image->segment_count;++i) {
        Segment *s=image->segments+i;
        if(within(at,n,s->address,s->size) && (s->flags&flags)==flags) return 1;
    }
    return 0;
}
static int in_module(BbTranslatedImage *image,uint64_t at) {
    for(size_t i=0;i<image->module_count;++i) if(within(at,1,image->modules[i].base,image->modules[i].size)) return 1;
    return 0;
}
static BbTranslatedStatus message(char *out,size_t n,BbTranslatedStatus status,const char *text) { if(out && n) snprintf(out,n,"%s",text); return status; }
static int relocation_order(const void *left,const void *right) {
    const Relocation *a=left,*b=right; return a->target<b->target ? -1 : a->target>b->target;
}
static int rollback(BbTranslatedImage *image) {
    BbGuestMemory *vm=bb_translated_memory(image->runtime); int ok=1;
    for(size_t i=0;i<image->segment_count;++i) if(image->segments[i].mapped) {
        if(bb_vm_unmap(vm,image->segments[i].address,image->segments[i].mapped)!=BB_VM_OK) ok=0; image->segments[i].mapped=0;
    }
    return ok;
}
BbTranslatedStatus bb_modules_load(BbTranslatedRuntime *runtime,const void *bytes,size_t size,
    BbTranslatedImage **out,char *diagnostic,size_t diagnostic_size) {
    if(!runtime || !bytes || !out || !bb_translated_cpu(runtime)) return BB_TRANSLATED_INVALID;
    *out=NULL;
    BbTranslatedImage *image=calloc(1,sizeof(*image));
    if(!image) return BB_TRANSLATED_MEMORY;
    image->runtime=runtime;
    Reader reader={bytes,size,0,0};
    const void *magic=take(&reader,8);
    uint64_t image_size=word(&reader); image->entry=word(&reader);
    uint64_t ns=word(&reader),nr=word(&reader),ni=word(&reader),flags=word(&reader);
    BbTranslatedStatus status=BB_TRANSLATED_INVALID;
    const char *reason="invalid translated image header/profile";
    char detail[256]={0};
    Binding *bindings=NULL; Relocation *relocations=NULL;
    if(reader.error || !magic || memcmp(magic,"BBPROBE5",8) || (flags&~UINT64_C(1))!=PROFILE ||
       image_size<=BB_DARWIN_IMAGE_BASE || image_size>BB_DARWIN_IMAGE_SIZE || !ns || ns>MAX_SEGMENTS || nr>MAX_RELOCS || ni>MAX_IMPORTS) goto done;
    image->procparam=word(&reader);
    uint64_t main_tls[4]; for(unsigned i=0;i<4;++i) main_tls[i]=word(&reader);
    uint64_t modules=word(&reader);
    if(reader.error || !modules || modules>MAX_MODULES) goto done;
    image->module_count=(size_t)modules;
    for(size_t i=0;i<image->module_count;++i) {
        Module *m=image->modules+i;
        m->base=word(&reader); m->size=word(&reader); m->init=word(&reader); m->tls=word(&reader);
        m->memsz=word(&reader); m->filesz=word(&reader); m->module=word(&reader);
        if(!within(m->base,m->size,BB_DARWIN_IMAGE_BASE,image_size-BB_DARWIN_IMAGE_BASE) || !within(m->init,1,m->base,m->size) ||
           m->filesz>m->memsz || m->memsz>1024*1024 || (m->module && (m->module<2 || m->module>7)) ||
           (!m->module && (m->tls || m->filesz || m->memsz))) goto done;
        for(size_t j=0;j<i;++j) if((m->module && m->module==image->modules[j].module) ||
            (m->base<image->modules[j].base+image->modules[j].size && image->modules[j].base<m->base+m->size)) goto done;
    }
    uint64_t nb=word(&reader);
    if(reader.error || nb>ni) goto done;
    bindings=calloc(ni ? (size_t)ni : 1,sizeof(*bindings));
    relocations=calloc(nr ? (size_t)nr : 1,sizeof(*relocations));
    image->segments=calloc((size_t)ns,sizeof(*image->segments));
    if(!bindings || !relocations || !image->segments) { status=BB_TRANSLATED_MEMORY; reason="image descriptor allocation failed"; goto done; }
    image->segment_count=(size_t)ns;
    for(uint64_t i=0;i<nb;++i) {
        uint64_t index=word(&reader),address=word(&reader),kind=word(&reader);
        if(index>=ni || !in_module(image,address) || (kind!=1 && kind!=2) || bindings[index].kind) goto done;
        bindings[index]=(Binding){index,address,kind};
    }
    reason="invalid, overlapping or occupied guest image segment";
    for(size_t i=0;i<image->segment_count;++i) {
        Segment *s=image->segments+i;
        s->address=word(&reader); s->size=word(&reader); s->flags=word(&reader);
        if(!within(s->address,s->size,BB_DARWIN_IMAGE_BASE,image_size-BB_DARWIN_IMAGE_BASE) || s->address%BB_VM_GUEST_PAGE || s->flags>7) goto done;
        uint64_t n=(s->size+BB_VM_GUEST_PAGE-1)&~(uint64_t)(BB_VM_GUEST_PAGE-1);
        if(n>BB_DARWIN_IMAGE_SIZE-s->address || !bb_vm_contains(bb_translated_memory(runtime),s->address,(size_t)n)) goto done;
        for(uint64_t off=0;off<n;off+=BB_VM_GUEST_PAGE) { BbVmView v; if(bb_vm_query(bb_translated_memory(runtime),s->address+off,&v)==BB_VM_OK) goto done; }
        for(size_t j=0;j<i;++j) {
            Segment *p=image->segments+j; uint64_t pn=(p->size+BB_VM_GUEST_PAGE-1)&~(uint64_t)(BB_VM_GUEST_PAGE-1);
            if(s->address<p->address+pn && p->address<s->address+n) goto done;
        }
    }
    const char (*names)[128]=take(&reader,(size_t)ni*128);
    if(reader.error) goto done;
    for(uint64_t i=0;i<ni;++i) if(!memchr(names[i],0,128)) { reason="unterminated import name"; goto done; }
    reason="invalid entry, TLS or module layout";
    if(!mapped(image,image->entry,1,1) || (image->procparam && !mapped(image,image->procparam,1,4))) goto done;
    if(main_tls[0]>image_size || main_tls[1]>image_size-main_tls[0] || main_tls[1]>main_tls[2] || main_tls[2]>1024*1024 || !main_tls[3] || main_tls[3]>BB_VM_GUEST_PAGE ||
       (main_tls[3]&(main_tls[3]-1)) || (main_tls[2] && !mapped(image,main_tls[0],main_tls[2],4))) {
        if(main_tls[0] || main_tls[1] || main_tls[2] || main_tls[3]) goto done;
    }
    for(size_t i=0;i<image->module_count;++i) {
        Module *m=image->modules+i;
        if(!mapped(image,m->init,1,1) || (m->module && (!m->memsz || !within(m->tls,m->memsz,m->base,m->size) || !mapped(image,m->tls,m->memsz,4)))) goto done;
    }
    for(uint64_t i=0;i<ni;++i) if(bindings[i].kind && !mapped(image,bindings[i].address,1,bindings[i].kind==1 ? 1 : 4)) goto done;
    reason="invalid import/image relocation";
    for(uint64_t i=0;i<nr;++i) {
        Relocation *r=relocations+i;
        r->target=word(&reader); r->kind=word(&reader); r->value=word(&reader); r->addend=word(&reader);
        if(reader.error || !mapped(image,r->target,8,0) || r->kind>2 || r->addend>=BB_VM_GUEST_PAGE || (r->kind!=2 && r->addend)) goto done;
        if(!r->kind) { if(r->value>=image_size) goto done; r->resolved=r->value; }
        else {
            if(r->value>=ni) goto done;
            BbImportKind kind=r->kind==1 ? BB_IMPORT_FUNCTION : BB_IMPORT_DATA;
            BbTranslatedStatus bound=bb_translated_bind(runtime,names[r->value],kind,&r->resolved);
            if(bound!=BB_TRANSLATED_OK) {
                Binding *binding=bindings+r->value;
                if(binding->kind!=r->kind) {
                    snprintf(detail,sizeof(detail),"unresolved named import %.127s at relocation 0x%llx",names[r->value],(unsigned long long)r->target);
                    reason=detail; status=BB_TRANSLATED_UNSUPPORTED; goto done;
                }
                r->resolved=binding->address;
                if(r->addend && !mapped(image,r->resolved,r->addend+1,4)) goto done;
            } else if(r->kind==2) {
                size_t export_size=!strcmp(names[r->value],"__stack_chk_guard") ? 8 : 4;
                /* Scoped names resolve to the same two explicit data exports. */
                const char *symbol=runtime_symbol(names[r->value]); if(symbol && !strcmp(symbol,"__stack_chk_guard")) export_size=8;
                if(r->addend>=export_size) goto done;
            }
            if(r->resolved>UINT64_MAX-r->addend) goto done;
            r->resolved+=r->addend;
        }
    }
    qsort(relocations,(size_t)nr,sizeof(*relocations),relocation_order);
    for(uint64_t i=1;i<nr;++i) if(relocations[i].target-relocations[i-1].target<8) { reason="overlapping relocation destinations"; goto done; }
    const unsigned char *payload=take(&reader,(size_t)image_size);
    if(reader.error || reader.at!=reader.size) { reason="truncated image or unexpected trailer"; goto done; }
    reason="TLS configuration requires a fresh reference runtime";
    BbTasks *tasks=bb_translated_tasks(runtime);
    if(bb_tasks_set_tls(tasks,1,payload+main_tls[0],(size_t)main_tls[1],(size_t)main_tls[2],main_tls[3] ? (size_t)main_tls[3] : 16)!=BB_TRANSLATED_OK) goto done;
    for(size_t i=0;i<image->module_count;++i) {
        Module *m=image->modules+i;
        if(m->module && bb_tasks_set_tls(tasks,(unsigned)m->module,payload+m->tls,(size_t)m->filesz,(size_t)m->memsz,16)!=BB_TRANSLATED_OK) goto done;
    }
    reason="owned image mapping failed; close the runtime if its VM is poisoned"; status=BB_TRANSLATED_MEMORY;
    BbGuestMemory *vm=bb_translated_memory(runtime);
    for(size_t i=0;i<image->segment_count;++i) {
        Segment *s=image->segments+i; size_t n=(size_t)((s->size+BB_VM_GUEST_PAGE-1)&~(uint64_t)(BB_VM_GUEST_PAGE-1));
        uint64_t offset=runtime_memory_darwin_image_offset()+s->address;
        void *storage=bb_vm_backing_view(vm,(size_t)offset,n);
        if(!storage) goto done;
        memset(storage,0,n);
        if(bb_vm_map(vm,s->address,n,(size_t)offset,3)!=BB_VM_OK) goto done;
        s->mapped=n;
        if(bb_vm_write(vm,s->address,payload+s->address,(size_t)s->size)!=BB_VM_OK) goto done;
    }
    for(uint64_t i=0;i<nr;++i) if(bb_vm_write(vm,relocations[i].target,&relocations[i].resolved,8)!=BB_VM_OK) goto done;
    for(size_t i=0;i<image->segment_count;++i) {
        Segment *s=image->segments+i;
        unsigned permissions=((s->flags&4) ? BB_VM_READ : 0)|((s->flags&2) ? BB_VM_WRITE : 0)|((s->flags&1) ? BB_VM_EXEC : 0);
        if(bb_vm_protect(vm,s->address,(size_t)s->mapped,permissions)!=BB_VM_OK) goto done;
    }
    *out=image; free(bindings); free(relocations); return BB_TRANSLATED_OK;
done:
    message(diagnostic,diagnostic_size,status,reason);
    rollback(image); free(image->segments); free(image); free(bindings); free(relocations); return status;
}
BbTranslatedStatus bb_modules_boot(BbTranslatedImage *image,unsigned workers,uint64_t *result) {
    if(!image || !result || image->booted || !bb_translated_cpu(image->runtime)) return BB_TRANSLATED_INVALID;
    image->booted=1;
    BbTasks *tasks=bb_translated_tasks(image->runtime);
    BbTranslatedStatus status=BB_TRANSLATED_OK;
    for(size_t i=0;i<image->module_count;++i) {
        uint64_t args[3]={0,0,image->procparam},rc=0;
        status=bb_tasks_call(tasks,image->modules[i].init,args,workers,&rc);
        if(status!=BB_TRANSLATED_OK || (int32_t)rc<0) {
            snprintf(image->diagnostic,sizeof(image->diagnostic),"module %zu initialization failed at 0x%llx, result=%d: %.120s",i,(unsigned long long)image->modules[i].init,(int32_t)rc,bb_tasks_diagnostic(tasks));
            status=BB_TRANSLATED_EXECUTION; break;
        }
    }
    if(status==BB_TRANSLATED_OK) {
        uint64_t args[3]={image->procparam,0,0},value=0;
        status=bb_tasks_call(tasks,image->entry,args,workers,&value);
        if(status==BB_TRANSLATED_OK) *result=value;
        else snprintf(image->diagnostic,sizeof(image->diagnostic),"guest entry failed: %.210s",bb_tasks_diagnostic(tasks));
    }
    BbTranslatedStatus cleanup=bb_tasks_finalize(tasks);
    if(cleanup!=BB_TRANSLATED_OK && status==BB_TRANSLATED_OK) { status=cleanup; snprintf(image->diagnostic,sizeof(image->diagnostic),"module/process cleanup failed: %.190s",bb_tasks_diagnostic(tasks)); }
    return status;
}
BbTranslatedStatus bb_modules_patch(BbTranslatedImage *image,uint64_t address,const void *expected,const void *replacement,size_t n) {
    if(!image || image->booted || !expected || !replacement || !n || n>BB_VM_GUEST_PAGE || !mapped(image,address,n,1)) return BB_TRANSLATED_INVALID;
    BbGuestMemory *vm=bb_translated_memory(image->runtime); unsigned char *old=malloc(n); if(!old) return BB_TRANSLATED_MEMORY;
    BbVmStatus s=bb_vm_read(vm,address,old,n);
    int matches=s==BB_VM_OK && !memcmp(old,expected,n); free(old);
    if(!matches) return BB_TRANSLATED_INVALID;
    uint64_t first=address&~(uint64_t)(BB_VM_GUEST_PAGE-1),last=(address+n+BB_VM_GUEST_PAGE-1)&~(uint64_t)(BB_VM_GUEST_PAGE-1);
    BbVmView before; if(last-first!=BB_VM_GUEST_PAGE || bb_vm_query(vm,address,&before)!=BB_VM_OK) return BB_TRANSLATED_INVALID;
    if(bb_vm_protect(vm,first,BB_VM_GUEST_PAGE,before.guest_permissions|BB_VM_WRITE)!=BB_VM_OK) return BB_TRANSLATED_MEMORY;
    s=bb_vm_write(vm,address,replacement,n);
    if(bb_vm_protect(vm,first,BB_VM_GUEST_PAGE,before.guest_permissions)!=BB_VM_OK || s!=BB_VM_OK) return BB_TRANSLATED_MEMORY;
    return BB_TRANSLATED_OK;
}
BbTranslatedStatus bb_modules_close(BbTranslatedImage *image) {
    if(!image || !bb_translated_cpu(image->runtime) || bb_tasks_busy(bb_translated_tasks(image->runtime))) return BB_TRANSLATED_INVALID;
    BbTranslatedStatus status=bb_tasks_retire(bb_translated_tasks(image->runtime));
    if(!rollback(image)) status=BB_TRANSLATED_MEMORY; free(image->segments); free(image); return status;
}
const char *bb_modules_diagnostic(BbTranslatedImage *image) { return image ? image->diagnostic : "no module image"; }
