#define _GNU_SOURCE
#define _DARWIN_C_SOURCE
#include "guest_format.h"
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int digit(char c) { return c>='0' && c<='9'; }
static int number(const char *s,unsigned *at,int *out) {
    unsigned start=*at; int n=0;
    while(digit(s[*at])) { if(n>(BB_FORMAT_LIMIT-(s[*at]-'0'))/10) return 0; n=n*10+s[(*at)++]-'0'; }
    *out=n; return *at!=start;
}
static int argument(BbFormatPlan *p,unsigned explicit_index,BbAbiType type,unsigned *next,int *mode) {
    int positional=explicit_index!=0;
    if(*mode && *mode!=(positional ? 2 : 1)) return 0;
    *mode=positional ? 2 : 1;
    unsigned index=positional ? explicit_index : (*next)++;
    if(!index || index>BB_ABI_MAX_ARGS) return 0;
    if(p->types[index-1] && p->types[index-1]!=type) return 0;
    p->types[index-1]=type; if(index>p->argument_count) p->argument_count=index;
    return (int)index;
}
BbFormatStatus bb_format_plan(const char *text,BbFormatPlan *p) {
    if(!text || !p) return BB_FORMAT_INVALID;
    size_t n=strnlen(text,BB_FORMAT_TEXT);
    if(n==BB_FORMAT_TEXT) return BB_FORMAT_LIMITED;
    memset(p,0,sizeof(*p)); memcpy(p->text,text,n+1);
    unsigned next=1; int mode=0;
    for(unsigned at=0;at<n;) {
        if(text[at++]!='%') continue;
        if(p->count==BB_FORMAT_PARTS) return BB_FORMAT_LIMITED;
        BbFormatPart *part=&p->parts[p->count++];
        part->begin=at-1; part->precision=-1;
        if(text[at]=='%') { part->conversion='%'; part->end=++at; continue; }
        unsigned index=0,start=at; int parsed=0;
        if(digit(text[at])) {
            if(!number(text,&at,&parsed)) return BB_FORMAT_LIMITED;
            if(text[at]=='$') { if(!parsed) return BB_FORMAT_INVALID; index=(unsigned)parsed; ++at; }
            else at=start;
        }
        unsigned flag_count=0;
        while(text[at] && strchr("-+ #0",text[at])) {
            if(!strchr(part->flags,text[at])) { if(flag_count==5) return BB_FORMAT_INVALID; part->flags[flag_count++]=text[at]; }
            ++at;
        }
        if(text[at]=='*') {
            ++at; unsigned wi=0;
            if(digit(text[at])) { if(!number(text,&at,&parsed) || text[at++]!='$' || !parsed) return BB_FORMAT_INVALID; wi=(unsigned)parsed; }
            int ai=argument(p,wi,BB_ABI_U32,&next,&mode); if(!ai) return BB_FORMAT_INVALID; part->width_arg=(unsigned)ai;
        } else if(digit(text[at]) && !number(text,&at,&part->width)) return BB_FORMAT_LIMITED;
        if(text[at]=='.') {
            ++at; part->precision=0;
            if(text[at]=='*') {
                ++at; unsigned pi=0;
                if(digit(text[at])) { if(!number(text,&at,&parsed) || text[at++]!='$' || !parsed) return BB_FORMAT_INVALID; pi=(unsigned)parsed; }
                int ai=argument(p,pi,BB_ABI_U32,&next,&mode); if(!ai) return BB_FORMAT_INVALID; part->precision_arg=(unsigned)ai;
            } else if(digit(text[at]) && !number(text,&at,&part->precision)) return BB_FORMAT_LIMITED;
        }
        /* 0=default, 1=hh, 2=h, 3=l, 4=ll/j/z/t (guest LP64). */
        if(text[at]=='h') { ++at; part->length=2; if(text[at]=='h') { ++at; part->length=1; } }
        else if(text[at]=='l') { ++at; part->length=3; if(text[at]=='l') { ++at; part->length=4; } }
        else if(text[at] && strchr("jzt",text[at])) { ++at; part->length=4; }
        else if(text[at]=='L') return BB_FORMAT_UNSUPPORTED;
        char c=text[at]; if(!c) return BB_FORMAT_INVALID;
        part->conversion=c; part->end=++at;
        BbAbiType type;
        if(strchr("diuoxX",c)) type=part->length>=3 ? BB_ABI_U64 : BB_ABI_U32;
        else if(strchr("fFeEgGaA",c)) { if(part->length && part->length!=3) return BB_FORMAT_UNSUPPORTED; type=BB_ABI_DOUBLE; }
        else if(c=='s' || c=='p' || c=='n') {
            if((c=='s' || c=='p') && part->length) return BB_FORMAT_UNSUPPORTED;
            type=BB_ABI_POINTER;
        } else if(c=='c' && !part->length) type=BB_ABI_U32;
        else return BB_FORMAT_UNSUPPORTED;
        int ai=argument(p,index,type,&next,&mode); if(!ai) return BB_FORMAT_INVALID; part->arg=(unsigned)ai;
        if(c=='n' && (flag_count || part->width || part->width_arg || part->precision>=0)) return BB_FORMAT_INVALID;
    }
    for(unsigned i=0;i<p->argument_count;++i) if(!p->types[i]) return BB_FORMAT_INVALID;
    return BB_FORMAT_OK;
}
BbFormatStatus bb_guest_string(const BbAbiMemory *m,uint64_t at,char *text,size_t capacity) {
    if(!m || !m->check || !m->read || !text || !capacity) return BB_FORMAT_INVALID;
    for(size_t i=0;i<capacity;++i) {
        if(at>UINT64_MAX-i || !m->check(m->user,at+i,1,0) || !m->read(m->user,at+i,text+i,1)) return BB_FORMAT_MEMORY;
        if(!text[i]) return BB_FORMAT_OK;
    }
    return BB_FORMAT_LIMITED;
}
static uint64_t bits(const BbAbiValue *v) { uint64_t n; memcpy(&n,v->bytes,8); return n; }
static int append(char *output,size_t *used,const char *data,size_t n) {
    if(n>BB_FORMAT_LIMIT-*used) return 0;
    memcpy(output+*used,data,n); *used+=n; output[*used]=0; return 1;
}
#ifdef _WIN32
#define LENGTH(fmt,value) _scprintf_l(fmt,locale,value)
#define PUT(buf,size,fmt,value) _snprintf_l(buf,size,fmt,locale,value)
#else
#define LENGTH(fmt,value) snprintf(NULL,0,fmt,value)
#define PUT(buf,size,fmt,value) snprintf(buf,size,fmt,value)
#endif
BbFormatStatus bb_format_write(const BbFormatPlan *p,const BbAbiValue *v,const BbAbiMemory *m,
    uint64_t destination,uint64_t capacity,int32_t *result) {
    if(!p || (!v && p->argument_count) || !m || !m->check || !m->read || !m->write || !result) return BB_FORMAT_INVALID;
    /* Plans are public values: reject forged indices/bounds before rendering. */
    BbFormatPlan validated;
    if(bb_format_plan(p->text,&validated)!=BB_FORMAT_OK || memcmp(p,&validated,sizeof(validated))) return BB_FORMAT_INVALID;
    static const BbAbiValue empty[BB_ABI_MAX_ARGS]={0};
    if(!v) v=empty;
    char *output=calloc(BB_FORMAT_LIMIT+1,1),*temp=malloc(BB_FORMAT_LIMIT+1),*string=malloc(BB_FORMAT_LIMIT+1);
    if(!output || !temp || !string) { free(output); free(temp); free(string); return BB_FORMAT_SYSTEM; }
#ifdef _WIN32
    _locale_t locale=_create_locale(LC_ALL,"C");
    if(!locale) { free(output); free(temp); free(string); return BB_FORMAT_SYSTEM; }
#else
    locale_t locale=newlocale(LC_ALL_MASK,"C",(locale_t)0),previous=(locale_t)0;
    if(!locale) { free(output); free(temp); free(string); return BB_FORMAT_SYSTEM; }
    previous=uselocale(locale);
    if(!previous) { freelocale(locale); free(output); free(temp); free(string); return BB_FORMAT_SYSTEM; }
#endif
    struct { uint64_t address,value; unsigned size; } counts[BB_FORMAT_PARTS]; unsigned count=0;
    size_t used=0,cursor=0; BbFormatStatus status=BB_FORMAT_OK;
#define FAIL(code) do { status=(code); goto done; } while(0)
    for(unsigned i=0;i<p->count;++i) {
        const BbFormatPart *part=p->parts+i;
        if(!append(output,&used,p->text+cursor,part->begin-cursor)) FAIL(BB_FORMAT_LIMITED);
        cursor=part->end;
        char c=part->conversion;
        if(c=='%') { if(!append(output,&used,"%",1)) FAIL(BB_FORMAT_LIMITED); continue; }
        int width=part->width,precision=part->precision;
        char flags[8]; strcpy(flags,part->flags);
        if(part->width_arg) width=(int32_t)bits(v+part->width_arg-1);
        if(width<0) { if(width==INT_MIN) FAIL(BB_FORMAT_LIMITED); width=-width; if(!strchr(flags,'-')) strcat(flags,"-"); }
        if(part->precision_arg) precision=(int32_t)bits(v+part->precision_arg-1);
        if(width>BB_FORMAT_LIMIT || precision>BB_FORMAT_LIMIT) FAIL(BB_FORMAT_LIMITED);
        uint64_t value=bits(v+part->arg-1);
        if(c=='n') {
            unsigned size=part->length==1 ? 1 : part->length==2 ? 2 : part->length>=3 ? 8 : 4;
            if(!m->check(m->user,value,size,1)) FAIL(BB_FORMAT_MEMORY);
            counts[count].address=value; counts[count].value=used; counts[count++].size=size;
            continue;
        }
        char fmt[64],numbers[32]; snprintf(fmt,sizeof(fmt),"%%%s",flags);
        if(width) { snprintf(numbers,sizeof(numbers),"%d",width); strcat(fmt,numbers); }
        if(precision>=0) { snprintf(numbers,sizeof(numbers),".%d",precision); strcat(fmt,numbers); }
        int n=-1;
        if(strchr("diuoxX",c)) {
            strcat(fmt,"ll"); size_t end=strlen(fmt); fmt[end]=c; fmt[end+1]=0;
            if(c=='d' || c=='i') {
                int64_t number=part->length==1 ? (int8_t)value : part->length==2 ? (int16_t)value : part->length<3 ? (int32_t)value : (int64_t)value;
                n=LENGTH(fmt,(long long)number); if(n>=0 && n<=BB_FORMAT_LIMIT) PUT(temp,(size_t)n+1,fmt,(long long)number);
            } else {
                uint64_t number=part->length==1 ? (uint8_t)value : part->length==2 ? (uint16_t)value : part->length<3 ? (uint32_t)value : value;
                n=LENGTH(fmt,(unsigned long long)number); if(n>=0 && n<=BB_FORMAT_LIMIT) PUT(temp,(size_t)n+1,fmt,(unsigned long long)number);
            }
        } else if(strchr("fFeEgGaA",c)) {
            size_t end=strlen(fmt); fmt[end]=c; fmt[end+1]=0; double number; memcpy(&number,v[part->arg-1].bytes,8);
            n=LENGTH(fmt,number); if(n>=0 && n<=BB_FORMAT_LIMIT) PUT(temp,(size_t)n+1,fmt,number);
        } else if(c=='s') {
            size_t j=0,limit=precision>=0 ? (size_t)precision : BB_FORMAT_LIMIT;
            if(!value) strcpy(string,"(null)");
            else {
                while(j<limit) {
                    if(value>UINT64_MAX-j || !m->check(m->user,value+j,1,0) || !m->read(m->user,value+j,string+j,1)) FAIL(BB_FORMAT_MEMORY);
                    if(!string[j]) break;
                    ++j;
                }
                if(j==BB_FORMAT_LIMIT && precision<0) FAIL(BB_FORMAT_LIMITED);
                string[j]=0;
            }
            strcat(fmt,"s"); n=LENGTH(fmt,string); if(n>=0 && n<=BB_FORMAT_LIMIT) PUT(temp,(size_t)n+1,fmt,string);
        } else if(c=='c') {
            strcat(fmt,"c"); n=LENGTH(fmt,(int)(uint8_t)value); if(n>=0 && n<=BB_FORMAT_LIMIT) PUT(temp,(size_t)n+1,fmt,(int)(uint8_t)value);
        } else if(c=='p') {
            int digits=snprintf(string,64,"0x%llx",(unsigned long long)value);
            if(digits<0) FAIL(BB_FORMAT_SYSTEM);
            size_t pad=width>digits ? (size_t)(width-digits) : 0;
            if(!strchr(flags,'-') && !strchr(flags,'0')) { memset(temp,' ',pad); memcpy(temp+pad,string,(size_t)digits); }
            else if(!strchr(flags,'-') && strchr(flags,'0')) { memcpy(temp,"0x",2); memset(temp+2,'0',pad); memcpy(temp+2+pad,string+2,(size_t)digits-2); }
            else { memcpy(temp,string,(size_t)digits); memset(temp+digits,' ',pad); }
            n=digits+(int)pad; temp[n]=0;
        }
        if(n<0) FAIL(BB_FORMAT_SYSTEM);
        if(n>BB_FORMAT_LIMIT || !append(output,&used,temp,(size_t)n)) FAIL(BB_FORMAT_LIMITED);
    }
    if(!append(output,&used,p->text+cursor,strlen(p->text+cursor))) FAIL(BB_FORMAT_LIMITED);
    size_t written=capacity ? (capacity-1<used ? (size_t)capacity-1 : used) : 0;
    if(capacity && !m->check(m->user,destination,written+1,1)) FAIL(BB_FORMAT_MEMORY);
    /* Recheck %n destinations before committing any writes. */
    for(unsigned i=0;i<count;++i) if(!m->check(m->user,counts[i].address,counts[i].size,1)) FAIL(BB_FORMAT_MEMORY);
    if(capacity) { output[written]=0; if(!m->write(m->user,destination,output,written+1)) FAIL(BB_FORMAT_MEMORY); }
    for(unsigned i=0;i<count;++i) if(!m->write(m->user,counts[i].address,&counts[i].value,counts[i].size)) FAIL(BB_FORMAT_MEMORY);
    *result=(int32_t)used;
done:
#ifndef _WIN32
    uselocale(previous); freelocale(locale);
#else
    _free_locale(locale);
#endif
    free(output); free(temp); free(string); return status;
#undef FAIL
}
