#include "ntdll_user.h"

typedef unsigned int DWORD;
typedef struct { u32 hwnd, message; u64 wParam; i64 lParam; u32 time; int pt_x, pt_y; } MSG;
typedef void *(*CreateWindowExA_t)(u32,const char*,const char*,u32,int,int,int,int,void*,void*,void*,void*);
typedef int (*ShowWindow_t)(void*,int);
typedef int (*GetMessageA_t)(MSG*,void*,u32,u32);
typedef int (*DispatchMessageA_t)(const MSG*);
typedef int (*TextOutA_t)(void*,int,int,const char*,int);
typedef u64 (*SetTimer_t)(void*,u64,u32,void*);
typedef int (*InvalidateRect_t)(void*,void*,int);

static CreateWindowExA_t pCreate;
static ShowWindow_t pShow;
static GetMessageA_t pGet;
static DispatchMessageA_t pDispatch;
static TextOutA_t pText;
static SetTimer_t pTimer;
static InvalidateRect_t pInvalidate;
static void *g_window;

static void put_u32(char *out, u32 value) {
    char digits[11]; int n=0;
    if (!value) { out[0]='0'; out[1]=0; return; }
    while (value && n < (int)sizeof(digits)) { digits[n++]=(char)('0'+value%10); value/=10; }
    int i=0; while(n) out[i++]=digits[--n]; out[i]=0;
}
static int append(char *out,int at,int cap,const char *s) {
    int i=0; while(s && s[i] && at<cap-1) out[at++]=s[i++]; out[at]=0; return at;
}
static void draw_processes(void) {
    if (!pText || !g_window) return;
    pText(g_window,0,0,"Task Manager - Processes", -1);
    pText(g_window,0,2,"PID     PARENT  STATE     IMAGE", -1);
    pText(g_window,0,3,"-----------------------------------------------", -1);
    /* Process rows are retained by USER32. Clear the full list region before
       writing a shorter snapshot so exited processes do not remain visible. */
    char blank[80];
    for (int i=0;i<(int)sizeof(blank)-1;i++) blank[i]=' ';
    blank[sizeof(blank)-1]=0;
    for (int i=0;i<8;i++) pText(g_window,0,4+i,blank,-1);
    NT_PROCESS_SNAPSHOT rows[8];
    u64 count=NtQueryProcessList(rows,8);
    if ((i64)count < 0) {
        pText(g_window,0,5,"Kernel process snapshot failed.",-1);
        return;
    }
    if(count>8)count=8;
    for(u64 i=0;i<count;i++) {
        char line[112]; int n=0; char number[16];
        line[0]=0;
        put_u32(number,rows[i].pid); n=append(line,n,sizeof(line),number);
        while(n<8)n=append(line,n,sizeof(line)," ");
        put_u32(number,rows[i].parent_pid); n=append(line,n,sizeof(line),number);
        while(n<16)n=append(line,n,sizeof(line)," ");
        n=append(line,n,sizeof(line),rows[i].state==1?"Starting  ":"Running   ");
        n=append(line,n,sizeof(line),rows[i].image_name[0]?rows[i].image_name:"unknown.exe");
        pText(g_window,0,4+(int)i,line,-1);
    }
    char footer[64]="Processes: "; char number[16]; put_u32(number,(u32)count); append(footer,11,sizeof(footer),number);
    pText(g_window,0,14,footer,-1);
}

void mainCRTStartup(void) {
    const char dll[]="user32.dll";
    pCreate=(CreateWindowExA_t)NtGetProcAddress(dll,"CreateWindowExA");
    pShow=(ShowWindow_t)NtGetProcAddress(dll,"ShowWindow");
    pGet=(GetMessageA_t)NtGetProcAddress(dll,"GetMessageA");
    pDispatch=(DispatchMessageA_t)NtGetProcAddress(dll,"DispatchMessageA");
    pText=(TextOutA_t)NtGetProcAddress(dll,"TextOutA");
    pTimer=(SetTimer_t)NtGetProcAddress(dll,"SetTimer");
    pInvalidate=(InvalidateRect_t)NtGetProcAddress(dll,"InvalidateRect");
    if(!pCreate||!pGet||!pText) {
        NtWriteConsole("taskmgr: required USER32 exports are unavailable\n");
        NtExit(1); return;
    }
    g_window=pCreate(0,"SentinelTaskManager","Task Manager",0x10CF0000u,
                     110,70,650,440,0,0,0,0);
    if(!g_window){NtWriteConsole("taskmgr: CreateWindowExA failed\n");NtExit(2);return;}
    if(pShow)pShow(g_window,1);
    if(pTimer)(void)pTimer(g_window,1,1000,0);
    draw_processes();
    NtWriteConsole("taskmgr: live kernel process list opened\n");
    MSG msg;
    for(;;) {
        if(!pGet(&msg,0,0,0))continue;
        if(pDispatch)(void)pDispatch(&msg);
        if(msg.message==0x0113u) {
            draw_processes();
            if(pInvalidate)(void)pInvalidate(g_window,0,0);
        } else if(msg.message==0x000Fu) {
            draw_processes();
        }
    }
}
