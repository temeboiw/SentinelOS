#pragma once
#include <stdint.h>

/* Fixed-point 3D reference renderer for SentinelOS.
 * Vertex x/y/z and focal_length are signed 16.16 values; z must be positive.
 * The caller supplies a 32-bit ARGB target and a width*height depth buffer.
 * This is a real software fallback; it does not claim hardware 3D acceleration.
 */
 #define S3D_VIRGL_RENDER_TARGET_ID 2u
#define S3D_VIRGL_VERTEX_BUFFER_ID 3u

typedef struct {
    int32_t x, y, z;
    uint32_t color;
} S3D_VERTEX;

typedef struct {
    uint32_t *pixels;
    uint32_t width, height, pitch;
    uint32_t backend; /* 1=firmware framebuffer, 2=VirtIO-GPU 2D */
    void *virgl_buffer;
    uint32_t virgl_buffer_resource_id, virgl_buffer_size;
} S3D_SURFACE;

typedef struct {
    uint32_t *color;
    uint32_t *depth;
    uint32_t width, height, pitch;
    int32_t focal_length;
    uint32_t backend;
} S3D_CONTEXT;

#ifdef __cplusplus
extern "C" {
#endif
int S3DGetSurface(S3D_SURFACE *surface);\nint S3DInitializeSurface(S3D_CONTEXT *ctx, const S3D_SURFACE *surface,\n                        uint32_t *depth, int32_t focal_length_16_16);\nint S3DInitialize(S3D_CONTEXT *ctx, uint32_t *color, uint32_t *depth,
                  uint32_t width, uint32_t height, uint32_t pitch_pixels,
                  int32_t focal_length_16_16);
void S3DClear(S3D_CONTEXT *ctx, uint32_t argb, uint32_t depth_value);
int S3DDrawTriangle(S3D_CONTEXT *ctx, const S3D_VERTEX *a,
                    const S3D_VERTEX *b, const S3D_VERTEX *c);
int S3DPresent(S3D_CONTEXT *ctx);
/* Submit a raw VirGL/Gallium command stream to the kernel-created GPU context.
 * Returns 0 when the VM/device does not expose a real VirGL backend. */
int S3DSubmitVirGL(const void *commands, uint32_t length);
int S3DPresentVirGL(void);
#ifdef __cplusplus
}
#endif
