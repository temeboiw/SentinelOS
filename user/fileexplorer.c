#include "ntdll_user.h"
void fileexplorer_main(void) {
    NtWriteConsole("fileexplorer: Directory of C:\\System32\n");
    for (u64 i = 0; i < 64; i++) {
        char name[32]; for (int k = 0; k < 32; k++) name[k] = 0;
        NTSTATUS st = NtQueryDirectory(i, name);
        if (st == STATUS_NO_MORE_FILES) break;
        if (st != STATUS_SUCCESS) break;
        NtWriteConsole("C:\\System32\\");
        NtWriteConsole(name);
        NtWriteConsole("\n");
    }
    NtWriteConsole("fileexplorer: done\n");
    NtExit(0);
}
