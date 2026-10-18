#pragma once
#define UP_MAX 8
typedef struct {
    KPROCESS *proc;
    uint64_t entry;
    uint64_t rip, rsp, rflags, rax;
    int alive;
    int fresh;
} UserProc;
static UserProc up_tab[UP_MAX];
static int up_n;
static int up_cur = -1;

static void up_save(Frame *f, int idx) {
    if (idx < 0 || idx >= UP_MAX || !up_tab[idx].alive) return;
    up_tab[idx].rip = f->rip;
    up_tab[idx].rsp = f->rsp;
    up_tab[idx].rflags = f->rflags;
    up_tab[idx].rax = f->rax;
    up_tab[idx].fresh = 0;
}
static void up_load(Frame *f, int idx) {
    UserProc *u = &up_tab[idx];
    if (!u->alive || !u->proc) return;
    if (ki_current) ki_current->Process = u->proc;
    MmSwitchToProcess(u->proc);
    f->cs = USER_CS;
    f->ss = USER_DS;
    if (u->fresh || u->rip < 0x10000) {
        f->rip = u->entry ? u->entry : u->rip;
        f->rsp = USER_STACK_TOP;
        f->rflags = 0x202;
        f->rax = 0;
        u->fresh = 0;
    } else {
        f->rip = u->rip;
        f->rsp = u->rsp;
        f->rflags = u->rflags ? u->rflags : 0x202;
        f->rax = u->rax;
    }
    up_cur = idx;
}
static int up_find_proc(KPROCESS *pr) {
    for (int i = 0; i < UP_MAX; i++)
        if (up_tab[i].alive && up_tab[i].proc == pr) return i;
    return -1;
}
static int up_pick_next(void) {
    int start = (up_cur < 0) ? 0 : up_cur;
    for (int k = 1; k <= UP_MAX; k++) {
        int i = (start + k) % UP_MAX;
        if (up_tab[i].alive && up_tab[i].proc) return i;
    }
    return -1;
}
