#include "m2_runtime_types.h"
static int format_list(U64 function,char *out,const char *format,...) {
    __builtin_va_list args; __builtin_va_start(args,format);
    int result=((int (*)(char *,U64,const char *,void *))function)(out,64,format,args);
    __builtin_va_end(args); return result;
}
void module(Imports *imports,ModuleOutput *out) {
    out->size=((U64 (*)(void))imports->size)();
    out->allocated=((int (*)(U64,U64,U64,U64,int,U64 *))imports->allocate)(0,out->size,16384,16384,3,&out->physical);
    out->mapping=0;
    out->mapped=((int (*)(U64 *,U64,int,int,U64,U64))imports->map)(&out->mapping,16384,3,0,out->physical,16384);
    if(!out->mapped) {
        *(U64 *)out->mapping=42;
        out->moved=((U64 (*)(void *,const void *,U64))imports->move)(out->copied,(void *)imports->input,6);
        out->length=((U64 (*)(const char *))imports->length)((void *)imports->input);
        out->formatted=((int (*)(char *,U64,const char *,...))imports->format)(out->text,64,(const char *)imports->format_text,7,out->number,(const char *)imports->input,42ULL);
        out->vformatted=format_list(imports->vformat,out->vtext,(const char *)imports->format_text,7,out->number,(const char *)imports->input,42ULL);
    }
    out->canary=*(U64 *)imports->canary; out->marker=*(unsigned *)imports->marker;
    out->released=((int (*)(U64,U64))imports->release)(out->physical,16384);
}
