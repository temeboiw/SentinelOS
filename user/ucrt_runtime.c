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
