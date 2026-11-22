/*
 * DLM Universe native GPU scene command (opcode 10, 15 u128 operands).
 * Compiled into the generated DLM C translation unit by Build.ps1.
 * All particle buffers, spatial bins, and integration stay GPU-resident.
 * No OpenGL headers, DLLs, external shader files, or OpenGL import library needed.
 * This is an application-specific native rendering backend, not a DLM language intrinsic.
 */
#include <stddef.h>
#include <time.h>
#include "../native/gpu_shaders.h"

typedef unsigned int up_enum;
typedef unsigned int up_uint;
typedef int up_int;
typedef ptrdiff_t up_size;
#define UPFN(ret, name, ...) static ret (*up_##name)(__VA_ARGS__)
UPFN(up_uint, glCreateShader, up_enum);
UPFN(void, glShaderSource, up_uint, up_int, const char *const *, const up_int *);
UPFN(void, glCompileShader, up_uint);
UPFN(void, glGetShaderiv, up_uint, up_enum, up_int *);
UPFN(void, glGetShaderInfoLog, up_uint, up_int, up_int *, char *);
UPFN(void, glDeleteShader, up_uint);
UPFN(up_uint, glCreateProgram, void);
UPFN(void, glAttachShader, up_uint, up_uint);
UPFN(void, glLinkProgram, up_uint);
UPFN(void, glGetProgramiv, up_uint, up_enum, up_int *);
UPFN(void, glGetProgramInfoLog, up_uint, up_int, up_int *, char *);
UPFN(void, glDeleteProgram, up_uint);
UPFN(void, glUseProgram, up_uint);
UPFN(up_int, glGetUniformLocation, up_uint, const char *);
UPFN(void, glUniform1i, up_int, up_int);
UPFN(void, glUniform1ui, up_int, up_uint);
UPFN(void, glUniform1f, up_int, float);
UPFN(void, glUniform2i, up_int, up_int, up_int);
UPFN(void, glUniform2f, up_int, float, float);
UPFN(void, glUniform3f, up_int, float, float, float);
UPFN(void, glUniform4f, up_int, float, float, float, float);
UPFN(void, glGenVertexArrays, up_int, up_uint *);
UPFN(void, glBindVertexArray, up_uint);
UPFN(void, glDeleteVertexArrays, up_int, const up_uint *);
UPFN(void, glDrawArrays, up_enum, up_int, up_int);
UPFN(unsigned char, glIsEnabled, up_enum);
UPFN(void, glGenQueries, up_int, up_uint *);
UPFN(void, glBeginQuery, up_enum, up_uint);
UPFN(void, glEndQuery, up_enum);
UPFN(void, glGetQueryObjectuiv, up_uint, up_enum, up_uint *);
UPFN(void, glDeleteQueries, up_int, const up_uint *);
UPFN(void, glGenBuffers, up_int, up_uint *);
UPFN(void, glBindBuffer, up_enum, up_uint);
UPFN(void, glBufferData, up_enum, up_size, const void *, up_enum);
UPFN(void, glGetInteger64v, up_enum, int64_t *);
UPFN(void, glBindBufferBase, up_enum, up_uint, up_uint);
UPFN(void, glDeleteBuffers, up_int, const up_uint *);
UPFN(void, glDispatchCompute, up_uint, up_uint, up_uint);
UPFN(void, glMemoryBarrier, up_enum);
UPFN(void, glGenFramebuffers, up_int, up_uint *);
UPFN(void, glBindFramebuffer, up_enum, up_uint);
UPFN(void, glFramebufferTexture2D, up_enum, up_enum, up_enum, up_uint, up_int);
UPFN(up_enum, glCheckFramebufferStatus, up_enum);
UPFN(void, glDeleteFramebuffers, up_int, const up_uint *);
UPFN(void, glActiveTexture, up_enum);
UPFN(void, glGetFloatv, up_enum, float *);
#undef UPFN

/* A shader storage block has a per-binding size limit independent of VRAM.
   Use multiple GPU buffers, each no larger than 2M particles / 64 MB. */
#define UP_PARTICLE_MAX_CHUNKS 16u
#define UP_PARTICLE_IDEAL_CHUNK 2000000u
#define UP_PARTICLE_BYTES 32u

static struct {
    up_uint program, emergency, init, clear, bin, update, vao, particles[UP_PARTICLE_MAX_CHUNKS], grid;
    up_uint composite, hdr_fbo, hdr_texture;
    int hdr_width, hdr_height;
    unsigned chunks, chunk_capacity;
    up_uint count, mode, seed, rendered_count, memory_fallback_count;
    uint64_t tick;
    int initialized, compute, failed, emergency_active, queries_available;
    up_uint checked_count, checked_mode, checked_seed;
    unsigned frames;
    double start_seconds;
} up_state;

static void up_log(const char *stage, const char *detail) {
    FILE *f = fopen("universe-gpu.log", "ab");
    if (f) { fprintf(f, "%s: %s\n", stage, detail ? detail : ""); fclose(f); }
}
static double up_seconds(void) {
#ifdef _WIN32
    return (double)GetTickCount64() / 1000.0;
#else
    return (double)clock() / CLOCKS_PER_SEC;
#endif
}
static void up_physics_stats(FILE *f);
static void up_stats(void) {
    up_state.frames++;
    double now = up_seconds();
    if (now - up_state.start_seconds < 1.0) return;
    double fps = (double)up_state.frames / (now - up_state.start_seconds);
    FILE *f = fopen("universe-gpu-stats.txt", "wb");
    if (f) {
        fprintf(f, "GPU particles: %u\nRenderer: %s\nFrames/s: %.1f\nFrame interval (CPU/wall): %.2f ms\nMode: %u\n",
                up_state.rendered_count,
                up_state.mode==3u ? "OpenGL spectral sea surface" :
                up_state.mode==6u ? "OpenGL 4.3 compute complex wave field" :
                up_state.mode==7u ? "OpenGL Schwarzschild ray tracing" :
                up_state.memory_fallback_count ? "OpenGL procedural (SSBO allocation fallback)" :
                (up_state.compute ? "OpenGL 4.3 compute + SSBO" : "OpenGL 3.3 procedural"),
                fps, 1000.0 / fps, up_state.mode);
        fprintf(f, "Particle SSBO: %.1f MiB\nGrid SSBO: %.3f MiB\n",
                up_state.compute && !up_state.memory_fallback_count ? (double)up_state.count * 32.0 / 1048576.0 : 0.0,
                up_state.grid ? 128.0 * 96.0 * 5.0 * 4.0 / 1048576.0 : 0.0);
        fprintf(f, "Estimated particle shader data: %.3f GB/frame, %.1f GB/s at 60 FPS (model, not measured)\n",
                up_state.compute && !up_state.memory_fallback_count ? (double)up_state.count * (up_state.mode == 0u ? 96.0 : 80.0) / 1e9 : 0.0,
                up_state.compute && !up_state.memory_fallback_count ? (double)up_state.count * (up_state.mode == 0u ? 96.0 : 80.0) * 60.0 / 1e9 : 0.0);
        fprintf(f, "Particle accumulation: RGBA16F HDR, %dx%d, %.2f MiB\n",
                up_state.hdr_width, up_state.hdr_height,
                (double)up_state.hdr_width * up_state.hdr_height * 8.0 / 1048576.0);
        fprintf(f, "Particle chunks: %u (max %u particles/chunk)\n",
                up_state.compute && !up_state.memory_fallback_count ? up_state.chunks : 0u,
                up_state.compute && !up_state.memory_fallback_count ? up_state.chunk_capacity : 0u);
        up_physics_stats(f);
        fprintf(f, "GPU: %s\n", (const char *)dgl_glGetString(0x1F01u));
        fprintf(f, "Render fallback: %s\n", up_state.memory_fallback_count ? "procedural GPU fallback (SSBO size/allocation)" :
                (up_state.emergency_active ? "procedural GPU fallback (no fragments from primary)" : "none"));
        fclose(f);
    }
    up_state.start_seconds = now;
    up_state.frames = 0;
}
static void *up_getproc(const char *name) {
    void *fn = dgl_proc(name);
#ifdef _WIN32
    /* Windows exports GL 1.1 entries (including glDrawArrays) from opengl32.dll,
       not through wglGetProcAddress. */
    if (!fn) {
        HMODULE gl = GetModuleHandleA("opengl32.dll");
        if (gl) fn = (void *)GetProcAddress(gl, name);
    }
#endif
    return fn;
}
static int up_load(void) {
#define UPLOAD(name) do { *(void **)(&up_##name) = up_getproc(#name); if (!up_##name) { up_log("missing GL symbol", #name); return 0; } } while(0)
    UPLOAD(glCreateShader); UPLOAD(glShaderSource); UPLOAD(glCompileShader); UPLOAD(glGetShaderiv);
    UPLOAD(glGetShaderInfoLog); UPLOAD(glDeleteShader); UPLOAD(glCreateProgram); UPLOAD(glAttachShader);
    UPLOAD(glLinkProgram); UPLOAD(glGetProgramiv); UPLOAD(glGetProgramInfoLog); UPLOAD(glDeleteProgram);
    UPLOAD(glUseProgram); UPLOAD(glGetUniformLocation); UPLOAD(glUniform1i); UPLOAD(glUniform1ui);
    UPLOAD(glUniform1f); UPLOAD(glUniform2i); UPLOAD(glUniform2f); UPLOAD(glUniform3f); UPLOAD(glUniform4f);
    UPLOAD(glGenVertexArrays); UPLOAD(glBindVertexArray); UPLOAD(glDeleteVertexArrays); UPLOAD(glDrawArrays);
    UPLOAD(glIsEnabled);
    UPLOAD(glGenFramebuffers); UPLOAD(glBindFramebuffer); UPLOAD(glFramebufferTexture2D);
    UPLOAD(glCheckFramebufferStatus); UPLOAD(glDeleteFramebuffers); UPLOAD(glActiveTexture);
    UPLOAD(glGetFloatv);
    up_glGenQueries = (void (*)(up_int, up_uint *))up_getproc("glGenQueries");
    up_glBeginQuery = (void (*)(up_enum, up_uint))up_getproc("glBeginQuery");
    up_glEndQuery = (void (*)(up_enum))up_getproc("glEndQuery");
    up_glGetQueryObjectuiv = (void (*)(up_uint, up_enum, up_uint *))up_getproc("glGetQueryObjectuiv");
    up_glDeleteQueries = (void (*)(up_int, const up_uint *))up_getproc("glDeleteQueries");
    up_state.queries_available = !!(up_glGenQueries && up_glBeginQuery && up_glEndQuery && up_glGetQueryObjectuiv && up_glDeleteQueries);
    return 1;
#undef UPLOAD
}
static int up_compute_load(void) {
#define UPLOAD(name) do { *(void **)(&up_##name) = up_getproc(#name); if (!up_##name) { up_log("missing compute symbol", #name); return 0; } } while(0)
    UPLOAD(glGenBuffers); UPLOAD(glBindBuffer); UPLOAD(glBufferData); UPLOAD(glGetInteger64v); UPLOAD(glBindBufferBase);
    UPLOAD(glDeleteBuffers); UPLOAD(glDispatchCompute); UPLOAD(glMemoryBarrier);
    return 1;
#undef UPLOAD
}
static up_uint up_shader(up_enum type, const char *source) {
    up_uint shader = up_glCreateShader(type);
    if (!shader) return 0;
    up_glShaderSource(shader, 1, &source, NULL);
    up_glCompileShader(shader);
    up_int ok = 0;
    up_glGetShaderiv(shader, 0x8B81u, &ok); /* GL_COMPILE_STATUS */
    if (ok) return shader;
    char log[2048] = {0};
    up_glGetShaderInfoLog(shader, sizeof log - 1, NULL, log);
    up_log("shader compile failed", log);
    up_glDeleteShader(shader);
    return 0;
}
static up_uint up_program(const char *vertex, const char *fragment, const char *compute) {
    up_uint vs = vertex ? up_shader(0x8B31u, vertex) : 0;
    up_uint fs = fragment ? up_shader(0x8B30u, fragment) : 0;
    up_uint cs = compute ? up_shader(0x91B9u, compute) : 0;
    if ((vertex && !vs) || (fragment && !fs) || (compute && !cs)) {
        if (vs) up_glDeleteShader(vs);
        if (fs) up_glDeleteShader(fs);
        if (cs) up_glDeleteShader(cs);
        return 0;
    }
    up_uint program = up_glCreateProgram();
    if (vs) up_glAttachShader(program, vs);
    if (fs) up_glAttachShader(program, fs);
    if (cs) up_glAttachShader(program, cs);
    up_glLinkProgram(program);
    up_int ok = 0;
    up_glGetProgramiv(program, 0x8B82u, &ok); /* GL_LINK_STATUS */
    if (!ok) {
        char log[2048] = {0};
        up_glGetProgramInfoLog(program, sizeof log - 1, NULL, log);
        up_log("shader link failed", log);
        up_glDeleteProgram(program);
        program = 0;
    }
    if (vs) up_glDeleteShader(vs);
    if (fs) up_glDeleteShader(fs);
    if (cs) up_glDeleteShader(cs);
    return program;
}
static void up_uniform_i(up_uint p, const char *n, int x) {
    up_int loc = up_glGetUniformLocation(p, n);
    if (loc >= 0) up_glUniform1i(loc, x);
}
static void up_uniform_u(up_uint p, const char *n, up_uint x) {
    up_int loc = up_glGetUniformLocation(p, n);
    if (loc >= 0) up_glUniform1ui(loc, x);
}
static void up_uniform_f(up_uint p, const char *n, float x) {
    up_int loc = up_glGetUniformLocation(p, n);
    if (loc >= 0) up_glUniform1f(loc, x);
}
static void up_release_hdr(void) {
    if (up_state.hdr_fbo) up_glDeleteFramebuffers(1, &up_state.hdr_fbo);
    if (up_state.hdr_texture) dgl_glDeleteTextures(1, &up_state.hdr_texture);
    up_state.hdr_fbo = up_state.hdr_texture = 0;
    up_state.hdr_width = up_state.hdr_height = 0;
}
/* An RGBA8 blend target rounds faint individual contributions to zero. At
   millions of particles, normalizing brightness by uCount crosses that limit.
   Accumulate every chunk in floating point, then tone-map once into the UI. */
static int up_hdr_target(int width, int height) {
    if (up_state.hdr_fbo && up_state.hdr_width == width && up_state.hdr_height == height) return 1;
    up_int framebuffer = 0, active_texture = 0, texture = 0;
    dgl_glGetIntegerv(0x8CA6u, &framebuffer); /* DRAW_FRAMEBUFFER_BINDING */
    dgl_glGetIntegerv(0x84E0u, &active_texture); /* ACTIVE_TEXTURE */
    up_glActiveTexture(0x84C0u);
    dgl_glGetIntegerv(0x8069u, &texture); /* TEXTURE_BINDING_2D on unit 0 */
    up_release_hdr();
    dgl_glGenTextures(1, &up_state.hdr_texture);
    up_glGenFramebuffers(1, &up_state.hdr_fbo);
    dgl_glBindTexture(0x0DE1u, up_state.hdr_texture);
    dgl_glTexParameteri(0x0DE1u, 0x2801u, 0x2600u);
    dgl_glTexParameteri(0x0DE1u, 0x2800u, 0x2600u);
    dgl_glTexParameteri(0x0DE1u, 0x2802u, 0x812Fu);
    dgl_glTexParameteri(0x0DE1u, 0x2803u, 0x812Fu);
    dgl_glTexImage2D(0x0DE1u, 0, 0x881Au, width, height, 0, 0x1908u, 0x1406u, NULL); /* RGBA16F, RGBA, FLOAT */
    up_glBindFramebuffer(0x8CA9u, up_state.hdr_fbo); /* DRAW_FRAMEBUFFER */
    up_glFramebufferTexture2D(0x8CA9u, 0x8CE0u, 0x0DE1u, up_state.hdr_texture, 0);
    up_enum status = up_glCheckFramebufferStatus(0x8CA9u);
    up_enum error = dgl_glGetError();
    up_glBindFramebuffer(0x8CA9u, (up_uint)framebuffer);
    dgl_glBindTexture(0x0DE1u, (up_uint)texture);
    up_glActiveTexture((up_enum)active_texture);
    if (!up_state.hdr_fbo || !up_state.hdr_texture || status != 0x8CD5u || error) {
        char detail[128];
        snprintf(detail, sizeof detail, "%dx%d, framebuffer=0x%04X, GL=0x%04X", width, height, status, error);
        up_log("HDR particle target allocation failed", detail);
        up_release_hdr();
        return 0;
    }
    up_state.hdr_width = width; up_state.hdr_height = height;
    return 1;
}
static void up_physics_destroy(void);
static unsigned up_physics_flags;

#include "cosmic_catalog.inc"

static void up_destroy(void) {
    up_cosmic_destroy();
    up_physics_destroy();
    up_release_hdr();
    if (up_state.composite) up_glDeleteProgram(up_state.composite);
    if (up_state.program) up_glDeleteProgram(up_state.program);
    if (up_state.emergency) up_glDeleteProgram(up_state.emergency);
    if (up_state.init) up_glDeleteProgram(up_state.init);
    if (up_state.clear) up_glDeleteProgram(up_state.clear);
    if (up_state.bin) up_glDeleteProgram(up_state.bin);
    if (up_state.update) up_glDeleteProgram(up_state.update);
    if (up_state.chunks) up_glDeleteBuffers((up_int)up_state.chunks, up_state.particles);
    if (up_state.grid) up_glDeleteBuffers(1, &up_state.grid);
    if (up_state.vao) up_glDeleteVertexArrays(1, &up_state.vao);
    memset(&up_state, 0, sizeof up_state);
}
static int up_init_renderer(void) {
    if (up_state.initialized) return !up_state.failed;
    up_state.initialized = 1;
    int maj = 0, min = 0;
    const char *version = (const char *)dgl_glGetString(0x1F02u);
    if (!version || sscanf(version, "%d.%d", &maj, &min) != 2 || maj < 3 || (maj == 3 && min < 3)) {
        up_log("unsupported OpenGL", version ? version : "no version");
        up_state.failed = 1;
        return 0;
    }
    up_log("OpenGL version", version);
    if (!up_load()) { up_state.failed = 1; return 0; }
    if (maj > 4 || (maj == 4 && min >= 3)) {
        if (up_compute_load()) {
            up_state.program = up_program(up_sh_draw_compute, up_sh_fragment, NULL);
            up_state.init = up_program(NULL, NULL, up_sh_init);
            up_state.clear = up_program(NULL, NULL, up_sh_clear);
            up_state.bin = up_program(NULL, NULL, up_sh_bin);
            up_state.update = up_program(NULL, NULL, up_sh_update);
            if (up_state.program && up_state.init && up_state.clear && up_state.bin && up_state.update) up_state.compute = 1;
            else {
                if (up_state.program) up_glDeleteProgram(up_state.program);
                if (up_state.init) up_glDeleteProgram(up_state.init);
                if (up_state.clear) up_glDeleteProgram(up_state.clear);
                if (up_state.bin) up_glDeleteProgram(up_state.bin);
                if (up_state.update) up_glDeleteProgram(up_state.update);
                up_state.program = up_state.init = up_state.clear = up_state.bin = up_state.update = 0;
            }
        }
    }
    if (!up_state.compute) up_state.program = up_program(up_sh_draw_procedural, up_sh_fragment, NULL);
    if (!up_state.program) { up_state.failed = 1; return 0; }
    up_state.composite = up_program(up_sh_composite_vertex, up_sh_composite_fragment, NULL);
    if (!up_state.composite) { up_state.failed = 1; return 0; }
    up_glGenVertexArrays(1, &up_state.vao);
    if (!up_state.vao) { up_state.failed = 1; return 0; }
    up_state.start_seconds = up_seconds();
    up_log("renderer", up_state.compute ? "OpenGL 4.3 compute + SSBO" : "OpenGL 3.3 procedural");
    return 1;
}
static void up_release_particles(void) {
    if (up_state.chunks) up_glDeleteBuffers((up_int)up_state.chunks, up_state.particles);
    memset(up_state.particles, 0, sizeof up_state.particles);
    up_state.chunks = 0;
    up_state.chunk_capacity = 0;
    up_state.count = 0;
}
static unsigned up_chunk_offset(unsigned chunk) {
    return chunk * up_state.chunk_capacity;
}
static unsigned up_chunk_count(unsigned chunk) {
    unsigned offset = up_chunk_offset(chunk);
    unsigned remaining = up_state.count - offset;
    return remaining < up_state.chunk_capacity ? remaining : up_state.chunk_capacity;
}
static int up_allocate(unsigned count, unsigned mode, unsigned seed, uint64_t tick) {
    if (!up_state.compute) {
        up_state.count = count; up_state.mode = mode; up_state.seed = seed; up_state.tick = tick;
        return 1;
    }
    if (!up_cosmic_prepare(seed)) return 0;
    if (up_state.chunks && up_state.count == count && up_state.mode == mode &&
        up_state.seed == seed && tick >= up_state.tick) return 1;

    int reuse = up_state.chunks && up_state.count == count;
    if (!reuse) {
        up_release_particles();
        int64_t limit = 0;
        up_glGetInteger64v(0x90DEu, &limit); /* GL_MAX_SHADER_STORAGE_BLOCK_SIZE */
        if (limit < (int64_t)UP_PARTICLE_BYTES * 256) {
            up_log("GPU memory limit", "GL_MAX_SHADER_STORAGE_BLOCK_SIZE too small or unavailable");
            return 0;
        }
        unsigned capacity = (unsigned)((limit / UP_PARTICLE_BYTES < UP_PARTICLE_IDEAL_CHUNK) ?
                                       limit / UP_PARTICLE_BYTES : UP_PARTICLE_IDEAL_CHUNK);
        unsigned chunks = (count + capacity - 1u) / capacity;
        if (!capacity || !chunks || chunks > UP_PARTICLE_MAX_CHUNKS) {
            up_log("GPU memory limit", "particle chunks exceed supported pool size");
            return 0;
        }
        up_state.chunks = chunks;
        up_state.chunk_capacity = capacity;
        up_state.count = count;
        up_glGenBuffers((up_int)chunks, up_state.particles);
        for (unsigned c = 0; c < chunks; ++c) {
            if (!up_state.particles[c]) {
                up_log("GPU memory allocation", "glGenBuffers returned zero");
                up_release_particles();
                return 0;
            }
            unsigned n = up_chunk_count(c);
            up_glBindBuffer(0x90D2u, up_state.particles[c]);
            up_glBufferData(0x90D2u, (up_size)n * UP_PARTICLE_BYTES, NULL, 0x88E8u);
            up_enum error = dgl_glGetError();
            if (error) {
                char detail[160];
                snprintf(detail, sizeof detail, "chunk %u/%u (%u particles): GL 0x%04X",
                         c + 1u, chunks, n, error);
                up_log("GPU memory allocation", detail);
                up_release_particles();
                return 0;
            }
        }
        char detail[160];
        snprintf(detail, sizeof detail, "count=%u; chunks=%u; capacity=%u; max SSBO block=%lld bytes",
                 count, chunks, capacity, (long long)limit);
        up_log("GPU particle allocation", detail);
    }
    if (!up_state.grid) up_glGenBuffers(1, &up_state.grid);
    if (!up_state.grid) {
        up_log("GPU memory allocation", "grid buffer creation failed");
        up_release_particles();
        return 0;
    }
    up_glBindBuffer(0x90D2u, up_state.grid);
    up_glBufferData(0x90D2u, 128 * 96 * 5 * (up_size)sizeof(up_uint), NULL, 0x88E8u);
    up_enum grid_error = dgl_glGetError();
    if (grid_error) {
        up_log("GPU memory allocation", "grid buffer allocation failed");
        up_release_particles();
        return 0;
    }
    up_glBindBufferBase(0x90D2u, 1u, up_state.grid);
    up_glUseProgram(up_state.init);
    up_uniform_u(up_state.init, "uCount", count);
    up_uniform_u(up_state.init, "uSeed", seed);
    up_uniform_u(up_state.init, "uEdgeCount", mode==5u?up_cosmic.edge_count:0u);
    up_uniform_i(up_state.init, "uMode", (int)mode);
    for (unsigned c = 0; c < up_state.chunks; ++c) {
        unsigned local_count = up_chunk_count(c);
        up_glBindBufferBase(0x90D2u, 0u, up_state.particles[c]);
        up_uniform_u(up_state.init, "uBase", up_chunk_offset(c));
        up_uniform_u(up_state.init, "uChunkCount", local_count);
        up_glDispatchCompute((local_count + 255u) / 256u, 1u, 1u);
    }
    up_glMemoryBarrier(0x2000u); /* GL_SHADER_STORAGE_BARRIER_BIT */
    up_state.mode = mode; up_state.seed = seed; up_state.tick = tick;
    return 1;
}
static void up_step(float delta, uint64_t tick, float mx, float my, float mouse_button) {
    if (!up_state.compute || !up_state.count || !up_state.chunks) return;
    up_glBindBufferBase(0x90D2u, 1u, up_state.grid);
    if (up_state.mode == 0u) {
        up_glUseProgram(up_state.clear);
        up_glDispatchCompute((128u * 96u * 5u + 255u) / 256u, 1u, 1u);
        up_glMemoryBarrier(0x2000u);
        up_glUseProgram(up_state.bin);
        up_uniform_u(up_state.bin, "uCount", up_state.count);
        for (unsigned c = 0; c < up_state.chunks; ++c) {
            unsigned local_count = up_chunk_count(c);
            up_glBindBufferBase(0x90D2u, 0u, up_state.particles[c]);
            up_uniform_u(up_state.bin, "uBase", up_chunk_offset(c));
            up_uniform_u(up_state.bin, "uChunkCount", local_count);
            up_glDispatchCompute((local_count + 255u) / 256u, 1u, 1u);
        }
        up_glMemoryBarrier(0x2000u);
    }
    up_glUseProgram(up_state.update);
    up_uniform_i(up_state.update, "uMode", (int)up_state.mode);
    up_uniform_u(up_state.update, "uCount", up_state.count);
    up_uniform_u(up_state.update, "uSeed", up_state.seed);
    up_uniform_f(up_state.update, "uTick", (float)tick);
    up_uniform_f(up_state.update, "uDelta", delta);
    up_int m = up_glGetUniformLocation(up_state.update, "uMouse");
    if (m >= 0) up_glUniform3f(m, mx, my, mouse_button);
    for (unsigned c = 0; c < up_state.chunks; ++c) {
        unsigned local_count = up_chunk_count(c);
        up_glBindBufferBase(0x90D2u, 0u, up_state.particles[c]);
        up_uniform_u(up_state.update, "uBase", up_chunk_offset(c));
        up_uniform_u(up_state.update, "uChunkCount", local_count);
        up_glDispatchCompute((local_count + 255u) / 256u, 1u, 1u);
    }
    up_glMemoryBarrier(0x2000u | 0x0001u); /* STORAGE + VERTEX_ATTRIB_ARRAY */
}
static void up_present_points(up_uint program, unsigned n, unsigned base, unsigned draw_count,
                              unsigned mode, unsigned seed, uint64_t tick, float cx, float cy, float zoom,
                              float rx, float ry, float rw, float rh,
                              int viewport_w, int viewport_h) {
    up_glUseProgram(program);
    up_int u = up_glGetUniformLocation(program, "uRect");
    if (u >= 0) up_glUniform4f(u, rx, ry, rw, rh);
    u = up_glGetUniformLocation(program, "uCamera");
    if (u >= 0) up_glUniform3f(u, cx, cy, zoom);
    u = up_glGetUniformLocation(program, "uViewport");
    if (u >= 0) up_glUniform2f(u, (float)viewport_w, (float)viewport_h);
    up_uniform_f(program, "uPointSize", fmaxf(1.8f, 2.0f * (float)viewport_w / 1200.f) * (mode==5u?fminf(4.f,fmaxf(1.f,zoom)):1.f));
    up_uniform_f(program, "uTick", (float)tick);
    up_uniform_i(program, "uMode", (int)mode);
    up_uniform_u(program, "uCount", n);
    up_uniform_u(program, "uBase", base);
    up_uniform_u(program, "uSeed", seed);
    up_uniform_i(program, "uFields", !!(up_physics_flags & 4u));
    up_uniform_i(program, "uSlice", !!(up_physics_flags & 8u));
    up_glDrawArrays(0x0000u, 0, (int)draw_count);
}
static void up_render_batches(up_uint program, unsigned n, unsigned mode, unsigned seed,
                              uint64_t tick, float cx, float cy, float zoom,
                              float rx, float ry, float rw, float rh,
                              int viewport_w, int viewport_h) {
    if (up_state.compute && program == up_state.program && up_state.chunks) {
        for (unsigned c = 0; c < up_state.chunks; ++c) {
            up_glBindBufferBase(0x90D2u, 0u, up_state.particles[c]);
            up_present_points(program, n, up_chunk_offset(c), up_chunk_count(c), mode, seed,
                              tick, cx, cy, zoom, rx, ry, rw, rh, viewport_w, viewport_h);
        }
    } else {
        up_present_points(program, n, 0, n, mode, seed, tick, cx, cy, zoom,
                          rx, ry, rw, rh, viewport_w, viewport_h);
    }
}
#include "physics_fields.inc"

static int up_draw(const nat_t *a, const dvy_scale *s, dvy_clip clip, int viewport_w, int viewport_h) {
    if (!up_init_renderer()) return 0;
    unsigned n = (unsigned)a[0], mode = (unsigned)a[1], seed = (unsigned)a[2];
    uint64_t tick = (uint64_t)a[3];
    if (n < 1 || n > 16000000u || mode > 7u) return 0;
    if((mode==1u||mode==5u||mode==6u)&&!up_state.compute){up_log("physical model unavailable","this scene requires OpenGL 4.3 compute");return 0;}
    float cx = (float)a[4] / 1000.f - 4096.f;
    float cy = (float)a[5] / 1000.f - 4096.f;
    float zoom = (float)a[6] / 1000.f;
    float mx = (float)a[7] / 1000.f - 4096.f;
    float my = (float)a[8] / 1000.f - 4096.f;
    float mb = (float)a[9];
    float rx = (float)dvy_c(s, a[10]) / 256.f;
    float ry = (float)dvy_c(s, a[11]) / 256.f;
    float rw = (float)dvy_c(s, a[12]) / 256.f;
    float rh = (float)dvy_c(s, a[13]) / 256.f;
    if (viewport_w < 1 || viewport_h < 1 || rw <= 0 || rh <= 0 || zoom <= 0) return 0;
    if (!up_hdr_target(viewport_w, viewport_h)) return 0;
    int field_scene = mode==3u || mode==6u || mode==7u;
    up_physics_flags=(unsigned)a[14];
    int reinitialize = (!up_state.count || up_state.count != n || up_state.mode != mode || up_state.seed != seed || tick < up_state.tick);
    int procedural_only = (up_state.memory_fallback_count == n);
    if (field_scene) {
        up_release_particles();
        up_state.memory_fallback_count=0;
        if(mode!=6u)up_phys.quantum_ready=0;
    }
    if (!field_scene && !procedural_only) {
        up_state.memory_fallback_count = 0;
        if (!up_allocate(n, mode, seed, tick)) {
            if(mode==1u||mode==5u){up_log("physical model allocation failed","reduce object quality; procedural substitution disabled");return 0;}
            if (!up_state.emergency)
                up_state.emergency = up_program(up_sh_draw_procedural, up_sh_fragment, NULL);
            if (!up_state.emergency) return 0;
            up_state.memory_fallback_count = n;
            procedural_only = 1;
        }
    }
    if (!field_scene && up_state.compute && !procedural_only && !reinitialize && tick > up_state.tick) {
        float delta = (float)(tick - up_state.tick);
        if (delta > 3.f) delta = 3.f;
        up_step(delta, tick, mx, my, mb);
    }
    up_state.tick = tick;
    up_state.rendered_count = field_scene ? 0u : n;
    up_state.mode = mode;
    up_state.seed = seed;
    dgl_scissor(clip);
    /* The vector UI and particle shaders share a compatibility-profile context.
       Do not inherit a stencil/depth/cull test from a preceding UI shape. */
    unsigned char stencil = up_glIsEnabled(0x0B90u);
    unsigned char depth = up_glIsEnabled(0x0B71u);
    unsigned char cull = up_glIsEnabled(0x0B44u);
    up_int profile = 0;
    dgl_glGetIntegerv(0x9126u, &profile); /* CONTEXT_PROFILE_MASK */
    unsigned char sprite = (profile & 2) ? up_glIsEnabled(0x8861u) : 0;
    if (profile & 2) dgl_glEnable(0x8861u); /* POINT_SPRITE in compatibility contexts */
    dgl_glDisable(0x0B90u);
    dgl_glDisable(0x0B71u);
    dgl_glDisable(0x0B44u);
    dgl_glColorMask(1, 1, 1, 1);
    dgl_glDisable(0x0DE1u);
    dgl_glEnable(0x0BE2u);
    dgl_glBlendFunc(1u, 1u); /* premultiplied additive light */
    dgl_glEnable(0x8642u); /* GL_PROGRAM_POINT_SIZE */
    up_glBindVertexArray(up_state.vao);

    up_int framebuffer = 0, viewport[4], active_texture = 0, texture = 0;
    float clear_color[4];
    int previous_vy = dgl_vy;
    unsigned char srgb = up_glIsEnabled(0x8DB9u); /* FRAMEBUFFER_SRGB */
    unsigned char scissor = up_glIsEnabled(0x0C11u);
    dgl_glGetIntegerv(0x8CA6u, &framebuffer);
    dgl_glGetIntegerv(0x0BA2u, viewport); /* VIEWPORT */
    up_glGetFloatv(0x0C22u, clear_color); /* COLOR_CLEAR_VALUE */
    up_glBindFramebuffer(0x8CA9u, up_state.hdr_fbo);
    dgl_glViewport(0, 0, viewport_w, viewport_h);
    dgl_glDisable(0x8DB9u);
    dgl_glDisable(0x0C11u);
    dgl_glClearColor(0.f, 0.f, 0.f, 0.f);
    dgl_glClear(0x00004000u);
    dgl_glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
    if (scissor) dgl_glEnable(0x0C11u);
    dgl_vy = 0;
    dgl_scissor(clip);

    /* Probe the first frame of each scene/quality setting. A successfully linked
       shader can still emit zero fragments on a particular driver/context. */
    int probe = !field_scene && mode!=1u && mode!=5u && up_state.queries_available &&
        (up_state.checked_count != n || up_state.checked_mode != mode || up_state.checked_seed != seed);
    if (probe) up_state.emergency_active = 0;
    up_uint query = 0;
    if (probe) {
        up_glGenQueries(1, &query);
        if (query) up_glBeginQuery(0x8C2Fu, query); /* GL_ANY_SAMPLES_PASSED */
    }
    up_uint program = (procedural_only || up_state.emergency_active) && up_state.emergency ? up_state.emergency : up_state.program;
    int field_ok=1;
    if(field_scene)field_ok=up_field_draw(mode,n,seed,tick,up_physics_flags,cx,cy,zoom,mx,my,mb,rx,ry,rw,rh,viewport_w,viewport_h);
    else up_render_batches(program,n,mode,seed,tick,cx,cy,zoom,rx,ry,rw,rh,viewport_w,viewport_h);
    if (query) {
        up_uint visible = 0;
        up_glEndQuery(0x8C2Fu);
        up_glGetQueryObjectuiv(query, 0x8866u, &visible); /* GL_QUERY_RESULT */
        up_glDeleteQueries(1, &query);
        if (!visible) {
            up_log("zero visible particle fragments", "trying procedural GPU fallback");
            if (!up_state.emergency)
                up_state.emergency = up_program(up_sh_draw_procedural, up_sh_fragment, NULL);
            if (up_state.emergency) {
                up_state.emergency_active = 1;
                up_render_batches(up_state.emergency, n, mode, seed, tick, cx, cy, zoom,
                                  rx, ry, rw, rh, viewport_w, viewport_h);
            }
        }
        up_state.checked_count = n;
        up_state.checked_mode = mode;
        up_state.checked_seed = seed;
    }
    /* Restore the actual UI draw target (which may itself be multisampled).
       Use its original viewport offset, including letterboxing, and keep the
       native texture unit/binding intact for subsequent vector UI gradients. */
    up_glBindFramebuffer(0x8CA9u, (up_uint)framebuffer);
    dgl_glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
    dgl_vy = previous_vy;
    dgl_glEnable(0x0C11u);
    dgl_scissor(clip);
    dgl_glGetIntegerv(0x84E0u, &active_texture);
    up_glActiveTexture(0x84C0u);
    dgl_glGetIntegerv(0x8069u, &texture);
    dgl_glBindTexture(0x0DE1u, up_state.hdr_texture);
    up_glUseProgram(up_state.composite);
    up_uniform_i(up_state.composite, "uAccumulation", 0);
    up_uniform_i(up_state.composite, "uOpaque", field_scene);
    if(field_scene)dgl_glBlendFunc(1u,0u);
    dgl_glDisable(0x8642u);
    up_glDrawArrays(0x0004u, 0, 3); /* full-viewport triangle */
    dgl_glBindTexture(0x0DE1u, (up_uint)texture);
    up_glActiveTexture((up_enum)active_texture);
    if (scissor) dgl_glEnable(0x0C11u); else dgl_glDisable(0x0C11u);
    if (srgb) dgl_glEnable(0x8DB9u);
    up_glBindVertexArray(0u);
    up_glUseProgram(0u);
    dgl_glDisable(0x8642u);
    dgl_glBlendFunc(0x0302u, 0x0303u);
    if (stencil) dgl_glEnable(0x0B90u);
    if (depth) dgl_glEnable(0x0B71u);
    if (cull) dgl_glEnable(0x0B44u);
    if ((profile & 2) && !sprite) dgl_glDisable(0x8861u);
    up_stats();
    up_enum error = dgl_glGetError();
    if (error) {
        char detail[96];
        snprintf(detail, sizeof detail, "glGetError=0x%04X, count=%u, mode=%u", error, n, mode);
        up_log("GPU draw error", detail);
    }
    return error == 0 && field_ok;
}
