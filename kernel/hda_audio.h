#pragma once
#include <stdint.h>
#include "hda.h"

/* Play PCM samples from a build-time downloaded, embedded WAV file. */
#define HDA_SD0_CTL  0x80
#define HDA_SD0_STS  0x83
#define HDA_SD0_LPIB 0x84
#define HDA_SD0_CBL  0x88
#define HDA_SD0_LVI  0x8C
#define HDA_SD0_FMT  0x92
#define HDA_SD0_BDPL 0x98
#define HDA_SD0_BDPU 0x9C
#define HDA_BDL_SEGMENT_BYTES 60000u
#define HDA_BDL_MAX_ENTRIES 256u

extern const uint8_t sentinel_wav_start[];
extern const uint8_t sentinel_wav_end[];

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} HdaBdlEntry;

static uint64_t hda_bdl_pa, hda_pcm_pa;
static uint8_t hda_pin_nid, hda_dac_nid;

static uint16_t wav_u16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t wav_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int hda_get_param(uint8_t nid, uint8_t param, uint32_t *v) {
    return hda_codec_verb(0, nid, 0xF00, param, v);
}
static int hda_get_long(uint8_t nid, uint8_t verb, uint16_t payload, uint32_t *v) {
    return hda_codec_verb16(0, nid, verb, payload, v);
}
static int hda_set_short(uint8_t nid, uint16_t verb, uint8_t payload) {
    uint32_t v=0; return hda_codec_verb(0, nid, verb, payload, &v);
}

/* Accept only little-endian, uncompressed 48 kHz / 16-bit / stereo PCM. */
static int hda_wav_find_pcm(const uint8_t **pcm_out, uint32_t *bytes_out) {
    const uint8_t *wav = sentinel_wav_start;
    uintptr_t total = (uintptr_t)sentinel_wav_end - (uintptr_t)sentinel_wav_start;
    if (total < 44 || wav_u32(wav) != 0x46464952u ||
        wav_u32(wav + 8) != 0x45564157u) {
        DbgPrint("hda-audio: embedded sample is not RIFF/WAVE\n");
        return -1;
    }

    uint32_t fmt_found = 0, data_found = 0;
    uint32_t offset = 12, pcm_offset = 0, pcm_bytes = 0;
    while ((uintptr_t)offset + 8 <= total) {
        const uint8_t *chunk = wav + offset;
        uint32_t size = wav_u32(chunk + 4);
        uint32_t body = offset + 8;
        if ((uintptr_t)body + size > total) {
            DbgPrint("hda-audio: malformed WAV chunk bounds\n");
            return -1;
        }
        if (wav_u32(chunk) == 0x20746d66u) { /* "fmt " */
            if (size < 16 || wav_u16(wav + body) != 1 ||
                wav_u16(wav + body + 2) != 2 ||
                wav_u32(wav + body + 4) != 48000 ||
                wav_u16(wav + body + 14) != 16) {
                DbgPrint("hda-audio: sample must be 48kHz stereo S16 PCM\n");
                return -1;
            }
            fmt_found = 1;
        } else if (wav_u32(chunk) == 0x61746164u) { /* "data" */
            pcm_offset = body;
            pcm_bytes = size;
            data_found = 1;
        }
        offset = body + size + (size & 1u);
    }

    if (!fmt_found || !data_found || !pcm_bytes || (pcm_bytes & 3u)) {
        DbgPrint("hda-audio: WAV is missing PCM fmt/data or has invalid size\n");
        return -1;
    }
    *pcm_out = wav + pcm_offset;
    *bytes_out = pcm_bytes;
    return 0;
}

static int hda_find_output_route(void) {
    uint32_t fg=0, nodes=0, v=0;
    if (hda_codec_verb(0,0,0xF00,0x04,&fg)) return -1;
    uint8_t fg_start=(uint8_t)(fg>>16), fg_count=(uint8_t)fg;
    for (uint8_t fi=0; fi<fg_count; fi++) {
        uint8_t f=(uint8_t)(fg_start+fi);
        if (hda_get_param(f,0x05,&v) || (v&0xff)!=1) continue;
        if (hda_get_param(f,0x04,&nodes)) continue;
        uint8_t first=(uint8_t)(nodes>>16), count=(uint8_t)nodes;
        for (uint16_t j=0;j<count;j++) {
            uint8_t n=(uint8_t)(first+j); uint32_t caps=0;
            if (hda_get_param(n,0x09,&caps)) continue;
            if (((caps>>20)&0xF)!=4) continue;
            uint32_t pin=0;
            if (hda_codec_verb(0,n,0xF00,0x0C,&pin) || !(pin & 0x10)) continue;
            uint32_t lenv=0;
            if (hda_get_param(n,0x0E,&lenv)) continue;
            uint32_t len=lenv&0x7f; if (!len) continue;
            if (len>16) len=16;
            for (uint32_t ci=0;ci<len;ci++) {
                uint32_t ent=0;
                if (hda_codec_verb(0,n,0xF02,(uint8_t)ci,&ent)) break;
                uint8_t conn=(uint8_t)(ent&0x7f);
                uint32_t cc=0;
                if (hda_get_param(conn,0x09,&cc)) continue;
                if (((cc>>20)&0xF)==0) {
                    hda_pin_nid=n; hda_dac_nid=conn;
                    DbgPrint("hda-audio: output pin nid=%u DAC nid=%u\n",n,conn);
                    return 0;
                }
            }
        }
    }
    DbgPrint("hda-audio: no simple pin-to-DAC connection found\n");
    return -1;
}

static int hda_audio_start_wav(void) {
    const uint8_t *source_pcm = 0;
    uint32_t pcm_bytes = 0;
    if (!hda_regs || !hda_corb_pa ||
        hda_wav_find_pcm(&source_pcm, &pcm_bytes) ||
        hda_find_output_route()) return -1;

    uint32_t entries = (pcm_bytes + HDA_BDL_SEGMENT_BYTES - 1) /
                       HDA_BDL_SEGMENT_BYTES;
    if (!entries || entries > HDA_BDL_MAX_ENTRIES) {
        DbgPrint("hda-audio: sample too large for one BDL page (%u bytes)\n", pcm_bytes);
        return -1;
    }

    uint32_t pages = (pcm_bytes + 4095u) / 4096u;
    hda_bdl_pa=pmm_alloc_n(1);
    hda_pcm_pa=pmm_alloc_n(pages);
    if (!hda_bdl_pa || !hda_pcm_pa) {
        DbgPrint("hda-audio: DMA allocation failed\n"); return -1;
    }

    volatile HdaBdlEntry *bdl=(volatile HdaBdlEntry *)(HHDM_BASE+hda_bdl_pa);
    volatile uint8_t *pcm=(volatile uint8_t *)(HHDM_BASE+hda_pcm_pa);
    for (uint32_t i=0; i<pcm_bytes; i++) pcm[i]=source_pcm[i];

    uint32_t remaining=pcm_bytes, offset=0;
    for (uint32_t i=0; i<entries; i++) {
        uint32_t length=remaining > HDA_BDL_SEGMENT_BYTES ?
                        HDA_BDL_SEGMENT_BYTES : remaining;
        /* PCM frame size is four bytes; each segment must preserve alignment. */
        length &= ~3u;
        bdl[i].address=hda_pcm_pa + offset;
        bdl[i].length=length;
        bdl[i].flags=(i+1==entries) ? 1u : 0u; /* IOC on final descriptor */
        offset += length;
        remaining -= length;
    }
    if (remaining != 0 || offset != pcm_bytes) {
        DbgPrint("hda-audio: internal BDL sizing error\n"); return -1;
    }

    if (hda_set_short(hda_pin_nid,0x707,0x40)) return -1; /* output enabled */
    (void)hda_set_short(hda_pin_nid,0x70C,0x02);          /* EAPD enable */
    if (hda_set_short(hda_dac_nid,0x706,0x10)) return -1; /* stream tag 1 */
    uint32_t ignored=0;
    if (hda_get_long(hda_dac_nid,0x2,0x0011,&ignored)) {
        DbgPrint("hda-audio: converter format command failed\n"); return -1;
    }

    hda_write8(HDA_SD0_CTL,0);
    for (volatile uint32_t d=0;d<10000;d++) __asm__ volatile("pause");
    hda_write8(HDA_SD0_STS,0x1C);
    hda_write32(HDA_SD0_BDPL,(uint32_t)hda_bdl_pa);
    hda_write32(HDA_SD0_BDPU,(uint32_t)(hda_bdl_pa>>32));
    hda_write32(HDA_SD0_CBL,pcm_bytes);
    hda_write16(HDA_SD0_LVI,(uint16_t)(entries-1));
    hda_write16(HDA_SD0_FMT,0x0011); /* 48kHz, 16-bit stereo */
    hda_write8(HDA_SD0_CTL+2,0x10); /* stream tag 1 */
    hda_write8(HDA_SD0_CTL,0x02);   /* RUN */
    DbgPrint("hda-audio: embedded WAV playback started: %u PCM bytes, %u BDL entries; SD0CTL=%02x tag=%02x LPIB=%08x STS=%02x\n",
             pcm_bytes,entries,hda_read8(HDA_SD0_CTL),hda_read8(HDA_SD0_CTL+2),
             hda_read32(HDA_SD0_LPIB),hda_read8(HDA_SD0_STS));
    return 0;
}
