#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "../kernel/driver_pe.h"

static void w16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void w64(uint8_t *p,uint64_t v){w32(p,(uint32_t)v);w32(p+4,(uint32_t)(v>>32));}
static void descriptor(uint8_t *p,uint32_t oft,uint32_t name,uint32_t ft){
 w32(p,oft);w32(p+4,0);w32(p+8,0);w32(p+12,name);w32(p+16,ft);
}
int main(void){
 uint8_t image[1024]={0};
 SENTINEL_PE_IMAGE_INFO info={0};
 info.image_size=sizeof image;info.import_rva=0x100;info.import_size=40;
 descriptor(image+0x100,0x300,0x200,0x320);
 memcpy(image+0x200,"KERNEL.EXE",11);
 w64(image+0x300,0x380);w64(image+0x308,0);
 w64(image+0x320,0); /* IAT storage need only be in range */
 w16(image+0x380,0);memcpy(image+0x382,"Init",5);
 assert(SentinelPeValidateImports(image,sizeof image,&info)==0);

 /* No null descriptor in the declared directory. */
 descriptor(image+0x120,0,0,0x320);
 info.import_size=20;
 assert(SentinelPeValidateImports(image,sizeof image,&info)<0);

 /* The descriptor is terminated, but the lookup thunk is not. */
 memset(image+0x100,0,40);descriptor(image+0x100,0x300,0x200,0x320);
 for(unsigned i=0x300;i+8<=sizeof image;i+=8)w64(image+i,0x380);
 info.import_size=40;
 assert(SentinelPeValidateImports(image,sizeof image,&info)<0);

 /* Ordinal imports are accepted when only the low 16 bits carry the ordinal. */
 memset(image+0x100,0,40);descriptor(image+0x100,0x300,0x200,0x320);
 w64(image+0x300,SENTINEL_PE_ORDINAL_FLAG64|7);w64(image+0x308,0);
 assert(SentinelPeValidateImports(image,sizeof image,&info)==0);
 w64(image+0x300,SENTINEL_PE_ORDINAL_FLAG64|0x10007);
 assert(SentinelPeValidateImports(image,sizeof image,&info)<0);

 /* An import directory extending beyond SizeOfImage is rejected. */
 info.import_rva=0x3f0;info.import_size=40;
 assert(SentinelPeValidateImports(image,sizeof image,&info)<0);
 return 0;
}
