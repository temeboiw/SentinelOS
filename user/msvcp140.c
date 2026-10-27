typedef unsigned int DWORD;
typedef int BOOL;
typedef void *HANDLE;
extern void AcquireSRWLockExclusive(void *);
extern void ReleaseSRWLockExclusive(void *);
extern void WakeConditionVariable(void *);
extern void WakeAllConditionVariable(void *);
extern BOOL SleepConditionVariableSRW(void *,void *,DWORD,DWORD);
extern DWORD WaitForSingleObject(HANDLE,DWORD);
extern BOOL GetExitCodeThread(HANDLE,DWORD *);
extern BOOL CloseHandle(HANDLE);
extern DWORD GetCurrentThreadId(void);
#define INFINITE 0xffffffffu
#define MCS(m) ((unsigned char *)(m)+8)
#define CVS(c) ((unsigned char *)(c)+8)
__attribute__((dllexport)) int _Mtx_lock(void *m){if(!m)return 4;AcquireSRWLockExclusive(MCS(m));*(long *)((unsigned char *)m+16)=(long)GetCurrentThreadId();(*(int *)((unsigned char *)m+24))++;return 0;}
__attribute__((dllexport)) int _Mtx_unlock(void *m){if(!m)return 4;int *count=(int *)((unsigned char *)m+24);if(*count>0)(*count)--;if(!*count)*(long *)((unsigned char *)m+16)=-1;ReleaseSRWLockExclusive(MCS(m));return 0;}
__attribute__((dllexport)) int _Cnd_broadcast(void *c){if(!c)return 4;WakeAllConditionVariable(CVS(c));return 0;}
__attribute__((dllexport)) int _Cnd_signal(void *c){if(!c)return 4;WakeConditionVariable(CVS(c));return 0;}
__attribute__((dllexport)) int _Cnd_wait(void *c,void *m){if(!c||!m)return 4;int *count=(int *)((unsigned char *)m+24);if(*count>0)(*count)--;*(long *)((unsigned char *)m+16)=-1;BOOL ok=SleepConditionVariableSRW(CVS(c),MCS(m),INFINITE,0);*(long *)((unsigned char *)m+16)=(long)GetCurrentThreadId();(*count)++;return ok?0:4;}
__attribute__((dllexport)) void _Cnd_do_broadcast_at_thread_exit(void){}
__attribute__((dllexport)) int _Thrd_join(HANDLE thread,int *result){DWORD code=0;if(!thread||WaitForSingleObject(thread,INFINITE)!=0)return 4;if(!GetExitCodeThread(thread,&code))return 4;if(result)*result=(int)code;CloseHandle(thread);return 0;}
__attribute__((dllexport)) DWORD _Thrd_id(HANDLE thread){(void)thread;return GetCurrentThreadId();}
void msvcp_Xout_of_range(const char *message){(void)message;__builtin_trap();}
void msvcp_Xlength_error(const char *message){(void)message;__builtin_trap();}
void msvcp_Throw_Cpp_error(int error){(void)error;__builtin_trap();}
void msvcp_Xbad_function_call(void){__builtin_trap();}
