#ifndef BB_ABI_FIXTURE_TYPES_H
#define BB_ABI_FIXTURE_TYPES_H
typedef unsigned long long U64;
typedef struct { U64 a,b; } II;
typedef struct { double a,b; } SS;
typedef struct { U64 a; double b; } IS;
typedef struct { double a; U64 b; } SI;
typedef struct { U64 a,b,c; } Triple;
typedef struct { U64 integer; double real; float small; II ii; SS ss; IS is; SI si; Triple triple; } Output;
#endif
