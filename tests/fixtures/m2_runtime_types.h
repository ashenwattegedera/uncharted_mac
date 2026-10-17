#ifndef BB_M2_RUNTIME_FIXTURE_H
#define BB_M2_RUNTIME_FIXTURE_H
typedef unsigned long long U64;
typedef struct { U64 size,allocate,map,release,format,vformat,move,length,canary,marker,format_text,input; } Imports;
typedef struct {
    U64 size,physical,mapping,moved,length,canary; unsigned marker; int allocated,mapped,released,formatted,vformatted;
    double number; char text[64],vtext[64],copied[64];
} ModuleOutput;
#endif
