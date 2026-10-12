#pragma once
// Minimal read-only ext4 reader for the loader.
// Supports: extents, linear directory scan, 32/64-bit group descriptors.
// No journal replay, no checksums, no writes. Include AFTER common.h.
#include <Protocol/BlockIo.h>

static EFI_GUID gBlockIoGuid = {0x964e5b21,0x6459,0x11d2,{0x8e,0x39,0x00,0xa0,0xc9,0x69,0x72,0x3b}};

#define R16(p,o) (*(UINT16 *)((UINT8 *)(p) + (o)))
#define R32(p,o) (*(UINT32 *)((UINT8 *)(p) + (o)))

typedef struct {
    EFI_BLOCK_IO_PROTOCOL *bio;
    UINT32 blksz, ipg, isize, dsize, first_data;
    int    is64;
    UINT8 *blk;                     // scratch block for extent-tree walks
} Ext4;

// byte-granular read on top of sector-granular BlockIO
static EFI_STATUS dread(Ext4 *fs, UINT64 off, UINTN len, void *out) {
    UINT32 ss   = fs->bio->Media->BlockSize;
    UINTN  skip = off % ss;
    UINTN  n    = ((skip + len + ss - 1) / ss) * ss;
    VOID  *tmp;
    EFI_STATUS s = bs->AllocatePool(EfiLoaderData, n, &tmp);
    if (EFI_ERROR(s)) return s;
    s = fs->bio->ReadBlocks(fs->bio, fs->bio->Media->MediaId, off / ss, n, tmp);
    if (!EFI_ERROR(s)) memcpy(out, (UINT8 *)tmp + skip, len);
    bs->FreePool(tmp);
    return s;
}

static EFI_STATUS ext_mount(Ext4 *fs, EFI_BLOCK_IO_PROTOCOL *bio) {
    UINT8 sb[1024];
    fs->bio = bio;
    if (!bio->Media->MediaPresent) return EFI_NO_MEDIA;
    EFI_STATUS s = dread(fs, 1024, 1024, sb);
    if (EFI_ERROR(s)) return s;
    if (R16(sb, 56) != 0xEF53) return EFI_UNSUPPORTED;      // not ext2/3/4

    fs->blksz      = 1024u << R32(sb, 24);
    fs->first_data = R32(sb, 20);
    fs->ipg        = R32(sb, 40);
    fs->isize      = R16(sb, 88);
    fs->is64       = (R32(sb, 96) & 0x80) != 0;             // INCOMPAT_64BIT
    fs->dsize      = fs->is64 ? R16(sb, 254) : 32;
    if (fs->dsize < 32 || fs->dsize > 64 || fs->blksz > 65536 || !fs->ipg) return EFI_UNSUPPORTED;
    return bs->AllocatePool(EfiLoaderData, fs->blksz, (VOID **)&fs->blk);
}

static EFI_STATUS ext_inode(Ext4 *fs, UINT32 ino, UINT8 *out /* >= 256 bytes */) {
    UINT32 g = (ino - 1) / fs->ipg, idx = (ino - 1) % fs->ipg;
    UINT8 gd[64];
    UINT64 gdt = (UINT64)(fs->first_data + 1) * fs->blksz;
    EFI_STATUS s = dread(fs, gdt + (UINT64)g * fs->dsize, fs->dsize, gd);
    if (EFI_ERROR(s)) return s;
    UINT64 itab = R32(gd, 8);
    if (fs->is64) itab |= (UINT64)R32(gd, 0x28) << 32;
    return dread(fs, itab * fs->blksz + (UINT64)idx * fs->isize,
                 fs->isize > 256 ? 256 : fs->isize, out);
}

static UINT64 ext_size(UINT8 *inode) { return R32(inode, 4) | ((UINT64)R32(inode, 108) << 32); }

// logical block -> physical block via the extent tree (0 = hole / error)
static UINT64 ext_map(Ext4 *fs, UINT8 *inode, UINT32 lb) {
    UINT8 *node = inode + 40;                               // i_block
    for (;;) {
        if (R16(node, 0) != 0xF30A) return 0;               // extent header magic
        UINT16 n = R16(node, 2), depth = R16(node, 6);
        UINT8 *e = node + 12;
        if (depth == 0) {
            for (UINT16 i = 0; i < n; i++, e += 12) {
                UINT32 first = R32(e, 0), len = R16(e, 4);
                if (len > 32768) len -= 32768;              // unwritten extent
                if (lb >= first && lb < first + len)
                    return (((UINT64)R16(e, 6) << 32) | R32(e, 8)) + (lb - first);
            }
            return 0;
        }
        UINT64 leaf = 0;
        for (UINT16 i = 0; i < n; i++, e += 12)
            if (R32(e, 0) <= lb) leaf = ((UINT64)R16(e, 8) << 32) | R32(e, 4);
        if (!leaf || EFI_ERROR(dread(fs, leaf * fs->blksz, fs->blksz, fs->blk))) return 0;
        node = fs->blk;
    }
}

static UINT32 ext_lookup(Ext4 *fs, UINT32 dir, const char *name) {
    UINT8 inode[256], *buf;
    if (EFI_ERROR(ext_inode(fs, dir, inode))) return 0;
    if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, fs->blksz, (VOID **)&buf))) return 0;

    UINTN nl = 0; while (name[nl]) nl++;
    UINT64 size = ext_size(inode);
    UINT32 found = 0;

    for (UINT64 pos = 0; pos < size && !found; pos += fs->blksz) {
        UINT64 pb = ext_map(fs, inode, (UINT32)(pos / fs->blksz));
        if (!pb || EFI_ERROR(dread(fs, pb * fs->blksz, fs->blksz, buf))) continue;
        for (UINT32 o = 0; o + 8 <= fs->blksz; ) {
            UINT16 rl = R16(buf, o + 4);
            if (rl < 8) break;
            if (R32(buf, o) && buf[o + 6] == nl) {
                UINTN i = 0;
                while (i < nl && buf[o + 8 + i] == (UINT8)name[i]) i++;
                if (i == nl) { found = R32(buf, o); break; }
            }
            o += rl;
        }
    }
    bs->FreePool(buf);
    return found;
}

static EFI_STATUS ext_read_all(Ext4 *fs, UINT8 *inode, UINT8 *out, UINT64 size) {
    for (UINT64 pos = 0; pos < size; pos += fs->blksz) {
        UINT32 chunk = (size - pos < fs->blksz) ? (UINT32)(size - pos) : fs->blksz;
        UINT64 pb = ext_map(fs, inode, (UINT32)(pos / fs->blksz));
        if (!pb) { memset(out + pos, 0, chunk); continue; }
        EFI_STATUS s = dread(fs, pb * fs->blksz, chunk, out + pos);
        if (EFI_ERROR(s)) return s;
    }
    return EFI_SUCCESS;
}

// scan every block device, load the file at path[0]/path[1]/... from the first ext4 volume that has it
static EFI_STATUS ext_load_file(const char *const *path, int depth, VOID **file, UINTN *size) {
    EFI_HANDLE *h; UINTN n;
    EFI_STATUS s = bs->LocateHandleBuffer(ByProtocol, &gBlockIoGuid, NULL, &n, &h);
    if (EFI_ERROR(s)) return s;

    for (UINTN i = 0; i < n; i++) {
        EFI_BLOCK_IO_PROTOCOL *bio; Ext4 fs;
        if (EFI_ERROR(bs->HandleProtocol(h[i], &gBlockIoGuid, (VOID **)&bio))) continue;
        if (EFI_ERROR(ext_mount(&fs, bio))) continue;

        UINT32 ino = 2;                                     // root inode
        for (int k = 0; k < depth && ino; k++) ino = ext_lookup(&fs, ino, path[k]);
        if (!ino) continue;

        UINT8 inode[256];
        if (EFI_ERROR(ext_inode(&fs, ino, inode))) continue;
        if (!(R32(inode, 32) & 0x80000)) continue;          // requires EXTENTS_FL

        UINT64 sz = ext_size(inode); VOID *buf;
        if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, sz, &buf))) continue;
        if (EFI_ERROR(ext_read_all(&fs, inode, buf, sz))) continue;
        *file = buf; *size = sz;
        return EFI_SUCCESS;
    }
    return EFI_NOT_FOUND;
}