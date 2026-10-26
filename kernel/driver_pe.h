#pragma once
/* Bounded PE32+ validation groundwork. This validates a file only; it does not
 * map executable pages, relocate code, resolve imports, or load a driver. */
#include <stdint.h>
#include <stddef.h>
#define SENTINEL_PE_MACHINE_AMD64 0x8664u
#define SENTINEL_PE_OPTIONAL_MAGIC64 0x020Bu
#define SENTINEL_PE_MAX_SECTIONS 96u
#define SENTINEL_PE_SCN_MEM_EXECUTE 0x20000000u
#define SENTINEL_PE_DIR_IMPORT 1u
#define SENTINEL_PE_DIR_BASERELOC 5u
#define SENTINEL_PE_ORDINAL_FLAG64 (1ull<<63)
typedef struct SENTINEL_PE_IMAGE_INFO {
 uint32_t image_size, headers_size, entry_rva, section_count, section_table_offset;
 uint32_t import_rva, import_size, reloc_rva, reloc_size; uint64_t preferred_base;
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
 uint32_t ir=0,iz=0,rr=0,rz=0;
 if(nd>SENTINEL_PE_DIR_IMPORT&&os>=112+2*8){
  ir=SentinelPeRead32(opt+112+SENTINEL_PE_DIR_IMPORT*8);
  iz=SentinelPeRead32(opt+112+SENTINEL_PE_DIR_IMPORT*8+4);
  if((ir==0)!=(iz==0))return -11;
  if(iz&&!SentinelPeRange(ir,iz,isz))return -12;
 }
 if(nd>SENTINEL_PE_DIR_BASERELOC&&os>=112+6*8){
  rr=SentinelPeRead32(opt+112+SENTINEL_PE_DIR_BASERELOC*8);
  rz=SentinelPeRead32(opt+112+SENTINEL_PE_DIR_BASERELOC*8+4);
  if((rr==0)!=(rz==0))return -13;
  if(rz&&!SentinelPeRange(rr,rz,isz))return -14;
 }
 uint64_t st=oo+os,tb=(uint64_t)ns*40;
 if(!SentinelPeRange(st,tb,file_size)||st+tb>hs)return -15;
 int executable=0;
 for(uint32_t i=0;i<ns;i++){
  const uint8_t*s=b+st+(uint64_t)i*40;
  uint32_t vs=SentinelPeRead32(s+8),va=SentinelPeRead32(s+12),rs=SentinelPeRead32(s+16),ro=SentinelPeRead32(s+20),ch=SentinelPeRead32(s+36);
  uint32_t ms=vs>rs?vs:rs;
  if(rs&&(!ro||ro<hs||!SentinelPeRange(ro,rs,file_size)))return -16;
  if(ms&&(!va||!SentinelPeRange(va,ms,isz)))return -17;
  if(entry>=va&&entry-va<ms&&(ch&SENTINEL_PE_SCN_MEM_EXECUTE))executable=1;
 }
 if(!executable)return -16;
 out->image_size=isz;out->headers_size=hs;out->entry_rva=entry;out->section_count=ns;
 out->section_table_offset=(uint32_t)st;out->import_rva=ir;out->import_size=iz;out->reloc_rva=rr;out->reloc_size=rz;out->preferred_base=base;
 return 0;
}

/* Materialize validated raw PE sections into a caller-owned, zeroed image
 * buffer. The caller must provide kernel memory with suitable page
 * permissions; this routine never allocates memory or marks pages executable. */
static int SentinelPeMapImage(const void *file,uint64_t file_size,
                              void *image,uint64_t image_capacity,
                              const SENTINEL_PE_IMAGE_INFO *info) {
 if(!file||!image||!info||!info->image_size||info->image_size>image_capacity)return -1;
 const uint8_t *src=(const uint8_t*)file; uint8_t *dst=(uint8_t*)image;
 if(info->headers_size>info->image_size||!SentinelPeRange(0,info->headers_size,file_size)||
    !SentinelPeRange(info->section_table_offset,(uint64_t)info->section_count*40,file_size))return -2;
 /* Validate every source and destination range before modifying the image. */
 for(uint32_t i=0;i<info->section_count;i++){
  const uint8_t*s=src+info->section_table_offset+(uint64_t)i*40;
  uint32_t va=SentinelPeRead32(s+12),rs=SentinelPeRead32(s+16),ro=SentinelPeRead32(s+20);
  if(rs&&(!SentinelPeRange(ro,rs,file_size)||!SentinelPeRange(va,rs,info->image_size)))return -3;
 }
 for(uint64_t i=0;i<info->image_size;i++)dst[i]=0;
 for(uint32_t i=0;i<info->headers_size;i++)dst[i]=src[i];
 for(uint32_t i=0;i<info->section_count;i++){
  const uint8_t*s=src+info->section_table_offset+(uint64_t)i*40;
  uint32_t va=SentinelPeRead32(s+12),rs=SentinelPeRead32(s+16),ro=SentinelPeRead32(s+20);
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
 uint64_t delta=actual_base-info->preferred_base;
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
   uint64_t adjusted=old+delta;
   for(uint32_t k=0;k<8;k++)image[target+k]=(uint8_t)(adjusted>>(k*8));
  }
  pos+=block;
 }
 return pos==end?0:-8;
}

/* Bounded import accessors inspect a mapped image; they do not resolve symbols
 * or write the IAT. Those operations belong to the loader's ABI policy layer. */
typedef struct SENTINEL_PE_IMPORT_DESCRIPTOR {
 uint32_t original_first_thunk, time_date_stamp, forwarder_chain, name_rva, first_thunk;
} SENTINEL_PE_IMPORT_DESCRIPTOR;
typedef struct SENTINEL_PE_IMPORT_SYMBOL {
 uint32_t dll_name_rva, lookup_thunk_rva, iat_rva, name_rva;
 uint16_t hint, ordinal; uint8_t by_ordinal;
} SENTINEL_PE_IMPORT_SYMBOL;

static int SentinelPeMappedString(const uint8_t *image,uint64_t size,uint32_t rva,uint64_t *length_out) {
 if(!image||!rva||rva>=size)return -1;
 for(uint64_t i=rva;i<size;i++)if(image[i]==0){if(length_out)*length_out=i-rva;return 0;}
 return -2;
}

/* Returns 1 for a descriptor, 0 at the null terminator, negative on error. */
static int SentinelPeGetImportDescriptor(const void *mapped_image,uint64_t mapped_size,
                                         const SENTINEL_PE_IMAGE_INFO *info,uint32_t descriptor_index,
                                         SENTINEL_PE_IMPORT_DESCRIPTOR *out) {
 if(!mapped_image||!info||!out||mapped_size<info->image_size)return -1;
 if(!info->import_rva||!info->import_size)return 0;
 if(!SentinelPeRange(info->import_rva,info->import_size,mapped_size))return -2;
 uint64_t off=(uint64_t)info->import_rva+(uint64_t)descriptor_index*20,end=(uint64_t)info->import_rva+info->import_size;
 if(off>end||end-off<20)return -3;
 const uint8_t*p=(const uint8_t*)mapped_image+off;
 out->original_first_thunk=SentinelPeRead32(p);out->time_date_stamp=SentinelPeRead32(p+4);
 out->forwarder_chain=SentinelPeRead32(p+8);out->name_rva=SentinelPeRead32(p+12);out->first_thunk=SentinelPeRead32(p+16);
 if(!(out->original_first_thunk|out->time_date_stamp|out->forwarder_chain|out->name_rva|out->first_thunk))return 0;
 uint64_t ignored;
 if(!out->name_rva||!out->first_thunk||SentinelPeMappedString((const uint8_t*)mapped_image,mapped_size,out->name_rva,&ignored))return -4;
 uint32_t lookup=out->original_first_thunk?out->original_first_thunk:out->first_thunk;
 if(!SentinelPeRange(lookup,8,mapped_size))return -5;
 return 1;
}

/* Returns 1 for a symbol, 0 at the zero thunk, negative on malformed input. */
static int SentinelPeGetImportSymbol(const void *mapped_image,uint64_t mapped_size,
                                     const SENTINEL_PE_IMPORT_DESCRIPTOR *desc,uint32_t symbol_index,
                                     SENTINEL_PE_IMPORT_SYMBOL *out) {
 if(!mapped_image||!desc||!out)return -1;
 const uint8_t*image=(const uint8_t*)mapped_image;
 uint32_t lookup=desc->original_first_thunk?desc->original_first_thunk:desc->first_thunk;
 uint64_t slot=(uint64_t)lookup+(uint64_t)symbol_index*8,iat=(uint64_t)desc->first_thunk+(uint64_t)symbol_index*8;
 if(!SentinelPeRange(slot,8,mapped_size)||!SentinelPeRange(iat,8,mapped_size))return -2;
 uint64_t thunk=SentinelPeRead64(image+slot);if(!thunk)return 0;
 out->dll_name_rva=desc->name_rva;out->lookup_thunk_rva=(uint32_t)slot;out->iat_rva=(uint32_t)iat;
 out->by_ordinal=(uint8_t)((thunk&SENTINEL_PE_ORDINAL_FLAG64)!=0);out->ordinal=0;out->name_rva=0;out->hint=0;
 if(out->by_ordinal){if(thunk&~(SENTINEL_PE_ORDINAL_FLAG64|0xFFFFull))return -3;out->ordinal=(uint16_t)(thunk&0xFFFFu);}
 else {
  if(thunk>UINT32_MAX||!SentinelPeRange(thunk,3,mapped_size))return -4;
  out->name_rva=(uint32_t)thunk;out->hint=SentinelPeRead16(image+thunk);
  uint64_t ignored;if(SentinelPeMappedString(image,mapped_size,out->name_rva+2,&ignored))return -5;
 }
 return 1;
}

/* Validate the complete import table before a loader resolves or writes any
 * IAT entries. Both descriptor and thunk terminators must be present within
 * their respective image bounds; malformed tables fail before partial binding. */
static int SentinelPeValidateImports(const void *mapped_image,uint64_t mapped_size,
                                     const SENTINEL_PE_IMAGE_INFO *info) {
 if(!mapped_image||!info||mapped_size<info->image_size)return -1;
 if(!info->import_rva&&!info->import_size)return 0;
 if(!info->import_rva||info->import_size<20||
    !SentinelPeRange(info->import_rva,info->import_size,info->image_size))return -2;
 uint64_t descriptor_count=info->import_size/20;
 int terminated=0;
 for(uint64_t d=0;d<descriptor_count;d++){
  SENTINEL_PE_IMPORT_DESCRIPTOR desc;
  int dr=SentinelPeGetImportDescriptor(mapped_image,info->image_size,info,(uint32_t)d,&desc);
  if(dr<0)return -3;
  if(dr==0){terminated=1;break;}
  uint64_t dll_len=0;
  if(SentinelPeMappedString((const uint8_t*)mapped_image,info->image_size,desc.name_rva,&dll_len)||!dll_len)return -4;
  uint32_t lookup=desc.original_first_thunk?desc.original_first_thunk:desc.first_thunk;
  uint64_t available=(info->image_size-lookup)/8;
  int thunk_terminated=0;
  for(uint64_t i=0;i<available;i++){
   SENTINEL_PE_IMPORT_SYMBOL symbol;
   int sr=SentinelPeGetImportSymbol(mapped_image,info->image_size,&desc,(uint32_t)i,&symbol);
   if(sr<0)return -5;
   if(sr==0){thunk_terminated=1;break;}
  }
  if(!thunk_terminated)return -6;
 }
 return terminated?0:-7;
}
