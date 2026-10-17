#include "guest_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char data[8192];
static int check(void *u,uint64_t at,size_t n,int write) { (void)u; (void)write; return at<sizeof(data) && n<=sizeof(data)-at; }
static int read_mem(void *u,uint64_t at,void *out,size_t n) { if(!check(u,at,n,0)) return 0; memcpy(out,data+at,n); return 1; }
static int write_mem(void *u,uint64_t at,const void *in,size_t n) { if(!check(u,at,n,1)) return 0; memcpy(data+at,in,n); return 1; }
static BbAbiMemory memory={check,read_mem,write_mem,NULL};
static BbAbiValue value(uint64_t v) { BbAbiValue out={0}; memcpy(out.bytes,&v,8); return out; }
static void render(const char *text,BbAbiValue *values,const char *expected) {
    BbFormatPlan p; assert(bb_format_plan(text,&p)==BB_FORMAT_OK);
    int32_t result=-99; memset(data+2048,0x5a,1024);
    assert(bb_format_write(&p,values,&memory,2048,1024,&result)==BB_FORMAT_OK);
    assert(result==(int32_t)strlen(expected) && !strcmp((char *)data+2048,expected));
}
int main(void) {
    strcpy((char *)data+64,"guest-string");
    BbAbiValue args[24]={0}; args[0]=value((uint32_t)-17); args[1]=value(42); args[2]=value(64);
    render("%+06d %#llx %.5s %%",args,"-00017 0x2a guest %");
    args[0]=value(64); args[1]=value(8); args[2]=value(3);
    render("%1$*2$.*3$s",args,"     gue");
    args[0]=value((uint32_t)-8); args[1]=value(3); args[2]=value(64);
    render("%*.*s",args,"gue     ");
    args[0]=value(0x1234); args[1]=value(0); render("%012p %p",args,"0x0000001234 0x0");
    double f=3.75; memcpy(args[0].bytes,&f,8); render("%1$.2f %1$g",args,"3.75 3.75");
    args[0]=value(255); args[1]=value(UINT64_MAX); render("%hhd %zu",args,"-1 18446744073709551615");
    args[0]=value(6000); render("abc%nXYZ",args,"abcXYZ");
    int32_t stored=0; memcpy(&stored,data+6000,4); assert(stored==3);
    BbFormatPlan p; assert(bb_format_plan("%1$d %d",&p)==BB_FORMAT_INVALID);
    assert(bb_format_plan("%2$d",&p)==BB_FORMAT_INVALID);
    assert(bb_format_plan("%1$s %1$d",&p)==BB_FORMAT_INVALID);
    assert(bb_format_plan("%Lf",&p)==BB_FORMAT_UNSUPPORTED);
    assert(bb_format_plan("%ls",&p)==BB_FORMAT_UNSUPPORTED);
    assert(bb_format_plan("%999999999d",&p)==BB_FORMAT_LIMITED);
    assert(bb_format_plan("%s",&p)==BB_FORMAT_OK);
    args[0]=value(sizeof(data)-2); data[sizeof(data)-2]='x'; data[sizeof(data)-1]='y';
    memset(data+2048,0x5a,16); int32_t result=123;
    assert(bb_format_write(&p,args,&memory,2048,16,&result)==BB_FORMAT_MEMORY && result==123 && data[2048]==0x5a);
    args[0]=value(64); assert(bb_format_write(&p,args,&memory,2048,4,&result)==BB_FORMAT_OK && result==12 && !strcmp((char *)data+2048,"gue"));
    assert(bb_format_write(&p,args,&memory,UINT64_MAX,0,&result)==BB_FORMAT_OK && result==12);
    assert(bb_format_write(&p,args,&memory,UINT64_MAX,2,&result)==BB_FORMAT_MEMORY);
    assert(bb_format_plan("literal",&p)==BB_FORMAT_OK);
    assert(bb_format_write(&p,NULL,&memory,2048,16,&result)==BB_FORMAT_OK && result==7);
    p.parts[0].arg=24; p.count=1;
    assert(bb_format_write(&p,NULL,&memory,2048,16,&result)==BB_FORMAT_INVALID);
    puts("PASS: checked guest formatting, positional/star operands, promoted doubles, LP64, %n, truncation and failure preservation");
    return 0;
}
