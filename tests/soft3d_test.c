#include "soft3d.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

int main(void) {
    uint32_t pixels[64 * 64];
    uint32_t depth[64 * 64];
    S3D_CONTEXT ctx;

    assert(S3DInitialize(&ctx, pixels, depth, 64, 64, 64, 32 * 65536));
    S3DClear(&ctx, 0xFF000000u, UINT32_MAX);

    const S3D_VERTEX near_tri[3] = {
        {-16384, -16384, 65536, 0xFFFF0000u},
        { 16384, -16384, 65536, 0xFFFF0000u},
        {     0,  16384, 65536, 0xFFFF0000u}
    };
    const S3D_VERTEX far_tri[3] = {
        {-32768, -32768, 131072, 0xFF0000FFu},
        { 32768, -32768, 131072, 0xFF0000FFu},
        {     0,  32768, 131072, 0xFF0000FFu}
    };

    assert(S3DDrawTriangle(&ctx, &near_tri[0], &near_tri[1], &near_tri[2]) > 0);
    uint32_t center_before = pixels[30 * 64 + 32];
    assert(center_before == 0xFFFF0000u);

    /* A farther triangle with the same projected bounds must fail the depth
       test instead of painting over the nearer red triangle. */
    assert(S3DDrawTriangle(&ctx, &far_tri[0], &far_tri[1], &far_tri[2]) == 0);
    assert(pixels[30 * 64 + 32] == center_before);

    const S3D_VERTEX clipped[3] = {
        {-16384, -16384, 4096, 0xFFFFFFFFu},
        { 16384, -16384, 4096, 0xFFFFFFFFu},
        {     0,  16384, 4096, 0xFFFFFFFFu}
    };
    assert(S3DDrawTriangle(&ctx, &clipped[0], &clipped[1], &clipped[2]) == 0);
    assert(!S3DInitialize(&ctx, pixels, depth, 64, 64, 63, 32 * 65536));

    puts("soft3d: projection, rasterization, depth test and near clip passed");
    return 0;
}
