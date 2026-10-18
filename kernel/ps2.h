#pragma once
/* PS/2 keyboard + mouse – status sampled BEFORE data (AUX bit). */
#include "serial.h"

#define PS2_DATA 0x60
#define PS2_STAT 0x64

static inline uint8_t ps2_inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static inline void ps2_outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}

static uint8_t  kbd_buf[64];
static unsigned kbd_r, kbd_w;
static int mouse_x = 640, mouse_y = 400;
static int mouse_btn;
static uint8_t mouse_pkt[3];
static int mouse_idx;
static int ps2_ready;
static int mouse_pkt_count;

static void ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(ps2_inb(PS2_STAT) & 2)) return;
}
static void ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (ps2_inb(PS2_STAT) & 1) return;
}
static void ps2_cmd(uint8_t c)  { ps2_wait_write(); ps2_outb(PS2_STAT, c); }
static void ps2_data(uint8_t c) { ps2_wait_write(); ps2_outb(PS2_DATA, c); }

static void ps2_handle_byte(uint8_t b, int is_mouse) {
    if (is_mouse) {
        if (mouse_idx == 0) {
            /* bit3 must be 1 in first byte of a valid 3-byte packet */
            if ((b & 0x08) == 0) return;
            mouse_pkt[0] = b;
            mouse_idx = 1;
            return;
        }
        mouse_pkt[mouse_idx++] = b;
        if (mouse_idx < 3) return;
        mouse_idx = 0;

        int dx = (int8_t)mouse_pkt[1];
        int dy = (int8_t)mouse_pkt[2];
        /* overflow bits */
        if (mouse_pkt[0] & 0x40) dx = 0;
        if (mouse_pkt[0] & 0x80) dy = 0;

        /* VNC deltas are tiny – scale up */
        int scale = 3;
        mouse_x += dx * scale;
        mouse_y -= dy * scale; /* PS/2 Y is opposite screen Y */
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x > 1279) mouse_x = 1279;
        if (mouse_y > 799) mouse_y = 799;
        mouse_btn = mouse_pkt[0] & 0x07;

        mouse_pkt_count++;
        if (mouse_pkt_count == 1 || (mouse_pkt_count % 50) == 0)
            DbgPrint("ps2: mouse pkt#%d dx=%d dy=%d -> (%d,%d)\n",
                     mouse_pkt_count, dx, dy, mouse_x, mouse_y);
    } else {
        kbd_buf[kbd_w++ % 64] = b;
        mouse_idx = 0; /* resync mouse if stream mixed */
    }
}

static void ps2_poll(void) {
    for (int n = 0; n < 64; n++) {
        uint8_t st = ps2_inb(PS2_STAT);
        if (!(st & 1)) break;
        int is_mouse = (st & 0x20) != 0;
        uint8_t b = ps2_inb(PS2_DATA);
        ps2_handle_byte(b, is_mouse);
    }
}

static void ps2_irq_kbd(Frame *f) {
    (void)f;
    ps2_poll();
}

static int ps2_init(void) {
    ps2_cmd(0xAD);
    ps2_cmd(0xA7);
    while (ps2_inb(PS2_STAT) & 1) (void)ps2_inb(PS2_DATA);

    ps2_cmd(0x20); ps2_wait_read();
    uint8_t cfg = ps2_inb(PS2_DATA);
    cfg |= 0x03;           /* IRQ1 + IRQ12 */
    cfg &= (uint8_t)~0x30;
    cfg |= 0x02;           /* clock mouse enable in config often bit1 */
    ps2_cmd(0x60); ps2_data(cfg);

    /* mouse port on */
    ps2_cmd(0xA8);
    /* defaults */
    ps2_cmd(0xD4); ps2_data(0xF6); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    /* sample rate 100 */
    ps2_cmd(0xD4); ps2_data(0xF3); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(100);  ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    /* enable streaming */
    ps2_cmd(0xD4); ps2_data(0xF4); ps2_wait_read(); (void)ps2_inb(PS2_DATA);

    /* keyboard enable scanning */
    ps2_data(0xF4); ps2_wait_read(); (void)ps2_inb(PS2_DATA);

    /* unmask PIC IRQ1 + IRQ12 */
    ps2_outb(0x21, ps2_inb(0x21) & (uint8_t)~0x02);
    ps2_outb(0x21, ps2_inb(0x21) & (uint8_t)~0x04); /* cascade */
    ps2_outb(0xA1, ps2_inb(0xA1) & (uint8_t)~0x10);

    KeConnectInterrupt(0x21, ps2_irq_kbd);
    KeConnectInterrupt(0x2C, ps2_irq_kbd);

    mouse_idx = 0;
    mouse_pkt_count = 0;
    ps2_ready = 1;
    DbgPrint("ps2: keyboard+mouse ready (synced)\n");
    return 1;
}

static char ps2_scancode_to_ascii(uint8_t sc) {
    static const char map[0x40] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,
        'q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,'\\',
        'z','x','c','v','b','n','m',',','.','/',0,'*',0,' ',0
    };
    if (sc & 0x80) return 0;
    if (sc < 0x40) return map[sc];
    return 0;
}

static int ps2_read_key(char *out) {
    ps2_poll();
    if (kbd_r == kbd_w) return 0;
    uint8_t sc = kbd_buf[kbd_r++ % 64];
    char c = ps2_scancode_to_ascii(sc);
    if (!c) return 0;
    *out = c;
    return 1;
}
