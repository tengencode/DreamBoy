#include "video_gl.h"

#include <android/log.h>

#include "fit_rect.h"

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "gbaemu", __VA_ARGS__)

/* A quad from gl_VertexID (triangle strip, no vertex buffer); uv (0,0) is the bottom left. */
static const char *VERTEX_SRC =
    "#version 300 es\n"
    "out vec2 v_uv;\n"
    "void main() {\n"
    "    v_uv = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "    gl_Position = vec4(v_uv * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

/* Row 0 of the frame at the top; each 5-bit channel c becomes (c << 3) | (c >> 2). Sharp: the nearest texel.
 * Smooth: the 4 texels around the point mixed by distance (bilinear; an integer texture can't filter itself). */
static const char *FRAGMENT_SRC =
    "#version 300 es\n"
    "precision highp float;\n"
    "precision highp int;\n"
    "precision highp usampler2D;\n"
    "uniform usampler2D u_frame;\n"
    "uniform int u_smooth;\n"
    "in vec2 v_uv;\n"
    "out vec4 o_color;\n"
    "vec3 texel(ivec2 t) {\n"
    "    uint p = texelFetch(u_frame, clamp(t, ivec2(0), ivec2(239, 159)), 0).r;\n"
    "    uvec3 c = uvec3(p & 31u, (p >> 5) & 31u, (p >> 10) & 31u);\n"
    "    return vec3((c << 3) | (c >> 2)) / 255.0;\n"
    "}\n"
    "void main() {\n"
    "    vec2 s = vec2(v_uv.x * 240.0, (1.0 - v_uv.y) * 160.0);\n"
    "    if (u_smooth == 0) {\n"
    "        o_color = vec4(texel(ivec2(min(s.x, 239.0), min(s.y, 159.0))), 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec2 p = s - 0.5;\n"
    "    ivec2 i = ivec2(floor(p));\n"
    "    vec2 f = p - floor(p);\n"
    "    vec3 top = mix(texel(i), texel(i + ivec2(1, 0)), f.x);\n"
    "    vec3 bottom = mix(texel(i + ivec2(0, 1)), texel(i + ivec2(1, 1)), f.x);\n"
    "    o_color = vec4(mix(top, bottom, f.y), 1.0);\n"
    "}\n";

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        LOGE("shader: %s", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

bool video_gl_init(video_gl_t *v) {
    GLuint vs = compile(GL_VERTEX_SHADER, VERTEX_SRC), fs = compile(GL_FRAGMENT_SHADER, FRAGMENT_SRC);
    if (!vs || !fs) return false;
    v->program = glCreateProgram();
    glAttachShader(v->program, vs);
    glAttachShader(v->program, fs);
    glLinkProgram(v->program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(v->program, GL_LINK_STATUS, &ok);
    if (!ok) {
        LOGE("shader program did not link");
        return false;
    }
    glUseProgram(v->program);
    glUniform1i(glGetUniformLocation(v->program, "u_frame"), 0);
    v->smooth_loc = glGetUniformLocation(v->program, "u_smooth");

    glGenTextures(1, &v->texture);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R16UI, 240, 160);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); /* integer textures need NEAREST */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenVertexArrays(1, &v->vao);
    return glGetError() == GL_NO_ERROR;
}

void video_gl_upload(video_gl_t *v, const uint16_t *pixels) {
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 240, 160, GL_RED_INTEGER, GL_UNSIGNED_SHORT, pixels);
}

void video_gl_draw(video_gl_t *v, int width, int height, int fit_mode, bool smooth, uint32_t background) {
    glViewport(0, 0, width, height);
    glClearColor((float)((background >> 16) & 0xFF) / 255.0f, (float)((background >> 8) & 0xFF) / 255.0f,
                 (float)(background & 0xFF) / 255.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    fit_rect_t r = fit_rect_mode(width, height, (fit_mode_t)fit_mode);
    if (r.w == 0) return;
    glViewport(r.x, r.y, r.w, r.h);
    glUseProgram(v->program);
    glUniform1i(v->smooth_loc, smooth ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, v->texture);
    glBindVertexArray(v->vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void video_gl_destroy(video_gl_t *v) {
    if (v->vao) glDeleteVertexArrays(1, &v->vao);
    if (v->texture) glDeleteTextures(1, &v->texture);
    if (v->program) glDeleteProgram(v->program);
    v->vao = v->texture = v->program = 0;
}
