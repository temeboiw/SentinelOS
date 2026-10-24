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
