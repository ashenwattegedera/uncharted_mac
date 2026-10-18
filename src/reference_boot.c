/* Diagnostic reference harness; no graphics or proprietary content is linked. */
#include "translated_modules.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv) {
    if(argc!=2) { fprintf(stderr,"Usage: %s PREPARED_TRANSLATED_BBPROBE5_IMAGE\n",argv[0]); return 2; }
    FILE *file=fopen(argv[1],"rb"); if(!file) { perror("open prepared image"); return 2; }
    if(fseek(file,0,SEEK_END)) { fclose(file); return 2; }
    long length=ftell(file);
    if(length<=0 || length>576L*1024*1024 || fseek(file,0,SEEK_SET)) { fclose(file); fputs("invalid image size\n",stderr); return 2; }
    void *bytes=malloc((size_t)length); if(!bytes) { fclose(file); return 2; }
    size_t read=fread(bytes,1,(size_t)length,file); int closed=fclose(file);
    if(read!=(size_t)length || closed) { free(bytes); fputs("could not read complete image\n",stderr); return 2; }
    BbTranslatedRuntime *runtime=NULL; BbTranslatedImage *image=NULL; char diagnostic[256]={0};
    BbTranslatedStatus status=bb_translated_open(&runtime);
    if(status==BB_TRANSLATED_OK) status=bb_modules_load(runtime,bytes,(size_t)length,&image,diagnostic,sizeof(diagnostic));
    free(bytes);
    uint64_t result=0;
    if(status==BB_TRANSLATED_OK) {
        status=bb_modules_boot(image,2,&result);
        if(status!=BB_TRANSLATED_OK) snprintf(diagnostic,sizeof(diagnostic),"%s",bb_modules_diagnostic(image));
    }
    if(image && bb_modules_close(image)!=BB_TRANSLATED_OK && status==BB_TRANSLATED_OK) status=BB_TRANSLATED_EXECUTION;
    if(runtime && bb_translated_close(runtime)!=BB_TRANSLATED_OK && status==BB_TRANSLATED_OK) status=BB_TRANSLATED_MEMORY;
    if(status!=BB_TRANSLATED_OK) { fprintf(stderr,"reference boot failed (%d): %s\n",status,diagnostic); return 1; }
    printf("Reference entry completed: result=0x%" PRIx64 "\n",result); return 0;
}
