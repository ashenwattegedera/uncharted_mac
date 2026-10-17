/* Freestanding x86-64 SysV callers. No SDK headers, libraries or game content. */
#include "guest_abi_types.h"
void integers(U64 fn,Output *out) {
    out->integer=((U64 (*)(U64,U64,U64,U64,U64,U64,U64,U64))fn)(1,2,3,4,5,6,7,8);
}
void floating(U64 fn,Output *out) {
    /* Integer-to-double conversions avoid relocatable literal pools. */
    double x=(double)(int)out->integer;
    out->real=((double (*)(double,double,double,double,double,double,double,double,double))fn)(x,x,x,x,x,x,x,x,x);
}
void small_float(U64 fn,Output *out) {
    out->small=((float (*)(float))fn)((float)(int)out->integer);
}
void pair_integer(U64 fn,Output *out) { II p={17,25}; out->ii=((II (*)(II))fn)(p); }
void pair_sse(U64 fn,Output *out) {
    double x=(double)(int)out->integer; SS p={x,x}; out->ss=((SS (*)(SS))fn)(p);
}
void pair_mixed(U64 fn,Output *out) {
    IS p={17,(double)(int)out->integer}; out->is=((IS (*)(IS))fn)(p);
}
void pair_reverse(U64 fn,Output *out) {
    SI p={(double)(int)out->integer,25}; out->si=((SI (*)(SI))fn)(p);
}
void rollback(U64 fn,Output *out) {
    II p={out->integer,out->integer+1};
    out->ii=((II (*)(U64,U64,U64,U64,U64,II,U64))fn)(1,2,3,4,5,p,77);
}
void memory_return(U64 fn,Output *out) {
    Triple p={out->integer,out->integer+1,out->integer+2}; out->triple=((Triple (*)(U64,Triple))fn)(10,p);
}
void varargs(U64 fn,Output *out) {
    out->real=((double (*)(unsigned,...))fn)(1,(double)(int)out->integer);
}
void data_export(U64 fn,Output *out) { out->integer=*(U64 *)fn; }
