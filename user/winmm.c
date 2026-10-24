/*
 * SentinelOS WinMM compatibility layer.
 * timeGetTime returns the OS monotonic millisecond tick count, matching the
 * wraparound behavior of the WinMM DWORD API.
 */
typedef unsigned int DWORD;
__attribute__((dllexport)) DWORD timeGetTime(void) {
    unsigned long long tick = 24;
    __asm__ volatile("int $0x80"
        : "+a"(tick)
        : "D"((unsigned long long)0), "S"((unsigned long long)0),
          "d"((unsigned long long)0)
        : "memory", "cc");
    if (tick == (unsigned long long)-1) return 0;
    return (DWORD)tick;
}
