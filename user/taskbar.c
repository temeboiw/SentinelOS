
typedef unsigned int u32;
typedef unsigned long long u64;
typedef unsigned short u16;

enum { SYS_NtGetSystemTime = 23 };
typedef struct {
    u16 year, month, day_of_week, day, hour, minute, second, milliseconds;
} TaskbarSystemTime;

/* Query the kernel RTC through the existing system-time syscall. */
static int taskbar_get_system_time(TaskbarSystemTime *out) {
    u64 result;
    __asm__ volatile(
        "mov $23, %%rax\n"
        "mov %[out], %%rdi\n"
        "int $0x80\n"
        "mov %%rax, %[result]"
        : [result] "=r" (result)
        : [out] "r" ((u64)out)
        : "rax", "rdi", "memory", "cc");
    return result == 0;
}

static const unsigned char FONT[40][7] = {
{0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},{0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
{0x0E,0x11,0x10,0x17,0x11,0x11,0x0E},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},{0x01,0x01,0x01,0x01,0x11,0x11,0x0E},
{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
{0x11,0x1B,0x15,0x11,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},
{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
{0x0E,0x11,0x10,0x0E,0x01,0x11,0x0E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},{0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
{0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
{0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
{0x0E,0x11,0x01,0x06,0x08,0x10,0x1F},{0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
{0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},{0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
{0,0,0,0,0,0,0},{0,0,0x04,0,0x04,0,0},
/* '-' and '/' keep the taskbar date legible in the compact bitmap font. */
{0,0,0x1F,0,0,0,0},{0x01,0x02,0x02,0x04,0x08,0x08,0x10},
};

#define COLOR_BG       0xFF1A1A2E
#define COLOR_HEADER   0xFF0F3460
#define COLOR_BAR      0xFF202020
#define COLOR_ACCENT   0xFF0078D4
#define COLOR_START    0xFF202020
#define COLOR_FILES    0xFF2B2B2B
#define COLOR_SEARCH   0xFF171717
#define COLOR_BORDER   0xFF454545
#define COLOR_WHITE    0xFFFFFFFF
#define COLOR_MUTED    0xFFBDBDBD

static int gi(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a';
    if (c >= '0' && c <= '9') return 26 + (c - '0');
    if (c == ':') return 37;
    if (c == '-') return 38;
    if (c == '/') return 39;
    return 36;
}

static void draw_char(
    u32 *fb, u32 pitch, u32 x, u32 y, char ch, u32 col
) {
    const unsigned char *g = FONT[gi(ch)];

    for (int r = 0; r < 7; r++) {
        for (int c = 0; c < 5; c++) {
            if (g[r] & (0x10 >> c))
                fb[(y + r) * pitch + (x + c)] = col;
        }
    }
}

static void draw_text(
    u32 *fb, u32 pitch, u32 x, u32 y,
    const char *s, u32 col
) {
    for (; *s; s++) {
        draw_char(fb, pitch, x, y, *s, col);
        x += 6;
    }
}

static void fill(
    u32 *fb, u32 pitch,
    u32 x, u32 y, u32 w, u32 h, u32 col
) {
    for (u32 yy = y; yy < y + h; yy++) {
        for (u32 xx = x; xx < x + w; xx++) {
            fb[yy * pitch + xx] = col;
        }
    }
}

static void draw_search_bar(
    u32 *fb, u32 pitch, u32 width, u32 y
) {
    /*
     * Search box sits beside START and FILES.
     * Leave room for the C: indicator at the right.
     */
    const u32 x = 204;
    const u32 right_margin = 76;
    const u32 minimum_width = 90;
    const u32 desired_width = 300;

    if (width <= x + right_margin + minimum_width)
        return;

    u32 available = width - x - right_margin;
    u32 box_width = desired_width;

    if (box_width > available)
        box_width = available;

    /* Border and background. */
    fill(fb, pitch, x, y, box_width, 36, COLOR_BORDER);
    fill(fb, pitch, x + 1, y + 1,
         box_width - 2, 34, COLOR_SEARCH);

    /* Simple magnifying-glass icon. */
    fill(fb, pitch, x + 12, y + 11, 9, 9, COLOR_MUTED);
    fill(fb, pitch, x + 14, y + 13, 5, 5, COLOR_SEARCH);
    fill(fb, pitch, x + 20, y + 20, 5, 3, COLOR_MUTED);

    /* Placeholder text. */
    if (box_width >= 112) {
        draw_text(
            fb, pitch,
            x + 34, y + 15,
            "SEARCH APPS",
            COLOR_MUTED
        );
    }
}

void TaskbarDraw(
    u32 *fb, u32 width, u32 height, u32 pitch_px
) {
    if (!fb || width < 64 || height < 64 || pitch_px < width)
        return;

    /* Desktop background. */
    fill(fb, pitch_px, 0, 0, width, height, COLOR_BG);

    /* Top header. */
    fill(fb, pitch_px, 0, 0, width, 28, COLOR_HEADER);
    draw_text(
        fb, pitch_px, 12, 10,
        "MAGIC OS", COLOR_ACCENT
    );

    /* Windows 10-inspired taskbar: dark surface, restrained accent,
       and a dedicated right-aligned notification/clock area. */
    u32 bar_h = 48;
    u32 y0 = height - bar_h;

    fill(fb, pitch_px, 0, y0, width, bar_h, COLOR_BAR);
    fill(fb, pitch_px, 0, y0, width, 2, COLOR_ACCENT);

    /* Start button with a four-pane Windows-style mark. */
    fill(fb, pitch_px, 0, y0 + 2, 48, bar_h - 2, COLOR_START);
    fill(fb, pitch_px, 14, y0 + 15, 8, 7, COLOR_WHITE);
    fill(fb, pitch_px, 24, y0 + 14, 8, 8, COLOR_WHITE);
    fill(fb, pitch_px, 14, y0 + 24, 8, 7, COLOR_WHITE);
    fill(fb, pitch_px, 24, y0 + 24, 8, 7, COLOR_WHITE);

    /* Files shortcut uses a quiet hover-ready surface instead of a bright tile. */
    fill(fb, pitch_px, 48, y0 + 2, 84, bar_h - 2, COLOR_FILES);
    draw_text(fb, pitch_px, 72, y0 + 21, "FILES", COLOR_WHITE);

    /* Search remains fixed-width and cannot collide with the clock area. */
    draw_search_bar(fb, pitch_px, width, y0 + 6);

    /* The time and date are read from the kernel RTC, not hard-coded. */
    fill(fb, pitch_px, width > 96 ? width - 96 : 0, y0 + 2,
         width > 96 ? 96 : width, bar_h - 2, COLOR_SEARCH);
    if (width >= 112) {
        TaskbarSystemTime now;
        char clock_text[6] = {'-', '-', ':', '-', '-', 0};
        char date_text[11] = {'-', '-', '-', '-', '-', '-', '-', '-', '-', '-', 0};
        if (taskbar_get_system_time(&now)) {
            clock_text[0] = (char)('0' + (now.hour / 10) % 10);
            clock_text[1] = (char)('0' + now.hour % 10);
            clock_text[3] = (char)('0' + (now.minute / 10) % 10);
            clock_text[4] = (char)('0' + now.minute % 10);
            date_text[0] = (char)('0' + (now.year / 1000) % 10);
            date_text[1] = (char)('0' + (now.year / 100) % 10);
            date_text[2] = (char)('0' + (now.year / 10) % 10);
            date_text[3] = (char)('0' + now.year % 10);
            date_text[4] = '-';
            date_text[5] = (char)('0' + (now.month / 10) % 10);
            date_text[6] = (char)('0' + now.month % 10);
            date_text[7] = '-';
            date_text[8] = (char)('0' + (now.day / 10) % 10);
            date_text[9] = (char)('0' + now.day % 10);
        }
        draw_text(fb, pitch_px, width - 42, y0 + 10, clock_text, COLOR_WHITE);
        draw_text(fb, pitch_px, width - 68, y0 + 27, date_text, COLOR_MUTED);
    }
}
