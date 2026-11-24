#include "render.h"
#include "cel.h"
#include "util.h"
#include <SDL.h>
#include <SDL_opengl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "../third_party/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb_image_write.h"

/* The game's projection (launchme 0x102bc(-64, 192)): screen = v * 256 / (z + 64) on 320x240,
 * i.e. focal 256 px, eye 64 units behind the camera position, vertical FOV 2*atan(120/256). */
float GAME_FOV_DEG = 50.1944f;
#define GAME_EYE_BACK 64.0f

/* ---------------------------------------------------------------- GL loader */
#define GLFUNCS(X) \
    X(PFNGLCREATESHADERPROC, glCreateShader) X(PFNGLSHADERSOURCEPROC, glShaderSource) \
    X(PFNGLCOMPILESHADERPROC, glCompileShader) X(PFNGLGETSHADERIVPROC, glGetShaderiv) \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog) X(PFNGLCREATEPROGRAMPROC, glCreateProgram) \
    X(PFNGLATTACHSHADERPROC, glAttachShader) X(PFNGLLINKPROGRAMPROC, glLinkProgram) \
    X(PFNGLBINDATTRIBLOCATIONPROC, glBindAttribLocation) X(PFNGLUSEPROGRAMPROC, glUseProgram) \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation) X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv) \
    X(PFNGLUNIFORM1IPROC, glUniform1i) X(PFNGLUNIFORM1FPROC, glUniform1f) X(PFNGLGENBUFFERSPROC, glGenBuffers) X(PFNGLBINDBUFFERPROC, glBindBuffer) \
    X(PFNGLBUFFERDATAPROC, glBufferData) X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers) \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray) \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer) X(PFNGLACTIVETEXTUREPROC, glActiveTexture)
#define DECL(t, n) static t p_##n;
GLFUNCS(DECL)
typedef void (APIENTRYP PFN_GENMIPMAP)(GLenum);
static PFN_GENMIPMAP p_glGenerateMipmap;

static int gl_load(void) {
#define LOAD(t, n) p_##n = (t)SDL_GL_GetProcAddress(#n); if (!p_##n) { log_msg("GL: missing %s", #n); return -1; }
    GLFUNCS(LOAD)
    p_glGenerateMipmap = (PFN_GENMIPMAP)SDL_GL_GetProcAddress("glGenerateMipmap");
    if (!p_glGenerateMipmap) p_glGenerateMipmap = (PFN_GENMIPMAP)SDL_GL_GetProcAddress("glGenerateMipmapEXT");
    return 0;
}

/* ---------------------------------------------------------------- state */
typedef struct { GLuint vbo; int nb; Batch b[64]; } ObjGeo;

struct Renderer {
    int gles;
    char hd_dir[1024];
    GLuint prog; GLint u_mv, u_proj, u_bias, u_tex;
    GLuint white;
    GLuint *room_tex; int nroom_tex;
    ObjGeo *geo; int ngeo;
    GLuint *human_tex; int nhuman_tex;
    GLuint player_vbo;
    float anisotropy;
};

/* u_bias pulls a vertex toward the camera in view space (world units): room objects such as doors
 * and windows lie exactly on the shell's walls and must win the depth tie (the 3DO had no z-buffer
 * and simply drew them after the shell). */
static const char *VS =
    "attribute vec3 a_pos;\nattribute vec2 a_uv;\nuniform mat4 u_mv;\nuniform mat4 u_proj;\nuniform float u_bias;\n"
    "varying vec2 v_uv;\n"
    "void main() { v_uv = a_uv; vec4 e = u_mv * vec4(a_pos, 1.0); e.z += u_bias; gl_Position = u_proj * e; }\n";
static const char *FS =
    "varying vec2 v_uv;\nuniform sampler2D u_tex;\n"
    "void main() { vec4 c = texture2D(u_tex, v_uv); if (c.a < 0.5) discard; gl_FragColor = vec4(c.rgb / c.a, 1.0); }\n";

static GLuint compile(GLenum type, const char *src, int gles) {
    const char *pre = gles ? "#version 100\nprecision mediump float;\n" : "#version 120\n";
    const char *srcs[2] = {pre, src};
    GLuint s = p_glCreateShader(type);
    p_glShaderSource(s, 2, srcs, NULL);
    p_glCompileShader(s);
    GLint ok = 0; p_glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char logb[1024]; p_glGetShaderInfoLog(s, sizeof logb, NULL, logb); log_msg("shader: %s", logb); }
    return s;
}

static GLuint upload_rgba(Renderer *r, const uint8_t *rgba, int w, int h) {
    /* premultiply so filtering never bleeds the transparent colour; the shader divides it back */
    uint8_t *pm = malloc((size_t)w * h * 4);
    for (int i = 0; i < w * h; i++) {
        int a = rgba[4 * i + 3];
        for (int c = 0; c < 3; c++) pm[4 * i + c] = (uint8_t)(rgba[4 * i + c] * a / 255);
        pm[4 * i + 3] = (uint8_t)a;
    }
    GLuint t; glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pm);
    free(pm);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    int pot = !(w & (w - 1)) && !(h & (h - 1));
    if (p_glGenerateMipmap && (pot || !r->gles)) {
        p_glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        if (r->anisotropy > 1) glTexParameterf(GL_TEXTURE_2D, 0x84FE /* MAX_ANISOTROPY */, r->anisotropy);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    }
    return t;
}

/* HD override by content hash (assets_hd/textures/<sha1[:16]>.png), else decode the CEL */
static GLuint load_texture(Renderer *r, const uint8_t *raw, uint32_t size, int *hd_used) {
    if (r->hd_dir[0] && size) {
        char h16[17], name[64], path[1200];
        sha1_hex16(raw, size, h16);
        snprintf(name, sizeof name, "%s.png", h16);
        path_join(path, sizeof path, r->hd_dir, name);
        int w, h, n;
        uint8_t *px = stbi_load(path, &w, &h, &n, 4);
        if (px) { GLuint t = upload_rgba(r, px, w, h); stbi_image_free(px); (*hd_used)++; return t; }
    }
    Image im = {0};
    if (size && !cel_decode(raw, size, &im)) { GLuint t = upload_rgba(r, im.rgba, im.w, im.h); image_free(&im); return t; }
    return r->white;
}

Renderer *render_create(int gles, const char *hd_dir) {
    if (gl_load()) return NULL;
    Renderer *r = calloc(1, sizeof *r);
    r->gles = gles;
    if (hd_dir) snprintf(r->hd_dir, sizeof r->hd_dir, "%s", hd_dir);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    if (ext && strstr(ext, "texture_filter_anisotropic")) { glGetFloatv(0x84FF, &r->anisotropy); if (r->anisotropy > 8) r->anisotropy = 8; }
    GLuint vs = compile(GL_VERTEX_SHADER, VS, gles), fs = compile(GL_FRAGMENT_SHADER, FS, gles);
    r->prog = p_glCreateProgram();
    p_glAttachShader(r->prog, vs); p_glAttachShader(r->prog, fs);
    p_glBindAttribLocation(r->prog, 0, "a_pos"); p_glBindAttribLocation(r->prog, 1, "a_uv");
    p_glLinkProgram(r->prog);
    r->u_mv = p_glGetUniformLocation(r->prog, "u_mv");
    r->u_proj = p_glGetUniformLocation(r->prog, "u_proj");
    r->u_bias = p_glGetUniformLocation(r->prog, "u_bias");
    r->u_tex = p_glGetUniformLocation(r->prog, "u_tex");
    uint8_t white[4] = {255, 0, 255, 255};
    r->white = upload_rgba(r, white, 1, 1);
    p_glGenBuffers(1, &r->player_vbo);
    log_msg("GL: %s / %s%s", glGetString(GL_RENDERER), glGetString(GL_VERSION), r->anisotropy > 1 ? " (anisotropic)" : "");
    return r;
}

static void free_room(Renderer *r) {
    for (int i = 0; i < r->nroom_tex; i++) if (r->room_tex[i] != r->white) glDeleteTextures(1, &r->room_tex[i]);
    for (int i = 0; i < r->ngeo; i++) p_glDeleteBuffers(1, &r->geo[i].vbo);
    free(r->room_tex); free(r->geo);
    r->room_tex = NULL; r->geo = NULL; r->nroom_tex = r->ngeo = 0;
}

void render_destroy(Renderer *r) {
    if (!r) return;
    free_room(r);
    free(r->human_tex);
    free(r);
}

void render_set_room(Renderer *r, const Room *room) {
    free_room(r);
    int hd = 0;
    r->nroom_tex = room->ntex;
    r->room_tex = calloc((size_t)room->ntex + 1, sizeof *r->room_tex);
    for (int i = 0; i < room->ntex; i++)
        r->room_tex[i] = load_texture(r, room->file.data + room->tex_off[i], room->tex_size[i], &hd);
    r->ngeo = room->nobjects;
    r->geo = calloc((size_t)room->nobjects + 1, sizeof *r->geo);
    TriList tl = {0};
    for (int i = 0; i < room->nobjects; i++) {
        const RoomObject *o = &room->objects[i];
        int table[16];
        for (int k = 0; k < 16; k++) table[k] = (o->table[k] >= 0 && o->table[k] < room->ntex) ? o->table[k] : -1;
        trilist_reset(&tl);
        trilist_add_mesh(&tl, (const float (*)[3])o->mesh.verts, &o->mesh, table, 0);
        ObjGeo *g = &r->geo[i];
        p_glGenBuffers(1, &g->vbo);
        p_glBindBuffer(GL_ARRAY_BUFFER, g->vbo);
        p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vtx) * (size_t)tl.nv), tl.v, GL_STATIC_DRAW);
        g->nb = tl.nb < 64 ? tl.nb : 64;
        memcpy(g->b, tl.b, sizeof(Batch) * (size_t)g->nb);
    }
    free(tl.v);
    log_msg("room: %d objects, %d textures (%d HD)", room->nobjects, room->ntex, hd);
}

void render_set_human(Renderer *r, const HumanModel *h) {
    int hd = 0;
    free(r->human_tex);
    r->nhuman_tex = h->ntex;
    r->human_tex = calloc((size_t)h->ntex + 1, sizeof *r->human_tex);
    for (int i = 0; i < h->ntex; i++) r->human_tex[i] = load_texture(r, h->tex_ptr[i], h->tex_size[i], &hd);
    log_msg("player: %d textures (%d HD)", h->ntex, hd);
}

/* ---------------------------------------------------------------- matrices (column-major) */
static void mat_mul(float o[16], const float a[16], const float b[16]) {
    float t[16];
    for (int c = 0; c < 4; c++) for (int rr = 0; rr < 4; rr++) {
        float s = 0; for (int k = 0; k < 4; k++) s += a[k * 4 + rr] * b[c * 4 + k];
        t[c * 4 + rr] = s;
    }
    memcpy(o, t, sizeof t);
}

static void norm3(float v[3]) { float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); if (l > 1e-9f) { v[0] /= l; v[1] /= l; v[2] /= l; } }
static void cross3(const float a[3], const float b[3], float o[3]) { o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0]; }

static void view_proj(const CameraView *cam, int w, int h, float V_out[16], float P_out[16]) {
    float f[3] = {cam->target[0] - cam->eye[0], cam->target[1] - cam->eye[1], cam->target[2] - cam->eye[2]};
    norm3(f);
    float eye[3] = {cam->eye[0] - f[0] * GAME_EYE_BACK, cam->eye[1] - f[1] * GAME_EYE_BACK, cam->eye[2] - f[2] * GAME_EYE_BACK};
    float up[3] = {0, -1, 0}, r[3], u[3];
    cross3(f, up, r);
    if (sqrtf(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]) < 1e-6f) { float z[3] = {0, 0, 1}; cross3(f, z, r); }
    norm3(r);
    cross3(r, f, u);
    float V[16] = {r[0], u[0], -f[0], 0, r[1], u[1], -f[1], 0, r[2], u[2], -f[2], 0, 0, 0, 0, 1};
    V[12] = -(r[0] * eye[0] + r[1] * eye[1] + r[2] * eye[2]);
    V[13] = -(u[0] * eye[0] + u[1] * eye[1] + u[2] * eye[2]);
    V[14] = (f[0] * eye[0] + f[1] * eye[1] + f[2] * eye[2]);
    float n = 16.0f, fa = 12000.0f, t = 1.0f / tanf(GAME_FOV_DEG * 3.14159265f / 360.0f), a = (float)w / (float)h;
    float P[16] = {t / a, 0, 0, 0, 0, t, 0, 0, 0, 0, (fa + n) / (n - fa), -1, 0, 0, 2 * fa * n / (n - fa), 0};
    memcpy(V_out, V, sizeof V);
    memcpy(P_out, P, sizeof P);
}

static void model_matrix(const float pos[3], const float rot[3], float M[16]) {
    const float k = 2.0f * 3.14159265358979f / 256.0f;
    float cx = cosf(rot[0] * k), sx = sinf(rot[0] * k), cy = cosf(rot[1] * k), sy = sinf(rot[1] * k), cz = cosf(rot[2] * k), sz = sinf(rot[2] * k);
    /* same as obj_transform: Rx, then Ry, then Rz, then translate */
    float Rx[16] = {1, 0, 0, 0, 0, cx, sx, 0, 0, -sx, cx, 0, 0, 0, 0, 1};
    float Ry[16] = {cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
    float Rz[16] = {cz, sz, 0, 0, -sz, cz, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float T[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, pos[0], pos[1], pos[2], 1};
    float a[16], b[16];
    mat_mul(a, Ry, Rx); mat_mul(b, Rz, a); mat_mul(M, T, b);
}

static void draw_batches(Renderer *r, GLuint vbo, const Batch *b, int nb, const GLuint *texs, int ntex) {
    p_glBindBuffer(GL_ARRAY_BUFFER, vbo);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)0);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vtx), (void *)(sizeof(float) * 3));
    for (int i = 0; i < nb; i++) {
        glBindTexture(GL_TEXTURE_2D, (b[i].tex >= 0 && b[i].tex < ntex) ? texs[b[i].tex] : r->white);
        glDrawArrays(GL_TRIANGLES, b[i].first, b[i].count);
    }
}

void render_frame(Renderer *r, const Room *room, int area, const CameraView *cam, const TriList *player, int w, int h) {
    glViewport(0, 0, w, h);
    glClearColor(0.06f, 0.06f, 0.08f, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    p_glUseProgram(r->prog);
    p_glActiveTexture(GL_TEXTURE0);
    p_glUniform1i(r->u_tex, 0);
    p_glEnableVertexAttribArray(0);
    p_glEnableVertexAttribArray(1);
    float V[16], P[16], M[16], MV[16];
    view_proj(cam, w, h, V, P);
    p_glUniformMatrix4fv(r->u_proj, 1, GL_FALSE, P);
    for (int i = 0; i < room->nobjects && i < r->ngeo; i++) {
        const RoomObject *o = &room->objects[i];
        if (o->area != area || !o->visible) continue;
        if (i == 0 && getenv("HZ_NO_SHELL")) continue;
        p_glUniform1f(r->u_bias, i > 0 ? 2.0f : 0.0f);
        model_matrix(o->pos, o->rot, M);
        mat_mul(MV, V, M);
        p_glUniformMatrix4fv(r->u_mv, 1, GL_FALSE, MV);
        draw_batches(r, r->geo[i].vbo, r->geo[i].b, r->geo[i].nb, r->room_tex, r->nroom_tex);
    }
    if (player && player->nv) {
        p_glUniform1f(r->u_bias, 0.0f);
        p_glUniformMatrix4fv(r->u_mv, 1, GL_FALSE, V);
        p_glBindBuffer(GL_ARRAY_BUFFER, r->player_vbo);
        p_glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(sizeof(Vtx) * (size_t)player->nv), player->v, GL_STREAM_DRAW);
        draw_batches(r, r->player_vbo, player->b, player->nb, r->human_tex, r->nhuman_tex);
    }
}

int render_screenshot(int w, int h, const char *path) {
    uint8_t *px = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_flip_vertically_on_write(1);
    int ok = stbi_write_png(path, w, h, 4, px, w * 4);
    free(px);
    return ok ? 0 : -1;
}
