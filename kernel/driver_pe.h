#pragma once
/* Bounded PE32+ validation groundwork. This validates a file only; it does not
 * map executable pages, relocate code, resolve imports, or load a driver. */
#include <stdint.h>
#include <stddef.h>
#define SENTINEL_PE_MACHINE_AMD64 0x8664u
#define SENTINEL_PE_OPTIONAL_MAGIC64 0x020Bu
#define SENTINEL_PE_MAX_SECTIONS 96u
#define SENTINEL_PE_SCN_MEM_EXECUTE 0x20000000u
#define SENTINEL_PE_DIR_BASERELOC 5u
typedef struct SENTINEL_PE_IMAGE_INFO {
 uint32_t image_size, headers_size, entry_rva, section_count, section_table_offset;
 uint32_t reloc_rva, reloc_size; uint64_t preferred_base;
} SENTINEL_PE_IMAGE_INFO;
static uint16_t SentinelPeRead16(const uint8_t*p){return (uint16_t)p[0]|((uint16_t)p[1]<<8);}
static uint32_t SentinelPeRead32(const uint8_t*p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t SentinelPeRead64(const uint8_t*p){return (uint64_t)SentinelPeRead32(p)|((uint64_t)SentinelPeRead32(p+4)<<32);}
static int SentinelPeRange(uint64_t o,uint64_t n,uint64_t total){return o<=total&&n<=total-o;}
/* Returns 0 if the AMD64 PE32+ raw file layout is structurally valid. */
static int SentinelPeValidateDriverFile(const void *file,uint64_t file_size,SENTINEL_PE_IMAGE_INFO*out){
 if(!file||!out||file_size<64)return -1;
 const uint8_t*b=(const uint8_t*)file;
 if(SentinelPeRead16(b)!=0x5A4D)return -2;
 uint32_t peoff=SentinelPeRead32(b+0x3C);
 if(!SentinelPeRange(peoff,24,file_size))return -3;
 if(SentinelPeRead32(b+peoff)!=0x00004550u)return -4;
 const uint8_t*coff=b+peoff+4;
 if(SentinelPeRead16(coff)!=SENTINEL_PE_MACHINE_AMD64)return -5;
 uint16_t ns=SentinelPeRead16(coff+2),os=SentinelPeRead16(coff+16);
 if(!ns||ns>SENTINEL_PE_MAX_SECTIONS||os<112)return -6;
 uint64_t oo=(uint64_t)peoff+24;
 if(!SentinelPeRange(oo,os,file_size))return -7;
 const uint8_t*opt=b+oo;
 if(SentinelPeRead16(opt)!=SENTINEL_PE_OPTIONAL_MAGIC64)return -8;
 uint32_t entry=SentinelPeRead32(opt+16),sa=SentinelPeRead32(opt+32),fa=SentinelPeRead32(opt+36);
 uint64_t base=SentinelPeRead64(opt+24);
 uint32_t isz=SentinelPeRead32(opt+56),hs=SentinelPeRead32(opt+60),nd=SentinelPeRead32(opt+108);
 if(!isz||!hs||hs>file_size||hs>isz||!sa||!fa||!entry||entry>=isz)return -9;
 if((sa&(sa-1))||(fa&(fa-1))||(sa<4096&&sa!=fa))return -10;
 uint32_t rr=0,rz=0;
 if(nd>SENTINEL_PE_DIR_BASERELOC&&os>=112+6*8){
  rr=SentinelPeRead32(opt+112+SENTINEL_PE_DIR_BASERELOC*8);
  rz=SentinelPeRead32(opt+112+SENTINEL_PE_DIR_BASERELOC*8+4);
  if((rr==0)!=(rz==0))return -11;
  if(rz&&!SentinelPeRange(rr,rz,isz))return -12;
 }
 uint64_t st=oo+os,tb=(uint64_t)ns*40;
 if(!SentinelPeRange(st,tb,file_size)||st+tb>hs)return -13;
 int executable=0;
 for(uint32_t i=0;i<ns;i++){
  const uint8_t*s=b+st+(uint64_t)i*40;
  uint32_t vs=SentinelPeRead32(s+8),va=SentinelPeRead32(s+12),rs=SentinelPeRead32(s+16),ro=SentinelPeRead32(s+20),ch=SentinelPeRead32(s+36);
  uint32_t ms=vs>rs?vs:rs;
  if(rs&&(!ro||ro<hs||!SentinelPeRange(ro,rs,file_size)))return -14;
  if(ms&&(!va||!SentinelPeRange(va,ms,isz)))return -15;
  if(entry>=va&&entry-va<ms&&(ch&SENTINEL_PE_SCN_MEM_EXECUTE))executable=1;
 }
 if(!executable)return -16;
 out->image_size=isz;out->headers_size=hs;out->entry_rva=entry;out->section_count=ns;
 out->section_table_offset=(uint32_t)st;out->reloc_rva=rr;out->reloc_size=rz;out->preferred_base=base;
 return 0;
}

/* Materialize validated raw PE sections into a caller-owned, zeroed image
 * buffer. The caller must provide kernel memory with suitable page
 * permissions; this routine never allocates memory or marks pages executable. */
static int SentinelPeMapImage(const void *file,uint64_t file_size,
                              void *image,uint64_t image_capacity,
                              const SENTINEL_PE_IMAGE_INFO *info) {
 if(!file||!image||!info||!info->image_size||info->image_size>image_capacity)
  return -1;
 const uint8_t *src=(const uint8_t*)file;
 uint8_t *dst=(uint8_t*)image;
 if(!SentinelPeRange(0,info->headers_size,file_size)||
    !SentinelPeRange(info->section_table_offset,
                     (uint64_t)info->section_count*40,file_size))
  return -2;
 for(uint64_t i=0;i<info->image_size;i++)dst[i]=0;
 for(uint32_t i=0;i<info->headers_size;i++)dst[i]=src[i];
 for(uint32_t i=0;i<info->section_count;i++){
  const uint8_t*s=src+info->section_table_offset+(uint64_t)i*40;
  uint32_t va=SentinelPeRead32(s+12),rs=SentinelPeRead32(s+16),ro=SentinelPeRead32(s+20);
  if(rs&&(!SentinelPeRange(ro,rs,file_size)||!SentinelPeRange(va,rs,info->image_size)))
   return -3;
  for(uint32_t j=0;j<rs;j++)dst[(uint64_t)va+j]=src[(uint64_t)ro+j];
 }
 return 0;
}

/* Apply only AMD64 IMAGE_REL_BASED_DIR64 relocations to an already mapped
 * image. Unknown relocation types, malformed blocks and out-of-image targets
 * are rejected rather than silently skipped. */
static int SentinelPeApplyRelocations(void *mapped_image,uint64_t mapped_size,
                                      const SENTINEL_PE_IMAGE_INFO *info,
                                      uint64_t actual_base) {
 if(!mapped_image||!info||mapped_size<info->image_size)return -1;
 int64_t delta=(int64_t)(actual_base-info->preferred_base);
 if(!delta)return 0;
 if(!info->reloc_rva||!info->reloc_size)return -2;
 if(!SentinelPeRange(info->reloc_rva,info->reloc_size,mapped_size))return -3;
 uint8_t *image=(uint8_t*)mapped_image;
 uint64_t pos=info->reloc_rva,end=pos+info->reloc_size;
 while(pos<end){
  if(end-pos<8)return -4;
  uint32_t page=SentinelPeRead32(image+pos);
  uint32_t block=SentinelPeRead32(image+pos+4);
  if(block<8||(block&1)||block>end-pos||page>=info->image_size)return -5;
  uint64_t n=(block-8)/2;
  for(uint64_t i=0;i<n;i++){
   uint16_t item=SentinelPeRead16(image+pos+8+i*2);
   uint16_t type=item>>12,off=item&0x0FFF;
   if(type==0)continue; /* IMAGE_REL_BASED_ABSOLUTE padding */
   if(type!=10)return -6; /* IMAGE_REL_BASED_DIR64 only */
   uint64_t target=(uint64_t)page+off;
   if(!SentinelPeRange(target,8,info->image_size))return -7;
   uint64_t old=SentinelPeRead64(image+target);
   uint64_t adjusted=old+(uint64_t)delta;
   for(uint32_t k=0;k<8;k++)image[target+k]=(uint8_t)(adjusted>>(k*8));
  }
  pos+=block;
 }
 return pos==end?0:-8;
}
