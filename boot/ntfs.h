#pragma once
// Read-only NTFS reader for the UEFI loader.
// Supports NTFS 3.x volumes, update-sequence fixups, MFT data runlists,
// resident FILE_NAME attributes, and non-resident file data. No writes,
// compression, encryption, or sparse-file decoding.
#include <Protocol/BlockIo.h>

static EFI_GUID gNtfsBlockIoGuid = {0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};
#define N16(p,o) (*(UINT16 *)((UINT8 *)(p) + (o)))
#define N32(p,o) (*(UINT32 *)((UINT8 *)(p) + (o)))
#define N64(p,o) (*(UINT64 *)((UINT8 *)(p) + (o)))

typedef struct {
    EFI_BLOCK_IO_PROTOCOL *bio;
    UINT32 sector, cluster, record;
    UINT64 total_sectors, mft_lcn, mft_size;
    UINT8 *mft_runs;
    UINTN mft_runs_len;
} Ntfs;

static EFI_STATUS nt_read_disk(Ntfs *fs, UINT64 off, UINTN len, VOID *dst) {
    UINT32 ss = fs->bio->Media->BlockSize;
    if (!ss || ss != fs->sector || !fs->total_sectors ||
        fs->total_sectors > UINT64_MAX / fs->sector) return EFI_DEVICE_ERROR;
    UINT64 disk_bytes = fs->total_sectors * fs->sector;
    if (off > disk_bytes || (UINT64)len > disk_bytes - off)
        return EFI_DEVICE_ERROR;
    while (len) {
        UINT64 lba = off / ss;
        UINTN skip = (UINTN)(off % ss);
        UINTN n = ss - skip;
        if (n > len) n = len;
        VOID *tmp = NULL;
        EFI_STATUS s = bs->AllocatePool(EfiLoaderData, ss, &tmp);
        if (EFI_ERROR(s)) return s;
        s = fs->bio->ReadBlocks(fs->bio, fs->bio->Media->MediaId, lba, ss, tmp);
        if (!EFI_ERROR(s)) memcpy(dst, (UINT8 *)tmp + skip, n);
        bs->FreePool(tmp);
        if (EFI_ERROR(s)) return s;
        off += n; dst = (UINT8 *)dst + n; len -= n;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS nt_fixup(UINT8 *r, UINT32 size, UINT32 sector) {
    if (size < 8 || r[0]!='F' || r[1]!='I' || r[2]!='L' || r[3]!='E') return EFI_LOAD_ERROR;
    UINT16 usa_off=N16(r,4), usa_count=N16(r,6);
    if (usa_count < 2 || usa_off + (UINT32)usa_count*2 > size ||
        (UINT32)(usa_count-1)*sector != size) return EFI_LOAD_ERROR;
    UINT16 seq=N16(r,usa_off);
    for (UINT16 i=1;i<usa_count;i++) {
        UINT32 off=(UINT32)i*sector-2;
        if (N16(r,off)!=seq) return EFI_LOAD_ERROR;
        r[off]=r[usa_off+i*2]; r[off+1]=r[usa_off+i*2+1];
    }
    return EFI_SUCCESS;
}

static int nt_run_next(const UINT8 *runs, UINTN len, UINTN *pos,
                       UINT64 *vcn, INT64 *lcn, UINT64 *count, int *sparse) {
    if (*pos >= len || !runs[*pos]) return 0;
    UINT8 h=runs[(*pos)++], nb=h&15, lb=h>>4;
    if (!nb || nb>8 || lb>8 || *pos+nb+lb>len) return -1;
    UINT64 n=0; for (UINT8 i=0;i<nb;i++) n|=(UINT64)runs[(*pos)++]<<(8*i);
    if (!n) return -1;
    *sparse = lb==0;
    if (lb) {
        UINT64 raw=0; for (UINT8 i=0;i<lb;i++) raw|=(UINT64)runs[(*pos)++]<<(8*i);
        if (runs[*pos-lb] & 0x80) {
            for (UINT8 i=lb;i<8;i++) raw|=(UINT64)0xff<<(8*i);
        }
        *lcn += (INT64)raw;
    }
    *count=n;
    return 1;
}

static EFI_STATUS nt_runs_read(Ntfs *fs, const UINT8 *runs, UINTN runlen,
                               UINT64 offset, UINTN len, VOID *dst) {
    if (!fs || !runs || !runlen || (!dst && len) ||
        offset > UINT64_MAX - (UINT64)len) return EFI_INVALID_PARAMETER;
    if (!len) return EFI_SUCCESS;
    UINT64 request_end = offset + (UINT64)len;
    UINT64 covered = offset;
    UINT64 vcn=0; INT64 lcn=0; UINTN p=0;
    while (p<runlen && runs[p]) {
        UINT64 count=0; int sparse=0;
        int rc=nt_run_next(runs,runlen,&p,&vcn,&lcn,&count,&sparse);
        if (rc<=0) return EFI_LOAD_ERROR;
        if (count > UINT64_MAX / fs->cluster ||
            vcn > UINT64_MAX / fs->cluster) return EFI_LOAD_ERROR;
        UINT64 bytes=count*fs->cluster, start=vcn*fs->cluster;
        if (start > UINT64_MAX - bytes) return EFI_LOAD_ERROR;
        UINT64 end=start+bytes;
        if (offset<end && request_end>start) {
            UINT64 a=offset>start?offset:start, b=request_end<end?request_end:end;
            if (a > covered) return EFI_LOAD_ERROR;
            if (b > covered) covered = b;
            UINTN n=(UINTN)(b-a); VOID *out=(UINT8 *)dst+(a-offset);
            if (sparse) memset(out,0,n);
            else {
                if (lcn<0 || (UINT64)lcn > UINT64_MAX / fs->cluster)
                    return EFI_LOAD_ERROR;
                UINT64 disk_off=(UINT64)lcn*fs->cluster;
                if (disk_off > UINT64_MAX - (a-start)) return EFI_LOAD_ERROR;
                EFI_STATUS s=nt_read_disk(fs,disk_off+(a-start),n,out);
                if (EFI_ERROR(s)) return s;
            }
        }
        if (vcn > UINT64_MAX - count) return EFI_LOAD_ERROR;
        vcn+=count;
    }
    return covered == request_end ? EFI_SUCCESS : EFI_LOAD_ERROR;
}

static EFI_STATUS nt_record(Ntfs *fs, UINT64 index, UINT8 *out) {
    if (index >= fs->mft_size / fs->record || fs->record > 65536) return EFI_NOT_FOUND;
    EFI_STATUS s=nt_runs_read(fs,fs->mft_runs,fs->mft_runs_len,index*fs->record,fs->record,out);
    if (EFI_ERROR(s)) return s;
    return nt_fixup(out,fs->record,fs->sector);
}

static EFI_STATUS nt_mount(Ntfs *fs, EFI_BLOCK_IO_PROTOCOL *bio) {
    UINT8 boot[512];
    memset(fs,0,sizeof(*fs)); fs->bio=bio;
    if (!bio || !bio->Media || !bio->Media->MediaPresent) return EFI_NO_MEDIA;
    fs->sector=bio->Media->BlockSize;
    fs->total_sectors=bio->Media->LastBlock+1;
    EFI_STATUS s=nt_read_disk(fs,0,512,boot); if(EFI_ERROR(s)) return s;
    if (boot[3]!='N'||boot[4]!='T'||boot[5]!='F'||boot[6]!='S'||N16(boot,510)!=0xaa55) return EFI_UNSUPPORTED;
    UINT16 bps=N16(boot,11); UINT8 spc=boot[13];
    if (!bps || !spc || (spc&(spc-1)) || bps!=fs->sector || bps*spc>65536) return EFI_UNSUPPORTED;
    fs->cluster=(UINT32)bps*spc; fs->mft_lcn=N64(boot,48);
    INT8 cr=(INT8)boot[64];
    if (cr<0) { if(cr < -16) return EFI_UNSUPPORTED; fs->record=1u<<(-cr); }
    else fs->record=(UINT32)cr*fs->cluster;
    if (fs->record<512 || fs->record>65536 ||
        fs->mft_lcn > UINT64_MAX / fs->cluster ||
        fs->total_sectors > UINT64_MAX / fs->sector ||
        fs->mft_lcn * fs->cluster >= fs->total_sectors * fs->sector)
        return EFI_UNSUPPORTED;
    UINT8 *rec=NULL;
    s=bs->AllocatePool(EfiLoaderData,fs->record,(VOID **)&rec); if(EFI_ERROR(s)) return s;
    s=nt_read_disk(fs,fs->mft_lcn*fs->cluster,fs->record,rec);
    if(!EFI_ERROR(s)) s=nt_fixup(rec,fs->record,fs->sector);
    if(EFI_ERROR(s)){bs->FreePool(rec);return s;}
    UINT16 ao=N16(rec,20); UINT8 *runs=NULL; UINTN runlen=0;
    for(UINT32 p=ao;p+16<fs->record;) {
        UINT32 type=N32(rec,p); if(type==0xffffffff) break;
        UINT32 al=N32(rec,p+4); if(al<16||p+al>fs->record) break;
        if(type==0x80 && rec[p+8]) {
            UINT16 ro=N16(rec,p+32); if(ro>=al) break;
            runs=rec+p+ro; runlen=al-ro; fs->mft_size=N64(rec,p+48); break;
        }
        p+=al;
    }
    if(!runs||!runlen||!fs->mft_size){bs->FreePool(rec);return EFI_UNSUPPORTED;}
    s=bs->AllocatePool(EfiLoaderData,runlen,(VOID **)&fs->mft_runs);
    if(!EFI_ERROR(s)){memcpy(fs->mft_runs,runs,runlen);fs->mft_runs_len=runlen;}
    bs->FreePool(rec);
    return s;
}

static int nt_name_eq(const UINT8 *u, UINT8 n, const char *name) {
    UINTN i=0; while(name[i] && i<n) { UINT8 c=(UINT8)name[i]; UINT16 w=N16(u,i*2);
        if(c>='A'&&c<='Z') c=(UINT8)(c+32);
        if(w>='A'&&w<='Z') w=(UINT16)(w+32);
        if(w!=c)return 0; i++; }
    return i==n && name[i]==0;
}

static UINT64 nt_lookup(Ntfs *fs, UINT64 parent, const char *name) {
    UINT8 *rec=NULL;
    if(EFI_ERROR(bs->AllocatePool(EfiLoaderData,fs->record,(VOID **)&rec)))return 0;
    UINT64 max=fs->mft_size/fs->record, found=0;
    for(UINT64 idx=0;idx<max;idx++) {
        if(EFI_ERROR(nt_record(fs,idx,rec)))continue;
        if(!(N16(rec,22)&1))continue;
        UINT16 ao=N16(rec,20);
        for(UINT32 p=ao;p+16<fs->record;) {
            UINT32 type=N32(rec,p); if(type==0xffffffff)break;
            UINT32 al=N32(rec,p+4); if(al<16||p+al>fs->record)break;
            if(type==0x30 && !rec[p+8] && al>=0x5c) {
                UINT8 *v=rec+p+N16(rec,p+20);
                UINT64 pref=N64(v,0)&0x0000ffffffffffffULL;
                UINT8 nl=v[64], ns=v[65];
                if(pref==parent && ns!=2 && nl && (UINT32)66+(UINT32)nl*2<=al-N16(rec,p+20) &&
                   nt_name_eq(v+66,nl,name)) { found=idx; break; }
            }
            p+=al;
        }
        if(found)break;
    }
    bs->FreePool(rec); return found;
}

static EFI_STATUS nt_file_data(Ntfs *fs, UINT64 idx, VOID **file, UINTN *size) {
    UINT8 *rec=NULL; EFI_STATUS s=bs->AllocatePool(EfiLoaderData,fs->record,(VOID **)&rec);
    if(EFI_ERROR(s))return s;
    s=nt_record(fs,idx,rec); if(EFI_ERROR(s)){bs->FreePool(rec);return s;}
    UINT16 ao=N16(rec,20); UINT8 *data=NULL; UINTN dlen=0; UINT64 fsize=0;
    for(UINT32 p=ao;p+16<fs->record;) {
        UINT32 type=N32(rec,p); if(type==0xffffffff)break;
        UINT32 al=N32(rec,p+4); if(al<16||p+al>fs->record)break;
        if(type==0x80) {
            if(rec[p+8]) { UINT16 ro=N16(rec,p+32); if(ro<al){data=rec+p+ro;dlen=al-ro;fsize=N64(rec,p+48);} }
            else { UINT32 vl=N32(rec,p+16); UINT16 vo=N16(rec,p+20); if(vo<=al&&vl<=al-vo){VOID *b=NULL;s=bs->AllocatePool(EfiLoaderData,vl?vl:1,&b);if(!EFI_ERROR(s)){memcpy(b,rec+p+vo,vl);*file=b;*size=vl;bs->FreePool(rec);return EFI_SUCCESS;}} }
            break;
        }
        p+=al;
    }
    if(!data||!dlen||fsize>0xffffffffu){bs->FreePool(rec);return EFI_UNSUPPORTED;}
    VOID *buf=NULL; s=bs->AllocatePool(EfiLoaderData,(UINTN)fsize? (UINTN)fsize:1,&buf);
    if(!EFI_ERROR(s))s=nt_runs_read(fs,data,dlen,0,(UINTN)fsize,buf);
    if(!EFI_ERROR(s)){*file=buf;*size=(UINTN)fsize;}else if(buf)bs->FreePool(buf);
    bs->FreePool(rec);return s;
}

static EFI_STATUS ntfs_load_file(const char *const *path, int depth, VOID **file, UINTN *size) {
    EFI_HANDLE *handles=NULL; UINTN count=0;
    EFI_STATUS s=bs->LocateHandleBuffer(ByProtocol,&gNtfsBlockIoGuid,NULL,&count,&handles);
    if(EFI_ERROR(s)) {
        say(L"ntfs: no Block I/O handles found\\r\\n");
        return s;
    }
    say(L"ntfs: Block I/O handles: ");
    CHAR16 digits[12]; UINTN nd=0; UINTN value=count;
    do { digits[nd++]=(CHAR16)(L'0'+value%10); value/=10; } while(value && nd<12);
    while(nd) { CHAR16 ch[2]={digits[--nd],0}; say(ch); }
    say(L"\\r\\n");
    EFI_STATUS last=EFI_NOT_FOUND;
    for(UINTN i=0;i<count;i++) {
        EFI_BLOCK_IO_PROTOCOL *bio=NULL; Ntfs fs;
        s=bs->HandleProtocol(handles[i],&gNtfsBlockIoGuid,(VOID **)&bio);
        if(EFI_ERROR(s) || !bio || !bio->Media) continue;
        s=nt_mount(&fs,bio);
        if(EFI_ERROR(s)) { last=s; continue; }
        say(L"ntfs: mounted candidate volume\\r\\n");
        UINT64 ino=5; /* NTFS root directory record */
        for(int k=0;k<depth && ino;k++) {
            ino=nt_lookup(&fs,ino,path[k]);
            if(!ino) {
                say(L"ntfs: path component not found: ");
                for (const char *ch=path[k]; *ch; ch++) {
                    CHAR16 one[2]={(CHAR16)(UINT8)*ch,0}; say(one);
                }
                say(L"\\r\\n");
            }
        }
        if(ino) {
            s=nt_file_data(&fs,ino,file,size);
            if(!EFI_ERROR(s)) {
                bs->FreePool(handles);
                return EFI_SUCCESS;
            }
            last=s;
            say(L"ntfs: found path but could not read file data\\r\\n");
        }
        if(fs.mft_runs) bs->FreePool(fs.mft_runs);
    }
    bs->FreePool(handles);
    return last;
}
