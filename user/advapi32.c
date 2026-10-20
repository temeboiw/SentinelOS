/* advapi32.dll - bounded in-memory registry + basic identity/token compatibility.
 * Registry data currently lives for the process lifetime; durable hive I/O is
 * intentionally not faked because SentinelOS has no registry-backed storage yet.
 */
typedef unsigned long long u64;
typedef unsigned int DWORD;
typedef unsigned short WORD;
typedef long LONG;
typedef LONG LSTATUS;
typedef void *HKEY;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef unsigned char BYTE;
#define E __attribute__((dllexport))
#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_ACCESS_DENIED 5L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_INVALID_PARAMETER 87L
#define ERROR_MORE_DATA 234L
#define ERROR_NO_MORE_ITEMS 259L
#define ERROR_ALREADY_EXISTS 183L
#define ERROR_NOT_ENOUGH_MEMORY 8L
#define REG_NONE 0u
#define REG_SZ 1u
#define REG_BINARY 3u
#define REG_DWORD 4u
#define REG_QWORD 11u
#define KEY_CLASSES_ROOT ((u64)0x80000000u)
#define KEY_CURRENT_USER ((u64)0x80000001u)
#define KEY_LOCAL_MACHINE ((u64)0x80000002u)
#define KEY_USERS ((u64)0x80000003u)
#define KEY_CURRENT_CONFIG ((u64)0x80000005u)
#define REG_KEY_MAX 96
#define REG_VALUE_MAX 192
#define REG_PATH_MAX 192
#define REG_NAME_MAX 64
#define REG_DATA_MAX 256
#define REG_HANDLE_BASE 0x10000u

typedef struct { int used; char path[REG_PATH_MAX]; } RegKey;
typedef struct { int used; int key; char name[REG_NAME_MAX]; DWORD type,size; BYTE data[REG_DATA_MAX]; } RegValue;
static RegKey reg_keys[REG_KEY_MAX];
static RegValue reg_values[REG_VALUE_MAX];
static int reg_initialized;

static int reg_lower(char c){return c>='A'&&c<='Z'?c+32:c;}
static int reg_eq(const char*a,const char*b) {
    if(!a||!b)return 0;
    while(*a&&*b){if(reg_lower(*a++)!=reg_lower(*b++))return 0;}
    return *a==0&&*b==0;
}
static unsigned reg_len(const char*s){unsigned n=0;if(s)while(s[n])n++;return n;}
static void reg_copy(char*d,const char*s,unsigned cap) {
    unsigned i=0;if(!cap)return;
    if(s)while(s[i]&&i+1<cap){d[i]=s[i];i++;}d[i]=0;
}
static void reg_init(void) {
    if(reg_initialized)return;reg_initialized=1;
    const char *roots[]={"HKCR","HKCU","HKLM","HKU","HKCC"};
    for(int i=0;i<5;i++){reg_keys[i].used=1;reg_copy(reg_keys[i].path,roots[i],REG_PATH_MAX);}
}
static int reg_root(u64 h) {
    if(h==KEY_CLASSES_ROOT)return 0;
    if(h==KEY_CURRENT_USER)return 1;
    if(h==KEY_LOCAL_MACHINE)return 2;
    if(h==KEY_USERS)return 3;
    if(h==KEY_CURRENT_CONFIG)return 4;
    return -1;
}
static int reg_key_index(HKEY h) {
    reg_init();u64 v=(u64)h;
    int root=reg_root(v);if(root>=0)return root;
    if(v<REG_HANDLE_BASE)return -1;
    u64 i=v-REG_HANDLE_BASE;
    if(i>=REG_KEY_MAX||!reg_keys[i].used)return -1;
    return (int)i;
}
static int reg_find_key(const char *path) {
    for(int i=0;i<REG_KEY_MAX;i++)if(reg_keys[i].used&&reg_eq(reg_keys[i].path,path))return i;
    return -1;
}
static int reg_join(int parent,const char *sub,char *out,unsigned cap) {
    if(parent<0||parent>=REG_KEY_MAX||!reg_keys[parent].used||!out||!cap)return -1;
    const char *p=reg_keys[parent].path;unsigned n=reg_len(p),i=0;
    if(n>=cap)return -1;
    for(unsigned j=0;j<n;j++)out[j]=p[j];
    while(sub&&(*sub=='\\\\'||*sub=='/'))sub++;
    if(sub&&*sub) {
        if(n+1>=cap)return -1;out[n++]='\\\\';
        while(sub[i]&&n+1<cap){char c=sub[i++];out[n++]=(c=='/'? '\\\\':c);}
        if(sub[i])return -1;
    }
    out[n]=0;return 0;
}
static int reg_make_key(const char *path) {
    int existing=reg_find_key(path);if(existing>=0)return existing;
    for(int i=5;i<REG_KEY_MAX;i++)if(!reg_keys[i].used) {
        reg_keys[i].used=1;reg_copy(reg_keys[i].path,path,REG_PATH_MAX);return i;
    }
    return -1;
}
static int reg_direct_child(const char *parent,const char *candidate,char *out,unsigned cap) {
    unsigned p=reg_len(parent);
    if(!candidate||!parent)return 0;
    if(!p || !((candidate[p]=='\\\\') && reg_eqn: 0)) return 0;
    return 0;
}
/* Compare a candidate registry path with parent, returning the next immediate
 * child component. This avoids exposing grandchildren as direct children. */
static int reg_child_name(const char *parent,const char *candidate,char *out,unsigned cap) {
    unsigned p=reg_len(parent),c=reg_len(candidate);
    if(c<=p||!reg_eq(parent,(char[REG_PATH_MAX]){0})) { /* no-op branch avoids locale APIs */ }
    for(unsigned i=0;i<p;i++)if(reg_lower(parent[i])!=reg_lower(candidate[i]))return 0;
    if(candidate[p]!='\\\\')return 0;
    unsigned i=p+1,n=0;
    if(!candidate[i])return 0;
    while(candidate[i]&&candidate[i]!='\\\\') {
        if(n+1>=cap)return 0;out[n++]=candidate[i++];
    }
    out[n]=0;
    return n>0;
}
static int reg_value_find(int key,const char *name) {
    for(int i=0;i<REG_VALUE_MAX;i++)
        if(reg_values[i].used&&reg_values[i].key==key&&reg_eq(reg_values[i].name,name?name:""))return i;
    return -1;
}

E LSTATUS RegOpenKeyExA(HKEY h,LPCSTR sub,DWORD options,DWORD access,HKEY *out) {
    (void)options;(void)access;reg_init();
    if(!out)return ERROR_INVALID_PARAMETER;*out=0;
    int parent=reg_key_index(h);if(parent<0)return ERROR_INVALID_HANDLE;
    char path[REG_PATH_MAX];
    if(reg_join(parent,sub?sub:"",path,sizeof path)<0)return ERROR_INVALID_PARAMETER;
    int k=reg_find_key(path);if(k<0)return ERROR_FILE_NOT_FOUND;
    *out=(HKEY)(u64)(REG_HANDLE_BASE+(u64)k);return ERROR_SUCCESS;
}
E LSTATUS RegCreateKeyExA(HKEY h,LPCSTR sub,DWORD reserved,LPSTR class_name,DWORD options,DWORD access,void *security,HKEY *out,DWORD *disposition) {
    (void)reserved;(void)class_name;(void)options;(void)access;(void)security;reg_init();
    if(!out)return ERROR_INVALID_PARAMETER;*out=0;
    int parent=reg_key_index(h);if(parent<0)return ERROR_INVALID_HANDLE;
    char path[REG_PATH_MAX];
    if(reg_join(parent,sub?sub:"",path,sizeof path)<0)return ERROR_INVALID_PARAMETER;
    int k=reg_find_key(path);DWORD disp=1;
    if(k<0){k=reg_make_key(path);disp=2;}
    if(k<0)return ERROR_NOT_ENOUGH_MEMORY;
    *out=(HKEY)(u64)(REG_HANDLE_BASE+(u64)k);if(disposition)*disposition=disp;
    return ERROR_SUCCESS;
}
E LSTATUS RegQueryValueExA(HKEY h,LPCSTR name,DWORD *reserved,DWORD *type,BYTE *data,DWORD *bytes) {
    (void)reserved;int k=reg_key_index(h);if(k<0)return ERROR_INVALID_HANDLE;
    int v=reg_value_find(k,name?name:"");if(v<0)return ERROR_FILE_NOT_FOUND;
    if(type)*type=reg_values[v].type;
    if(!bytes)return data?ERROR_INVALID_PARAMETER:ERROR_SUCCESS;
    DWORD cap=*bytes;*bytes=reg_values[v].size;
    if(!data)return ERROR_SUCCESS;
    if(cap<reg_values[v].size){for(DWORD i=0;i<cap;i++)data[i]=reg_values[v].data[i];return ERROR_MORE_DATA;}
    for(DWORD i=0;i<reg_values[v].size;i++)data[i]=reg_values[v].data[i];
    return ERROR_SUCCESS;
}
E LSTATUS RegSetValueExA(HKEY h,LPCSTR name,DWORD reserved,DWORD type,const BYTE *data,DWORD size) {
    (void)reserved;int k=reg_key_index(h);if(k<0)return ERROR_INVALID_HANDLE;
    if(size>REG_DATA_MAX||(!data&&size))return ERROR_INVALID_PARAMETER;
    int v=reg_value_find(k,name?name:"");
    if(v<0)for(int i=0;i<REG_VALUE_MAX;i++)if(!reg_values[i].used){v=i;break;}
    if(v<0)return ERROR_NOT_ENOUGH_MEMORY;
    reg_values[v].used=1;reg_values[v].key=k;reg_copy(reg_values[v].name,name?name:"",REG_NAME_MAX);
    reg_values[v].type=type;reg_values[v].size=size;
    for(DWORD i=0;i<size;i++)reg_values[v].data[i]=data[i];
    return ERROR_SUCCESS;
}
E LSTATUS RegCloseKey(HKEY h) {return reg_key_index(h)<0?ERROR_INVALID_HANDLE:ERROR_SUCCESS;}
E LSTATUS RegDeleteValueA(HKEY h,LPCSTR name) {
    int k=reg_key_index(h);if(k<0)return ERROR_INVALID_HANDLE;
    int v=reg_value_find(k,name?name:"");if(v<0)return ERROR_FILE_NOT_FOUND;
    reg_values[v].used=0;return ERROR_SUCCESS;
}
E LSTATUS RegDeleteKeyA(HKEY h,LPCSTR sub) {
    int parent=reg_key_index(h);if(parent<0)return ERROR_INVALID_HANDLE;
    char path[REG_PATH_MAX];if(reg_join(parent,sub?sub:"",path,sizeof path)<0)return ERROR_INVALID_PARAMETER;
    int k=reg_find_key(path);if(k<5)return ERROR_FILE_NOT_FOUND;
    for(int i=5;i<REG_KEY_MAX;i++)if(reg_keys[i].used&&i!=k) {
        char child[REG_NAME_MAX];if(reg_child_name(path,reg_keys[i].path,child,sizeof child))return ERROR_ACCESS_DENIED;
    }
    for(int i=0;i<REG_VALUE_MAX;i++)if(reg_values[i].used&&reg_values[i].key==k)reg_values[i].used=0;
    reg_keys[k].used=0;return ERROR_SUCCESS;
}
E LSTATUS RegEnumKeyExA(HKEY h,DWORD index,LPSTR name,DWORD *name_len,DWORD *reserved,LPSTR class_name,DWORD *class_len,void *last_write) {
    (void)reserved;(void)class_name;(void)class_len;(void)last_write;
    int k=reg_key_index(h);if(k<0)return ERROR_INVALID_HANDLE;
    if(!name_len)return ERROR_INVALID_PARAMETER;
    char found[REG_NAME_MAX];DWORD count=0;
    for(int i=5;i<REG_KEY_MAX;i++)if(reg_keys[i].used&&reg_child_name(reg_keys[k].path,reg_keys[i].path,found,sizeof found)) {
        if(count++==index){DWORD need=reg_len(found);if(*name_len<=need){*name_len=need;return ERROR_MORE_DATA;}reg_copy(name,found,*name_len);*name_len=need;return ERROR_SUCCESS;}
    }
    return ERROR_NO_MORE_ITEMS;
}
E LSTATUS RegEnumValueA(HKEY h,DWORD index,LPSTR name,DWORD *name_len,DWORD *reserved,DWORD *type,BYTE *data,DWORD *data_len) {
    (void)reserved;int k=reg_key_index(h);if(k<0)return ERROR_INVALID_HANDLE;
    DWORD count=0;
    for(int i=0;i<REG_VALUE_MAX;i++)if(reg_values[i].used&&reg_values[i].key==k) {
        if(count++!=index)continue;
        DWORD need=reg_len(reg_values[i].name);
        if(name_len) {
            if(name && *name_len<=need){*name_len=need;return ERROR_MORE_DATA;}
            if(name)reg_copy(name,reg_values[i].name,*name_len);
            *name_len=need;
        }
        if(type)*type=reg_values[i].type;
        if(data_len) {
            DWORD cap=*data_len;*data_len=reg_values[i].size;
            if(data&&cap<reg_values[i].size){for(DWORD j=0;j<cap;j++)data[j]=reg_values[i].data[j];return ERROR_MORE_DATA;}
            if(data)for(DWORD j=0;j<reg_values[i].size;j++)data[j]=reg_values[i].data[j];
        }
        return ERROR_SUCCESS;
    }
    return ERROR_NO_MORE_ITEMS;
}

typedef struct { DWORD LowPart; LONG HighPart; } LUID_LOCAL;
typedef struct { DWORD Attributes; LUID_LOCAL Luid; } LUID_AND_ATTRIBUTES_LOCAL;
typedef struct { DWORD PrivilegeCount; LUID_AND_ATTRIBUTES_LOCAL Privileges[1]; } TOKEN_PRIVILEGES_LOCAL;
E int OpenProcessToken(void *process,DWORD access,void **token) {
    (void)process;(void)access;if(token)*token=0;return 0; /* no security-token object model yet */
}
E int LookupPrivilegeValueA(LPCSTR system,LPCSTR name,void *luid) {
    (void)system;if(!name||!luid)return 0;
    LUID_LOCAL *l=(LUID_LOCAL *)luid;l->LowPart=0;l->HighPart=0;return 0;
}
E int AdjustTokenPrivileges(void *token,int disable,void *new_state,DWORD length,void *previous,DWORD *returned) {
    (void)token;(void)disable;(void)new_state;(void)length;(void)previous;if(returned)*returned=0;return 0;
}
E int GetUserNameA(LPSTR name,DWORD *size) {
    const char s[]="Public";DWORD n=sizeof(s);
    if(!size)return 0;
    if(!name||*size<n){*size=n;return 0;}
    for(DWORD i=0;i<n;i++)name[i]=s[i];*size=n-1;return 1;
}
E int GetComputerNameA(LPSTR name,DWORD *size) {
    const char s[]="SENTINELOS";DWORD n=sizeof(s)-1;
    if(!size)return 0;
    if(!name||*size<=n){*size=n+1;return 0;}
    for(DWORD i=0;i<=n;i++)name[i]=s[i];*size=n;return 1;
}
