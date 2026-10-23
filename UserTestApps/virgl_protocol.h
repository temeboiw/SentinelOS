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
    VIRGL_CCMD_BIND_SHADER = 31
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
    VIRGL_OBJECT_SURFACE = 8
};

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
        if (opcode > 63u || payload > dword_count - at - 1u) return 0;
        at += 1u + payload;
    }
    return at == dword_count;
}

static inline uint32_t virgl_stream_bytes(const VirglStream *s) {
    if (!s || s->failed || !s->words ||
        !virgl_stream_validate(s->words, s->used_dwords)) return 0;
    return s->used_dwords * (uint32_t)sizeof(uint32_t);
}
