typedef unsigned int DWORD;
typedef int BOOL;
static DWORD comdlg_error;
__attribute__((dllexport)) BOOL GetOpenFileNameA(void *ofn){(void)ofn;comdlg_error=0xFFFFu;return 0;}
__attribute__((dllexport)) BOOL GetSaveFileNameA(void *ofn){(void)ofn;comdlg_error=0xFFFFu;return 0;}
__attribute__((dllexport)) DWORD CommDlgExtendedError(void){return comdlg_error;}
