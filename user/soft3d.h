#pragma once
#include <stdint.h>

/* Fixed-point 3D reference renderer for SentinelOS.
 * Vertex x/y/z and focal_length are signed 16.16 values; z must be positive.
 * The caller supplies a 32-bit ARGB target and a width*height depth buffer.
 * This is a real software fallback; it does not claim hardware 3D acceleration.
 */
typedef struct {
    int32_t x, y, z;
    uint32_t color;
} S3D_VERTEX;

typedef struct {
    uint32_t *color;
    uint32_t *depth;
    uint32_t width, height, pitch;
    int32_t focal_length;
} S3D_CONTEXT;

#ifdef __cplusplus
extern "C" {
#endif
int S3DInitialize(S3D_CONTEXT *ctx, uint32_t *color, uint32_t *depth,
                  uint32_t width, uint32_t height, uint32_t pitch_pixels,
                  int32_t focal_length_16_16);
void S3DClear(S3D_CONTEXT *ctx, uint32_t argb, uint32_t depth_value);
int S3DDrawTriangle(S3D_CONTEXT *ctx, const S3D_VERTEX *a,
                    const S3D_VERTEX *b, const S3D_VERTEX *c);
int S3DPresent(S3D_CONTEXT *ctx);
#ifdef __cplusplus
}
#endif
