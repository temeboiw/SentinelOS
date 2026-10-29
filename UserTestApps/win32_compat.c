/*
 * Ordinary Windows-ABI Win32 smoke test for SentinelOS.
 *
 * This deliberately imports documented kernel32/user32 entry points rather
 * than calling SentinelOS syscalls or resolving exports through its syscall
 * interface. It is freestanding only to avoid depending on a CRT in this
 * first compatibility test; the PE import table and API ABI are Windows-style.
 */
typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef unsigned long long ULONG_PTR;
typedef long long LONG_PTR;
typedef int BOOL;
typedef void *HANDLE;
typedef void *HINSTANCE;
typedef void *HWND;
typedef const char *LPCSTR;
typedef char *LPSTR;

typedef LONG_PTR (*WNDPROC)(HWND, UINT, ULONG_PTR, LONG_PTR);

typedef struct {
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HANDLE hIcon;
    HANDLE hCursor;
    HANDLE hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA;

typedef struct {
    HWND hwnd;
    UINT message;
    unsigned int _padding;
    ULONG_PTR wParam;
    LONG_PTR lParam;
    DWORD time;
    long x;
    long y;
} MSG;

__declspec(dllimport) unsigned short RegisterClassA(const WNDCLASSA *);
__declspec(dllimport) LONG_PTR DefWindowProcA(HWND, UINT, ULONG_PTR, LONG_PTR);
__declspec(dllimport) HWND CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD,
    int, int, int, int, HWND, HANDLE, HINSTANCE, void *);
__declspec(dllimport) BOOL ShowWindow(HWND, int);
__declspec(dllimport) BOOL UpdateWindow(HWND);
__declspec(dllimport) BOOL GetMessageA(MSG *, HWND, UINT, UINT);
__declspec(dllimport) BOOL TranslateMessage(const MSG *);
__declspec(dllimport) LONG_PTR DispatchMessageA(const MSG *);
__declspec(dllimport) BOOL DestroyWindow(HWND);
__declspec(dllimport) void PostQuitMessage(int);
__declspec(dllimport) void ExitProcess(UINT);

static int window_destroyed;

static LONG_PTR test_window_proc(HWND hwnd, UINT message,
                                 ULONG_PTR wParam, LONG_PTR lParam) {
    if (message == 0x0002u) { /* WM_DESTROY */
        window_destroyed = 1;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, message, wParam, lParam);
}

void win32_compat_main(void) {
    static const char class_name[] = "SentinelWin32CompatSmoke";
    static const char title[] = "Win32 ABI Compatibility Test";
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = test_window_proc;
    wc.hInstance = (HINSTANCE)0;
    wc.lpszClassName = class_name;

    if (!RegisterClassA(&wc)) ExitProcess(10);
    HWND window = CreateWindowExA(0, class_name, title, 0x00CF0000u,
        80, 80, 480, 300, (HWND)0, (HANDLE)0, wc.hInstance, (void *)0);
    if (!window) ExitProcess(11);
    ShowWindow(window, 5);
    UpdateWindow(window);

    MSG message = {0};
    for (;;) {
        BOOL result = GetMessageA(&message, (HWND)0, 0, 0);
        if (result == 0) break;
        if (result < 0) ExitProcess(12);
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    /* Closing from the title bar already delivered WM_DESTROY. Avoid calling
       DestroyWindow a second time with a stale window handle in that path. */
    if (!window_destroyed) DestroyWindow(window);
    ExitProcess((UINT)message.wParam);
}
