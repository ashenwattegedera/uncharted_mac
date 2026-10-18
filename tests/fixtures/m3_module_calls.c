#include "m3_module_types.h"
static U *main_tls(void) { U p; __asm__("movq %%fs:0,%0":"=r"(p)); return (U *)(p-16); }
int module_one_init(U size,void *arg,ModuleShared *s) {
    (void)size; (void)arg;
    volatile U index[2]; index[0]=2; index[1]=0; U *tls=s->imports.tls((U *)index);
    if(main_tls()[0]!=13 || main_tls()[1] || tls[0]!=17 || tls[1]) return -1;
    main_tls()[0]=21; tls[0]=23;
    unsigned fp=0xbfc0; __asm__ volatile("ldmxcsr %0"::"m"(fp));
    s->order=1;
    return s->imports.at_exit(s->destructor_one,s,(void *)s->dso_one);
}
int module_two_init(U size,void *arg,ModuleShared *s) {
    (void)size; (void)arg;
    volatile U index[2]; index[0]=3; index[1]=0; U *tls=s->imports.tls((U *)index);
    unsigned fp=0; __asm__ volatile("stmxcsr %0":"=m"(fp));
    if(main_tls()[0]!=21 || tls[0]!=19 || tls[1] || fp!=0xbfc0) return -2;
    tls[0]=31; s->order=s->order*10+2;
    if(s->fail_second) return -7;
    return s->imports.at_exit(s->destructor_two,s,(void *)s->dso_two);
}
void module_one_destructor(ModuleShared *s) { s->order=s->order*10+5; }
void module_two_destructor(ModuleShared *s) { s->order=s->order*10+4; }
U module_export(U input) { return input+40; }
U module_patch_target(void) { return 31; }
U module_entry(ModuleShared *s) {
    volatile U index[2]; index[0]=2; index[1]=0; U *one=s->imports.tls((U *)index);
    index[0]=3; U *two=s->imports.tls((U *)index);
    unsigned fp=0; __asm__ volatile("stmxcsr %0":"=m"(fp));
    if(main_tls()[0]!=21 || one[0]!=23 || two[0]!=31 || fp!=0xbfc0 || *s->imports.export_data!=77) return 1;
    s->sum=s->imports.export_function(2);
    if(s->imports.format(s->output,sizeof(s->output),s->format,(unsigned)s->sum,s->number)<0) return 2;
    s->order=s->order*10+3;
    return s->sum;
}
