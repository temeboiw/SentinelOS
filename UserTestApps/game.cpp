/*
 * SentinelOS UserTestApps: standalone user-mode maze game.
 * Freestanding C++: no CRT, C++ runtime, exceptions, or imported DLL calls.
 * All USER32 entry points are resolved through SentinelOS GetProcAddress.
 */
using u64 = unsigned long long;
using u32 = unsigned int;
using BOOL = int;
using LPCSTR = const char *;

enum : u64 {
    SYS_TERMINATE_PROCESS = 0,
    SYS_WRITE_FILE = 1,
    SYS_GET_PROC_ADDRESS = 9
};

static u64 syscall3(u64 number, u64 a, u64 b, u64 c) {
    u64 result;
    __asm__ volatile(
        "mov %[n], %%rax\n"
        "mov %[a], %%rdi\n"
        "mov %[b], %%rsi\n"
        "mov %[c], %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[r]"
        : [r] "=r"(result)
        : [n] "r"(number), [a] "r"(a), [b] "r"(b), [c] "r"(c)
        : "rax", "rdi", "rsi", "rdx", "memory", "cc");
    return result;
}

static void *get_proc(const char *dll, const char *name) {
    return reinterpret_cast<void *>(syscall3(
        SYS_GET_PROC_ADDRESS, reinterpret_cast<u64>(dll),
        reinterpret_cast<u64>(name), 0));
}

static void debug_print(const char *text) {
    u64 length = 0;
    while (text[length]) ++length;
    (void)syscall3(SYS_WRITE_FILE, 1, reinterpret_cast<u64>(text), length);
}

using CreateWindowExA_t = void *(*)(u32, LPCSTR, LPCSTR, u32, int, int, int, int,
                                    void *, void *, void *, void *);
using ShowWindow_t = BOOL (*)(void *, int);
using UpdateWindow_t = BOOL (*)(void *);
using TextOutA_t = BOOL (*)(void *, int, int, LPCSTR, int);
using GetMessageA_t = BOOL (*)(void *, void *, u32, u32);
using TranslateMessage_t = BOOL (*)(const void *);
using DispatchMessageA_t = long long (*)(const void *);

struct GameMessage {
    u32 hwnd;
    u32 message;
    u64 wParam;
    long long lParam;
    u32 time;
    int x;
    int y;
};

static constexpr int MAP_W = 30;
static constexpr int MAP_H = 12;
static char world[MAP_H][MAP_W];
static int player_x = 1, player_y = 1;
static int enemy_x = 15, enemy_y = 6;
static int score = 0, moves = 0;
static int finished = 0, won = 0;
static void *window_handle;
static TextOutA_t text_out;

static void put_wall(int x, int y, int w, int h) {
    for (int yy = y; yy < y + h; ++yy)
        for (int xx = x; xx < x + w; ++xx)
            if (xx > 0 && xx < MAP_W - 1 && yy > 0 && yy < MAP_H - 1)
                world[yy][xx] = '#';
}

static void place_coin(int x, int y) {
    if (world[y][x] == ' ') world[y][x] = '.';
}

static void initialize_world() {
    for (int y = 0; y < MAP_H; ++y)
        for (int x = 0; x < MAP_W; ++x)
            world[y][x] = (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1)
                              ? '#' : ' ';

    put_wall(5, 2, 1, 5);
    put_wall(9, 1, 1, 5);
    put_wall(13, 4, 1, 6);
    put_wall(18, 1, 1, 5);
    put_wall(22, 5, 1, 6);
    put_wall(25, 2, 1, 4);
    put_wall(3, 8, 6, 1);
    put_wall(8, 9, 4, 1);
    put_wall(16, 8, 4, 1);
    put_wall(24, 9, 3, 1);

    place_coin(3, 1);  place_coin(7, 1);  place_coin(11, 2);
    place_coin(15, 1); place_coin(21, 2); place_coin(27, 3);
    place_coin(2, 5);  place_coin(7, 6);  place_coin(11, 7);
    place_coin(16, 6); place_coin(20, 4); place_coin(27, 7);
    place_coin(2, 10); place_coin(7, 10); place_coin(12, 10);
    place_coin(17, 10); place_coin(21, 10); place_coin(27, 10);
    world[10][28] = 'E';
}

static void draw_text(int x, int y, const char *s) {
    if (text_out && window_handle) (void)text_out(window_handle, x, y, s, -1);
}

static void draw_game() {
    char line[MAP_W + 1];
    char status[96];
    int p = 0;
    const char *prefix = "SENTINEL MAZE  |  Score: ";
    while (prefix[p]) { status[p] = prefix[p]; ++p; }
    int n = score;
    if (n >= 100) status[p++] = (char)('0' + (n / 100) % 10);
    if (n >= 10) status[p++] = (char)('0' + (n / 10) % 10);
    status[p++] = (char)('0' + n % 10);
    const char *suffix = "  |  WASD move  |  ESC quit";
    for (int i = 0; suffix[i] && p < 94; ++i) status[p++] = suffix[i];
    status[p] = 0;
    draw_text(0, 0, "SentinelOS UserTestApps");
    draw_text(0, 1, status);
    draw_text(0, 2, "Collect the dots (.) and reach E. Avoid the moving X.");
    draw_text(0, 3, "");

    for (int y = 0; y < MAP_H; ++y) {
        for (int x = 0; x < MAP_W; ++x) {
            char c = world[y][x];
            if (x == player_x && y == player_y) c = '@';
            else if (x == enemy_x && y == enemy_y) c = 'X';
            line[x] = c;
        }
        line[MAP_W] = 0;
        draw_text(0, 4 + y, line);
    }

    if (finished) {
        if (won) draw_text(0, 17, "YOU WIN! Press ESC to close the game.");
        else draw_text(0, 17, "CAUGHT! Press ESC to close the game.");
    } else {
        draw_text(0, 17, "@ = you   X = chaser   # = wall   . = collectible   E = exit");
    }
    draw_text(0, 19, "Standalone PE executable loaded from the SentinelOS disk.");
}

static int can_walk(int x, int y) {
    return x > 0 && x < MAP_W - 1 && y > 0 && y < MAP_H - 1 &&
           world[y][x] != '#';
}

static void move_enemy() {
    if ((moves & 1) != 0) return;
    int dx = player_x - enemy_x;
    int dy = player_y - enemy_y;
    int nx = enemy_x, ny = enemy_y;
    if (dx < 0) nx--; else if (dx > 0) nx++;
    if (can_walk(nx, ny)) enemy_x = nx;
    else {
        nx = enemy_x;
        if (dy < 0) ny--; else if (dy > 0) ny++;
        if (can_walk(nx, ny)) enemy_y = ny;
    }
    if (enemy_x == player_x && enemy_y == player_y) {
        finished = 1;
        won = 0;
    }
}

static void move_player(int dx, int dy) {
    if (finished) return;
    int nx = player_x + dx, ny = player_y + dy;
    if (!can_walk(nx, ny)) return;
    player_x = nx;
    player_y = ny;
    ++moves;
    if (world[ny][nx] == '.') {
        world[ny][nx] = ' ';
        ++score;
    }
    if (world[ny][nx] == 'E') {
        finished = 1;
        won = 1;
    }
    move_enemy();
    draw_game();
}

extern "C" void game_mainCRTStartup() {
    auto create_window = reinterpret_cast<CreateWindowExA_t>(
        get_proc("user32.dll", "CreateWindowExA"));
    auto show_window = reinterpret_cast<ShowWindow_t>(
        get_proc("user32.dll", "ShowWindow"));
    auto update_window = reinterpret_cast<UpdateWindow_t>(
        get_proc("user32.dll", "UpdateWindow"));
    text_out = reinterpret_cast<TextOutA_t>(get_proc("user32.dll", "TextOutA"));
    auto get_message = reinterpret_cast<GetMessageA_t>(
        get_proc("user32.dll", "GetMessageA"));
    auto translate_message = reinterpret_cast<TranslateMessage_t>(
        get_proc("user32.dll", "TranslateMessage"));
    auto dispatch_message = reinterpret_cast<DispatchMessageA_t>(
        get_proc("user32.dll", "DispatchMessageA"));

    if (!create_window || !show_window || !text_out || !get_message ||
        !dispatch_message) {
        debug_print("game.exe: required USER32 functions are unavailable\n");
        (void)syscall3(SYS_TERMINATE_PROCESS, 1, 0, 0);
        for (;;) __asm__ volatile("pause");
    }

    initialize_world();
    window_handle = create_window(0, "SentinelGame", "SentinelOS - Maze",
                                  0, 100, 70, 680, 440,
                                  0, 0, 0, 0);
    if (!window_handle) {
        debug_print("game.exe: CreateWindowExA failed\n");
        (void)syscall3(SYS_TERMINATE_PROCESS, 1, 0, 0);
        for (;;) __asm__ volatile("pause");
    }

    (void)show_window(window_handle, 1);
    if (update_window) (void)update_window(window_handle);
    draw_game();
    debug_print("game.exe: started; WASD moves, Escape exits\n");

    GameMessage msg{};
    for (;;) {
        if (!get_message(&msg, nullptr, 0, 0)) {
            (void)syscall3(SYS_TERMINATE_PROCESS, 0, 0, 0);
            for (;;) __asm__ volatile("pause");
        }
        if (translate_message) (void)translate_message(&msg);
        (void)dispatch_message(&msg);

        if (msg.message == 0x0010 || msg.message == 0x0012) {
            (void)syscall3(SYS_TERMINATE_PROCESS, 0, 0, 0);
            for (;;) __asm__ volatile("pause");
        }
        if (msg.message != 0x0100) continue;

        switch ((unsigned int)msg.wParam) {
            case 0x57: case 0x77: move_player(0, -1); break; /* W */
            case 0x41: case 0x61: move_player(-1, 0); break; /* A */
            case 0x53: case 0x73: move_player(0, 1); break;  /* S */
            case 0x44: case 0x64: move_player(1, 0); break;  /* D */
            case 0x1B:
                (void)syscall3(SYS_TERMINATE_PROCESS, 0, 0, 0);
                for (;;) __asm__ volatile("pause");
        }
    }
}
