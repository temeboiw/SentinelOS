typedef unsigned long long u64;
enum { SYS_Exit=0, SYS_Write=1, SYS_Create=4, SYS_Yield=21 };
static u64 sy3(u64 n, u64 a, u64 b, u64 c) {
    u64 r;
    __asm__ volatile(
        "mov %[n], %%rax\nmov %[a], %%rdi\nmov %[b], %%rsi\nmov %[c], %%rdx\nint $0x80\nmov %%rax, %[r]"
        : [r]"=r"(r) : [n]"r"(n),[a]"r"(a),[b]"r"(b),[c]"r"(c)
        : "rax","rdi","rsi","rdx","memory","cc");
    return r;
}
static void puts(const char *s) {
    u64 n = 0; while (s[n]) n++;
    sy3(SYS_Write, 1, (u64)s, n);
}
void smss_main(void) {
    puts("smss: Session Manager started\n");
    puts("smss: starting bootvid.exe\n");
    char bootvid_name[] = { 98,111,111,116,118,105,100,46,101,120,101,0 };
    u64 bootvid_pid = sy3(SYS_Create, (u64)bootvid_name, 0, 0);
    if (bootvid_pid == 0 || bootvid_pid == (u64)-1 || bootvid_pid == (u64)-2) {
        puts("smss: ERROR unable to start bootvid.exe\n");
        /* continue to start the shell even if bootvid failed */
    }
    puts("smss: starting explorer.exe\n");
    char explorer_name[] = { 101,120,112,108,111,114,101,114,46,101,120,101,0 };
    u64 explorer_pid = sy3(SYS_Create, (u64)explorer_name, 0, 0);
    if (explorer_pid == 0 || explorer_pid == (u64)-1 || explorer_pid == (u64)-2) {
        puts("smss: ERROR unable to start explorer.exe\n");
    }
    puts("smss: session manager staying alive\n");
    for (;;) (void)sy3(SYS_Yield, 0, 0, 0);
}
