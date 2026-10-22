/*
 * SentinelOS Vulkan CPU soft-pipeline primitives.
 * Freestanding C; no libc, heap, or host graphics dependencies.
 * This is an internal rasterizer building block, NOT by itself a Vulkan ICD.
 */
#ifndef SENTINEL_VK_SOFTPIPE_H
#define SENTINEL_VK_SOFTPIPE_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    float x, y, z, inv_w;
    float r, g, b, a;
    float u, v;
} VkSoftVertex;

typedef struct {
    uint32_t *color;       /* 0xAARRGGBB */
    float *depth;           /* normalized [0,1] */
    uint32_t width, height;
    uint32_t stride_pixels;
    uint8_t depth_test;
    uint8_t depth_write;
    uint8_t blend_src_alpha;
    uint8_t color_write;
    uint8_t depth_compare; /* 0 less, 1 less/equal, 2 always, 3 never, 4 greater */
} VkSoftTarget;

typedef struct {
    const uint32_t *pixels; /* 0xAARRGGBB */
    uint32_t width, height, stride_pixels;
    uint8_t linear_filter;
    uint8_t repeat_u, repeat_v;
    uint8_t compare_enable, compare_op;
} VkSoftTexture;

typedef int (*VkSoftFragmentShader)(void *context, float u, float v, uint32_t *argb);
uint32_t vksoft_sample_texture(const VkSoftTexture *texture, float u, float v);
void vksoft_clear(VkSoftTarget *t, uint32_t argb, float depth);
int vksoft_triangle(VkSoftTarget *t, const VkSoftVertex *a,
                    const VkSoftVertex *b, const VkSoftVertex *c,
                    const VkSoftTexture *tex);
int vksoft_triangle_fragment(VkSoftTarget *t, const VkSoftVertex *a,
                    const VkSoftVertex *b, const VkSoftVertex *c,
                    VkSoftFragmentShader shader, void *context);
#endif
