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
static uint8_t mouse_pkt[4];
static int mouse_idx;
static int mouse_packet_len = 3;
static int mouse_wheel;
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
        if (mouse_idx < mouse_packet_len) return;
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
        if (mouse_packet_len == 4) {
            int wheel = mouse_pkt[3] & 0x0F;
            if (wheel & 0x08) wheel -= 16;
            /* Keep wheel deltas pending until the input syscall consumes them. */
            mouse_wheel += wheel;
        }

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
    /* IntelliMouse wheel negotiation: sample-rate sequence 200, 100, 80. */
    ps2_cmd(0xD4); ps2_data(0xF3); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(200);  ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(0xF3); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(100);  ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(0xF3); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_cmd(0xD4); ps2_data(80);   ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    /* Read device ID: 0 = standard mouse, 3 = wheel, 4 = 5-button wheel. */
    ps2_cmd(0xD4); ps2_data(0xF2); ps2_wait_read(); (void)ps2_inb(PS2_DATA);
    ps2_wait_read();
    uint8_t mouse_id = ps2_inb(PS2_DATA);
    mouse_packet_len = (mouse_id == 3 || mouse_id == 4) ? 4 : 3;
    DbgPrint("ps2: mouse id=%u packet=%d bytes\\n", mouse_id, mouse_packet_len);
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

static int kbd_shift, kbd_caps;

/* Finnish PC layout, PS/2 Set-1 scancodes. Finnish letters use Latin-1
 * bytes because the current user input ABI carries one byte per key. */
static char ps2_scancode_to_ascii(uint8_t sc) {
    static const unsigned char normal[0x59] = {
        [0x01]=27, [0x02]='1', [0x03]='2', [0x04]='3', [0x05]='4',
        [0x06]='5', [0x07]='6', [0x08]='7', [0x09]='8', [0x0A]='9',
        [0x0B]='0', [0x0C]='+', [0x0D]=0xB4, [0x0E]=8, [0x0F]=9,
        [0x10]='q', [0x11]='w', [0x12]='e', [0x13]='r', [0x14]='t',
        [0x15]='y', [0x16]='u', [0x17]='i', [0x18]='o', [0x19]='p',
        [0x1A]=0xE5, [0x1B]=0xA8, [0x1C]='\n',
        [0x1E]='a', [0x1F]='s', [0x20]='d', [0x21]='f', [0x22]='g',
        [0x23]='h', [0x24]='j', [0x25]='k', [0x26]='l',
        [0x27]=0xF6, [0x28]=0xE4, [0x29]=0xA7, [0x2B]='\'',
        [0x2C]='z', [0x2D]='x', [0x2E]='c', [0x2F]='v', [0x30]='b',
        [0x31]='n', [0x32]='m', [0x33]=',', [0x34]='.', [0x35]='-',
        [0x39]=' ', [0x56]='<'
    };
    static const unsigned char shifted[0x59] = {
        [0x01]=27, [0x02]='!', [0x03]='"', [0x04]='#', [0x05]='$',
        [0x06]='%', [0x07]='&', [0x08]='/', [0x09]='(', [0x0A]=')',
        [0x0B]='=', [0x0C]='?', [0x0D]=0x60, [0x0E]=8, [0x0F]=9,
        [0x10]='Q', [0x11]='W', [0x12]='E', [0x13]='R', [0x14]='T',
        [0x15]='Y', [0x16]='U', [0x17]='I', [0x18]='O', [0x19]='P',
        [0x1A]=0xC5, [0x1B]='^', [0x1C]='\n',
        [0x1E]='A', [0x1F]='S', [0x20]='D', [0x21]='F', [0x22]='G',
        [0x23]='H', [0x24]='J', [0x25]='K', [0x26]='L',
        [0x27]=0xD6, [0x28]=0xC4, [0x29]=0xA7, [0x2B]='*',
        [0x2C]='Z', [0x2D]='X', [0x2E]='C', [0x2F]='V', [0x30]='B',
        [0x31]='N', [0x32]='M', [0x33]=';', [0x34]=':', [0x35]='_',
        [0x39]=' ', [0x56]='>'
    };

    if (sc == 0x2A || sc == 0x36) { kbd_shift = 1; return 0; }
    if (sc == 0xAA || sc == 0xB6) { kbd_shift = 0; return 0; }
    if (sc == 0x3A) { kbd_caps = !kbd_caps; return 0; }
    if (sc & 0x80) return 0;
    if (sc >= 0x59) return 0;

    unsigned char c = (kbd_shift ? shifted : normal)[sc];
    if (kbd_caps && c >= 'a' && c <= 'z')
        c = (unsigned char)(c - 'a' + 'A');
    else if (kbd_caps && c >= 'A' && c <= 'Z')
        c = (unsigned char)(c - 'A' + 'a');
    return (char)c;
}

static int ps2_read_key(char *out) {
    ps2_poll();
    while (kbd_r != kbd_w) {
        uint8_t sc = kbd_buf[kbd_r++ % 64];
        char c = ps2_scancode_to_ascii(sc);
        if (c) {
            *out = c;
            return 1;
        }
    }
    return 0;
}
