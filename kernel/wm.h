#pragma once
#include <stdint.h>

#define WM_MAX 8

typedef struct {
    int pid, minimized, alive;
    int x, y, w, h;
    unsigned long long z;
    char title[40];
} WmEntry;

static WmEntry wm_tab[WM_MAX];
static unsigned long long wm_zseq;

static int wm_find_pid(int pid) {
    for (int i = 0; i < WM_MAX; i++)
        if (wm_tab[i].alive && wm_tab[i].pid == pid) return i;
    return -1;
}

static void wm_register(int pid, const char *title) {
    int i = wm_find_pid(pid);
    if (i < 0) {
        for (i = 0; i < WM_MAX; i++)
            if (!wm_tab[i].alive) break;
        if (i >= WM_MAX) return;
        wm_tab[i] = (WmEntry){0};
        wm_tab[i].alive = 1;
        wm_tab[i].pid = pid;
    }
    wm_tab[i].minimized = 0;
    wm_tab[i].z = ++wm_zseq;
    int k = 0;
    if (title)
        for (; title[k] && k < 39; k++) wm_tab[i].title[k] = title[k];
    wm_tab[i].title[k] = 0;
}

static void wm_set_rect(int pid, int x, int y, int w, int h) {
    int i = wm_find_pid(pid);
    if (i < 0) return;
    wm_tab[i].x = x;
    wm_tab[i].y = y;
    wm_tab[i].w = w;
    wm_tab[i].h = h;
}

static void wm_set_min(int pid, int min) {
    int i = wm_find_pid(pid);
    if (i < 0) return;
    wm_tab[i].minimized = min ? 1 : 0;
    if (!min) wm_tab[i].z = ++wm_zseq;
}

static void wm_unregister(int pid) {
    int i = wm_find_pid(pid);
    if (i >= 0) wm_tab[i].alive = 0;
}

static int wm_query(int index, char *name_out, int *min_out, int *pid_out) {
    int seen = 0;
    for (int i = 0; i < WM_MAX; i++) {
        if (!wm_tab[i].alive) continue;
        if (seen++ != index) continue;
        if (name_out) {
            int k = 0;
            for (; wm_tab[i].title[k] && k < 39; k++)
                name_out[k] = wm_tab[i].title[k];
            name_out[k] = 0;
        }
        if (min_out) *min_out = wm_tab[i].minimized;
        if (pid_out) *pid_out = wm_tab[i].pid;
        return 1;
    }
    return 0;
}

/* Pick the highest-z visible window under the pointer.
   Outside windows (for example, the taskbar), use the frontmost
   non-minimized window so desktop controls still receive clicks. */
static int wm_hit_test(int x, int y) {
    int best = -1;
    unsigned long long best_z = 0;
    for (int i = 0; i < WM_MAX; i++) {
        WmEntry *e = &wm_tab[i];
        if (!e->alive || e->minimized || e->w <= 0 || e->h <= 0)
            continue;
        if (x >= e->x && y >= e->y &&
            x < e->x + e->w && y < e->y + e->h &&
            (best < 0 || e->z > best_z)) {
            best = i;
            best_z = e->z;
        }
    }
    if (best >= 0) return wm_tab[best].pid;

    best = -1;
    best_z = 0;
    for (int i = 0; i < WM_MAX; i++) {
        WmEntry *e = &wm_tab[i];
        if (!e->alive || e->minimized) continue;
        if (best < 0 || e->z > best_z) {
            best = i;
            best_z = e->z;
        }
    }
    return best >= 0 ? wm_tab[best].pid : 0;
}

static void wm_restore_pid(int pid) {
    int i = wm_find_pid(pid);
    if (i >= 0) {
        wm_tab[i].minimized = 0;
        wm_tab[i].z = ++wm_zseq;
    }
}
