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

    return fails ? 1 : 0;
}
