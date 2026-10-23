#pragma once
/*
 * Minimal VirGL command-stream encoder for SentinelOS user applications.
 * Command IDs and header packing follow virglrenderer/src/virgl_protocol.h.
 * This is only the encoding layer; callers must still provide valid Gallium
 * objects, shaders, framebuffer state and draw state before issuing draws.
 */
#include <stdint.h>
#include <stddef.h>

enum VirglContextCommand : uint32_t {
    VIRGL_CCMD_NOP = 0,
    VIRGL_CCMD_CREATE_OBJECT = 1,
    VIRGL_CCMD_BIND_OBJECT = 2,
    VIRGL_CCMD_DESTROY_OBJECT = 3,
    VIRGL_CCMD_SET_VIEWPORT_STATE = 4,
    VIRGL_CCMD_SET_FRAMEBUFFER_STATE = 5,
    VIRGL_CCMD_SET_VERTEX_BUFFERS = 6,
    VIRGL_CCMD_CLEAR = 7,
    VIRGL_CCMD_DRAW_VBO = 8,
    VIRGL_CCMD_SET_SCISSOR_STATE = 15,
    VIRGL_CCMD_BLIT = 16,
    VIRGL_CCMD_RESOURCE_COPY_REGION = 17,
    VIRGL_CCMD_BIND_SAMPLER_STATES = 18,
    VIRGL_CCMD_BEGIN_QUERY = 19,
    VIRGL_CCMD_END_QUERY = 20,
    VIRGL_CCMD_GET_QUERY_RESULT = 21,
    VIRGL_CCMD_SET_POLYGON_STIPPLE = 22,
    VIRGL_CCMD_SET_CLIP_STATE = 23,
    VIRGL_CCMD_SET_SAMPLE_MASK = 24,
    VIRGL_CCMD_SET_STREAMOUT_TARGETS = 25,
    VIRGL_CCMD_SET_RENDER_CONDITION = 26,
    VIRGL_CCMD_SET_UNIFORM_BUFFER = 27,
    VIRGL_CCMD_SET_SUB_CTX = 28,
    VIRGL_CCMD_CREATE_SUB_CTX = 29,
    VIRGL_CCMD_DESTROY_SUB_CTX = 30,
    VIRGL_CCMD_BIND_SHADER = 31,
    VIRGL_CCMD_SET_TESS_STATE = 32,
    VIRGL_CCMD_SET_MIN_SAMPLES = 33,
    VIRGL_CCMD_SET_SHADER_BUFFERS = 34,
    VIRGL_CCMD_SET_SHADER_IMAGES = 35,
    VIRGL_CCMD_MEMORY_BARRIER = 36,
    VIRGL_CCMD_LAUNCH_GRID = 37,
    VIRGL_CCMD_SET_FRAMEBUFFER_STATE_NO_ATTACH = 38,
    VIRGL_CCMD_TEXTURE_BARRIER = 39,
    VIRGL_CCMD_SET_ATOMIC_BUFFERS = 40,
    VIRGL_CCMD_SET_DEBUG_FLAGS = 41,
    VIRGL_CCMD_GET_QUERY_RESULT_QBO = 42,
    VIRGL_CCMD_TRANSFER3D = 43,
    VIRGL_CCMD_END_TRANSFERS = 44,
    VIRGL_CCMD_COPY_TRANSFER3D = 45,
    VIRGL_CCMD_SET_TWEAKS = 46,
    VIRGL_CCMD_CLEAR_TEXTURE = 47,
    VIRGL_CCMD_PIPE_RESOURCE_CREATE = 48,
    VIRGL_CCMD_PIPE_RESOURCE_SET_TYPE = 49,
    VIRGL_CCMD_GET_MEMORY_INFO = 50,
    VIRGL_CCMD_MAX = 51
};

enum VirglObjectType : uint32_t {
    VIRGL_OBJECT_NULL = 0,
    VIRGL_OBJECT_BLEND = 1,
    VIRGL_OBJECT_RASTERIZER = 2,
    VIRGL_OBJECT_DSA = 3,
    VIRGL_OBJECT_SHADER = 4,
    VIRGL_OBJECT_VERTEX_ELEMENTS = 5,
    VIRGL_OBJECT_SAMPLER_VIEW = 6,
    VIRGL_OBJECT_SAMPLER_STATE = 7,
    VIRGL_OBJECT_SURFACE = 8,
    VIRGL_OBJECT_QUERY = 9,
    VIRGL_OBJECT_STREAMOUT_TARGET = 10,
    VIRGL_OBJECT_MAX = 11
};

enum VirglPrimitiveMode : uint32_t {
    VIRGL_PRIM_POINTS = 0,
    VIRGL_PRIM_LINES = 1,
    VIRGL_PRIM_LINE_LOOP = 2,
    VIRGL_PRIM_LINE_STRIP = 3,
    VIRGL_PRIM_TRIANGLES = 4,
    VIRGL_PRIM_TRIANGLE_STRIP = 5,
    VIRGL_PRIM_TRIANGLE_FAN = 6
};

#define VIRGL_DRAW_VBO_DWORDS 12u
#define VIRGL_DRAW_VBO_START 0u
#define VIRGL_DRAW_VBO_COUNT 1u
#define VIRGL_DRAW_VBO_MODE 2u
#define VIRGL_DRAW_VBO_INDEXED 3u
#define VIRGL_DRAW_VBO_INSTANCE_COUNT 4u
#define VIRGL_DRAW_VBO_INDEX_BIAS 5u
#define VIRGL_DRAW_VBO_START_INSTANCE 6u
#define VIRGL_DRAW_VBO_PRIMITIVE_RESTART 7u
#define VIRGL_DRAW_VBO_RESTART_INDEX 8u
#define VIRGL_DRAW_VBO_MIN_INDEX 9u
#define VIRGL_DRAW_VBO_MAX_INDEX 10u
#define VIRGL_DRAW_VBO_COUNT_FROM_SO 11u

#define VIRGL_CMD0(cmd, obj, payload_dwords) \
    ((uint32_t)(cmd) | ((uint32_t)(obj) << 8) | ((uint32_t)(payload_dwords) << 16))

struct VirglStream {
    uint32_t *words;
    uint32_t capacity_dwords;
    uint32_t used_dwords;
    uint32_t failed;
};

static inline void virgl_stream_init(VirglStream *s, uint32_t *storage,
                                     uint32_t capacity_dwords) {
    if (!s) return;
    s->words = storage;
    s->capacity_dwords = capacity_dwords;
    s->used_dwords = 0;
    s->failed = (!storage || !capacity_dwords) ? 1u : 0u;
}

static inline int virgl_stream_word(VirglStream *s, uint32_t word) {
    if (!s || s->failed || !s->words ||
        s->used_dwords >= s->capacity_dwords) {
        if (s) s->failed = 1;
        return 0;
    }
    s->words[s->used_dwords++] = word;
    return 1;
}

/* Append a complete command. payload may be null only when payload_dwords=0. */
static inline int virgl_stream_command(VirglStream *s, uint8_t opcode,
                                       uint8_t object_type,
                                       const uint32_t *payload,
                                       uint16_t payload_dwords) {
    if (!s || s->failed || !s->words || s->used_dwords > s->capacity_dwords ||
        (payload_dwords && !payload) ||
        (uint32_t)payload_dwords + 1u > s->capacity_dwords - s->used_dwords) {
        if (s) s->failed = 1;
        return 0;
    }
    if (!virgl_stream_word(s, VIRGL_CMD0(opcode, object_type, payload_dwords)))
        return 0;
    for (uint32_t i = 0; i < payload_dwords; ++i)
        if (!virgl_stream_word(s, payload[i])) return 0;
    return 1;
}

/* Validate every command boundary before passing a stream to the kernel. */
static inline int virgl_stream_validate(const uint32_t *words,
                                        uint32_t dword_count) {
    if (!words || !dword_count) return 0;
    uint32_t at = 0;
    while (at < dword_count) {
        uint32_t header = words[at];
        uint32_t opcode = header & 0xffu;
        uint32_t object_type = (header >> 8) & 0xffu;
        uint32_t payload = header >> 16;
        (void)object_type;
        if (opcode >= VIRGL_CCMD_MAX || object_type >= VIRGL_OBJECT_MAX ||
            payload > dword_count - at - 1u) return 0;
        at += 1u + payload;
    }
    return at == dword_count;
}


/* Encode a non-indexed DRAW_VBO packet (12 payload dwords). The reserved
   fields are explicitly zeroed so callers cannot leak uninitialized state. */
static inline int virgl_stream_draw_arrays(VirglStream *s, uint32_t start,
                                           uint32_t count, uint32_t primitive,
                                           uint32_t instances) {
    if (!count || !instances || primitive > VIRGL_PRIM_TRIANGLE_FAN ||
        count - 1u > UINT32_MAX - start) {
        if (s) s->failed = 1;
        return 0;
    }
    const uint32_t p[VIRGL_DRAW_VBO_DWORDS] = {
        start, count, primitive, 0u, instances, 0u, 0u, 0u, 0u, start,
        start + count - 1u, 0u
    };
    return virgl_stream_command(s, VIRGL_CCMD_DRAW_VBO, VIRGL_OBJECT_NULL,
                                p, VIRGL_DRAW_VBO_DWORDS);
}

static inline uint32_t virgl_stream_bytes(const VirglStream *s) {
    if (!s || s->failed || !s->words ||
        !virgl_stream_validate(s->words, s->used_dwords)) return 0;
    return s->used_dwords * (uint32_t)sizeof(uint32_t);
}
