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

/*
 * Small process-local CRT heap backed by SentinelOS virtual-memory syscalls.
 * Each allocation owns whole pages; a private header records both the
 * requested size and mapping base so realloc/free/msize have real semantics.
 */
typedef unsigned long long ucrt_size_t;
typedef union {
    struct {
        ucrt_size_t requested;
        ucrt_size_t reserved;
        void *mapping;
    } h;
    long double align_ld;
    void *align_ptr;
} UCRT_HEAP_HEADER;

static int ucrt_errno_value;

int *_errno(void)
{
    return &ucrt_errno_value;
}

static void *ucrt_vm_alloc(ucrt_size_t size)
{
    ucrt_size_t result;
    __asm__ volatile(
        "mov $3, %%rax\n"
        "xor %%rdi, %%rdi\n"
        "mov %[size], %%rsi\n"
        "mov $4, %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[result]"
        : [result] "=r"(result)
        : [size] "r"(size)
        : "rax", "rdi", "rsi", "rdx", "memory", "cc");
    return (void *)result;
}

static int ucrt_vm_free(void *base)
{
    ucrt_size_t result;
    ucrt_size_t zero = 0;
    ucrt_size_t release = 0x8000;
    __asm__ volatile(
        "mov $27, %%rax\n"
        "mov %[base], %%rdi\n"
        "mov %[zero], %%rsi\n"
        "mov %[release], %%rdx\n"
        "int $0x80\n"
        "mov %%rax, %[result]"
        : [result] "=r"(result)
        : [base] "r"(base), [zero] "r"(zero), [release] "r"(release)
        : "rax", "rdi", "rsi", "rdx", "memory", "cc");
    return result != 0;
}

static ucrt_size_t ucrt_strlen_bound(ucrt_size_t size)
{
    if (size > 0xFFFFFFFFFFFFF000ULL) return 0;
    ucrt_size_t total = size + sizeof(UCRT_HEAP_HEADER);
    return (total + 0xFFFULL) & ~0xFFFULL;
}

__attribute__((dllexport)) void *malloc(ucrt_size_t size)
{
    if (!size) size = 1;
    ucrt_size_t mapped = ucrt_strlen_bound(size);
    if (!mapped || mapped > 16ULL * 1024 * 1024) {
        ucrt_errno_value = 12;
        return 0;
    }
    UCRT_HEAP_HEADER *header = (UCRT_HEAP_HEADER *)ucrt_vm_alloc(mapped);
    if (!header) {
        ucrt_errno_value = 12;
        return 0;
    }
    header->h.requested = size;
    header->h.reserved = mapped;
    header->h.mapping = header;
    ucrt_errno_value = 0;
    return (void *)(header + 1);
}

__attribute__((dllexport)) void free(void *ptr)
{
    if (!ptr) return;
    UCRT_HEAP_HEADER *header = ((UCRT_HEAP_HEADER *)ptr) - 1;
    if (header->h.mapping != (void *)header ||
        !header->h.reserved || (header->h.reserved & 0xFFFULL)) {
        ucrt_errno_value = 22;
        return;
    }
    void *base = header->h.mapping;
    header->h.mapping = 0;
    if (!ucrt_vm_free(base)) ucrt_errno_value = 22;
    else ucrt_errno_value = 0;
}

__attribute__((dllexport)) void *calloc(ucrt_size_t count, ucrt_size_t size)
{
    if (size && count > 0xFFFFFFFFFFFFF000ULL / size) {
        ucrt_errno_value = 12;
        return 0;
    }
    ucrt_size_t total = count * size;
    void *p = malloc(total);
    if (p) {
        unsigned char *b = (unsigned char *)p;
        for (ucrt_size_t i = 0; i < (total ? total : 1); ++i) b[i] = 0;
    }
    return p;
}

__attribute__((dllexport)) ucrt_size_t _msize(void *ptr)
{
    if (!ptr) {
        ucrt_errno_value = 22;
        return (ucrt_size_t)-1;
    }
    UCRT_HEAP_HEADER *header = ((UCRT_HEAP_HEADER *)ptr) - 1;
    if (header->h.mapping != (void *)header ||
        !header->h.reserved || (header->h.reserved & 0xFFFULL)) {
        ucrt_errno_value = 22;
        return (ucrt_size_t)-1;
    }
    return header->h.requested;
}

__attribute__((dllexport)) void *realloc(void *ptr, ucrt_size_t size)
{
    if (!ptr) return malloc(size);
    if (!size) {
        free(ptr);
        return 0;
    }
    ucrt_size_t old_size = _msize(ptr);
    if (old_size == (ucrt_size_t)-1) return 0;
    void *replacement = malloc(size);
    if (!replacement) return 0;
    ucrt_size_t copy = old_size < size ? old_size : size;
    unsigned char *dst = (unsigned char *)replacement;
    const unsigned char *src = (const unsigned char *)ptr;
    for (ucrt_size_t i = 0; i < copy; ++i) dst[i] = src[i];
    free(ptr);
    return replacement;
}
