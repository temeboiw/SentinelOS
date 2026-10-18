#pragma once
#include "serial.h"

static inline uint8_t ps2_inb(uint16_t p) {
    uint8_t v; __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(p)); return v;
}
static inline void ps2_outb(uint16_t p, uint8_t v) {
    __asm__ volatile("outb %0, %1" :: "a"(v), "Nd"(p));
}

#define PS2_DATA 0x60
#define PS2_STAT 0x64

static uint8_t  kbd_buf[64];
static unsigned kbd_r, kbd_w;
static int mouse_x = 640, mouse_y = 400;
static int mouse_btn;
static uint8_t mouse_pkt[3];
static int mouse_idx;
static int ps2_ready;
static uint64_t mouse_last_tsc;

static inline uint64_t ps2_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

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
            if (!(b & 0x08)) return;
            if (b & 0xC0) return;
            mouse_pkt[0] = b;
            mouse_idx = 1;
            return;
        }
        mouse_pkt[mouse_idx++] = b;
        if (mouse_idx < 3) return;

        mouse_idx = 0;
        int dx = (int8_t)mouse_pkt[1];
        int dy = (int8_t)mouse_pkt[2];
        if (dx > 25) dx = 25;
        if (dx < -25) dx = -25;
        if (dy > 25) dy = 25;
        if (dy < -25) dy = -25;

        mouse_btn = mouse_pkt[0] & 7;
        if (dx || dy) {
            /*
             * Velocity from real motion: distance / TSC time.
             * Slow glide → 1:1. Fast flick → mild boost. No runaway.
             */
            uint64_t now = ps2_rdtsc();
            uint64_t dt = now - mouse_last_tsc;
            mouse_last_tsc = now;
            if (dt < 1) dt = 1;

            int adx = dx < 0 ? -dx : dx;
            int ady = dy < 0 ? -dy : dy;
            int mag = adx + ady;
            if (mag < 1) mag = 1;

            uint64_t dt_s = dt >> 8;
            if (dt_s < 1) dt_s = 1;
            uint64_t speed = ((uint64_t)mag << 10) / dt_s;

            int scale = 1;
            if (speed > 80)  scale = 2;
            if (speed > 200) scale = 3;
            if (speed > 400) scale = 4;
            if (mag <= 2) scale = 1;   /* precision / touchpad creep */

            mouse_x += dx * scale;
            mouse_y -= dy * scale;
            if (mouse_x < 0) mouse_x = 0;
            if (mouse_y < 0) mouse_y = 0;
            if (mouse_x > 1279) mouse_x = 1279;
            if (mouse_y > 799) mouse_y = 799;
        }
    } else {
        kbd_buf[kbd_w++ % 64] = b;
        mouse_idx = 0;
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
    cfg |= 0x03;
    cfg &= (uint8_t)~0x30;
    ps2_cmd(0x60); ps2_data(cfg);

    ps2_cmd(0xA8);
    ps2_cmd(0xD4); ps2_data(0xF6); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(0xF4); ps2_wait_read(); (void)ps2_inb(PS2_DATA);

    ps2_data(0xF4); ps2_wait_read(); (void)ps2_inb(PS2_DATA);

    ps2_outb(0x21, ps2_inb(0x21) & (uint8_t)~0x06);
    ps2_outb(0xA1, ps2_inb(0xA1) & (uint8_t)~0x10);

    KeConnectInterrupt(0x21, ps2_irq_kbd);
    KeConnectInterrupt(0x2C, ps2_irq_kbd);

    mouse_idx = 0;
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
