#pragma once
#include <stdint.h>
#include "hda.h"

/* First PCM milestone: 48 kHz, signed 16-bit stereo sine tone through HDA DMA.
 * The QEMU WAV backend records this stream as build/sentinel-audio.wav.
 */
#define HDA_SD0_CTL  0x80
#define HDA_SD0_STS  0x83
#define HDA_SD0_LPIB 0x84
#define HDA_SD0_CBL  0x88
#define HDA_SD0_LVI  0x8C
#define HDA_SD0_FMT  0x92
#define HDA_SD0_BDPL 0x98
#define HDA_SD0_BDPU 0x9C
#define HDA_PCM_FRAMES 48000
#define HDA_PCM_BYTES (HDA_PCM_FRAMES * 4)

typedef struct __attribute__((packed)) {
    uint64_t address;
    uint32_t length;
    uint32_t flags;
} HdaBdlEntry;

static uint64_t hda_bdl_pa, hda_pcm_pa;
static uint8_t hda_pin_nid, hda_dac_nid;

static int hda_get_param(uint8_t nid, uint8_t param, uint32_t *v) {
    return hda_codec_verb(0, nid, 0xF00, param, v);
}
static int hda_get_long(uint8_t nid, uint8_t verb, uint16_t payload, uint32_t *v) {
    return hda_codec_verb16(0, nid, verb, payload, v);
}
static int hda_set_short(uint8_t nid, uint16_t verb, uint8_t payload) {
    uint32_t v=0; return hda_codec_verb(0, nid, verb, payload, &v);
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
        /* First pass: find an output pin (widget type 4). */
        for (uint16_t j=0;j<count;j++) {
            uint8_t n=(uint8_t)(first+j); uint32_t caps=0;
            if (hda_get_param(n,0x09,&caps)) continue;
            uint8_t type=(caps>>20)&0xF;
            if (type!=4) continue;
            uint32_t pin=0;
            if (hda_codec_verb(0,n,0xF00,0x0C,&pin)) continue;
            if (!(pin & 0x10)) continue; /* no output capability */
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

static void hda_fill_test_tone(volatile int16_t *pcm) {
    /* 440 Hz, 48 kHz, stereo, ~1 second; integer triangle approximation
       avoids libm in the freestanding kernel. */
    for (uint32_t i=0;i<HDA_PCM_FRAMES;i++) {
        uint32_t phase=(i*440u)%48000u;
        int32_t tri = phase < 12000 ? (int32_t)phase*4 :
                      phase < 36000 ? 48000-(int32_t)phase*4 :
                      (int32_t)phase*4-192000;
        int16_t sample=(int16_t)((tri*9000)/48000);
        pcm[i*2]=sample; pcm[i*2+1]=sample;
    }
}

static int hda_audio_start_test_tone(void) {
    if (!hda_regs || !hda_corb_pa || hda_find_output_route()) return -1;
    hda_bdl_pa=pmm_alloc_n(1);
    hda_pcm_pa=pmm_alloc_n((HDA_PCM_BYTES+4095)/4096);
    if (!hda_bdl_pa || !hda_pcm_pa) {
        DbgPrint("hda-audio: DMA allocation failed\n"); return -1;
    }
    volatile HdaBdlEntry *bdl=(volatile HdaBdlEntry *)(HHDM_BASE+hda_bdl_pa);
    volatile int16_t *pcm=(volatile int16_t *)(HHDM_BASE+hda_pcm_pa);
    /* Keep every BDL segment <= 64 KiB; split the 192,000-byte buffer
       into three aligned 64,000-byte descriptors. */
    for (uint32_t i=0; i<3; i++) {
        bdl[i].address=hda_pcm_pa + (uint64_t)i*64000;
        bdl[i].length=64000;
        bdl[i].flags=(i==2) ? 1 : 0; /* IOC on final descriptor */
    }
    hda_fill_test_tone(pcm);

    /* Configure output pin, EAPD if present, and DAC format/stream tag. */
    if (hda_set_short(hda_pin_nid,0x707,0x40)) return -1; /* output enabled */
    (void)hda_set_short(hda_pin_nid,0x70C,0x02);          /* EAPD enable */
    if (hda_set_short(hda_dac_nid,0x706,0x10)) return -1; /* stream tag 1 */
    /* SET_STREAM_FORMAT is a long verb; 0x0011 = 48kHz, 16-bit stereo. */
    uint32_t ignored=0;
    if (hda_get_long(hda_dac_nid,0x2,0x0011,&ignored)) {
        DbgPrint("hda-audio: converter format command failed\n"); return -1;
    }

    /* Stop SD0 and program the three-entry looping buffer descriptor list. */
    hda_write8(HDA_SD0_CTL,0);
    for (volatile uint32_t d=0;d<10000;d++) __asm__ volatile("pause");
    hda_write8(HDA_SD0_STS,0x1C);
    hda_write32(HDA_SD0_BDPL,(uint32_t)hda_bdl_pa);
    hda_write32(HDA_SD0_BDPU,(uint32_t)(hda_bdl_pa>>32));
    hda_write32(HDA_SD0_CBL,HDA_PCM_BYTES);
    hda_write16(HDA_SD0_LVI,2);
    hda_write16(HDA_SD0_FMT,0x0011);
    hda_write8(HDA_SD0_CTL+2,0x10); /* stream tag 1 */
    hda_write8(HDA_SD0_CTL,0x02); /* RUN */
    DbgPrint("hda-audio: tone stream started, 48kHz stereo S16 (%u bytes); SD0CTL=%02x tag=%02x LPIB=%08x STS=%02x\n",
             HDA_PCM_BYTES,hda_read8(HDA_SD0_CTL),hda_read8(HDA_SD0_CTL+2),
             hda_read32(HDA_SD0_LPIB),hda_read8(HDA_SD0_STS));
    return 0;
}
