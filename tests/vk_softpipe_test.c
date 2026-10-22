#include "../user/vk_softpipe.h"
#include <stdio.h>
#include <string.h>

static uint32_t color[16 * 16];
static float depth[16 * 16];

static int check(int ok, const char *name) {
    if (!ok) fprintf(stderr, "FAIL: %s\n", name);
    else fprintf(stdout, "PASS: %s\n", name);
    return ok ? 0 : 1;
}

int main(void) {
    VkSoftTarget target;
    VkSoftVertex a, b, c;
    int fails = 0;
    memset(&target, 0, sizeof(target));
    target.color = color;
    target.depth = depth;
    target.width = target.height = target.stride_pixels = 16;
    target.depth_test = target.depth_write = target.color_write = 1;

    vksoft_clear(&target, 0xff102030u, 1.0f);
    fails += check(color[0] == 0xff102030u && depth[0] == 1.0f,
                   "color and depth clear");

    memset(&a, 0, sizeof(a)); memset(&b, 0, sizeof(b));
    memset(&c, 0, sizeof(c));
    a.x=2; a.y=2; a.z=0.5f; a.r=1; a.a=1;
    b.x=14; b.y=2; b.z=0.5f; b.r=1; b.a=1;
    c.x=2; c.y=14; c.z=0.5f; c.r=1; c.a=1;
    fails += check(vksoft_triangle(&target,&a,&b,&c,0) > 0,
                   "triangle rasterizes pixels");
    fails += check(color[4 * 16 + 4] == 0xffff0000u,
                   "interpolated red fragment reaches target");
    fails += check(depth[4 * 16 + 4] == 0.5f,
                   "depth is written");

    a.z=b.z=c.z=0.8f; a.g=b.g=c.g=1; a.r=b.r=c.r=0;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    fails += check(color[4 * 16 + 4] == 0xffff0000u,
                   "farther triangle fails depth test");

    a.z=b.z=c.z=0.2f;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    fails += check(color[4 * 16 + 4] == 0xff00ff00u,
                   "nearer triangle replaces fragment");

    /* Vulkan-style depth comparison modes: less/equal, always, never, greater. */
    target.depth_compare = 1u; /* LESS_OR_EQUAL */
    depth[4 * 16 + 4] = 0.2f;
    a.z=b.z=c.z=0.2f; a.r=0; a.g=0; a.b=1;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    fails += check(color[4 * 16 + 4] == 0xff0000ffu,
                   "less-or-equal accepts equal depth");
    target.depth_compare = 4u; /* GREATER */
    depth[4 * 16 + 4] = 0.1f;
    a.z=b.z=c.z=0.5f; a.r=1; a.g=1; a.b=0;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    fails += check(color[4 * 16 + 4] == 0xffffff00u,
                   "greater depth comparison accepts farther value");
    target.depth_compare = 2u; /* ALWAYS */
    depth[4 * 16 + 4] = 0.1f;
    a.z=b.z=c.z=0.5f; a.r=0; a.g=1; a.b=1;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    fails += check(color[4 * 16 + 4] == 0xff00ffffu,
                   "always depth comparison accepts fragment");
    target.depth_compare = 3u; /* NEVER */
    depth[4 * 16 + 4] = 1.0f;
    a.z=b.z=c.z=0.1f; a.r=1; a.g=0; a.b=1;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    fails += check(color[4 * 16 + 4] == 0xff00ffffu,
                   "never depth comparison rejects fragment");
    target.depth_compare = 0u;

    vksoft_clear(&target, 0xff000000u, 1.0f);
    a.x=2; a.y=2; a.z=0.5f; a.r=1; a.g=0; a.b=0; a.a=1; a.inv_w=1.0f;
    b.x=14; b.y=2; b.z=0.5f; b.r=0; b.g=0; b.b=0; b.a=1; b.inv_w=0.25f;
    c.x=2; c.y=14; c.z=0.5f; c.r=0; c.g=0; c.b=0; c.a=1; c.inv_w=0.25f;
    (void)vksoft_triangle(&target,&a,&b,&c,0);
    {
        unsigned red = (color[4 * 16 + 4] >> 16) & 255u;
        fails += check(red >= 210u && red <= 222u,
                       "perspective-correct color interpolation");
    }

    return fails ? 1 : 0;
}
