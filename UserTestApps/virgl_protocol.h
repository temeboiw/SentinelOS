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

/* Add-on 1/4: create a Gallium state object. The object handle is the
 * first payload dword, followed by the object's protocol-specific state. */
static inline int virgl_stream_create_object(VirglStream *s,
                                             uint32_t object_type,
                                             uint32_t handle,
                                             const uint32_t *state,
                                             uint16_t state_dwords) {
    if (object_type == VIRGL_OBJECT_NULL || object_type >= VIRGL_OBJECT_MAX ||
        !handle || state_dwords == UINT16_MAX ||
        (state_dwords && !state)) {
        if (s) s->failed = 1;
        return 0;
    }
    uint32_t payload[256];
    if ((uint32_t)state_dwords + 1u > (uint32_t)(sizeof(payload)/sizeof(payload[0]))) {
        if (s) s->failed = 1;
        return 0;
    }
    payload[0] = handle;
    for (uint32_t i = 0; i < state_dwords; ++i) payload[i + 1u] = state[i];
    return virgl_stream_command(s, VIRGL_CCMD_CREATE_OBJECT,
                                (uint8_t)object_type, payload,
                                (uint16_t)(state_dwords + 1u));
}

/* Add-on 2/4: bind a previously created Gallium state object. */
static inline int virgl_stream_bind_object(VirglStream *s,
                                           uint32_t object_type,
                                           uint32_t handle) {
    if (object_type == VIRGL_OBJECT_NULL || object_type >= VIRGL_OBJECT_MAX ||
        !handle) {
        if (s) s->failed = 1;
        return 0;
    }
    return virgl_stream_command(s, VIRGL_CCMD_BIND_OBJECT,
                                (uint8_t)object_type, &handle, 1u);
}

/* Add-on 3/4: bind a shader object to a pipeline stage.
 * shader_type follows Gallium PIPE_SHADER_* (vertex=0, fragment=1). */
static inline int virgl_stream_bind_shader(VirglStream *s,
                                           uint32_t shader_handle,
                                           uint32_t shader_type) {
    if (!shader_handle || shader_type > 5u) {
        if (s) s->failed = 1;
        return 0;
    }
    const uint32_t payload[2] = { shader_handle, shader_type };
    return virgl_stream_command(s, VIRGL_CCMD_BIND_SHADER,
                                VIRGL_OBJECT_NULL, payload, 2u);
}

/* Add-on 4/4: set a Gallium viewport (index + scale[3] + translate[3]).
 * Float parameters are passed as their IEEE-754 bit patterns to keep this
 * header independent of libm and runtime conversion helpers. */
static inline int virgl_stream_set_viewport_bits(VirglStream *s,
                                                 uint32_t index,
                                                 const uint32_t scale[3],
                                                 const uint32_t translate[3]) {
    if (!scale || !translate || index > 15u) {
        if (s) s->failed = 1;
        return 0;
    }
    const uint32_t payload[7] = {
        index, scale[0], scale[1], scale[2],
        translate[0], translate[1], translate[2]
    };
    return virgl_stream_command(s, VIRGL_CCMD_SET_VIEWPORT_STATE,
                                VIRGL_OBJECT_NULL, payload, 7u);
}

/* Batch 2, add-on 3/4: create a TGSI shader object. shader_type follows
 * Gallium PIPE_SHADER_* (vertex=0, fragment=1). tokens must be a valid TGSI
 * token stream; this encoder does not compile GLSL or validate shader tokens. */
static inline int virgl_stream_create_shader(VirglStream *s,
                                              uint32_t handle,
                                              uint32_t shader_type,
                                              const uint32_t *tokens,
                                              uint32_t token_count) {
    if (!s || !handle || shader_type > 5u || !tokens || !token_count ||
        token_count > UINT16_MAX - 4u) {
        if (s) s->failed = 1;
        return 0;
    }
    uint32_t payload_dwords = 4u + token_count;
    if (s->failed || payload_dwords + 1u > s->capacity_dwords - s->used_dwords) {
        s->failed = 1;
        return 0;
    }
    if (!virgl_stream_word(s, VIRGL_CMD0(VIRGL_CCMD_CREATE_OBJECT,
                                         VIRGL_OBJECT_SHADER, payload_dwords)) ||
        !virgl_stream_word(s, handle) ||
        !virgl_stream_word(s, shader_type) ||
        !virgl_stream_word(s, 0u) ||
        !virgl_stream_word(s, token_count)) return 0;
    for (uint32_t i = 0; i < token_count; ++i)
        if (!virgl_stream_word(s, tokens[i])) return 0;
    return 1;
}

/* Batch 2, add-on 4/4: destroy a previously created Gallium object. */
static inline int virgl_stream_destroy_object(VirglStream *s,
                                               uint32_t object_type,
                                               uint32_t handle) {
    if (!s || object_type == VIRGL_OBJECT_NULL ||
        object_type >= VIRGL_OBJECT_MAX || !handle) {
        if (s) s->failed = 1;
        return 0;
    }
    return virgl_stream_command(s, VIRGL_CCMD_DESTROY_OBJECT,
                                (uint8_t)object_type, &handle, 1u);
}


/* Batch 3, add-on 1/4: bind the vertex-element layout used by DRAW_VBO. */
static inline int virgl_stream_bind_vertex_elements(VirglStream *s,
                                                    uint32_t handle) {
    if (!handle) {
        if (s) s->failed = 1u;
        return 0;
    }
    return virgl_stream_command(s, VIRGL_CCMD_BIND_OBJECT,
                                VIRGL_OBJECT_VERTEX_ELEMENTS, &handle, 1u);
}

/* Batch 3, add-on 2/4: set a scissor rectangle in framebuffer pixel space.
 * max_x/max_y are exclusive upper bounds, matching Gallium scissor boxes. */
static inline int virgl_stream_set_scissor(VirglStream *s, uint32_t index,
                                           uint32_t min_x, uint32_t min_y,
                                           uint32_t max_x, uint32_t max_y) {
    if (index > 15u || max_x < min_x || max_y < min_y) {
        if (s) s->failed = 1u;
        return 0;
    }
    const uint32_t payload[5] = { index, min_x, min_y, max_x, max_y };
    return virgl_stream_command(s, VIRGL_CCMD_SET_SCISSOR_STATE,
                                VIRGL_OBJECT_NULL, payload, 5u);
}

/* Batch 3, add-on 3/4: bind a constant/uniform buffer to a shader stage.
 * buffer_handle is a VirGL resource handle; offset and size are byte units. */
static inline int virgl_stream_set_uniform_buffer(VirglStream *s,
                                                  uint32_t shader_type,
                                                  uint32_t slot,
                                                  uint32_t buffer_handle,
                                                  uint32_t offset_bytes,
                                                  uint32_t size_bytes) {
    if (shader_type > 5u || slot > 31u || !buffer_handle || !size_bytes ||
        (offset_bytes & 3u) || (size_bytes & 3u)) {
        if (s) s->failed = 1u;
        return 0;
    }
    const uint32_t payload[6] = {
        shader_type, slot, 0u, buffer_handle, offset_bytes, size_bytes
    };
    return virgl_stream_command(s, VIRGL_CCMD_SET_UNIFORM_BUFFER,
                                VIRGL_OBJECT_NULL, payload, 6u);
}

/* Batch 3, add-on 4/4: set the fragment sample mask. */
static inline int virgl_stream_set_sample_mask(VirglStream *s,
                                               uint32_t sample_mask) {
    return virgl_stream_command(s, VIRGL_CCMD_SET_SAMPLE_MASK,
                                VIRGL_OBJECT_NULL, &sample_mask, 1u);
}


/* Batch 4, add-on 1/4: configure framebuffer attachments.
 * cbuf_handles contains one VirGL surface handle per color attachment;
 * depth_handle is zero when no depth/stencil surface is attached. */
static inline int virgl_stream_set_framebuffer(VirglStream *s,
                                               uint32_t color_count,
                                               uint32_t depth_handle,
                                               const uint32_t *cbuf_handles) {
    if (color_count > 8u || (color_count && !cbuf_handles)) {
        if (s) s->failed = 1u;
        return 0;
    }
    uint32_t payload[10];
    payload[0] = color_count;
    payload[1] = depth_handle;
    for (uint32_t i = 0; i < color_count; ++i) {
        if (!cbuf_handles[i]) {
            if (s) s->failed = 1u;
            return 0;
        }
        payload[i + 2u] = cbuf_handles[i];
    }
    return virgl_stream_command(s, VIRGL_CCMD_SET_FRAMEBUFFER_STATE,
                                VIRGL_OBJECT_NULL, payload,
                                (uint16_t)(color_count + 2u));
}

/* Batch 4, add-on 2/4: clear selected framebuffer attachments.
 * color/depth are IEEE-754 bit patterns, not numeric integer values. */
static inline int virgl_stream_clear_bits(VirglStream *s, uint32_t buffers,
                                          const uint32_t color[4],
                                          uint32_t depth_bits,
                                          uint32_t stencil) {
    if (!buffers || !color) {
        if (s) s->failed = 1u;
        return 0;
    }
    const uint32_t payload[7] = {
        buffers, color[0], color[1], color[2], color[3],
        depth_bits, stencil
    };
    return virgl_stream_command(s, VIRGL_CCMD_CLEAR, VIRGL_OBJECT_NULL,
                                payload, 7u);
}

/* Batch 4, add-on 3/4: set vertex-buffer bindings.
 * Each entry is {slot, resource_handle, stride_bytes, offset_bytes}. */
static inline int virgl_stream_set_vertex_buffers(VirglStream *s,
                                                  const uint32_t *entries,
                                                  uint32_t buffer_count) {
    if (!s || !entries || !buffer_count || buffer_count > 16u) {
        if (s) s->failed = 1u;
        return 0;
    }
    uint32_t payload[1u + 4u * 16u];
    payload[0] = buffer_count;
    for (uint32_t i = 0; i < buffer_count; ++i) {
        const uint32_t *e = entries + i * 4u;
        if (e[0] > 31u || !e[1] || (e[2] && (e[3] % e[2]) != 0u)) {
            s->failed = 1u;
            return 0;
        }
        payload[1u + i * 4u] = e[0];
        payload[2u + i * 4u] = e[1];
        payload[3u + i * 4u] = e[2];
        payload[4u + i * 4u] = e[3];
    }
    return virgl_stream_command(s, VIRGL_CCMD_SET_VERTEX_BUFFERS,
                                VIRGL_OBJECT_NULL, payload,
                                (uint16_t)(1u + buffer_count * 4u));
}

/* Batch 4, add-on 4/4: request a minimum number of samples for subsequent
 * rendering. The backend may clamp this to the framebuffer's sample count. */
static inline int virgl_stream_set_min_samples(VirglStream *s,
                                               uint32_t samples) {
    if (samples > 32u) {
        if (s) s->failed = 1u;
        return 0;
    }
    return virgl_stream_command(s, VIRGL_CCMD_SET_MIN_SAMPLES,
                                VIRGL_OBJECT_NULL, &samples, 1u);
}

static inline uint32_t virgl_stream_bytes(const VirglStream *s) {
    if (!s || s->failed || !s->words ||
        !virgl_stream_validate(s->words, s->used_dwords)) return 0;
    return s->used_dwords * (uint32_t)sizeof(uint32_t);
}
