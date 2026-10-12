#include "ntdll_user.h"
typedef void (*TaskbarDrawFn)(unsigned int *, unsigned int, unsigned int, unsigned int);
void explorer_main(void) {
    NtWriteConsole("explorer: starting\n");
    FBInfo fb = {0};
    if (!NtMapFramebuffer(&fb) || !fb.va) { NtWriteConsole("explorer: no framebuffer\n"); NtExit(1); }
    char dll[] = {'t','a','s','k','b','a','r','.','d','l','l',0};
    char fn[]  = {'T','a','s','k','b','a','r','D','r','a','w',0};
    u64 sym = NtGetProcAddress(dll, fn);
    if (!sym) { NtWriteConsole("explorer: TaskbarDraw not found\n"); NtExit(1); }
    NtWriteConsole("explorer: taskbar.dll loaded, drawing\n");
    ((TaskbarDrawFn)sym)((unsigned int *)fb.va, fb.w, fb.h, fb.pitch / 4);
    char name[] = {'f','i','l','e','e','x','p','l','o','r','e','r','.','e','x','e',0};
    NtWriteConsole("explorer: launching fileexplorer.exe\n");
    if (NtCreateProcess(name)) NtWriteConsole("explorer: spawn failed\n");
    else NtWriteConsole("explorer: fileexplorer scheduled\n");
    NtExit(0);
}
