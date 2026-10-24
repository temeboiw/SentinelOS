/*
 * SentinelOS minimal Universal CRT runtime entry points.
 *
 * Geometry Dash imports _initterm_e from api-ms-win-crt-runtime-l1-1-0.dll.
 * These routines implement the CRT initializer-table contract; they are not
 * no-op compatibility stubs. The executable's linker-generated .CRT$XI*
 * and .CRT$XC* ranges contain function pointers terminated by null entries.
 */
typedef int (*_PIFV)(void);
typedef void (*_PVFV)(void);

int _initterm_e(_PIFV *first, _PIFV *last)
{
    if (!first || !last || last < first)
        return -1;

    for (_PIFV *p = first; p < last; ++p) {
        _PIFV fn = *p;
        if (fn) {
            int result = fn();
            if (result != 0)
                return result;
        }
    }
    return 0;
}

void _initterm(_PVFV *first, _PVFV *last)
{
    if (!first || !last || last < first)
        return;

    for (_PVFV *p = first; p < last; ++p) {
        _PVFV fn = *p;
        if (fn)
            fn();
    }
}

/* Process-local errno storage used by the CRT error-reporting contract. */
static int ucrt_errno_value;
__attribute__((dllexport)) int *_errno(void) { return &ucrt_errno_value; }

/* MSVC on-exit table ABI: three pointers (first, last, end). */
typedef void (*UCRT_ONEXIT_FN)(void);
typedef struct {
    UCRT_ONEXIT_FN *first;
    UCRT_ONEXIT_FN *last;
    UCRT_ONEXIT_FN *end;
} UCRT_ONEXIT_TABLE;
static UCRT_ONEXIT_TABLE ucrt_process_onexit_table;

__attribute__((dllexport)) int _initialize_onexit_table(UCRT_ONEXIT_TABLE *table) {
    if (!table) return -1;
    table->first = 0;
    table->last = 0;
    table->end = 0;
    return 0;
}
__attribute__((dllexport)) int _execute_onexit_table(UCRT_ONEXIT_TABLE *table) {
    if (!table) return -1;
    if (!table->first && !table->last && !table->end) return 0;
    if (!table->first || !table->last || !table->end ||
        table->first > table->last || table->last > table->end) return -1;
    while (table->last > table->first) {
        UCRT_ONEXIT_FN fn = *--table->last;
        if (fn) fn();
    }
    table->first = 0;
    table->last = 0;
    table->end = 0;
    return 0;
}
__attribute__((dllexport)) void _cexit(void) {
    (void)_execute_onexit_table(&ucrt_process_onexit_table);
}
__attribute__((dllexport)) int _get_errno(int *out) {
    if (!out) return 22;
    *out = ucrt_errno_value;
    return 0;
}
__attribute__((dllexport)) int _set_errno(int value) {
    ucrt_errno_value = value;
    return 0;
}
__attribute__((dllexport)) char *strerror(int error) {
    static char unknown[32];
    const char *message = 0;
    switch (error) {
        case 0: message = "No error"; break;
        case 2: message = "No such file or directory"; break;
        case 12: message = "Not enough memory"; break;
        case 13: message = "Permission denied"; break;
        case 22: message = "Invalid argument"; break;
        case 28: message = "No space left on device"; break;
        case 34: message = "Result out of range"; break;
        default: break;
    }
    if (message) return (char *)message;
    const char prefix[] = "Unknown error ";
    unsigned int i = 0;
    while (prefix[i]) { unknown[i] = prefix[i]; ++i; }
    char digits[12]; unsigned int n = 0;
    unsigned int value = error < 0 ? (unsigned int)(-(error + 1)) + 1u : (unsigned int)error;
    do { digits[n++] = (char)('0' + value % 10u); value /= 10u; } while (value && n < sizeof(digits));
    if (error < 0) unknown[i++] = '-';
    while (n) unknown[i++] = digits[--n];
    unknown[i] = 0;
    return unknown;
}

/* Small heap allocator using the process virtual-memory syscall. Each block
   owns page-rounded memory, with its mapping base kept immediately before the
   aligned user pointer so free/realloc can release the correct mapping. */
typedef unsigned long long ucrt_size_t;
typedef struct { ucrt_size_t requested; ucrt_size_t mapped; void *base; } UCRT_ALLOC_HEADER;
static ucrt_size_t ucrt_sys_alloc(ucrt_size_t size) {
    ucrt_size_t result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(3), "D"((ucrt_size_t)0), "S"(size), "d"((ucrt_size_t)4) : "memory", "cc");
    return result;
}
static int ucrt_sys_free(void *base) {
    ucrt_size_t result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(27), "D"((ucrt_size_t)base), "S"((ucrt_size_t)0), "d"((ucrt_size_t)0x8000) : "memory", "cc");
    return result != 0;
}
static ucrt_size_t ucrt_map_size(ucrt_size_t n) {
    if (n > 0xFFFFFFFFFFFFE000ULL) return 0;
    n += sizeof(UCRT_ALLOC_HEADER) + 15;
    if (n > 0xFFFFFFFFFFFFF000ULL) return 0;
    return (n + 0xFFFULL) & ~0xFFFULL;
}
__attribute__((dllexport)) void *malloc(ucrt_size_t size) {
    if (!size) size = 1;
    ucrt_size_t mapped = ucrt_map_size(size);
    if (!mapped || mapped > 16ULL * 1024 * 1024) { ucrt_errno_value = 12; return 0; }
    UCRT_ALLOC_HEADER *h = (UCRT_ALLOC_HEADER *)ucrt_sys_alloc(mapped);
    if (!h || (ucrt_size_t)h == (ucrt_size_t)-1) { ucrt_errno_value = 12; return 0; }
    h->requested = size; h->mapped = mapped; h->base = h;
    ucrt_errno_value = 0;
    return (void *)(((ucrt_size_t)(h + 1) + 15ULL) & ~15ULL);
}
static UCRT_ALLOC_HEADER *ucrt_header(void *p) {
    if (!p) return 0;
    /* Header is immediately before the aligned pointer for the current ABI. */
    UCRT_ALLOC_HEADER *h = (UCRT_ALLOC_HEADER *)(((ucrt_size_t)p - sizeof(UCRT_ALLOC_HEADER)) & ~15ULL);
    if (h->base != (void *)h || !h->mapped || (h->mapped & 0xFFFULL)) return 0;
    return h;
}
__attribute__((dllexport)) void free(void *p) {
    if (!p) return;
    UCRT_ALLOC_HEADER *h = ucrt_header(p);
    if (!h) { ucrt_errno_value = 22; return; }
    void *base = h->base; h->base = 0;
    if (!ucrt_sys_free(base)) ucrt_errno_value = 22; else ucrt_errno_value = 0;
}
__attribute__((dllexport)) ucrt_size_t _msize(void *p) {
    UCRT_ALLOC_HEADER *h = ucrt_header(p);
    if (!h) { ucrt_errno_value = 22; return (ucrt_size_t)-1; }
    return h->requested;
}
__attribute__((dllexport)) void *calloc(ucrt_size_t count, ucrt_size_t size) {
    if (size && count > 0xFFFFFFFFFFFFF000ULL / size) { ucrt_errno_value = 12; return 0; }
    ucrt_size_t total = count * size; void *p = malloc(total);
    if (p) { unsigned char *b = (unsigned char *)p; for (ucrt_size_t i=0;i<(total?total:1);++i)b[i]=0; }
    return p;
}
__attribute__((dllexport)) void *realloc(void *p, ucrt_size_t size) {
    if (!p) return malloc(size);
    if (!size) { free(p); return 0; }
    ucrt_size_t old = _msize(p); if (old == (ucrt_size_t)-1) return 0;
    void *n = malloc(size); if (!n) return 0;
    ucrt_size_t copy = old < size ? old : size;
    unsigned char *d=(unsigned char *)n; const unsigned char *s=(const unsigned char *)p;
    for (ucrt_size_t i=0;i<copy;++i)d[i]=s[i];
    free(p); return n;
}



/*
 * Thread/process failure entry points used by the MSVC CRT.
 * _beginthreadex delegates to the OS thread service through kernel32's
 * existing CreateThread implementation, preserving the native thread handle
 * and optional thread ID. Failure paths return zero as the CRT contract
 * requires; they do not fabricate a handle.
 */
typedef unsigned int (*UCRT_THREAD_START)(void *);
typedef void *(*UCRT_CREATE_THREAD)(void *, ucrt_size_t, void *, void *,
                                    unsigned int, unsigned int *);
static ucrt_size_t ucrt_sys3(ucrt_size_t number, ucrt_size_t a,
                             ucrt_size_t b, ucrt_size_t c) {
    ucrt_size_t result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "a"(number), "D"(a), "S"(b), "d"(c)
                     : "memory", "cc");
    return result;
}
__attribute__((dllexport)) ucrt_size_t _beginthreadex(
    void *security, unsigned int stack_size, UCRT_THREAD_START start,
    void *argument, unsigned int initflag, unsigned int *thread_id) {
    if (!start) { ucrt_errno_value = 22; return 0; }
    UCRT_CREATE_THREAD create = (UCRT_CREATE_THREAD)ucrt_sys3(
        9, (ucrt_size_t)"kernel32.dll", (ucrt_size_t)"CreateThread", 0);
    if (!create) { ucrt_errno_value = 38; return 0; }
    void *handle = create(security, (ucrt_size_t)stack_size,
                          (void *)start, argument, initflag, thread_id);
    if (!handle) { ucrt_errno_value = 11; return 0; }
    ucrt_errno_value = 0;
    return (ucrt_size_t)handle;
}
typedef void (*UCRT_EXIT_PROCESS)(unsigned int);
static void ucrt_fail_fast(unsigned int code) {
    UCRT_EXIT_PROCESS exit_process = (UCRT_EXIT_PROCESS)ucrt_sys3(
        9, (ucrt_size_t)"kernel32.dll", (ucrt_size_t)"ExitProcess", 0);
    if (exit_process) exit_process(code);
    for (;;) __asm__ volatile("pause");
}
__attribute__((dllexport)) void terminate(void) { ucrt_fail_fast(3); }
__attribute__((dllexport)) void abort(void) { ucrt_fail_fast(3); }
__attribute__((dllexport)) void _invoke_watson(const unsigned short *expression,
    const unsigned short *function, const unsigned short *file, unsigned int line,
    ucrt_size_t reserved) {
    (void)expression; (void)function; (void)file; (void)line; (void)reserved;
    ucrt_fail_fast(3);
}

/* Fundamental C runtime memory/string primitives. These are byte-accurate
   implementations and deliberately compiled with -fno-builtin so the compiler
   cannot rewrite their loops into recursive calls to the same exports. */
__attribute__((dllexport)) void *memcpy(void *dst, const void *src, ucrt_size_t n) {
    unsigned char *d=(unsigned char *)dst; const unsigned char *s=(const unsigned char *)src;
    for(ucrt_size_t i=0;i<n;i++) d[i]=s[i];
    return dst;
}
__attribute__((dllexport)) void *memmove(void *dst, const void *src, ucrt_size_t n) {
    unsigned char *d=(unsigned char *)dst; const unsigned char *s=(const unsigned char *)src;
    if(d==s||!n)return dst;
    if((ucrt_size_t)d<(ucrt_size_t)s || (ucrt_size_t)d-(ucrt_size_t)s>=n) {
        for(ucrt_size_t i=0;i<n;i++)d[i]=s[i];
    } else {
        for(ucrt_size_t i=n;i>0;i--)d[i-1]=s[i-1];
    }
    return dst;
}
__attribute__((dllexport)) void *memset(void *dst, int value, ucrt_size_t n) {
    unsigned char *d=(unsigned char *)dst; unsigned char v=(unsigned char)value;
    for(ucrt_size_t i=0;i<n;i++)d[i]=v;
    return dst;
}
__attribute__((dllexport)) int memcmp(const void *a, const void *b, ucrt_size_t n) {
    const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
    for(ucrt_size_t i=0;i<n;i++)if(x[i]!=y[i])return (int)x[i]-(int)y[i];
    return 0;
}
__attribute__((dllexport)) void *memchr(const void *s, int c, ucrt_size_t n) {
    const unsigned char *p=(const unsigned char *)s; unsigned char v=(unsigned char)c;
    for(ucrt_size_t i=0;i<n;i++)if(p[i]==v)return (void *)(p+i);
    return 0;
}
__attribute__((dllexport)) ucrt_size_t strlen(const char *s) {
    ucrt_size_t n=0; if(!s)return 0; while(s[n])n++; return n;
}
__attribute__((dllexport)) int strcmp(const char *a, const char *b) {
    const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
    while(*x&&*x==*y){x++;y++;} return (int)*x-(int)*y;
}
__attribute__((dllexport)) int strncmp(const char *a, const char *b, ucrt_size_t n) {
    const unsigned char *x=(const unsigned char *)a,*y=(const unsigned char *)b;
    for(ucrt_size_t i=0;i<n;i++){if(x[i]!=y[i])return (int)x[i]-(int)y[i];if(!x[i])return 0;} return 0;
}
__attribute__((dllexport)) char *strchr(const char *s, int c) {
    if(!s)return 0; unsigned char v=(unsigned char)c;
    for(;;s++){if((unsigned char)*s==v)return (char *)s;if(!*s)return 0;}
}
__attribute__((dllexport)) char *strrchr(const char *s, int c) {
    if (!s) return 0;
    unsigned char value = (unsigned char)c;
    const char *last = 0;
    for (;;) {
        if ((unsigned char)*s == value) last = s;
        if (!*s) return (char *)last;
        ++s;
    }
}
__attribute__((dllexport)) char *strstr(const char *haystack, const char *needle) {
    if(!haystack||!needle)return 0;
    if(!*needle)return (char *)haystack;
    ucrt_size_t n=strlen(needle);
    for(const char *p=haystack;*p;p++) {
        ucrt_size_t i=0; while(i<n&&p[i]&&p[i]==needle[i])i++;
        if(i==n)return (char *)p;
    }
    return 0;
}


/* Additional exports used by current MSVC-built DLLs. */
__attribute__((dllexport)) int _configure_narrow_argv(int mode) {
    /* The host supplies a single ANSI command line; argument expansion is
       performed by the process startup layer. Accept supported CRT modes. */
    if (mode < 0 || mode > 2) return 22;
    return 0;
}
__attribute__((dllexport)) int _initialize_narrow_environment(void) { return 0; }
__attribute__((dllexport)) int _seh_filter_dll(unsigned int code, void *exception_info) {
    (void)code; (void)exception_info;
    /* No language-specific handler is installed by this minimal CRT. */
    return 0;
}
__attribute__((dllexport)) void __std_type_info_destroy_list(void *list) {
    /* The C++ runtime owns the opaque node layout. Do not free guessed
       pointers: that can corrupt the process heap during DLL detach. */
    (void)list;
}
__attribute__((dllexport)) int __C_specific_handler(void *exception_record,
    unsigned long long establisher_frame, void *context_record,
    void *dispatcher_context) {
    (void)exception_record; (void)establisher_frame;
    (void)context_record; (void)dispatcher_context;
    /* Continue search rather than claiming an exception was handled. */
    return 0;
}


/* Utility and conversion exports shared by the UCRT API-set contracts. */
__attribute__((dllexport)) int atoi(const char *s) {
    if (!s) return 0;
    while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n' ||
           *s == '\f' || *s == '\v') ++s;
    int sign = 1;
    if (*s == '+' || *s == '-') { if (*s++ == '-') sign = -1; }
    unsigned int value = 0;
    while (*s >= '0' && *s <= '9') {
        unsigned int digit = (unsigned int)(*s++ - '0');
        if (value > 214748364u ||
            (value == 214748364u && digit > (sign < 0 ? 8u : 7u)))
            return sign < 0 ? (-2147483647 - 1) : 2147483647;
        value = value * 10u + digit;
    }
    return sign < 0 ? (value == 2147483648u ? (-2147483647 - 1) : -(int)value)
                    : (int)value;
}
__attribute__((dllexport)) int isprint(int c) {
    unsigned char ch = (unsigned char)c;
    return ch >= 0x20u && ch <= 0x7Eu;
}
__attribute__((dllexport)) unsigned long long strspn(const char *s, const char *accept) {
    if (!s || !accept) return 0;
    unsigned long long n = 0;
    for (; s[n]; ++n) {
        int found = 0;
        for (unsigned long long j = 0; accept[j]; ++j)
            if (s[n] == accept[j]) { found = 1; break; }
        if (!found) break;
    }
    return n;
}
__attribute__((dllexport)) unsigned long long strcspn(const char *s, const char *reject) {
    if (!s || !reject) return 0;
    unsigned long long n = 0;
    for (; s[n]; ++n) {
        for (unsigned long long j = 0; reject[j]; ++j)
            if (s[n] == reject[j]) return n;
    }
    return n;
}
typedef int (*UCRT_COMPARE)(const void *, const void *);
__attribute__((dllexport)) void *bsearch(const void *key, const void *base,
                                        unsigned long long count,
                                        unsigned long long width,
                                        UCRT_COMPARE compare) {
    if (!key || !base || !width || !compare) return 0;
    const unsigned char *bytes = (const unsigned char *)base;
    unsigned long long lo = 0, hi = count;
    while (lo < hi) {
        unsigned long long mid = lo + (hi - lo) / 2;
        int order = compare(key, bytes + mid * width);
        if (order == 0) return (void *)(bytes + mid * width);
        if (order < 0) hi = mid; else lo = mid + 1;
    }
    return 0;
}
static void ucrt_swap_records(unsigned char *a, unsigned char *b,
                              unsigned long long width) {
    for (unsigned long long i = 0; i < width; ++i) {
        unsigned char t = a[i]; a[i] = b[i]; b[i] = t;
    }
}
/* In-place insertion sort is deterministic and avoids recursion on user data. */
__attribute__((dllexport)) void qsort(void *base, unsigned long long count,
                                      unsigned long long width,
                                      UCRT_COMPARE compare) {
    if (!base || count < 2 || !width || !compare ||
        count > 0xFFFFFFFFFFFFF000ULL / width) return;
    unsigned char *items = (unsigned char *)base;
    for (unsigned long long i = 1; i < count; ++i) {
        unsigned long long j = i;
        while (j > 0 && compare(items + (j - 1) * width,
                                items + j * width) > 0) {
            ucrt_swap_records(items + (j - 1) * width,
                              items + j * width, width);
            --j;
        }
    }
}
static unsigned int ucrt_rand_state = 1u;
__attribute__((dllexport)) int rand(void) {
    ucrt_rand_state = ucrt_rand_state * 1103515245u + 12345u;
    return (int)((ucrt_rand_state >> 16) & 0x7FFFu);
}
__attribute__((dllexport)) void srand(unsigned int seed) {
    ucrt_rand_state = seed;
}
