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
static int wm_active_pid;

static int wm_find_pid(int pid) {
    for (int i = 0; i < WM_MAX; i++)
        if (wm_tab[i].alive && wm_tab[i].pid == pid) return i;
    return -1;
}

/* The active window is always the highest-z, non-minimized live window. */
static void wm_choose_active(void) {
    int best = -1;
    unsigned long long best_z = 0;
    for (int i = 0; i < WM_MAX; i++) {
        WmEntry *e = &wm_tab[i];
        if (!e->alive || e->minimized) continue;
        if (best < 0 || e->z > best_z) {
            best = i;
            best_z = e->z;
        }
    }
    wm_active_pid = best >= 0 ? wm_tab[best].pid : 0;
}

static void wm_activate_pid(int pid) {
    int i = wm_find_pid(pid);
    if (i < 0 || wm_tab[i].minimized) return;
    wm_tab[i].z = ++wm_zseq;
    wm_active_pid = pid;
}

static int wm_get_active_pid(void) {
    if (wm_find_pid(wm_active_pid) >= 0 &&
        !wm_tab[wm_find_pid(wm_active_pid)].minimized)
        return wm_active_pid;
    wm_choose_active();
    return wm_active_pid;
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
    wm_active_pid = pid;
    int k = 0;
    if (title)
        for (; title[k] && k < 39; k++) wm_tab[i].title[k] = title[k];
    wm_tab[i].title[k] = 0;
}

static void wm_set_rect(int pid, int x, int y, int w, int h) {
    int i = wm_find_pid(pid);
    if (i < 0) return;
    /* Invalid/empty rectangles must never become click targets. */
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    wm_tab[i].x = x;
    wm_tab[i].y = y;
    wm_tab[i].w = w;
    wm_tab[i].h = h;
}

static void wm_set_min(int pid, int min) {
    int i = wm_find_pid(pid);
    if (i < 0) return;
    wm_tab[i].minimized = min ? 1 : 0;
    if (min) {
        if (wm_active_pid == pid) wm_choose_active();
    } else {
        wm_tab[i].z = ++wm_zseq;
        wm_active_pid = pid;
    }
}

static void wm_unregister(int pid) {
    int i = wm_find_pid(pid);
    if (i >= 0) {
        wm_tab[i].alive = 0;
        if (wm_active_pid == pid) wm_choose_active();
    }
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
/* Return the PID of the topmost visible window at a screen coordinate.
 * Use widened edge arithmetic: malformed or extreme coordinates must not
 * overflow x+w/y+h and turn an unrelated window into a click target. */
static int wm_hit_test(int x, int y) {
    int best = -1;
    unsigned long long best_z = 0;
    for (int i = 0; i < WM_MAX; i++) {
        WmEntry *e = &wm_tab[i];
        if (!e->alive || e->minimized || e->w <= 0 || e->h <= 0)
            continue;
        int64_t right = (int64_t)e->x + (int64_t)e->w;
        int64_t bottom = (int64_t)e->y + (int64_t)e->h;
        if ((int64_t)x >= e->x && (int64_t)y >= e->y &&
            (int64_t)x < right && (int64_t)y < bottom &&
            (best < 0 || e->z > best_z)) {
            best = i;
            best_z = e->z;
        }
    }
    if (best >= 0) return wm_tab[best].pid;

    /* Desktop/taskbar input outside all windows is routed to the frontmost
     * non-minimized window by the existing single-owner USER32 model. */
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

/* Enumerate live windows from back to front for the compositor. The caller
 * receives a stable copy, never a pointer into the mutable window table.
 * Rank zero is the bottom-most live window; minimized windows are omitted. */
static int wm_get_z_order(int rank, WmEntry *out) {
    if (rank < 0 || !out) return 0;
    int count = 0;
    for (int i = 0; i < WM_MAX; i++)
        if (wm_tab[i].alive && !wm_tab[i].minimized) count++;
    if (rank >= count) return 0;

    for (int wanted = 0; wanted < count; wanted++) {
        int best = -1;
        unsigned long long best_z = ~0ULL;
        for (int i = 0; i < WM_MAX; i++) {
            WmEntry *e = &wm_tab[i];
            if (!e->alive || e->minimized) continue;
            int already_before = 0;
            for (int j = 0; j < WM_MAX; j++) {
                WmEntry *p = &wm_tab[j];
                if (p->alive && !p->minimized &&
                    (p->z < e->z || (p->z == e->z && j < i)))
                    already_before++;
            }
            if (already_before == wanted &&
                (best < 0 || e->z < best_z)) {
                best = i;
                best_z = e->z;
            }
        }
        if (wanted == rank && best >= 0) {
            *out = wm_tab[best];
            return 1;
        }
    }
    return 0;
}

static void wm_restore_pid(int pid) {
    int i = wm_find_pid(pid);
    if (i >= 0) {
        wm_tab[i].minimized = 0;
        wm_tab[i].z = ++wm_zseq;
        wm_active_pid = pid;
    }
}
