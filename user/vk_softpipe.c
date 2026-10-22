/*
 * SentinelOS CPU Vulkan soft-pipeline rasterizer primitives.
 * Deterministic bounded triangle rasterization, depth test/write,
 * interpolated vertex color, nearest/bilinear texture sampling and alpha blend.
 *
 * This file is an internal rendering building block; it does not claim to
 * implement SPIR-V or the complete Vulkan graphics-pipeline specification.
 */
#include "vk_softpipe.h"

static float sf_min(float a, float b) { return a < b ? a : b; }
static float sf_max(float a, float b) { return a > b ? a : b; }
static float sf_clamp(float x, float lo, float hi) {
    if (!(x >= lo)) return lo; /* also maps NaN to lo */
    if (x > hi) return hi;
    return x;
}
static uint8_t sf_u8(float x) {
    x = sf_clamp(x, 0.0f, 1.0f) * 255.0f;
    return (uint8_t)(x + 0.5f);
}
static float sf_chan(uint32_t p, unsigned shift) {
    return (float)((p >> shift) & 255u) * (1.0f / 255.0f);
}
static uint32_t sf_pack(float r, float g, float b, float a) {
    return ((uint32_t)sf_u8(a) << 24) |
           ((uint32_t)sf_u8(r) << 16) |
           ((uint32_t)sf_u8(g) << 8) | (uint32_t)sf_u8(b);
}
static float sf_edge(float ax, float ay, float bx, float by,
                     float px, float py) {
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}
static float sf_wrap(float x, uint8_t repeat) {
    if (repeat) {
        x = x - (float)(int)x;
        if (x < 0.0f) x += 1.0f;
        return x;
    }
    return sf_clamp(x, 0.0f, 0.99999994f);
}
static uint32_t sf_texel(const VkSoftTexture *t, int x, int y) {
    if (t->repeat_u) {
        x %= (int)t->width;
        if (x < 0) x += (int)t->width;
    } else {
        if (x < 0) x = 0;
        if ((uint32_t)x >= t->width) x = (int)t->width - 1;
    }
    if (t->repeat_v) {
        y %= (int)t->height;
        if (y < 0) y += (int)t->height;
    } else {
        if (y < 0) y = 0;
        if ((uint32_t)y >= t->height) y = (int)t->height - 1;
    }
    return t->pixels[(size_t)y * t->stride_pixels + (uint32_t)x];
}
static uint32_t sf_sample(const VkSoftTexture *t, float u, float v) {
    if (!t || !t->pixels || !t->width || !t->height ||
        t->stride_pixels < t->width || t->width > 32768u ||
        t->height > 32768u) return 0xffffffffu;
    u = sf_wrap(u, t->repeat_u);
    v = sf_wrap(v, t->repeat_v);
    if (!t->linear_filter) {
        int x = (int)(u * (float)t->width);
        int y = (int)(v * (float)t->height);
        return sf_texel(t, x, y);
    }
    {
        float fx = u * (float)t->width - 0.5f;
        float fy = v * (float)t->height - 0.5f;
        int x0 = (int)fx, y0 = (int)fy;
        float tx = fx - (float)x0, ty = fy - (float)y0;
        uint32_t p00 = sf_texel(t, x0, y0);
        uint32_t p10 = sf_texel(t, x0 + 1, y0);
        uint32_t p01 = sf_texel(t, x0, y0 + 1);
        uint32_t p11 = sf_texel(t, x0 + 1, y0 + 1);
        float out[4];
        const unsigned sh[4] = {16, 8, 0, 24};
        for (unsigned k = 0; k < 4; ++k) {
            float a = sf_chan(p00, sh[k]) * (1.0f - tx) +
                      sf_chan(p10, sh[k]) * tx;
            float b = sf_chan(p01, sh[k]) * (1.0f - tx) +
                      sf_chan(p11, sh[k]) * tx;
            out[k] = a * (1.0f - ty) + b * ty;
        }
        return sf_pack(out[0], out[1], out[2], out[3]);
    }
}
void vksoft_clear(VkSoftTarget *t, uint32_t argb, float depth) {
    if (!t || !t->width || !t->height ||
        t->width > t->stride_pixels || t->stride_pixels > 32768u ||
        t->height > 32768u) return;
    size_t count = (size_t)t->stride_pixels * t->height;
    if (t->color && t->color_write)
        for (size_t i = 0; i < count; ++i) t->color[i] = argb;
    if (t->depth)
        for (size_t i = 0; i < count; ++i) t->depth[i] = depth;
}
int vksoft_triangle(VkSoftTarget *t, const VkSoftVertex *a,
                    const VkSoftVertex *b, const VkSoftVertex *c,
                    const VkSoftTexture *tex) {
    float area, minx, miny, maxx, maxy;
    int x0, y0, x1, y1, drawn = 0;
    if (!t || !a || !b || !c || !t->width || !t->height ||
        t->width > t->stride_pixels || t->stride_pixels > 32768u ||
        t->height > 32768u || (!t->color && t->color_write)) return -1;
    if (!(a->x == a->x && a->y == a->y && b->x == b->x &&
          b->y == b->y && c->x == c->x && c->y == c->y)) return -1;
    area = sf_edge(a->x, a->y, b->x, b->y, c->x, c->y);
    if (area > -1.0e-12f && area < 1.0e-12f) return 0;
    minx = sf_min(a->x, sf_min(b->x, c->x));
    miny = sf_min(a->y, sf_min(b->y, c->y));
    maxx = sf_max(a->x, sf_max(b->x, c->x));
    maxy = sf_max(a->y, sf_max(b->y, c->y));
    if (maxx < 0.0f || maxy < 0.0f ||
        minx >= (float)t->width || miny >= (float)t->height) return 0;
    x0 = (int)sf_clamp(minx, 0.0f, (float)(t->width - 1u));
    y0 = (int)sf_clamp(miny, 0.0f, (float)(t->height - 1u));
    x1 = (int)sf_clamp(maxx, 0.0f, (float)(t->width - 1u));
    y1 = (int)sf_clamp(maxy, 0.0f, (float)(t->height - 1u));
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float wa = sf_edge(b->x, b->y, c->x, c->y, px, py) / area;
            float wb = sf_edge(c->x, c->y, a->x, a->y, px, py) / area;
            float wc = 1.0f - wa - wb;
            float z;
            size_t at;
            float r, g, bl, al;
            uint32_t src;
            if (wa < -1.0e-6f || wb < -1.0e-6f || wc < -1.0e-6f)
                continue;
            z = wa * a->z + wb * b->z + wc * c->z;
            if (!(z >= 0.0f && z <= 1.0f)) continue;
            at = (size_t)y * t->stride_pixels + (uint32_t)x;
            if (t->depth_test && t->depth) {
                float old_z = t->depth[at];
                uint8_t cmp = t->depth_compare;
                int pass = 0;
                switch (cmp) {
                case 0u: pass = 0; break;                 /* NEVER */
                case 1u: pass = z < old_z; break;         /* LESS */
                case 2u: pass = z == old_z; break;        /* EQUAL */
                case 3u: pass = z <= old_z; break;        /* LESS_OR_EQUAL */
                case 4u: pass = z > old_z; break;         /* GREATER */
                case 5u: pass = z != old_z; break;        /* NOT_EQUAL */
                case 6u: pass = z >= old_z; break;        /* GREATER_OR_EQUAL */
                case 7u: pass = 1; break;                 /* ALWAYS */
                default: pass = 0; break;
                }
                if (!pass) continue;
            }
            /* Perspective-correct varyings when clip-space reciprocal W
             * is supplied; retain affine behavior for legacy callers that
             * leave all inv_w values zero. Depth remains screen-linear. */
            {
                float iw = wa*a->inv_w + wb*b->inv_w + wc*c->inv_w;
                if (iw > 1.0e-20f || iw < -1.0e-20f) {
                    r = (wa*a->r*a->inv_w + wb*b->r*b->inv_w + wc*c->r*c->inv_w) / iw;
                    g = (wa*a->g*a->inv_w + wb*b->g*b->inv_w + wc*c->g*c->inv_w) / iw;
                    bl = (wa*a->b*a->inv_w + wb*b->b*b->inv_w + wc*c->b*c->inv_w) / iw;
                    al = (wa*a->a*a->inv_w + wb*b->a*b->inv_w + wc*c->a*c->inv_w) / iw;
                } else {
                    r = wa*a->r + wb*b->r + wc*c->r;
                    g = wa*a->g + wb*b->g + wc*c->g;
                    bl = wa*a->b + wb*b->b + wc*c->b;
                    al = wa*a->a + wb*b->a + wc*c->a;
                }
            }
            if (tex && tex->pixels) {
                float u = wa*a->u + wb*b->u + wc*c->u;
                float v = wa*a->v + wb*b->v + wc*c->v;
                float iw = wa*a->inv_w + wb*b->inv_w + wc*c->inv_w;
                if (iw > 1.0e-20f || iw < -1.0e-20f) {
                    u = (wa*a->u*a->inv_w + wb*b->u*b->inv_w + wc*c->u*c->inv_w) / iw;
                    v = (wa*a->v*a->inv_w + wb*b->v*b->inv_w + wc*c->v*c->inv_w) / iw;
                }
                uint32_t q = sf_sample(tex, u, v);
                r *= sf_chan(q, 16); g *= sf_chan(q, 8);
                bl *= sf_chan(q, 0); al *= sf_chan(q, 24);
            }
            src = sf_pack(r, g, bl, al);
            if (t->color && t->color_write) {
                if (t->blend_src_alpha) {
                    uint32_t dst = t->color[at];
                    float sa = sf_chan(src, 24), ia = 1.0f - sa;
                    float rr = sf_chan(src,16)*sa + sf_chan(dst,16)*ia;
                    float gg = sf_chan(src,8)*sa + sf_chan(dst,8)*ia;
                    float bb = sf_chan(src,0)*sa + sf_chan(dst,0)*ia;
                    float aa = sa + sf_chan(dst,24)*ia;
                    t->color[at] = sf_pack(rr,gg,bb,aa);
                } else t->color[at] = src;
            }
            if (t->depth && t->depth_write) t->depth[at] = z;
            ++drawn;
        }
    }
    return drawn;
}
