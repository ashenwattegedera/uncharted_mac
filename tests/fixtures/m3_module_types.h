#ifndef M3_MODULE_TYPES_H
#define M3_MODULE_TYPES_H
#include <stdint.h>
typedef uint64_t U;
typedef struct {
    int (*format)(char *,U,const char *,...);
    void *(*tls)(U *);
    int (*at_exit)(U,void *,void *);
    U (*export_function)(U);
    U *export_data;
} ModuleImports;
typedef struct {
    ModuleImports imports;
    U destructor_one,destructor_two,dso_one,dso_two,order,fail_second,sum;
    double number;
    char format[32],output[64];
} ModuleShared;
#endif
