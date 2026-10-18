#include "ntdll_user.h"
typedef unsigned int u32; typedef long long i64;
typedef struct { u32 hwnd, message; u64 wParam; i64 lParam; u32 time; int pt_x, pt_y; } MSG;
typedef void *(*CreateWindowExA_t)(u32, const char *, const char *, u32, int, int, int, int, void *, void *, void *, void *);
typedef int (*ShowWindow_t)(void *, int);
typedef int (*GetMessageA_t)(MSG *, void *, u32, u32);
typedef int (*TranslateMessage_t)(const MSG *);
typedef i64 (*DispatchMessageA_t)(const MSG *);
typedef int (*TextOutA_t)(void *, int, int, const char *, int);
typedef void (*DrawTaskbar_t)(void);
typedef int (*InvalidateRect_t)(void *, void *, int);
typedef int (*UpdateWindow_t)(void *);
static int g_launched;
static int hit_icon(int x, int y) {
    return x >= 28 && x < 100 && y >= 64 && y < 150;
}
void explorer_main(void) {
    NtWriteConsole("explorer: desktop shell\n");
    char u32n[] = {'u','s','e','r','3','2','.','d','l','l',0};
    CreateWindowExA_t pCreate = (CreateWindowExA_t)NtGetProcAddress(u32n, "CreateWindowExA");
    ShowWindow_t pShow = (ShowWindow_t)NtGetProcAddress(u32n, "ShowWindow");
    GetMessageA_t pGet = (GetMessageA_t)NtGetProcAddress(u32n, "GetMessageA");
    TranslateMessage_t pTrans = (TranslateMessage_t)NtGetProcAddress(u32n, "TranslateMessage");
    DispatchMessageA_t pDisp = (DispatchMessageA_t)NtGetProcAddress(u32n, "DispatchMessageA");
    TextOutA_t pText = (TextOutA_t)NtGetProcAddress(u32n, "TextOutA");
    DrawTaskbar_t pBar = (DrawTaskbar_t)NtGetProcAddress(u32n, "DrawTaskbar");
    InvalidateRect_t pInv = (InvalidateRect_t)NtGetProcAddress(u32n, "InvalidateRect");
    UpdateWindow_t pUpd = (UpdateWindow_t)NtGetProcAddress(u32n, "UpdateWindow");
    if (!pGet) { NtWriteConsole("explorer: no user32\n"); for(;;) __asm__ volatile("pause"); }
    void *hwnd = pCreate ? pCreate(0, "Desktop", "Desktop", 0, 0, 0, 1, 1, 0, 0, 0, 0) : 0;
    if (pShow) pShow(hwnd, 1);
    if (pText) { pText(0,0,4,"FILE EXPLORER",-1); pText(0,0,5,"(click the cube)",-1); }
    if (pBar) pBar(); if (pInv) pInv(0,0,1); if (pUpd) pUpd(hwnd);
    NtWriteConsole("explorer: File Explorer icon ready\n");
    MSG msg;
    for (;;) {
        if (!pGet(&msg,0,0,0)) continue;
        if (pTrans) pTrans(&msg); if (pDisp) pDisp(&msg);
        if (msg.message == 0x0201 && !g_launched && hit_icon(msg.pt_x, msg.pt_y)) {
            NtWriteConsole("explorer: open File Explorer\n");
            char name[] = {'f','i','l','e','e','x','p','l','o','r','e','r','.','e','x','e',0};
            u64 pid = NtCreateProcess(name);
            if (pid != 0 && pid != (u64)-1) { g_launched = 1; NtWriteConsole("explorer: fileexplorer running\n"); }
        }
    }
}
