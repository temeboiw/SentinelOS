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

static void ps2_wait_write(void) {
    for (int i = 0; i < 100000; i++)
        if (!(ps2_inb(PS2_STAT) & 2)) return;
}
static void ps2_wait_read(void) {
    for (int i = 0; i < 100000; i++)
        if (ps2_inb(PS2_STAT) & 1) return;
}
static void ps2_cmd(uint8_t c) { ps2_wait_write(); ps2_outb(PS2_STAT, c); }
static void ps2_data(uint8_t c) { ps2_wait_write(); ps2_outb(PS2_DATA, c); }

static void ps2_handle_byte(uint8_t b) {
    if (ps2_inb(PS2_STAT) & 0x20) {
        if (mouse_idx == 0 && !(b & 0x08)) return;
        mouse_pkt[mouse_idx++] = b;
        if (mouse_idx >= 3) {
            mouse_idx = 0;
            int dx = (int8_t)mouse_pkt[1];
            int dy = (int8_t)mouse_pkt[2];
            if (mouse_pkt[0] & 0x10) dx = (int)(dx | 0xFFFFFF00);
            if (mouse_pkt[0] & 0x20) dy = (int)(dy | 0xFFFFFF00);
            if (dx > 40) dx = 40;
            if (dx < -40) dx = -40;
            if (dy > 40) dy = 40;
            if (dy < -40) dy = -40;
            mouse_btn = mouse_pkt[0] & 7;
            if (dx || dy) {
                mouse_x += dx * 2;
                mouse_y -= dy * 2;
                if (mouse_x < 0) mouse_x = 0;
                if (mouse_y < 0) mouse_y = 0;
                if (mouse_x > 1279) mouse_x = 1279;
                if (mouse_y > 799) mouse_y = 799;
            }
        }
    } else {
        kbd_buf[kbd_w++ % 64] = b;
    }
}

static void ps2_poll(void) {
    while (ps2_inb(PS2_STAT) & 1)
        ps2_handle_byte(ps2_inb(PS2_DATA));
}

static void ps2_irq_kbd(Frame *f) {
    (void)f;
    while (ps2_inb(PS2_STAT) & 1)
        ps2_handle_byte(ps2_inb(PS2_DATA));
}

static int ps2_init(void) {
    ps2_cmd(0xAD); ps2_cmd(0xA7);
    while (ps2_inb(PS2_STAT) & 1) (void)ps2_inb(PS2_DATA);
    ps2_cmd(0x20); ps2_wait_read();
    uint8_t cfg = ps2_inb(PS2_DATA);
    cfg |= 0x03; cfg &= (uint8_t)~0x30;
    ps2_cmd(0x60); ps2_data(cfg);
    ps2_cmd(0xA8);
    ps2_cmd(0xD4); ps2_data(0xF6); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(0xF4); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_data(0xF4); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_outb(0x21, ps2_inb(0x21) & (uint8_t)~0x06);
    ps2_outb(0xA1, ps2_inb(0xA1) & (uint8_t)~0x10);
    KeConnectInterrupt(0x21, ps2_irq_kbd);
    KeConnectInterrupt(0x2C, ps2_irq_kbd);
    ps2_ready = 1;
    DbgPrint("ps2: keyboard+mouse ready (IRQ1/IRQ12)\n");
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
