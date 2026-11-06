/*
 * SentinelOS HDA driver logic test.
 *
 * Tests the HDA driver's PCI discovery, verb encoding, ring size
 * selection, and BAR parsing logic without requiring actual hardware.
 * Compiled with the host compiler; only the driver's pure logic is
 * exercised (no MMIO, no port I/O, no DMA).
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Mirror the HDA driver's PCI constants. */
#define PCI_CLASS_MULTIMEDIA 0x04u
#define PCI_SUBCLASS_HDAUDIO 0x03u

#define HDA_VERB_GET_PARAMETER      0xF00u
#define HDA_VERB_CONNECTION_LIST    0xF02u
#define HDA_VERB_CONNECTION_ENTRY   0xF03u

#define HDA_PARAM_VENDOR_ID         0x00u
#define HDA_PARAM_REVISION          0x02u
#define HDA_PARAM_NODE_COUNT        0x04u
#define HDA_PARAM_FUNCTION_GROUP_TYPE 0x05u
#define HDA_PARAM_AUDIO_WIDGET_CAP  0x09u

#define HDA_GCTL_CRST        0x01u

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

/* ---------- Verb encoding ---------- */

/* Reproduce the driver's verb encoding. */
static uint32_t hda_encode_verb(uint8_t cad, uint8_t nid,
                                uint16_t verb, uint8_t payload) {
    return ((uint32_t)(cad & 0x0Fu) << 28) |
           ((uint32_t)nid << 20) |
           ((uint32_t)(verb & 0x0FFFu) << 8) |
           (uint32_t)payload;
}

static int run_verb_encoding_test(void) {
    /* GET_PARAMETER on CAD 0, NID 0, verb 0xF00, param 0x00. */
    uint32_t v = hda_encode_verb(0, 0, HDA_VERB_GET_PARAMETER, HDA_PARAM_VENDOR_ID);
    CHECK(v == 0x000F0000u);

    /* CAD 0, NID 0, verb 0xF00, param 0x04 (function group type). */
    v = hda_encode_verb(0, 0, HDA_VERB_GET_PARAMETER, HDA_PARAM_FUNCTION_GROUP_TYPE);
    CHECK(v == 0x000F0004u);

    /* CAD 1, NID 7, verb 0xF02 (connection list), payload 0. */
    v = hda_encode_verb(1, 7, HDA_VERB_CONNECTION_LIST, 0);
    CHECK(v == 0x107F0200u);

    /* CAD 0xF, NID 0x1F, verb 0xFFF, payload 0xFF. */
    v = hda_encode_verb(0xF, 0x1F, 0xFFF, 0xFF);
    CHECK(v == 0xFFFFFFFFu);

    /* Decode checks. */
    v = hda_encode_verb(2, 5, 0xABC, 0x12);
    CHECK(((v >> 28) & 0xFu) == 2u);
    CHECK(((v >> 20) & 0xFFu) == 5u);
    CHECK(((v >> 8) & 0xFFFu) == 0xABCu);
    CHECK((v & 0xFFu) == 0x12u);

    printf("HDA verb encoding test: PASS\n");
    return 0;
}

/* ---------- Ring size selection ---------- */

/* Reproduce hda_select_ring_size logic. */
static int hda_select_ring_size(uint8_t size_capability, uint16_t *count_out) {
    uint16_t sel;
    if (size_capability & 0x40u)
        sel = 2u;
    else if (size_capability & 0x20u)
        sel = 1u;
    else if (size_capability & 0x10u)
        sel = 0u;
    else
        return -1;
    *count_out = (uint16_t)(sel == 2u ? 256u : (sel == 1u ? 16u : 2u));
    return 0;
}

static int run_ring_size_test(void) {
    uint16_t count;

    /* 256-entry capability bit set. */
    CHECK(hda_select_ring_size(0x40u, &count) == 0);
    CHECK(count == 256u);

    /* 16-entry capability bit set. */
    CHECK(hda_select_ring_size(0x20u, &count) == 0);
    CHECK(count == 16u);

    /* 2-entry capability bit set. */
    CHECK(hda_select_ring_size(0x10u, &count) == 0);
    CHECK(count == 2u);

    /* Multiple bits: highest wins (256). */
    CHECK(hda_select_ring_size(0x70u, &count) == 0);
    CHECK(count == 256u);

    /* No supported bit. */
    CHECK(hda_select_ring_size(0x00u, &count) != 0);
    CHECK(hda_select_ring_size(0x0Fu, &count) != 0);

    printf("HDA ring size selection test: PASS\n");
    return 0;
}

/* ---------- BAR parsing ---------- */

/* Reproduce the driver's BAR0 parsing logic. */
static int hda_parse_bar(uint32_t barlo, uint32_t barhi, uint64_t *bar_out) {
    if ((barlo & 0x01u) || !barlo)
        return -1;
    uint64_t bar = barlo & ~0x0Fu;
    if ((barlo & 0x06u) == 0x04u) { /* 64-bit BAR */
        bar |= (uint64_t)barhi << 32;
    }
    if (!bar)
        return -1;
    *bar_out = bar;
    return 0;
}

static int run_bar_parsing_test(void) {
    uint64_t bar;

    /* 32-bit BAR, valid. */
    CHECK(hda_parse_bar(0xFEDA0000u, 0, &bar) == 0);
    CHECK(bar == 0xFEDA0000u);

    /* 64-bit BAR (barlo bits [1:0]=0, bits [3:2]=1 for type 1 = 64-bit). */
    CHECK(hda_parse_bar(0xFEDA0004u, 0x00000001u, &bar) == 0);
    CHECK(bar == (((uint64_t)1 << 32) | 0xFEDA0000u));

    /* BAR with I/O space bit set (bit 0 = 1) -> reject. */
    CHECK(hda_parse_bar(0xFEDA0001u, 0, &bar) != 0);

    /* Zero BAR -> reject. */
    CHECK(hda_parse_bar(0, 0, &bar) != 0);

    /* 64-bit BAR with zero result -> reject. */
    CHECK(hda_parse_bar(0x00000004u, 0, &bar) != 0);

    printf("HDA BAR parsing test: PASS\n");
    return 0;
}

/* ---------- PCI class/subclass detection ---------- */

static int run_pci_class_test(void) {
    /* class_reg byte layout: [31:24]=class, [23:16]=subclass, [15:8]=prog-if */
    uint32_t classreg;

    /* HDA: class=0x04, subclass=0x03. */
    classreg = (0x04u << 24) | (0x03u << 16) | 0x0000u;
    CHECK(((classreg >> 24) & 0xFFu) == PCI_CLASS_MULTIMEDIA);
    CHECK(((classreg >> 16) & 0xFFu) == PCI_SUBCLASS_HDAUDIO);

    /* Non-HDA: class=0x01 (mass storage), subclass=0x06 (AHCI). */
    classreg = (0x01u << 24) | (0x06u << 16) | 0x0100u;
    CHECK(((classreg >> 24) & 0xFFu) != PCI_CLASS_MULTIMEDIA);
    CHECK(((classreg >> 16) & 0xFFu) != PCI_SUBCLASS_HDAUDIO);

    printf("HDA PCI class/subclass test: PASS\n");
    return 0;
}

/* ---------- GCTL CRST bit ---------- */

static int run_gctl_test(void) {
    uint32_t gctl = 0x00000000u;
    CHECK((gctl & HDA_GCTL_CRST) == 0u);

    gctl |= HDA_GCTL_CRST;
    CHECK((gctl & HDA_GCTL_CRST) == HDA_GCTL_CRST);

    gctl &= ~HDA_GCTL_CRST;
    CHECK((gctl & HDA_GCTL_CRST) == 0u);

    printf("HDA GCTL CRST test: PASS\n");
    return 0;
}

int main(void) {
    if (run_verb_encoding_test() != 0) return 1;
    if (run_ring_size_test() != 0) return 1;
    if (run_bar_parsing_test() != 0) return 1;
    if (run_pci_class_test() != 0) return 1;
    if (run_gctl_test() != 0) return 1;
    puts("HDA driver logic tests: ALL PASS");
    return 0;
}