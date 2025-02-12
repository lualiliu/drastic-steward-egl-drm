/*
  Special customized version for the DraStic emulator that runs on
      Miyoo Mini (Plus)
      TRIMUI-SMART
      Miyoo A30
      Anbernic RG28XX
      Fxtec Pro1 (QX1000)

  Copyright (C) 1997-2022 Sam Lantinga <slouken@libsdl.org>
  Copyright (C) 2022-2024 Steward Fu <steward.fu@gmail.com>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
 
#include <time.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <time.h>
#include <json-c/json.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <errno.h>

#include "../../SDL_internal.h"
#include "../SDL_sysvideo.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

#include "SDL_image.h"
#include "SDL_version.h"
#include "SDL_syswm.h"
#include "SDL_loadso.h"
#include "SDL_events.h"
#include "SDL_video.h"
#include "SDL_mouse.h"
#include "SDL_video_mmiyoo.h"
#include "SDL_event_mmiyoo.h"

#include "hex_pen.h"
#include "drastic_bios_arm7.h"
#include "drastic_bios_arm9.h"
#include "nds_bios_arm7.h"
#include "nds_bios_arm9.h"
#include "nds_firmware.h"

NDS nds = {0};
GFX gfx = {0};
MMIYOO_VideoInfo vid = {0};

extern MMIYOO_EventInfo evt;

int FB_W = 0;
int FB_H = 0;
int FB_SIZE = 0;
int LINE_H = 0;
int TMP_SIZE = 0;
int FONT_SIZE = 0;
int show_fps = 0;
int pixel_filter = 1;
int savestate_busy = 0;
SDL_Surface *fps_info = NULL;


struct drm_device {
	uint32_t width;			//显示器的宽的像素点数量
	uint32_t height;		//显示器的高的像素点数量
	uint32_t pitch;			//每行占据的字节数
	uint32_t handle;		//drm_mode_create_dumb的返回句柄
	uint32_t size;			//显示器占据的总字节数
	uint32_t *vaddr;		//mmap的首地址
	uint32_t fb_id;			//创建的framebuffer的id号
    	struct gbm_device *gbm;
    	struct gbm_surface *gbm_surface;
	struct gbm_bo *previous_bo;
	struct drm_mode_create_dumb create ;	//创建的dumb
    struct drm_mode_map_dumb map;			//内存映射结构体
};

drmModeConnector *conn;	    //connetor相关的结构体
drmModeRes *res;		    //资源
uint32_t conn_id;           //connetor的id号
uint32_t crtc_id;           //crtc的id号
int dri_fd;					    //文件描述符

#define RED 0XFF0000
#define GREEN 0X00FF00
#define BLUE 0X0000FF

struct drm_device drm_buf;

static volatile int is_video_thread_running = 0;

static pthread_t thread;
static int need_reload_bg = RELOAD_BG_COUNT;
static SDL_Surface *cvt = NULL;

static int MMIYOO_VideoInit(_THIS);
static int MMIYOO_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode);
static void MMIYOO_VideoQuit(_THIS);

static CUST_MENU drastic_menu = {0};
static char *translate[MAX_LANG_LINE] = {0};

#ifdef TRIMUI
static uint32_t LUT_256x192_S00[NDS_W * NDS_H] = {0};
static uint32_t LUT_256x192_S01[NDS_W * NDS_H] = {0};
static uint32_t LUT_256x192_S10[NDS_W * NDS_H] = {0};
static uint32_t LUT_256x192_S11[NDS_W * NDS_H] = {0};

int need_restore = 0;
int pre_dismode = 0;
#endif

#if defined(A30) || defined(RG28XX)
GLfloat bgVertices[] = {
   -1.0f,  1.0f,  0.0f,  0.0f,  0.0f,
   -1.0f, -1.0f,  0.0f,  0.0f,  1.0f,
    1.0f, -1.0f,  0.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  0.0f,  1.0f,  0.0f
};

GLfloat vVertices[] = {
   -1.0f,  1.0f,  0.0f,  0.0f,  0.0f,
   -1.0f, -1.0f,  0.0f,  0.0f,  1.0f,
    1.0f, -1.0f,  0.0f,  1.0f,  1.0f,
    1.0f,  1.0f,  0.0f,  1.0f,  0.0f
};
GLushort indices[] = {0, 1, 2, 0, 2, 3};

const char *vShaderSrc =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texCoord;   \n"
    "attribute float a_alpha;     \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "    const float angle = 0.0 * (3.1415 * 2.0) / 360.0;                                                                            \n"
    "    mat4 rot = mat4(cos(angle), -sin(angle), 0.0, 0.0, sin(angle), cos(angle), 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0); \n"
    "    gl_Position = a_position * rot; \n"
    "    v_texCoord = a_texCoord;        \n"
    "}                                   \n";
    
const char *fShaderSrc =
    "precision mediump float;                                  \n"
    "varying vec2 v_texCoord;                                  \n"
    "uniform float s_alpha;                                    \n"
    "uniform sampler2D s_texture;                              \n"
    "void main()                                               \n"
    "{                                                         \n"
    "    if (s_alpha >= 2.0) {                                 \n"
    "        gl_FragColor = texture2D(s_texture, v_texCoord);  \n"
    "    }                                                     \n"
    "    else if (s_alpha > 0.0) {                             \n"
    "        vec3 tex = texture2D(s_texture, v_texCoord).bgr;  \n"
    "        gl_FragColor = vec4(tex, s_alpha);                \n"
    "    }                                                     \n"
    "    else {                                                \n"
    "        vec3 tex = texture2D(s_texture, v_texCoord).bgr;  \n"
    "        gl_FragColor = vec4(tex, 1.0);                    \n"
    "    }                                                     \n"
    "}                                                         \n";

#ifdef A30
static struct _cpu_clock cpu_clock[] = {
    {96, 0x80000110},
    {144, 0x80000120},
    {192, 0x80000130},
    {216, 0x80000220},
    {240, 0x80000410},
    {288, 0x80000230},
    {336, 0x80000610},
    {360, 0x80000420},
    {384, 0x80000330},
    {432, 0x80000520},
    {480, 0x80000430},
    {504, 0x80000620},
    {528, 0x80000a10},
    {576, 0x80000530},
    {624, 0x80000c10},
    {648, 0x80000820},
    {672, 0x80000630},
    {720, 0x80000920},
    {768, 0x80000730},
    {792, 0x80000a20},
    {816, 0x80001010},
    {864, 0x80000830},
    {864, 0x80001110},
    {912, 0x80001210},
    {936, 0x80000c20},
    {960, 0x80000930},
    {1008, 0x80000d20},
    {1056, 0x80000a30},
    {1080, 0x80000e20},
    {1104, 0x80001610},
    {1152, 0x80000b30},
    {1200, 0x80001810},
    {1224, 0x80001020},
    {1248, 0x80000c30},
    {1296, 0x80001120},
    {1344, 0x80000d30},
    {1368, 0x80001220},
    {1392, 0x80001c10},
    {1440, 0x80000e30},
    {1488, 0x80001e10},
    {1512, 0x80001420},
    {1536, 0x80000f30},
    {1584, 0x80001520},
    {1632, 0x80001030},
    {1656, 0x80001620},
    {1728, 0x80001130},
    {1800, 0x80001820},
    {1824, 0x80001230},
    {1872, 0x80001920},
    {1920, 0x80001330},
    {1944, 0x80001a20},
    {2016, 0x80001430},
    {2088, 0x80001c20},
    {2112, 0x80001530},
    {2160, 0x80001d20},
    {2208, 0x80001630},
    {2232, 0x80001e20},
    {2304, 0x80001730},
    {2400, 0x80001830},
    {2496, 0x80001930},
    {2592, 0x80001a30},
    {2688, 0x80001b30},
    {2784, 0x80001c30},
    {2880, 0x80001d30},
    {2976, 0x80001e30},
    {3072, 0x80001f30},
};

static int max_cpu_item = sizeof(cpu_clock) / sizeof(struct _cpu_clock);
#endif
#endif

#ifdef QX1000
static volatile int is_wl_thread_running = 0;

static struct _wayland wl = {0};
static pthread_t thread_id[2] = {0};

EGLint egl_attribs[] = {
    EGL_SURFACE_TYPE,
    EGL_WINDOW_BIT,
    EGL_RENDERABLE_TYPE,
    EGL_OPENGL_ES2_BIT,
    EGL_RED_SIZE,
    8,
    EGL_GREEN_SIZE,
    8,
    EGL_BLUE_SIZE,
    8,
    EGL_NONE
};

EGLint ctx_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 
    2, 
    EGL_NONE
};

GLfloat egl_bg_vertices[] = {
    -1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 1.0f, 
     1.0f,  1.0f, 0.0f, 1.0f, 0.0f
};

GLfloat egl_fb_vertices[] = {
    -0.5f,  0.5f, 0.0f, 0.0f, 0.0f, 
    -0.5f, -0.5f, 0.0f, 0.0f, 1.0f,
     0.5f, -0.5f, 0.0f, 1.0f, 1.0f, 
     0.5f,  0.5f, 0.0f, 1.0f, 0.0f
};

GLushort egl_indices[] = {0, 1, 2, 0, 2, 3};

const char *vShaderSrc =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texCoord;   \n"
    "varying vec2 v_texCoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texCoord = a_texCoord;  \n"
    "}                            \n";

const char *fShaderSrc =
    "#ifdef GL_ES                                              \n"
    "precision mediump float;                                  \n"
    "#endif                                                    \n"
    "varying vec2 v_texCoord;                                  \n"
    "uniform float angle;                                      \n"
    "uniform float aspect;                                     \n"
    "uniform sampler2D s_texture;                              \n"
    "const vec2 HALF = vec2(0.5);                              \n"
    "void main()                                               \n"
    "{                                                         \n"
    "    float aSin = sin(angle);                              \n"
    "    float aCos = cos(angle);                              \n"
    "    vec2 tc = v_texCoord;                                 \n"
    "    mat2 rotMat = mat2(aCos, -aSin, aSin, aCos);          \n"
    "    mat2 scaleMat = mat2(aspect, 0.0, 0.0, 1.0);          \n"
    "    mat2 scaleMatInv = mat2(1.0 / aspect, 0.0, 0.0, 1.0); \n"
    "    tc -= HALF.xy;                                        \n"
    "    tc = scaleMatInv * rotMat * scaleMat * tc;            \n"
    "    tc += HALF.xy;                                        \n"
    "    vec3 tex = texture2D(s_texture, tc).bgr;              \n"
    "    gl_FragColor = vec4(tex, 1.0);                        \n"
    "}                                                         \n";

static void cb_remove(void *dat, struct wl_registry *reg, uint32_t id);
static void cb_handle(void *dat, struct wl_registry *reg, uint32_t id, const char *intf, uint32_t ver);

static struct wl_registry_listener cb_global = {
    .global = cb_handle,
    .global_remove = cb_remove
};

void update_wayland_res(int w, int h)
{
    int c0 = 0;
    int c1 = 0;
    int scale = 0;
    float x0 = 0;
    float y0 = 0;

    FB_W = w;
    FB_H = h;
    wl.info.w = FB_W;
    wl.info.h = FB_H;
    wl.info.bpp = 32;
    wl.info.size = wl.info.w * (wl.info.bpp / 8) * wl.info.h;

    c0 = LCD_H / wl.info.w;
    c1 = LCD_W / wl.info.h;
    scale = c0 > c1 ? c1 : c0;

    y0 = ((float)(wl.info.w * scale) / LCD_H);
    x0 = ((float)(wl.info.h * scale) / LCD_W);

    egl_fb_vertices[0] = -x0;
    egl_fb_vertices[1] = y0;
    egl_fb_vertices[5] = -x0;
    egl_fb_vertices[6] = -y0;
    egl_fb_vertices[10] =  x0;
    egl_fb_vertices[11] = -y0;
    egl_fb_vertices[15] =  x0;
    egl_fb_vertices[16] =  y0;

    memset(wl.data, 0, LCD_W * LCD_H * 2);
    wl.pixels[0] = (uint16_t*)wl.data;
    wl.pixels[1] = (uint16_t*)(wl.data + wl.info.size);
    printf(PREFIX"Updated wayland width=%d height=%d scale=%d\n", w, h, scale);
}

static void* wl_thread(void* pParam)
{
    while (is_wl_thread_running) {
        if (wl.init && wl.ready) {
            wl_display_dispatch(wl.display);
        }
        usleep(100);
    }
    return NULL;
}

static void cb_ping(void *dat, struct wl_shell_surface *shell_surf, uint32_t serial)
{
    wl_shell_surface_pong(shell_surf, serial);
}

static void cb_configure(void *dat, struct wl_shell_surface *shell_surf, uint32_t edges, int32_t w, int32_t h)
{
}

static void cb_popup_done(void *dat, struct wl_shell_surface *shell_surf)
{
}

static const struct wl_shell_surface_listener cb_shell_surf = {
    cb_ping,
    cb_configure,
    cb_popup_done
};

static void cb_handle(void *dat, struct wl_registry *reg, uint32_t id, const char *intf, uint32_t ver)
{
    if (strcmp(intf, "wl_compositor") == 0) {
        wl.compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 1);
    }
    else if (strcmp(intf, "wl_shell") == 0) {
        wl.shell = wl_registry_bind(reg, id, &wl_shell_interface, 1);
    }
}

static void cb_remove(void *dat, struct wl_registry *reg, uint32_t id)
{
}

void egl_free(void)
{
    wl.init = 0;
    wl.ready = 0;
    eglDestroySurface(wl.egl.display, wl.egl.surface);
    eglDestroyContext(wl.egl.display, wl.egl.context);
    wl_egl_window_destroy(wl.egl.window);
    eglTerminate(wl.egl.display);
    glDeleteShader(wl.egl.vShader);
    glDeleteShader(wl.egl.fShader);
    glDeleteProgram(wl.egl.pObject);
}

void wl_free(void)
{
    wl.init = 0;
    wl.ready = 0;
    wl_shell_surface_destroy(wl.shell_surface);
    wl_shell_destroy(wl.shell);
    wl_surface_destroy(wl.surface);
    wl_compositor_destroy(wl.compositor);
    wl_registry_destroy(wl.registry);
    wl_display_disconnect(wl.display);
    free(wl.data);
}

void wl_create(void)
{
    wl.display = wl_display_connect(NULL);
    wl.registry = wl_display_get_registry(wl.display);

    wl_registry_add_listener(wl.registry, &cb_global, NULL);
    wl_display_dispatch(wl.display);
    wl_display_roundtrip(wl.display);

    wl.surface = wl_compositor_create_surface(wl.compositor);
    wl.shell_surface = wl_shell_get_shell_surface(wl.shell, wl.surface);
    wl_shell_surface_set_toplevel(wl.shell_surface);
    wl_shell_surface_add_listener(wl.shell_surface, &cb_shell_surf, NULL);
    
    wl.region = wl_compositor_create_region(wl.compositor);
    wl_region_add(wl.region, 0, 0, LCD_W, LCD_H);
    wl_surface_set_opaque_region(wl.surface, wl.region);
    wl.egl.window = wl_egl_window_create(wl.surface, LCD_W, LCD_H);
}

void egl_create(void)
{
    GLint compiled = 0;
    EGLConfig cfg = 0;
    EGLint major = 0, minor = 0, cnt = 0;

    wl.egl.display = eglGetDisplay((EGLNativeDisplayType)wl.display);
    eglInitialize(wl.egl.display, &major, &minor);
    eglGetConfigs(wl.egl.display, NULL, 0, &cnt);
    eglChooseConfig(wl.egl.display, egl_attribs, &cfg, 1, &cnt);
    wl.egl.surface = eglCreateWindowSurface(wl.egl.display, cfg, (EGLNativeWindowType)wl.egl.window, NULL);
    wl.egl.context = eglCreateContext(wl.egl.display, cfg, EGL_NO_CONTEXT, ctx_attribs);
    eglMakeCurrent(wl.egl.display, wl.egl.surface, wl.egl.surface, wl.egl.context);

    wl.egl.vShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(wl.egl.vShader, 1, &vShaderSrc, NULL);
    glCompileShader(wl.egl.vShader);

    glGetShaderiv(wl.egl.vShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint len = 0;
        glGetShaderiv(wl.egl.vShader, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char* info = malloc(sizeof(char) * len);
            glGetShaderInfoLog(wl.egl.vShader, len, NULL, info);
            printf(PREFIX"%s, failed to compile vShader: %s\n", __func__, info);
            free(info);
        }
    }

    wl.egl.fShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(wl.egl.fShader, 1, &fShaderSrc, NULL);
    glCompileShader(wl.egl.fShader);
    
    glGetShaderiv(wl.egl.fShader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint len = 0;
        glGetShaderiv(wl.egl.fShader, GL_INFO_LOG_LENGTH, &len);
        if (len > 1) {
            char* info = malloc(sizeof(char) * len);
            glGetShaderInfoLog(wl.egl.fShader, len, NULL, info);
            printf(PREFIX"%s, failed to compile fShader: %s\n", __func__, info);
            free(info);
        }
    }

    wl.egl.pObject = glCreateProgram();
    glAttachShader(wl.egl.pObject, wl.egl.vShader);
    glAttachShader(wl.egl.pObject, wl.egl.fShader);
    glLinkProgram(wl.egl.pObject);
    glUseProgram(wl.egl.pObject);

    wl.egl.positionLoc = glGetAttribLocation(wl.egl.pObject, "a_position");
    wl.egl.texCoordLoc = glGetAttribLocation(wl.egl.pObject, "a_texCoord");
    wl.egl.samplerLoc = glGetUniformLocation(wl.egl.pObject, "s_texture");
    glUniform1f(glGetUniformLocation(wl.egl.pObject, "angle"), 0.0 * (3.1415 * 2.0) / 360.0);
    glUniform1f(glGetUniformLocation(wl.egl.pObject, "aspect"), 1);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    glGenTextures(1, &wl.egl.textureId);
    glBindTexture(GL_TEXTURE_2D, wl.egl.textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glViewport(0, 0, LCD_W, LCD_H);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glVertexAttribPointer(wl.egl.positionLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), egl_bg_vertices);
    glVertexAttribPointer(wl.egl.texCoordLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &egl_bg_vertices[3]);
    glEnableVertexAttribArray(wl.egl.positionLoc);
    glEnableVertexAttribArray(wl.egl.texCoordLoc);
    glActiveTexture(GL_TEXTURE0);
    glUniform1i(wl.egl.samplerLoc, 0);
}

static void* draw_thread(void *pParam)
{
    wl_create();
    egl_create();

    wl.init = 1;
    while (is_wl_thread_running) {
        if (wl.ready) {
            glVertexAttribPointer(wl.egl.positionLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), egl_bg_vertices);
            glVertexAttribPointer(wl.egl.texCoordLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &egl_bg_vertices[3]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wl.info.w, wl.info.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, wl.bg);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, egl_indices);

            glVertexAttribPointer(wl.egl.positionLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), egl_fb_vertices);
            glVertexAttribPointer(wl.egl.texCoordLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &egl_fb_vertices[3]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, wl.info.w, wl.info.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, wl.pixels[wl.flip ^ 1]);
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, egl_indices);
            eglSwapBuffers(wl.egl.display, wl.egl.surface);
        }
        else {
            usleep(100);
        }
    }
    return NULL;
}
#endif

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
static void* sdl_malloc(size_t size)
{
    static int idx = 0;

    void *r = NULL;
    uint32_t bpp = *((uint32_t *)VAR_SDL_SCREEN_BPP);

    if ((size == (NDS_W * NDS_H * bpp)) ||
        (size == (NDS_Wx2 * NDS_Hx2 * bpp)))
    {
        r = gfx.lcd.virAddr[0][idx];
        idx += 1;
        idx %= 2;
    }
    else {
        r = malloc(size);
    }
    return r;
}

static void sdl_free(void *ptr)
{
    int c0 = 0;
    int c1 = 0;
    int found = 0;

    for (c0 = 0; c0 < 2; c0++) {
        for (c1 = 0; c1 < 2; c1++) {
            if (ptr == gfx.lcd.virAddr[c0][c1]) {
                found = 1;
                break;
            }
        }
    }

    if (found == 0) {
        free(ptr);
    }
}

static void* sdl_realloc(void *ptr, size_t size)
{
    void *r = NULL;
    uint32_t bpp = *((uint32_t *)VAR_SDL_SCREEN_BPP);

    if ((size == (NDS_W * NDS_H * bpp)) ||
        (size == (NDS_Wx2 * NDS_Hx2 * bpp)))
    {
        r = sdl_malloc(size);
    }
    else {
        r = realloc(ptr, size);
    }
    return r;
}
#endif

#ifdef MMIYOO
static int get_bat_val(void)
{
    int r = 0;
    char buf[255] = {0};
    uint32_t v[2] = {0};
    struct stat st = {0};
    const char *AXP = "/customer/app/axp_test";

    if (stat (AXP, &st) == 0) {
        // {"battery":99, "voltage":4254, "charging":3}
        FILE *fd = popen(AXP, "r");

        if (fd) {
            fgets(buf, sizeof(buf), fd);
            pclose(fd);
            return (atoi(&buf[11]));
        }
        return 0;
    }

    if (vid.sar_fd < 0) {
        return 0;
    }

    ioctl(vid.sar_fd, 0x6100, 0);
    ioctl(vid.sar_fd, 0x6101, v);
    r = 100 - (((BAT_MAX_VAL - v[1]) * 100) / (BAT_MAX_VAL - BAT_MIN_VAL));
    if (r > 100) {
        r = 100;
    }
    if (r < 0) {
        r = 0;
    }
    return r;
}
#endif

#ifdef A30
static int get_bat_val(void)
{
    static int maxv = 0;

    FILE *fd = NULL;
    char buf[32] = {0};
    int r = 0;
    int curv = 0;

    if (maxv == 0) {
        fd = popen(BAT_MAX_CMD, "r");
        if (fd) {
            fgets(buf, sizeof(buf), fd);
            pclose(fd);
            maxv = atoi(buf);
        }
    }

    fd = popen(BAT_CUR_CMD, "r");
    if (fd) {
        fgets(buf, sizeof(buf), fd);
        pclose(fd);
        curv = atoi(buf);
    }

    r = 100 - (((maxv - curv) * 100) / 1200000);
    if (r > 100) {
        r = 100;
    }
    if (r < 0) {
        r = 0;
    }
    return r;
}
#endif

static void write_file(const char *fname, const void *buf, int len)
{
    struct stat st = {0};
    int fd = -1;

    if (stat(buf, &st) == -1) {
        fd = open(fname, O_WRONLY | O_CREAT, 0755);
    }
    else {
        fd = open(fname, O_WRONLY);
    }

    if (fd >= 0) {
        write(fd, buf, len);
        close(fd);
    }
}

static int get_current_menu_layer(void)
{
    int cc = 0;
    const char *P0 = "Change Options";
    const char *P1 = "Frame skip type";
    const char *P2 = "D-Pad Up";
    const char *P3 = "Enter Menu";
    const char *P4 = "Username";
    const char *P5 = "KB Space: toggle cheat/folder    KB Left Ctrl: return to main menu";
    const char *P6 = "KB Space: select";

    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (!memcmp(drastic_menu.item[cc].msg, P0, strlen(P0))) {
            return NDS_DRASTIC_MENU_MAIN;
        }
        else if (!memcmp(drastic_menu.item[cc].msg, P1, strlen(P1))) {
            return NDS_DRASTIC_MENU_OPTION;
        }
        else if (!memcmp(drastic_menu.item[cc].msg, P2, strlen(P2))) {
            return NDS_DRASTIC_MENU_CONTROLLER;
        }
        else if (!memcmp(drastic_menu.item[cc].msg, P3, strlen(P3))) {
            return NDS_DRASTIC_MENU_CONTROLLER2;
        }
        else if (!memcmp(drastic_menu.item[cc].msg, P4, strlen(P4))) {
            return NDS_DRASTIC_MENU_FIRMWARE;
        }
        else if (!memcmp(drastic_menu.item[cc].msg, P5, strlen(P5))) {
            return NDS_DRASTIC_MENU_CHEAT;
        }
        else if (!memcmp(drastic_menu.item[cc].msg, P6, strlen(P6))) {
            return NDS_DRASTIC_MENU_ROM;
        }
    }
    return -1;
}

static int draw_drastic_menu_main(void)
{
    int cc = 0;
    int div = 1;
    int w = 30;
    int h = 100;
    int draw = 0;
    int draw_shot = 0;
    int x = 0, y = 0;
    SDL_Rect rt = {0};
    CUST_MENU_SUB *p = NULL;
    char buf[MAX_PATH << 1] = {0};

#if defined(TRIMUI)
    div = 2;
#endif

    for (cc=0; cc<drastic_menu.cnt; cc++) {
        draw = 0;
        x = 90 / div;
        w = LINE_H / div;
        h = nds.enable_752x560 ? (115 / div) : (100 / div);

#if defined(TRIMUI)
        x = 30 / div;
#endif

        memset(buf, 0, sizeof(buf));
        p = &drastic_menu.item[cc];
        if (p->y == 201) {
            draw = 1;
#if defined(MMIYOO) || defined(TRIMUI) || defined(A30) || defined(RG28XX)
            sprintf(buf, "NDS %s", &p->msg[8]);
#else
            sprintf(buf, "%s", &p->msg[8]);
#endif
            x = FB_W - get_font_width(buf) - 10;
            y = 10 / div;
        }
        else if (p->y == 280) {
            draw = 1;
            y = h + (0 * w);
            strcpy(buf, to_lang("Change Options"));
        }
        else if (p->y == 288) {
            draw = 1;
            y = h + (1 * w);
            strcpy(buf, to_lang("Configure Controls"));
        }
        else if (p->y == 296) {
            draw = 1;
            y = h + (2 * w);
            strcpy(buf, to_lang("Configure Firmware"));
        }
        else if (p->y == 304) {
            draw = 1;
            y = h + (3 * w);
            strcpy(buf, to_lang("Configure Cheats"));
        }
        else if (p->y == 320) {
            draw = 1;
            y = h + (4 * w);
            sprintf(buf, "%s %s", to_lang("Load state"), &p->msg[13]);
        }
        else if (p->y == 328) {
            draw = 1;
            y = h + (5 * w);
            sprintf(buf, "%s %s", to_lang("Save state"), &p->msg[13]);
        }
        else if (p->y == 344) {
            draw = 1;
            y = h + (6 * w);
            strcpy(buf, to_lang("Load new game"));
        }
        else if (p->y == 352) {
            draw = 1;
            y = h + (7 * w);
            strcpy(buf, to_lang("Restart game"));
        }
        else if (p->y == 368) {
            draw = 1;
            y = h + (8 * w);
            strcpy(buf, to_lang("Return to game"));
        }
        else if (p->y == NDS_Hx2) {
            draw = 1;
            y = h + (9 * w);
            strcpy(buf, to_lang("Exit DraStic"));
        }

        if (draw) {
            if (p->bg) {
                rt.x = 5 / div;
                rt.y = y - (3 / div);
                rt.w = FB_W - (10 / div);
                rt.h = w;
                SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, 
                    (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
                if ((p->y == 320) || (p->y == 328)) {
                    draw_shot = 1;
                }

                if (nds.menu.show_cursor && nds.menu.drastic.cursor) {
                    rt.x = (5 / div) + (x - nds.menu.drastic.cursor->w) / 2;
                    rt.y -= ((nds.menu.drastic.cursor->h - LINE_H) / 2);
                    rt.w = 0;
                    rt.h = 0;
                    SDL_BlitSurface(nds.menu.drastic.cursor, NULL, nds.menu.drastic.main, &rt);
                }
            }
            draw_info(nds.menu.drastic.main, buf, x, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
        }
    }

    y = 10;
#ifdef RG28XX
    sprintf(buf, "Rel "NDS_VER", Res %s", "640*480");
#endif

#ifdef A30
    if (nds.chk_bat) {
        sprintf(buf, "Rel "NDS_VER", Res %s, BAT %d%%", "640*480", get_bat_val());
    }
    else {
        sprintf(buf, "Rel "NDS_VER", Res %s", "640*480");
    }
#endif

#ifdef MMIYOO
    if (nds.chk_bat) {
        sprintf(buf, "Rel "NDS_VER", Res %s, BAT %d%%", nds.enable_752x560 ? "752*560" : "640*480", get_bat_val());
    }
    else {
        sprintf(buf, "Rel "NDS_VER", Res %s", nds.enable_752x560 ? "752*560" : "640*480");
    }
#endif

#ifdef TRIMUI
    sprintf(buf, "Rel "NDS_VER", Res %s", "320*240");
#endif

#ifdef PANDORA
    sprintf(buf, "Rel "NDS_VER", Res %s", "800*480");
#endif

#ifdef QX1000
    sprintf(buf, "Rel "NDS_VER);
#endif

    draw_info(nds.menu.drastic.main, buf, 10, y / div, nds.menu.c1, 0);

    if (draw_shot) {
        const uint32_t len = NDS_W * NDS_H * 2;
        uint16_t *top = malloc(len);
        uint16_t *bottom = malloc(len);

        if (top && bottom) {
            SDL_Surface *t = NULL;
#if defined(TRIMUI)
            SDL_Surface *sm = NULL;
#endif

#if defined(TRIMUI)
            const int SM_W = (float)NDS_W / 1.35;
            const int SM_H = (float)NDS_H / 1.35;
#endif
            uint32_t slot = *((uint32_t *)VAR_SYSTEM_SAVESTATE_NUM);
            nds_load_state_index _func = (nds_load_state_index)FUN_LOAD_STATE_INDEX;

#if defined(TRIMUI)
            sm = SDL_CreateRGBSurface(0, SM_W, SM_H, 16, 0, 0, 0, 0);
#endif

            memset(top, 0, len);
            memset(bottom, 0, len);
            _func((void*)VAR_SYSTEM, slot, top, bottom, 1);
            t = SDL_CreateRGBSurfaceFrom(top, NDS_W, NDS_H, 16, NDS_W * 2, 0, 0, 0, 0);
            if (t) {
#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
                rt.x = FB_W - (NDS_W + (nds.enable_752x560 ? 30 : 10));
                rt.y = nds.enable_752x560 ? h - 20 : 50;
                rt.w = NDS_W;
                rt.h = NDS_H;
                SDL_BlitSurface(t, NULL, nds.menu.drastic.main, &rt);
#endif
                SDL_FreeSurface(t);
            }

            t = SDL_CreateRGBSurfaceFrom(bottom, NDS_W, NDS_H, 16, NDS_W * 2, 0, 0, 0, 0);
            if (t) {
#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
                rt.x = FB_W - (NDS_W + (nds.enable_752x560 ? 30 : 10));
                rt.y = nds.enable_752x560 ? (h + NDS_H) - 20 : 50 + NDS_H;
                rt.w = NDS_W;
                rt.h = NDS_H;
                SDL_BlitSurface(t, NULL, nds.menu.drastic.main, &rt);
#endif

#if defined(TRIMUI)
                SDL_SoftStretch(t, NULL, sm, NULL);

                rt.x = FB_W - (SM_W + 5);
                rt.y = (FB_H - SM_H) >> 1;
                rt.w = SM_W;
                rt.h = SM_H;
                SDL_BlitSurface(sm, NULL, nds.menu.drastic.main, &rt);
#endif
                SDL_FreeSurface(t);
            }

#if defined(TRIMUI)
            if (sm) {
                SDL_FreeSurface(sm);
            }
#endif
        }

        if (top) {
            free(top);
        }

        if (bottom) {
            free(bottom);
        }
    }
    return 0;
}

static int mark_double_spaces(char *p)
{
    int cc = 0;
    int len = strlen(p);

    for (cc=0; cc<len - 1; cc++) {
        if ((p[cc] == ' ') && (p[cc + 1] == ' ')) {
            p[cc] = 0;
            return 0;
        }
    }
    return -1;
}

static char* find_menu_string_tail(char *p)
{
    int cc = 0;

    for (cc=strlen(p) - 1; cc>=0; cc--) {
        if (p[cc] == ' ') {
            return &p[cc + 1];
        }
    }
    return NULL;
}

static int draw_drastic_menu_option(void)
{
    int w = 0;
    int y = 0;
    int ww = 0;
    int s0 = 0;
    int s1 = 0;
    int cc = 0;
    int div = 1;
    int cnt = 0;
    int cursor = 0;
    SDL_Rect rt = {0};
    CUST_MENU_SUB *p = NULL;
    char buf[MAX_PATH] = {0};

#if defined(TRIMUI)
    div = 2;
#endif

    cursor = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (drastic_menu.item[cc].bg > 0) {
            cursor = cc;
        }
    }

    if (cursor <= 6) {
        s0 = 1;
        s1 = 14;
    }
    else if (cursor >= (drastic_menu.cnt - 7)) {
        s0 = drastic_menu.cnt - 14;
        s1 = drastic_menu.cnt - 1;
    }
    else {
        s0 = cursor - 6;
        s1 = cursor + 7;
    }

    for (cc=0; cc<drastic_menu.cnt; cc++) {
        ww = LINE_H / div;

        if ((cc >= s0) && (cc < s1)) {
            y = (25 / div) + (cnt * ww);
            memset(buf, 0, sizeof(buf));
            p = &drastic_menu.item[cc];
        
            cnt+= 1;            
            if (p->bg) {
                rt.x = 5 / div;
                rt.y = y - (3 / div);
                rt.w = FB_W - (10 / div);
                rt.h = ww;
                SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, 
                    (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
            }

            if (p->y <= NDS_Hx2) {
                strcpy(buf, to_lang(find_menu_string_tail(p->msg)));
                w = get_font_width(buf);
                draw_info(nds.menu.drastic.main, buf, FB_W - w - (ww / div), y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);

                mark_double_spaces(p->msg);
                strcpy(buf, to_lang(p->msg));
            }
            else {
                strcpy(buf, to_lang(p->msg));
            }
            draw_info(nds.menu.drastic.main, buf, ww / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
        }
    }
    return 0;
}

static int draw_drastic_menu_controller(void)
{
    int y = 0;
    int w = 0;
    int cc = 0;
    int div = 1;
    int cnt = 0;
    int cursor = 0;
    SDL_Rect rt = {0};
    int s0 = 0, s1 = 0;
    CUST_MENU_SUB *p = NULL;
    char buf[MAX_PATH] = {0};

#if defined(TRIMUI)
    div = 2;
#endif

    cursor = 0;
    for (cc=0; cc<drastic_menu.cnt;) {
        if ((drastic_menu.item[cc].y >= 240) && (drastic_menu.item[cc].y <= 376)) {
            if ((drastic_menu.item[cc + 1].bg > 0) || (drastic_menu.item[cc + 2].bg > 0)) {
                break;
            }
            cc+= 3;
        }
        else {
            if (drastic_menu.item[cc].bg > 0) {
                break;
            }
            cc+= 1;
        }
        cursor+= 1;
    }
    
    if (cursor <= 6) {
        s0 = 0;
        s1 = 13;
    }
    else if (cursor >= (24 - 7)) {
        s0 = 24 - 13;
        s1 = 24;
    }
    else {
        s0 = cursor - 6;
        s1 = cursor + 7;
    }

    cnt = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        w = LINE_H / div;
        p = &drastic_menu.item[cc];

        if ((p->y == 224) || (p->y == 232) || (p->y == 201)) {
            continue;
        }

        memset(buf, 0, sizeof(buf));
        if ((cnt >= s0) && (cnt < s1)) {
            y = (25 / div) + ((cnt - s0) * w);

            if ((p->y >= 240) && (p->y <= 376)) {
                if (drastic_menu.item[cc + 1].bg || drastic_menu.item[cc + 2].bg) {
                    int sum = drastic_menu.item[cc + 1].bg + drastic_menu.item[cc + 2].bg;
                    uint32_t c = sum > 500 ? 0xff0000 : nds.menu.c2;

                    rt.x = 5 / div;
                    rt.y = y - (3 / div);
                    rt.w = FB_W - (10 / div);
                    rt.h = w;
                    SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
                }
                draw_info(nds.menu.drastic.main, p->msg, 20 / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
                if ((p->y >= 240) && (p->y <= 376)) {
                    if (nds.enable_752x560) {
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 1].msg), 320 / div, y, drastic_menu.item[cc + 1].bg ? nds.menu.c0 : nds.menu.c1, 0);
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 2].msg), 550 / div, y, drastic_menu.item[cc + 2].bg ? nds.menu.c0 : nds.menu.c1, 0);
                    }
                    else {
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 1].msg), 300 / div, y, drastic_menu.item[cc + 1].bg ? nds.menu.c0 : nds.menu.c1, 0);
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 2].msg), 480 / div, y, drastic_menu.item[cc + 2].bg ? nds.menu.c0 : nds.menu.c1, 0);
                    }
                }
            }
            else {
                if (p->bg) {
                    rt.x = 5 / div;
                    rt.y = y - (3 / div);
                    rt.w = FB_W - (10 / div);
                    rt.h = w;
                    SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, 
                        (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
                }
                draw_info(nds.menu.drastic.main, to_lang(p->msg), 20 / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
            }
        }

        cnt+= 1;
        if ((p->y >= 240) && (p->y <= 376)) {
            cc+= 2;
        }
    }
    return 0;
}

static int draw_drastic_menu_controller2(void)
{
    int y = 0;
    int w = 0;
    int cc = 0;
    int cnt = 0;
    int div = 1;
    int cursor = 0;
    SDL_Rect rt = {0};
    int s0 = 0, s1 = 0;
    CUST_MENU_SUB *p = NULL;
    char buf[MAX_PATH] = {0};

#if defined(TRIMUI)
    div = 2;
#endif

    cursor = 0;
    for (cc=0; cc<drastic_menu.cnt;) {
        if ((drastic_menu.item[cc].y >= 240) && (drastic_menu.item[cc].y <= NDS_Hx2)) {
            if ((drastic_menu.item[cc + 1].bg > 0) || (drastic_menu.item[cc + 2].bg > 0)) {
                break;
            }
            cc+= 3;
        }
        else {
            if (drastic_menu.item[cc].bg > 0) {
                break;
            }
            cc+= 1;
        }
        cursor+= 1;
    }
    
    if (cursor <= 6) {
        s0 = 0;
        s1 = 13;
    }
    else if (cursor >= (23 - 7)) {
        s0 = 23 - 13;
        s1 = 23;
    }
    else {
        s0 = cursor - 6;
        s1 = cursor + 7;
    }

    cnt = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        w = LINE_H / div;
        p = &drastic_menu.item[cc];

        if ((p->y == 224) || (p->y == 232) || (p->y == 201)) {
            continue;
        }

        memset(buf, 0, sizeof(buf));
        if ((cnt >= s0) && (cnt < s1)) {
            y = (25 / div) + ((cnt - s0) * w);

            if ((p->y >= 240) && (p->y <= NDS_Hx2)) {
                if (drastic_menu.item[cc + 1].bg || drastic_menu.item[cc + 2].bg) {
                    int sum = drastic_menu.item[cc + 1].bg + drastic_menu.item[cc + 2].bg;
                    uint32_t c = sum > 500 ? 0xff0000 : nds.menu.c2;

                    rt.x = 5 / div;
                    rt.y = y - (3 / div);
                    rt.w = FB_W - (10 / div);
                    rt.h = w;
                    SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
                }
                draw_info(nds.menu.drastic.main, p->msg, 20 / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
                if ((p->y >= 240) && (p->y <= NDS_Hx2)) {
                    if (nds.enable_752x560) {
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 1].msg), 320 / div, y, drastic_menu.item[cc + 1].bg ? nds.menu.c0 : nds.menu.c1, 0);
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 2].msg), 550 / div, y, drastic_menu.item[cc + 2].bg ? nds.menu.c0 : nds.menu.c1, 0);
                    }
                    else {
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 1].msg), 300 / div, y, drastic_menu.item[cc + 1].bg ? nds.menu.c0 : nds.menu.c1, 0);
                        draw_info(nds.menu.drastic.main, to_lang(drastic_menu.item[cc + 2].msg), 480 / div, y, drastic_menu.item[cc + 2].bg ? nds.menu.c0 : nds.menu.c1, 0);
                    }
                }
            }
            else {
                if (p->bg) {
                    rt.x = 5 / div;
                    rt.y = y - (3 / div);
                    rt.w = FB_W - (10 / div);
                    rt.h = w;
                    SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, 
                        (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
                }
                draw_info(nds.menu.drastic.main, to_lang(p->msg), 20 / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
            }
        }

        cnt+= 1;
        if ((p->y >= 240) && (p->y <= NDS_Hx2)) {
            cc+= 2;
        }
    }
    return 0;
}

static int draw_drastic_menu_firmware(void)
{
    int t = 0;
    int w = 0;
    int y = 0;
    int ww = 30;
    int cc = 0;
    int div = 1;
    int cnt = 0;
    SDL_Rect rt = {0};
    CUST_MENU_SUB *p = NULL;
    char buf[MAX_PATH] = {0};
    char name[MAX_PATH] = {0};

#if defined(TRIMUI)
    div = 2;
#endif

    for (cc=0; cc<drastic_menu.cnt; cc++) {
        ww = LINE_H / div;
        p = &drastic_menu.item[cc];
        if ((p->x == 352) || (p->x == 108)) {
            continue;
        }
    
        memset(buf, 0, sizeof(buf));
        if ((p->x != 92) && (p->x != 256)) {
            strcat(name, p->msg);
        }
        else {
            y = (25 / div) + (cnt * ww);
            if (((p->x == 92) || (p->x == 256)) && (p->bg)) {
                rt.x = 5 / div;
                rt.y = y - (3 / div);
                rt.w = FB_W - (10 / div);
                rt.h = ww;
                SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, 
                    (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
            }

            cnt+= 1;
            if (p->y == 280) {
                mark_double_spaces(p->msg);
                strcpy(buf, to_lang(p->msg));
            }
            else if (p->y == 296) {
                w = get_font_width(name);
                draw_info(nds.menu.drastic.main, name, FB_W - w - (ww / div), 25 / div, nds.menu.c1, 0);

                w = strlen(p->msg);
                p->msg[w - 3] = 0;
                for (t=14; t<w; t++) {
                    if (p->msg[t] != ' ') {
                        strcpy(buf, &p->msg[t]);
                        break;
                    }
                }
                w = get_font_width(buf);
                draw_info(nds.menu.drastic.main, buf, FB_W - w - (ww / div), y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);

                strcpy(buf, to_lang("Favorite Color"));
            }
            else if (p->y <= 312) {
                strcpy(buf, to_lang(find_menu_string_tail(p->msg)));
                w = get_font_width(buf);
                draw_info(nds.menu.drastic.main, buf, FB_W - w - (ww / div), y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);

                mark_double_spaces(p->msg);
                strcpy(buf, to_lang(p->msg));
            }
            else {
                strcpy(buf, to_lang(p->msg));
            }
            draw_info(nds.menu.drastic.main, buf, ww / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
        }
    }
    return 0;
}

static int draw_drastic_menu_cheat(void)
{
    int y = 0;
    int w = 30;
    int cc = 0;
    int div = 1;
    int cnt = 0;
    int cursor = 0;
    SDL_Rect rt = {0};
    int s0 = 0, s1 = 0;
    CUST_MENU_SUB *p = NULL;
    char buf[MAX_PATH] = {0};

#if defined(TRIMUI)
    div = 2;
#endif
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        p = &drastic_menu.item[cc];
        if (p->x == 650) {
            for (s0=0; s0<drastic_menu.cnt; s0++) {
                if ((drastic_menu.item[s0].x == 10) && (drastic_menu.item[s0].y == p->y)) {
                    drastic_menu.item[s0].cheat = 1;
                    drastic_menu.item[s0].enable = strcmp(p->msg, "enabled") == 0 ? 1 : 0;
                    break;
                }
            }
        }
    }

    s0 = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (drastic_menu.item[cc].x == 10) {
            memcpy(&drastic_menu.item[s0], &drastic_menu.item[cc], sizeof(drastic_menu.item[cc]));
            s0+= 1;
        }
        memset(&drastic_menu.item[cc], 0, sizeof(drastic_menu.item[cc]));
    }
    drastic_menu.cnt = s0;

    cursor = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (drastic_menu.item[cc].bg > 0) {
            cursor = cc;
        }
    }

    if (drastic_menu.cnt == 0) {
        return 0;
    }

    if (drastic_menu.cnt < 13) {
        s0 = 0;
        s1 = drastic_menu.cnt;
    }
    else if (cursor <= 6) {
        s0 = 0;
        s1 = 13;
    }
    else if (cursor >= (drastic_menu.cnt - 7)) {
        s0 = drastic_menu.cnt - 13;
        s1 = drastic_menu.cnt;
    }
    else {
        s0 = cursor - 6;
        s1 = cursor + 7;
    }

    cnt = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        w = LINE_H / div;
        memset(buf, 0, sizeof(buf));
        p = &drastic_menu.item[cc];

        if (p->x != 10) {
            continue;
        }

        if ((cc >= s0) && (cc < s1)) {
            y = (25 / div) + (cnt * w);

            if (p->bg) {
                rt.x = 5 / div;
                rt.y = y - (3 / div);
                rt.w = FB_W - (10 / div);
                rt.h = w;
                SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format,
                    (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
            }

            cnt+= 1;
            draw_info(nds.menu.drastic.main, p->msg, w / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
            if (p->cheat && nds.menu.drastic.yes && nds.menu.drastic.no) {
                rt.x = FB_W - nds.menu.drastic.yes->w - (w / div);
                rt.y = y - 1;
                rt.w = 0;
                rt.h = 0;
                SDL_BlitSurface((p->enable > 0 ) ? nds.menu.drastic.yes : nds.menu.drastic.no, NULL, nds.menu.drastic.main, &rt);
            }
        }
    }
    return 0;
}

static int draw_drastic_menu_rom(void)
{
    int y = 0;
    int w = 0;
    int cc = 0;
    int div = 1;
    int chk = 0;
    int all = 0;
    int cnt = 0;
    int cursor = 0;
    SDL_Rect rt = {0};
    int s0 = 0, s1 = 0;
    CUST_MENU_SUB *p = NULL;

#if defined(TRIMUI)
    div = 2;
#endif

    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (drastic_menu.item[cc].x == 10) {
            if (drastic_menu.item[cc].bg > 0) {
                chk = 10;
                break;
            }
        }
        if (drastic_menu.item[cc].x == 587) {
            if (drastic_menu.item[cc].bg > 0) {
                chk = 587;
                break;
            }
        }
    }

    cursor = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (drastic_menu.item[cc].x == chk) {
            if (drastic_menu.item[cc].bg > 0) {
                break;
            }
            cursor+= 1;
        }
    }

    all = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        if (drastic_menu.item[cc].x == chk) {
            all+= 1;
        }
    }

    if (all < 12) {
        s0 = 0;
        s1 = all;
    }
    else if (cursor <= 6) {
        s0 = 0;
        s1 = 12;
    }
    else if (cursor >= (all - 6)) {
        s0 = all - 12;
        s1 = all;
    }
    else {
        s0 = cursor - 6;
        s1 = cursor + 6;
    }

    {
        uint32_t c = 0x335445;

        w = LINE_H / div;
        p = &drastic_menu.item[0];
        rt.x = 5 / div;
        rt.y = (25 / div) - (4 / div);
        rt.w = FB_W - (10 / div);
        rt.h = w;
        SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format, (c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff));
        draw_info(nds.menu.drastic.main, p->msg, 20 / div, 25 / div, 0xa0cb93, 0);
    }

    cnt = 0;
    for (cc=0; cc<drastic_menu.cnt; cc++) {
        w = LINE_H / div;
        p = &drastic_menu.item[cc];
        if (p->x == chk) {
            y = (25 / div) + (((cnt - s0) + 1) * w);
            if ((cnt >= s0) && (cnt < s1)) {
                if (p->bg) {
                    rt.x = 5 / div;
                    rt.y = y - (4 / div);
                    rt.w = FB_W - (10 / div);
                    rt.h = w;
                    SDL_FillRect(nds.menu.drastic.main, &rt, SDL_MapRGB(nds.menu.drastic.main->format,
                        (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
                }
                draw_info(nds.menu.drastic.main, p->msg, 20 / div, y, p->bg ? nds.menu.c0 : nds.menu.c1, 0);
            }
            cnt+= 1;
        }
    }
    return 0;
}

int process_drastic_menu(void)
{
    int layer = get_current_menu_layer();

    if (layer == NDS_DRASTIC_MENU_MAIN) {
        SDL_SoftStretch(nds.menu.drastic.bg0, NULL, nds.menu.drastic.main, NULL);
    }
    else {
        SDL_SoftStretch(nds.menu.drastic.bg1, NULL, nds.menu.drastic.main, NULL);
    }

    switch (layer) {
    case NDS_DRASTIC_MENU_MAIN:
        draw_drastic_menu_main();
        break;
    case NDS_DRASTIC_MENU_OPTION:
        draw_drastic_menu_option();
        break;
    case NDS_DRASTIC_MENU_CONTROLLER:
        draw_drastic_menu_controller();
        break;
    case NDS_DRASTIC_MENU_CONTROLLER2:
        draw_drastic_menu_controller2();
        break;
    case NDS_DRASTIC_MENU_FIRMWARE:
        draw_drastic_menu_firmware();
        break;
    case NDS_DRASTIC_MENU_CHEAT:
        draw_drastic_menu_cheat();
        break;
    case NDS_DRASTIC_MENU_ROM:
        draw_drastic_menu_rom();
        break;
    default:
        SDL_SendKeyboardKey(SDL_PRESSED, SDL_GetScancodeFromKey(SDLK_RIGHT));
        usleep(100000);
        SDL_SendKeyboardKey(SDL_RELEASED, SDL_GetScancodeFromKey(SDLK_RIGHT));
        memset(&drastic_menu, 0, sizeof(drastic_menu));
        return 0;
    }
#if defined(A30) || defined(RG28XX)
    nds.update_menu = 1;
#else
    GFX_Copy(-1, nds.menu.drastic.main->pixels, nds.menu.drastic.main->clip_rect, nds.menu.drastic.main->clip_rect, nds.menu.drastic.main->pitch, 0, E_MI_GFX_ROTATE_180);
    GFX_Flip();
    GFX_Copy(-1, nds.menu.drastic.main->pixels, nds.menu.drastic.main->clip_rect, nds.menu.drastic.main->clip_rect, nds.menu.drastic.main->pitch, 0, E_MI_GFX_ROTATE_180);
    GFX_Flip();
#endif
    memset(&drastic_menu, 0, sizeof(drastic_menu));
    return 0;
}

static int process_screen(void)
{
    static int need_loadstate = 15;
    static int show_info_cnt = 0;
    static int cur_fb_w = 0;
    static int cur_volume = 0;
    static int cur_dis_mode = 0;
    static int cur_touchpad = 0;
    static int cur_theme_sel = 0;
    static int cur_pixel_filter = 0;
    static int pre_dis_mode = NDS_DIS_MODE_VH_S0;
    static int pre_hres_mode = NDS_DIS_MODE_HRES0;
    static char show_info_buf[MAX_PATH << 1] = {0};
#if defined(MMIYOO) || defined(A30)
    static int bat_chk_cnt = BAT_CHK_CNT;
#endif
    static int col_fg = 0xe0e000;
    static int col_bg = 0x000000;

    int idx = 0;
    int screen_cnt = 0;
    char buf[MAX_PATH] = {0};

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
    int cur_sel = gfx.lcd.cur_sel ^ 1;
#endif

#if defined(MMIYOO) || defined(PANDORA) || defined(QX1000) || defined(A30) || defined(RG28XX)
    screen_cnt = 2;
#else
    screen_cnt = 1;
#endif
    if (nds.auto_state > 0) {
        if (need_loadstate > 0) {
            need_loadstate-= 1;
            if (need_loadstate == 0) {
                dtr_loadstate(nds.auto_slot);
            }
        }
    }

    if (nds.menu.enable) {
        need_reload_bg = RELOAD_BG_COUNT;
        return 0;
    }

    if (nds.menu.drastic.enable) {
        nds.menu.drastic.enable = 0;
        need_reload_bg = RELOAD_BG_COUNT;
#ifdef QX1000
        update_wayland_res(NDS_W * 2, NDS_H);
#endif
    }

    if ((cur_fb_w != FB_W) ||
        (nds.shot.take) ||
        (cur_touchpad != nds.pen.pos) ||
        (cur_dis_mode != nds.dis_mode) ||
        (cur_theme_sel != nds.theme.sel) ||
        (cur_pixel_filter != pixel_filter) ||
        (cur_volume != nds.volume))
    {
        if (cur_fb_w != FB_W) {
            show_info_cnt = 150;
            sprintf(show_info_buf, " %d x %d ", FB_W, FB_H);
        }
        else if (cur_volume != nds.volume) {
            show_info_cnt = 50;
            sprintf(show_info_buf, " %s %d ", to_lang("Volume"), nds.volume);
        }
        else if (cur_touchpad != nds.pen.pos) {
            show_info_cnt = 50;
            sprintf(show_info_buf, " %s %d ", to_lang("Touchpad"), nds.pen.pos);
        }
        else if (cur_theme_sel != nds.theme.sel) {
            show_info_cnt = 50;
            if ((nds.theme.max > 0) && (nds.theme.sel < nds.theme.max)) {
                if (get_dir_path(nds.theme.path, nds.theme.sel, buf) == 0) {
                    int off = strlen(nds.theme.path);
                    sprintf(show_info_buf, " %s %s ", to_lang("Wallpaper"), &buf[off + 1]);
                }
                else {
                    sprintf(show_info_buf, " %s %d ", to_lang("Wallpaper"), nds.theme.sel);
                }
            }
            else {
                sprintf(show_info_buf, " %s %s ", to_lang("Wallpaper"), to_lang("None"));
            }
        }
        else if (cur_pixel_filter != pixel_filter) {
            show_info_cnt = 50;
            sprintf(show_info_buf, " %s ", to_lang(pixel_filter ? "Pixel" : "Blur"));
        }
        else if (nds.shot.take) {
            show_info_cnt = 50;
            nds.shot.take = 0;
            sprintf(show_info_buf, " %s ", to_lang("Take Screenshot"));
        }

        cur_fb_w = FB_W;
        cur_theme_sel = nds.theme.sel;
        cur_volume = nds.volume;
        cur_dis_mode = nds.dis_mode;
        cur_touchpad = nds.pen.pos;
        cur_pixel_filter = pixel_filter;
        need_reload_bg = RELOAD_BG_COUNT;
    }

    if (show_info_cnt == 0) {
        need_reload_bg = RELOAD_BG_COUNT;
        show_info_cnt = -1;
        col_fg = 0xe0e000;
        col_bg = 0x000000;
    }
        
    if (nds.defer_update_bg > 0) {
        nds.defer_update_bg-= 1;
    }
    else if (nds.defer_update_bg == 0) {
        nds.defer_update_bg = -1;
        need_reload_bg = RELOAD_BG_COUNT;
    }

    if (nds.state) {
        if (nds.state & NDS_STATE_QSAVE) {
            show_info_cnt = 50;
            sprintf(show_info_buf, " %s ", to_lang("Quick Save"));
            nds.state&= ~NDS_STATE_QSAVE;
        }
        else if (nds.state & NDS_STATE_QLOAD) {
            show_info_cnt = 50;
            sprintf(show_info_buf, " %s ", to_lang("Quick Load"));
            nds.state&= ~NDS_STATE_QLOAD;
        }
        else if (nds.state & NDS_STATE_FF) {
            show_info_cnt = 50;
            sprintf(show_info_buf, " %s ", to_lang("Fast Forward"));
            nds.state&= ~NDS_STATE_FF;
        }
    }

    if (nds.chk_bat) {
#if defined(MMIYOO) || defined(A30)
        bat_chk_cnt -= 1;
        if (bat_chk_cnt <= 0) {
            int v = get_bat_val();

            if (v <= 10) {
                show_info_cnt = 50;
                col_fg = 0xffffff;
                col_bg = 0xff0000;
                sprintf(show_info_buf, " %s %d%% ", to_lang("Battery"), v);
            }

            bat_chk_cnt = BAT_CHK_CNT;
            if (v <= 5) {
                bat_chk_cnt /= 2;
            }
        }
#endif
    }

    nds.screen.bpp = *((uint32_t *)VAR_SDL_SCREEN_BPP);
    nds.screen.init = *((uint32_t *)VAR_SDL_SCREEN_NEED_INIT);

    if (need_reload_bg) {
        reload_bg();
        need_reload_bg -= 1;
    }

    for (idx = 0; idx < screen_cnt; idx++) {
        int screen0 = 0;
        int screen1 = 0;
        int show_pen = 1;
        int need_update = 1;
        int rotate = E_MI_GFX_ROTATE_180;
        SDL_Rect srt = {0, 0, NDS_W, NDS_H};
        SDL_Rect drt = {0, 0, 160, 120};

        nds.screen.hres_mode[idx] = idx ?
            *((uint8_t *)VAR_SDL_SCREEN1_HRES_MODE):
            *((uint8_t *)VAR_SDL_SCREEN0_HRES_MODE);

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
        nds.screen.pixels[idx] = gfx.lcd.virAddr[cur_sel][idx];
#else
        nds.screen.pixels[idx] = (idx == 0) ?
            (uint32_t *)(*((uint32_t *)VAR_SDL_SCREEN0_PIXELS)):
            (uint32_t *)(*((uint32_t *)VAR_SDL_SCREEN1_PIXELS));
#endif

        if (nds.screen.hres_mode[idx]) {
            srt.w = NDS_Wx2;
            srt.h = NDS_Hx2;
            nds.screen.pitch[idx] = nds.screen.bpp * srt.w;
            if (nds.hres_mode == 0) {
                nds.pen.pos = 0;
                nds.hres_mode = 1;
                pre_dis_mode = nds.dis_mode;
                nds.dis_mode = pre_hres_mode;
#ifdef QX1000
                update_wayland_res(NDS_Wx2, NDS_Hx2);
#endif
            }
        }
        else {
            srt.w = NDS_W;
            srt.h = NDS_H;
            nds.screen.pitch[idx] = nds.screen.bpp * srt.w;

            drt.y = idx * 120;
            screen0 = (idx == 0);
            screen1 = (idx != 0);
            show_pen = nds.pen.pos ? screen1 : screen0;
            if (nds.hres_mode == 1) {
                nds.hres_mode = 0;
                pre_hres_mode = nds.dis_mode;
                nds.dis_mode = pre_dis_mode;
#ifdef QX1000
                update_wayland_res(NDS_W * 2, NDS_H);
#endif
            }
        }

#if defined(QX1000)
#elif defined(TRIMUI)
#elif defined(PANDORA)
#elif defined(MMIYOO) || defined(A30) || defined(RG28XX)
        switch (nds.dis_mode) {
        case NDS_DIS_MODE_VH_T0:
            if (screen1) {
                drt.x = 0;
                drt.y = 0;
                drt.w = FB_W;
                drt.h = FB_H;
                show_pen = 1;
                nds.pen.pos = 1;
            }
            else {
                show_pen = 0;
                need_update = 0;
            }
            break;
        case NDS_DIS_MODE_VH_T1:
            if (screen1) {
                drt.x = 0;
                drt.y = 0;
                drt.w = FB_W;
                drt.h = FB_H;
                show_pen = 1;
                nds.pen.pos = 1;
            }
            else {
                show_pen = 0;
                need_update = 0;
            }
            break;
        case NDS_DIS_MODE_S0:
            if (screen1) {
                drt.w = NDS_W * 2;
                drt.h = NDS_H * 2;
                drt.x = (FB_W - drt.w) / 2;
                drt.y = (FB_H - drt.h) / 2;
                show_pen = 1;
                nds.pen.pos = 1;
            }
            else {
                show_pen = 0;
                need_update = 0;
            }
            break;
        case NDS_DIS_MODE_S1:
            if (screen1) {
                drt.x = 0;
                drt.y = 0;
                drt.w = FB_W;
                drt.h = FB_H;
                show_pen = 1;
                nds.pen.pos = 1;
            }
            else {
                show_pen = 0;
                need_update = 0;
            }
            break;
        case NDS_DIS_MODE_V0:
            drt.w = NDS_W;
            drt.h = NDS_H;
            drt.x = (FB_W - drt.w) / 2;
            if (nds.enable_752x560 == 0) {
                drt.y = screen0 ? 48 : 48 + drt.h;
            }
            else {
                drt.y = screen0 ? 88 : 88 + drt.h;
            }
            break;
        case NDS_DIS_MODE_V1:
            if (nds.enable_752x560 == 0) {
                drt.w = 320;
                drt.h = 240;
            }
            else {
                drt.w = 373;
                drt.h = 280;
            }
            drt.x = (FB_W - drt.w) / 2;
            drt.y = screen0 ? 0 : drt.h;
            break;
        case NDS_DIS_MODE_H0:
            drt.w = NDS_W;
            drt.h = NDS_H;
            if (nds.enable_752x560 == 0) {
                drt.x = screen0 ? 64 : 64 + drt.w;
            }
            else {
                drt.x = screen0 ? 120 : 120 + drt.w;
            }
            drt.y = (FB_H - drt.h) / 2;
            break;
        case NDS_DIS_MODE_H1:
            if (nds.enable_752x560 == 0) {
                drt.w = 320;
                drt.h = 240;
                drt.x = screen0 ? 0 : drt.w;
            }
            else {
                drt.w = 373;
                drt.h = 280;
                drt.x = screen0 ? 3 : (drt.w + 3);
            }
            drt.y = (FB_H - drt.h) / 2;
            break;
        case NDS_DIS_MODE_VH_S0:
            drt.x = screen1 ? 160 : 0;
            drt.y = screen1 ? 120 : 0;
            drt.w = screen1 ? (FB_W - 160) : 160;
            drt.h = screen1 ? (FB_H - 120) : 120;
            break;
        case NDS_DIS_MODE_VH_S1:
            drt.x = screen1 ? NDS_W : 0;
            drt.y = screen1 ? NDS_H : 0;
            drt.w = screen1 ? (FB_W - NDS_W) : NDS_W;
            drt.h = screen1 ? (FB_H - NDS_H) : NDS_H;
            break;
        case NDS_DIS_MODE_VH_S2:
            drt.w = screen1 ? (FB_W - 160) : 160;
            drt.h = screen1 ? (FB_H - 120) : 120;
            drt.x = screen1 ? ((FB_W - drt.w) / 2) : ((FB_W - drt.w) / 2);
            drt.y = screen1 ? 120 : 0;
            break;
        case NDS_DIS_MODE_VH_C0:
            drt.w = screen0 ? NDS_W : (FB_W - NDS_W);
            drt.h = screen0 ? NDS_H : (FB_H - NDS_H);
            drt.x = screen0 ? ((FB_W - drt.w) / 2) : ((FB_W - drt.w) / 2);
            drt.y = screen0 ? 0 : NDS_H;
            break;
        case NDS_DIS_MODE_VH_C1:
            drt.w = screen0 ? NDS_W : (FB_W - NDS_W);
            drt.h = screen0 ? NDS_H : (FB_H - NDS_H);
            drt.x = screen0 ? 0 : NDS_W;
            drt.y = screen0 ? ((FB_H - drt.h) / 2) : ((FB_H - drt.h) / 2);
            break;
        case NDS_DIS_MODE_HH0:
        case NDS_DIS_MODE_HH1:
            if (nds.enable_752x560 == 0) {
                drt.x = screen0 ? 0 : 320;
                drt.y = 26;
                drt.w = 427;
                drt.h = 320;
            }
            else {
                drt.x = screen0 ? 0 : 376;
                drt.y = 29;
                drt.w = 501;
                drt.h = 376;
            }
            rotate = (nds.dis_mode == NDS_DIS_MODE_HH0) ? E_MI_GFX_ROTATE_90 : E_MI_GFX_ROTATE_270;
            break;
        case NDS_DIS_MODE_HH2:
        case NDS_DIS_MODE_HH3:
            if (nds.enable_752x560 == 0) {
                drt.x = screen0 ? 0 : 320;
                drt.y = 0;
                drt.w = 480;
                drt.h = 320;
            }
            else {
                drt.x = screen0 ? 0 : 376;
                drt.y = 0;
                drt.w = 560;
                drt.h = 376;
            }
            rotate = (nds.dis_mode == NDS_DIS_MODE_HH2) ? E_MI_GFX_ROTATE_90 : E_MI_GFX_ROTATE_270;
            break;
        case NDS_DIS_MODE_HRES0:
            drt.w = NDS_Wx2;
            drt.h = NDS_Hx2;
            drt.x = (FB_W - drt.w) / 2;
            drt.y = (FB_H - drt.h) / 2;
            break;
        case NDS_DIS_MODE_HRES1:
            drt.x = 0;
            drt.y = 0;
            drt.w = FB_W;
            drt.h = FB_H;
            break;
        }
#else
        printf(PREFIX"Unsupported platform\n");
        return 0;
#endif

#if defined(A30) || defined(RG28XX)
        if (rotate == E_MI_GFX_ROTATE_180) {
            drt.y = (DEF_FB_H - drt.y) - drt.h;
            drt.x = (DEF_FB_W - drt.x) - drt.w;
        }
        else if (rotate == E_MI_GFX_ROTATE_90) {
            drt.x = (drt.x == 0) ? 320 : 0;
        }

#ifdef A30
        if (show_pen && ((evt.mode == MMIYOO_MOUSE_MODE) || (nds.joy.show_cnt && (nds.joy.mode == MYJOY_MODE_STYLUS)))) {
#else
        if (show_pen && (evt.mode == MMIYOO_MOUSE_MODE)) {
#endif
#else
        if (show_pen && (evt.mode == MMIYOO_MOUSE_MODE)) {
#endif
            draw_pen(nds.screen.pixels[idx], srt.w, nds.screen.pitch[idx]);

#ifdef A30
            if (nds.joy.show_cnt && (nds.joy.mode == MYJOY_MODE_STYLUS)) {
                nds.joy.show_cnt -= 1;
            }
#endif
        }

#if defined(A30) || defined(RG28XX)
        if ((idx == 0) && (nds.alpha.border > 0) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
            int c0 = 0;
            uint32_t *p0 = NULL;
            uint32_t *p1 = NULL;
            uint32_t col[] = { 0, 0xffffff, 0xff0000, 0x00ff00, 0x0000ff, 0x000000, 0xffff00, 0x00ffff };

            p0 = (uint32_t *)nds.screen.pixels[idx];
            p1 = (uint32_t *)nds.screen.pixels[idx] + ((srt.h - 1) * srt.w);
            for (c0 = 0; c0 < srt.w; c0++) {
                *p0++ = col[nds.alpha.border];
                *p1++ = col[nds.alpha.border];
            }

            p0 = (uint32_t *)nds.screen.pixels[idx];
            p1 = (uint32_t *)nds.screen.pixels[idx] + (srt.w - 1);
            for (c0 = 0; c0 < srt.h; c0++) {
                *p0 = col[nds.alpha.border];
                *p1 = col[nds.alpha.border];
                p0 += srt.w;
                p1 += srt.w;
            }
        }
        glBindTexture(GL_TEXTURE_2D, vid.texID[idx]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (pixel_filter) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, srt.w, srt.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nds.screen.pixels[idx]);
#endif

        if (need_update) {
#ifdef MMIYOO
            MI_SYS_FlushInvCache(nds.screen.pixels[idx], nds.screen.pitch[idx] * srt.h);
#endif

#if defined(A30) || defined(RG28XX)
            GFX_Copy(idx, nds.screen.pixels[idx], srt, drt, nds.screen.pitch[idx], 0, rotate);
	    
#else
            GFX_Copy(-1, nds.screen.pixels[idx], srt, drt, nds.screen.pitch[idx], 0, rotate);
#endif

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
            switch (nds.dis_mode) {
            case NDS_DIS_MODE_VH_T0:
                drt.x = 0;
                drt.y = 0;
                drt.w = 160;
                drt.h = 120;
#if defined(A30) || defined(RG28XX)
                switch (nds.alpha.pos) {
                case 0:
                    drt.x = DEF_FB_W - drt.w;
                    drt.y = 0;
                    break;
                case 1:
                    drt.x = 0;
                    drt.y = 0;
                    break;
                case 2:
                    drt.x = 0;
                    drt.y = DEF_FB_H - drt.h;
                    break;
                case 3:
                    drt.x = DEF_FB_W - drt.w;
                    drt.y = DEF_FB_H - drt.h;
                    break;
                }
                GFX_Copy(TEX_SCR0, nds.screen.pixels[0], srt, drt, nds.screen.pitch[0], 1, rotate);
#else
                GFX_Copy(-1, nds.screen.pixels[0], srt, drt, nds.screen.pitch[0], 1, rotate);
#endif
                break;
            case NDS_DIS_MODE_VH_T1:
                drt.x = 0;
                drt.y = 0;
                drt.w = NDS_W;
                drt.h = NDS_H;
#if defined(A30) || defined(RG28XX)
                switch (nds.alpha.pos) {
                case 0:
                    drt.x = DEF_FB_W - drt.w;
                    drt.y = 0;
                    break;
                case 1:
                    drt.x = 0;
                    drt.y = 0;
                    break;
                case 2:
                    drt.x = 0;
                    drt.y = DEF_FB_H - drt.h;
                    break;
                case 3:
                    drt.x = DEF_FB_W - drt.w;
                    drt.y = DEF_FB_H - drt.h;
                    break;
                }
                GFX_Copy(TEX_SCR0, nds.screen.pixels[0], srt, drt, nds.screen.pitch[0], 1, rotate);
#else 
                GFX_Copy(-1, nds.screen.pixels[0], srt, drt, nds.screen.pitch[0], 1, rotate);
#endif
                break;
            }
#endif
        }

        if (nds.screen.hres_mode[idx]) {
            break;
        }
    }

    if (show_info_cnt > 0) {
        draw_info(NULL, show_info_buf, FB_W - get_font_width(show_info_buf), 0, col_fg, col_bg);
        show_info_cnt-= 1;
    }
    else if (show_fps && fps_info) {
        SDL_Rect rt = {0};

        rt.x = FB_W - fps_info->w;
        rt.y = 0;
        rt.w = fps_info->w;
        rt.h = fps_info->h;
        GFX_Copy(-1, fps_info->pixels, fps_info->clip_rect, rt, fps_info->pitch, 0, E_MI_GFX_ROTATE_180);
    }

#ifdef TRIMUI
    if (need_restore) {
        need_restore = 0;
        nds.dis_mode = pre_dismode;
        disp_resize();
    }
#endif

    if (nds.screen.init) {
        nds_set_screen_menu_off _func = (nds_set_screen_menu_off)FUN_SET_SCREEN_MENU_OFF;
        _func();
    }
    GFX_Flip();
    return 0;
}

void sdl_blit_screen_menu(uint16_t *src, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
}

void sdl_update_screen(void)
{
    static int prepare_time = 30;

    if (prepare_time) {
        process_screen();
        prepare_time -= 1;
    }
    else if (nds.update_screen == 0) {
#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
        gfx.lcd.cur_sel ^= 1;
        *((uint32_t *)VAR_SDL_SCREEN0_PIXELS) = (uint32_t)gfx.lcd.virAddr[gfx.lcd.cur_sel][0];
        *((uint32_t *)VAR_SDL_SCREEN1_PIXELS) = (uint32_t)gfx.lcd.virAddr[gfx.lcd.cur_sel][1];
#if defined(A30) || defined(RG28XX)
        nds.menu.drastic.enable = 0;
#endif
#endif
        nds.update_screen = 1;
    }
}

void sdl_print_string(char *p, uint32_t fg, uint32_t bg, uint32_t x, uint32_t y)
{
    int w = 0, h = 0;
    SDL_Color col = {0};
    SDL_Surface *t0 = NULL;
    SDL_Surface *t1 = NULL;
    static int fps_cnt = 0;

    if (p && (strlen(p) > 0)) {
        if (drastic_menu.cnt < MAX_MENU_LINE) {
            drastic_menu.item[drastic_menu.cnt].x = x;
            drastic_menu.item[drastic_menu.cnt].y = y;
            drastic_menu.item[drastic_menu.cnt].fg = fg;
            drastic_menu.item[drastic_menu.cnt].bg = bg;
            strcpy(drastic_menu.item[drastic_menu.cnt].msg, p);
            drastic_menu.cnt+= 1;
        }
        //printf(PREFIX"x:%d, y:%d, fg:0x%x, bg:0x%x, \'%s\'\n", x, y, fg, bg, p);
    }

    if ((x == 0) && (y == 0) && (fg == 0xffff) && (bg == 0x0000)) {
        if (fps_cnt++ > 60) {
            fps_cnt = 0;

            w = strlen(p);
            for (h=w-1; h>=0; h--) {
                if (p[h] == ' ') {
                    p[h] = 0;
                    break;
                }
            }

            col.r = 0xcc;
            col.g = 0xcc;
            col.b = 0x00;
            TTF_SizeUTF8(nds.font, p, &w, &h);
            t0 = TTF_RenderUTF8_Solid(nds.font, p, col);
            if (t0) {
                t1 = SDL_CreateRGBSurface(SDL_SWSURFACE, t0->w, t0->h, 32, 0, 0, 0, 0);
                if (t1) {
                    SDL_FillRect(t1, &t1->clip_rect, 0x000000);
                    SDL_BlitSurface(t0, NULL, t1, NULL);

                    if (fps_info) {
                        SDL_FreeSurface(fps_info);
                    }
                    fps_info = SDL_ConvertSurface(t1, cvt->format, 0);
                    SDL_FreeSurface(t1);
                }
                SDL_FreeSurface(t0);
            }
            show_fps = 1;
        }
    }
}

void sdl_savestate_pre(void)
{
#ifndef UNITTEST
    asm volatile (
        "mov r1, %0                 \n"
        "mov r2, #1                 \n"
        "str r2, [r1]               \n"
        "add sp, sp, #(0x20 + 0)    \n"
        "ldr pc, [sp], #4           \n"
        ::"r"(&savestate_busy):
    );
#endif
}

void sdl_savestate_post(void)
{
#ifndef UNITTEST
    asm volatile (
        "mov r1, %0                 \n"
        "mov r2, #0                 \n"
        "str r2, [r1]               \n"
        "add sp, sp, #(0x18 + 0)    \n"
        "ldr pc, [sp], #4           \n"
        ::"r"(&savestate_busy):
    );
#endif
}

void sigterm_handler(int sig)
{
    static int ran = 0;

    if (ran == 0) {
        ran = 1;
        printf(PREFIX"Oops sigterm !\n");
        dtr_quit();
    }
}

static void strip_newline(char *p)
{
    int cc = 0, len = strlen(p);

    for(cc=0; cc<len; cc++) {
        if ((p[cc] == '\r') || (p[cc] == '\n')) {
            p[cc] = 0;
            break;
        }
    }
}

static void *video_handler(void *threadid)
{
#if defined(A30) || defined(RG28XX)
    EGLint egl_major = 0;
    EGLint egl_minor = 0;
    EGLint num_configs = 0;
    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE,   8,  
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE,  8,  
        EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLint window_attributes[] = { 
        EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
        EGL_NONE
    };
    EGLint const context_attributes[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE,
    };
  //GBM

    vid.eglDisplay = eglGetDisplay((EGLNativeDisplayType)drm_buf.gbm);
    if (vid.eglDisplay == EGL_NO_DISPLAY) {
        printf("Failed to get EGL display\n");
        return -1;
    }
    if (!eglInitialize(vid.eglDisplay, NULL, NULL)) {
        printf("Failed to initialize EGL display\n");
        return -1;
    }
    eglChooseConfig(vid.eglDisplay, config_attribs, &vid.eglConfig, 1, &num_configs);

    vid.eglSurface = eglCreateWindowSurface(vid.eglDisplay, vid.eglConfig, (EGLNativeWindowType)drm_buf.gbm_surface, NULL);
    if (vid.eglSurface == EGL_NO_SURFACE) {
        EGLint error = eglGetError();
        printf("Failed to create EGL surface, error: 0x%x\n", error);
        return -1;
    }
    vid.eglContext = eglCreateContext(vid.eglDisplay, vid.eglConfig, EGL_NO_CONTEXT, context_attributes);
    if (vid.eglSurface == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        printf("Failed to create EGL Context, error: 0x%x\n", error);
        return -1;
    }

    if (!eglMakeCurrent(vid.eglDisplay, vid.eglSurface, vid.eglSurface, vid.eglContext)){
    	printf("Cannot make EGL context current\n");
        return -1;
    }
  
    vid.vShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vid.vShader, 1, &vShaderSrc, NULL);
    glCompileShader(vid.vShader);
  
    vid.fShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(vid.fShader, 1, &fShaderSrc, NULL);
    glCompileShader(vid.fShader);
   
    vid.pObject = glCreateProgram();
    glAttachShader(vid.pObject, vid.vShader);
    glAttachShader(vid.pObject, vid.fShader);
    glLinkProgram(vid.pObject);
    glUseProgram(vid.pObject);

    eglSwapInterval(vid.eglDisplay, 1);
    vid.posLoc = glGetAttribLocation(vid.pObject, "a_position");
    vid.texLoc = glGetAttribLocation(vid.pObject, "a_texCoord");
    vid.samLoc = glGetUniformLocation(vid.pObject, "s_texture");
    vid.alphaLoc = glGetUniformLocation(vid.pObject, "s_alpha");

    glGenTextures(TEX_MAX, vid.texID);

    glViewport(0, 0, DEF_FB_W, DEF_FB_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnableVertexAttribArray(vid.posLoc);
    glEnableVertexAttribArray(vid.texLoc);
    glUniform1i(vid.samLoc, 0);
    glUniform1f(vid.alphaLoc, 0.0);

    pixel_filter = 0;

    gfx.lcd.virAddr[0][0] = malloc(SCREEN_DMA_SIZE);
    gfx.lcd.virAddr[0][1] = malloc(SCREEN_DMA_SIZE);
    gfx.lcd.virAddr[1][0] = malloc(SCREEN_DMA_SIZE);
    gfx.lcd.virAddr[1][1] = malloc(SCREEN_DMA_SIZE);

    //gfx.lcd.virAddr[0][0] Up screen
    //gfx.lcd.virAddr[0][1] Down screen

    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[0][0]);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[0][1]);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[1][0]);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[1][1]);
#endif

    while (is_video_thread_running) {
#if defined(A30) || defined(RG28XX)
        if (nds.menu.enable) {
            if (nds.update_menu) {
                nds.update_menu = 0;
                GFX_Copy(-1, cvt->pixels, cvt->clip_rect, cvt->clip_rect, cvt->pitch, 0, E_MI_GFX_ROTATE_180);
                GFX_Flip();
            }
        }
        else if (nds.menu.drastic.enable) {
            if (nds.update_menu) {
                nds.update_menu = 0;
                GFX_Copy(-1, nds.menu.drastic.main->pixels, nds.menu.drastic.main->clip_rect, nds.menu.drastic.main->clip_rect, nds.menu.drastic.main->pitch, 0, 0);
                GFX_Flip();
            }
        }
        else if (nds.update_screen) {
#else
        if (nds.update_screen) {
#endif

            process_screen();
            nds.update_screen = 0;
        }
        else {
            usleep(0);
        }
    }

#if defined(A30) || defined(RG28XX)
    glDeleteTextures(TEX_MAX, vid.texID);
    eglMakeCurrent(vid.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(vid.eglDisplay, vid.eglContext);
    eglDestroySurface(vid.eglDisplay, vid.eglSurface);
    eglTerminate(vid.eglDisplay);

//GBM
    gbm_surface_destroy(drm_buf.gbm_surface);
    gbm_device_destroy(drm_buf.gbm);
    close(dri_fd);

    free(gfx.lcd.virAddr[0][0]);
    free(gfx.lcd.virAddr[0][1]);
    free(gfx.lcd.virAddr[1][0]);
    free(gfx.lcd.virAddr[1][1]);
#endif
    pthread_exit(NULL);
}

static int lang_unload(void)
{
    int cc = 0;

    for (cc=0; translate[cc]; cc++) {
        if (translate[cc]) {
            free(translate[cc]);
        }
        translate[cc] = NULL;
    }
    memset(translate, 0, sizeof(translate));
    return 0;
}

static int lang_load(const char *lang)
{
    FILE *f = NULL;
    char buf[MAX_PATH << 1] = {0};

    if (strcasecmp(nds.lang.trans[DEF_LANG_SLOT], DEF_LANG_LANG)) {
        sprintf(buf, "%s/%s", nds.lang.path, lang);
        f = fopen(buf, "r");

        if (f != NULL) {
            int cc = 0, len = 0;

            memset(buf, 0, sizeof(buf));
            while (fgets(buf, sizeof(buf), f)) {
                strip_newline(buf);
                len = strlen(buf) + 2;
                if (len == 0) {
                    continue;
                }

                if (translate[cc] != NULL) {
                    free(translate[cc]);
                }
                translate[cc] = malloc(len);
                if (translate[cc] != NULL) {
                    memcpy(translate[cc], buf, len);
                    //printf(PREFIX"Translate: \'%s\'(len=%d)\n", translate[cc], len);
                }
                cc+= 1;
                if (cc >= MAX_LANG_LINE) {
                    break;
                }
                memset(buf, 0, sizeof(buf));
            }
            fclose(f);
        }
        else {
            printf(PREFIX"Failed to open lang folder \'%s\'\n", nds.lang.path);
        }
    }
    return 0;
}

static void lang_enum(void)
{
    int idx = 2;
    DIR *d = NULL;
    struct dirent *dir = NULL;

    strcpy(nds.lang.trans[DEF_LANG_SLOT], DEF_LANG_LANG);
    strcpy(nds.lang.trans[DEF_LANG_SLOT + 1], DEF_LANG_LANG);
    d = opendir(nds.lang.path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_DIR) {
                continue;
            }
            if (strcmp(dir->d_name, ".") == 0) {
                continue;
            }
            if (strcmp(dir->d_name, "..") == 0) {
                continue;
            }

            //printf(PREFIX"found lang \'lang[%d]=%s\'\n", idx, dir->d_name);
            strcpy(nds.lang.trans[idx], dir->d_name);
            idx+= 1;
            if (idx >= MAX_LANG_FILE) {
                break;
            }
        }
        closedir(d);
    }
}

static int read_config(void)
{
#if defined(TRIMUI) || defined(PANDORA)
    int fd = -1;
#endif

    struct json_object *jval = NULL;
    struct json_object *jfile = NULL;

    jfile = json_object_from_file(nds.cfg.path);
    if (jfile == NULL) {
        printf(PREFIX"Failed to read settings from json file (%s)\n", nds.cfg.path);
        return -1;
    }

    json_object_object_get_ex(jfile, JSON_NDS_PEN_SEL, &jval);
    if (jval) {
        nds.pen.sel = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_PEN_POS, &jval);
    if (jval) {
        nds.pen.pos = json_object_get_int(jval) == 0 ? 0 : 1;
    }

    json_object_object_get_ex(jfile, JSON_NDS_THEME_SEL, &jval);
    if (jval) {
        nds.theme.sel = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_DIS_MODE, &jval);
    if (jval) {
        nds.dis_mode = json_object_get_int(jval);
    }
    
    json_object_object_get_ex(jfile, JSON_NDS_ALT_MODE, &jval);
    if (jval) {
        nds.alt_mode = json_object_get_int(jval);
    }
    
    json_object_object_get_ex(jfile, JSON_NDS_PEN_XV, &jval);
    if (jval) {
        nds.pen.xv = json_object_get_int(jval);
    }
    
    json_object_object_get_ex(jfile, JSON_NDS_PEN_YV, &jval);
    if (jval) {
        nds.pen.yv = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_ALPHA_VALUE, &jval);
    if (jval) {
        nds.alpha.val = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_ALPHA_POSITION, &jval);
    if (jval) {
        nds.alpha.pos = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_ALPHA_BORDER, &jval);
    if (jval) {
        nds.alpha.border = json_object_get_int(jval);
        nds.alpha.border%= NDS_BORDER_MAX;
    }

    json_object_object_get_ex(jfile, JSON_NDS_MAX_CPU, &jval);
    if (jval) {
        nds.maxcpu = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_MIN_CPU, &jval);
    if (jval) {
        nds.mincpu = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_MAX_CORE, &jval);
    if (jval) {
        nds.maxcore = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_MIN_CORE, &jval);
    if (jval) {
        nds.mincore = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_OVERLAY, &jval);
    if (jval) {
        nds.overlay.sel = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_SWAP_L1L2, &jval);
    if (jval) {
        nds.swap_l1l2 = json_object_get_int(jval) ? 1 : 0;
    }

    json_object_object_get_ex(jfile, JSON_NDS_SWAP_R1R2, &jval);
    if (jval) {
        nds.swap_r1r2 = json_object_get_int(jval) ? 1 : 0;
    }

    json_object_object_get_ex(jfile, JSON_NDS_KEYS_ROTATE, &jval);
    if (jval) {
        nds.keys_rotate = json_object_get_int(jval) % 3;
    }

    json_object_object_get_ex(jfile, JSON_NDS_CHK_BAT, &jval);
    if (jval) {
        nds.chk_bat = json_object_get_int(jval) ? 1: 0;
    }

#ifdef A30
    json_object_object_get_ex(jfile, JSON_NDS_JOY_MODE, &jval);
    if (jval) {
        nds.joy.mode = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_JOY_DZONE, &jval);
    if (jval) {
        nds.joy.dzone = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_JOY_CUSKEY0, &jval);
    if (jval) {
        nds.joy.cuskey[0] = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_JOY_CUSKEY1, &jval);
    if (jval) {
        nds.joy.cuskey[1] = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_JOY_CUSKEY2, &jval);
    if (jval) {
        nds.joy.cuskey[2] = json_object_get_int(jval);
    }

    json_object_object_get_ex(jfile, JSON_NDS_JOY_CUSKEY3, &jval);
    if (jval) {
        nds.joy.cuskey[3] = json_object_get_int(jval);
    }
#endif

    nds.menu.c0 = 0xffffff;
    json_object_object_get_ex(jfile, JSON_NDS_MENU_C0, &jval);
    if (jval) {
        const char *p = json_object_get_string(jval);
        nds.menu.c0 = strtol(p, NULL, 16);
    }

    nds.menu.c1 = 0xffffff;
    json_object_object_get_ex(jfile, JSON_NDS_MENU_C1, &jval);
    if (jval) {
        const char *p = json_object_get_string(jval);
        nds.menu.c1 = strtol(p, NULL, 16);
    }

    nds.menu.c2 = 0x289a35;
    json_object_object_get_ex(jfile, JSON_NDS_MENU_C2, &jval);
    if (jval) {
        const char *p = json_object_get_string(jval);
        nds.menu.c2 = strtol(p, NULL, 16);
    }

    nds.auto_state = 1;
    json_object_object_get_ex(jfile, JSON_NDS_AUTO_STATE, &jval);
    if (jval) {
        nds.auto_state = json_object_get_int(jval) ? 1 : 0;
    }

    nds.auto_slot = 1;
    json_object_object_get_ex(jfile, JSON_NDS_AUTO_SLOT, &jval);
    if (jval) {
        nds.auto_slot = json_object_get_int(jval);
    }

    lang_enum();
    json_object_object_get_ex(jfile, JSON_NDS_LANG, &jval);
    if (jval) {
        const char *lang = json_object_get_string(jval);

        strcpy(nds.lang.trans[DEF_LANG_SLOT], lang);
        lang_load(lang);
    }

    json_object_object_get_ex(jfile, JSON_NDS_HOTKEY, &jval);
    if (jval) {
        nds.hotkey = json_object_get_int(jval);
    }

    nds.menu.show_cursor = 0;
    json_object_object_get_ex(jfile, JSON_NDS_MENU_CURSOR, &jval);
    if (jval) {
        nds.menu.show_cursor = json_object_get_int(jval);
    }

    nds.fast_forward = 6;
    json_object_object_get_ex(jfile, JSON_NDS_FAST_FORWARD, &jval);
    if (jval) {
        nds.fast_forward = json_object_get_int(jval);
    }

#if defined(MMIYOO) || defined(A30) || defined(QX1000) || defined(RG28XX)
    json_object_object_get_ex(jfile, JSON_NDS_STATES, &jval);
    if (jval) {
        struct stat st = {0};
        const char *path = json_object_get_string(jval);

        if ((path != NULL) && (path[0] != 0)) {
            strcpy(nds.states.path, path);

            if (stat(nds.states.path, &st) == -1) {
                mkdir(nds.states.path, 0755);
                printf(PREFIX"Create states folder in \'%s\'\n", nds.states.path);
            }
        }
    }
#endif

    if (nds.dis_mode > NDS_DIS_MODE_LAST) {
        nds.dis_mode = NDS_DIS_MODE_VH_S0;
        nds.alt_mode = NDS_DIS_MODE_S0;
    }

    nds.menu.sel = 0;
    json_object_object_get_ex(jfile, JSON_NDS_MENU_BG, &jval);
    if (jval) {
        nds.menu.sel = json_object_get_int(jval);
        if (nds.menu.sel >= nds.menu.max) {
            nds.menu.sel = 0;
        }
    }
    reload_menu();

    reload_pen();
#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
    reload_overlay();
#endif
    json_object_put(jfile);

#if defined(TRIMUI) || defined(PANDORA)
    fd = open("/dev/dsp", O_RDWR);
    if (fd > 0) {
        close(fd);
#endif

#ifndef RG28XX
    snd_nds_reload_config();
#endif

#if defined(TRIMUI) || defined(PANDORA)
    }
#endif

#ifdef TRIMUI
    if ((nds.dis_mode != NDS_DIS_MODE_S0) && (nds.dis_mode != NDS_DIS_MODE_S1)) {
        nds.dis_mode = NDS_DIS_MODE_S0;
    }
    disp_resize();
#endif

#ifdef QX1000
    nds.dis_mode = NDS_DIS_MODE_H0;
#endif

#ifdef A30
    nds.joy.max_x = 200;
    nds.joy.zero_x = 135;
    nds.joy.min_x = 75;
    nds.joy.max_y = 200;
    nds.joy.zero_y = 135;
    nds.joy.min_y = 75;
#endif
    return 0;
}

static int write_config(void)
{
    struct json_object *jfile = NULL;

    jfile = json_object_from_file(nds.cfg.path);
    if (jfile == NULL) {
        printf(PREFIX"Failed to write settings to json file (%s)\n", nds.cfg.path);
        return -1;
    }

#ifdef TRIMUI
    if (need_restore) {
        nds.dis_mode = pre_dismode;
    }
#endif

    if (nds.dis_mode > NDS_DIS_MODE_LAST) {
        nds.dis_mode = NDS_DIS_MODE_LAST;
        nds.alt_mode = NDS_DIS_MODE_S0;
    }

    json_object_object_add(jfile, JSON_NDS_PEN_SEL, json_object_new_int(nds.pen.sel));
    json_object_object_add(jfile, JSON_NDS_THEME_SEL, json_object_new_int(nds.theme.sel));
    json_object_object_add(jfile, JSON_NDS_DIS_MODE, json_object_new_int(nds.dis_mode));
    json_object_object_add(jfile, JSON_NDS_ALPHA_VALUE, json_object_new_int(nds.alpha.val));
    json_object_object_add(jfile, JSON_NDS_ALPHA_POSITION, json_object_new_int(nds.alpha.pos));
    json_object_object_add(jfile, JSON_NDS_ALPHA_BORDER, json_object_new_int(nds.alpha.border));
    json_object_object_add(jfile, JSON_NDS_OVERLAY, json_object_new_int(nds.overlay.sel));
    json_object_object_add(jfile, JSON_NDS_ALT_MODE, json_object_new_int(nds.alt_mode));
    json_object_object_add(jfile, JSON_NDS_KEYS_ROTATE, json_object_new_int(nds.keys_rotate));
    json_object_object_add(jfile, JSON_NDS_LANG, json_object_new_string(nds.lang.trans[DEF_LANG_SLOT]));
    json_object_object_add(jfile, JSON_NDS_HOTKEY, json_object_new_int(nds.hotkey));
    json_object_object_add(jfile, JSON_NDS_SWAP_L1L2, json_object_new_int(nds.swap_l1l2));
    json_object_object_add(jfile, JSON_NDS_SWAP_R1R2, json_object_new_int(nds.swap_r1r2));
    json_object_object_add(jfile, JSON_NDS_PEN_XV, json_object_new_int(nds.pen.xv));
    json_object_object_add(jfile, JSON_NDS_PEN_YV, json_object_new_int(nds.pen.yv));
    json_object_object_add(jfile, JSON_NDS_MENU_BG, json_object_new_int(nds.menu.sel));
    json_object_object_add(jfile, JSON_NDS_MENU_CURSOR, json_object_new_int(nds.menu.show_cursor));
    json_object_object_add(jfile, JSON_NDS_FAST_FORWARD, json_object_new_int(nds.fast_forward));
    json_object_object_add(jfile, JSON_NDS_CHK_BAT, json_object_new_int(nds.chk_bat));

#ifdef A30
    json_object_object_add(jfile, JSON_NDS_JOY_MODE, json_object_new_int(nds.joy.mode));
    json_object_object_add(jfile, JSON_NDS_JOY_DZONE, json_object_new_int(nds.joy.dzone));
    json_object_object_add(jfile, JSON_NDS_JOY_CUSKEY0, json_object_new_int(nds.joy.cuskey[0]));
    json_object_object_add(jfile, JSON_NDS_JOY_CUSKEY1, json_object_new_int(nds.joy.cuskey[1]));
    json_object_object_add(jfile, JSON_NDS_JOY_CUSKEY2, json_object_new_int(nds.joy.cuskey[2]));
    json_object_object_add(jfile, JSON_NDS_JOY_CUSKEY3, json_object_new_int(nds.joy.cuskey[3]));
#endif
    json_object_object_add(jfile, JSON_NDS_CHK_BAT, json_object_new_int(nds.chk_bat));

    json_object_to_file_ext(nds.cfg.path, jfile, JSON_C_TO_STRING_PRETTY);
    json_object_put(jfile);
    printf(PREFIX"Wrote changed settings back !\n");
    return 0;
}

#ifdef MMIYOO
static int get_cpuclock(void)
{
    static const uint64_t divsrc = 432000000llu * 524288;

    int fd_mem = -1;
    void *pll_map = NULL;
    uint32_t rate = 0;
    uint32_t lpf_value = 0;
    uint32_t post_div = 0;

    fd_mem = open("/dev/mem", O_RDWR);
    if (fd_mem < 0) {
        return 0;
    }

    pll_map = mmap(0, PLL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_mem, BASE_REG_MPLL_PA);
    if (pll_map) {
        volatile uint8_t* p8 = (uint8_t*)pll_map;
        volatile uint16_t* p16 = (uint16_t*)pll_map;

        lpf_value = p16[0x2a4] + (p16[0x2a6] << 16);
        post_div = p16[0x232] + 1;
        if (lpf_value == 0) {
            lpf_value = (p8[0x2c2 << 1] << 16) + (p8[0x2c1 << 1] << 8) + p8[0x2c0 << 1];
        }

        if (lpf_value && post_div) {
            rate = (divsrc / lpf_value * 2 / post_div * 16);
        }
        printf(PREFIX"Current cpuclock=%u (lpf=%u, post_div=%u)\n", rate, lpf_value, post_div);
        munmap(pll_map, PLL_SIZE);
    }
    close(fd_mem);
    return rate / 1000000;
}

static int set_cpuclock(uint32_t newclock)
{
    int fd_mem = -1;
    void *pll_map = NULL;
    uint32_t post_div = 0;
    char clockstr[16] = {0};
    const char fn_governor[] = "/sys/devices/system/cpu/cpufreq/policy0/scaling_governor";
    const char fn_setspeed[] = "/sys/devices/system/cpu/cpufreq/policy0/scaling_setspeed";

    if ((newclock < nds.mincpu) || (newclock > nds.maxcpu)) {
        return -1;
    }

    fd_mem = open("/dev/mem", O_RDWR);
    if (fd_mem < 0) {
        return -1;
    }

    pll_map = mmap(0, PLL_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd_mem, BASE_REG_MPLL_PA);
    if (pll_map) {
        printf(PREFIX"Set CPU %dMHz\n", newclock);

        newclock*= 1000;
        sprintf(clockstr, "%d", newclock);
        write_file(fn_governor, "userspace", strlen("userspace"));
        write_file(fn_setspeed, clockstr, strlen(clockstr));

        if (newclock >= 800000) {
            post_div = 2;
        }
        else if (newclock >= 400000) {
            post_div = 4;
        }
        else if (newclock >= 200000) {
            post_div = 8;
        }
        else {
            post_div = 16;
        }

        if (1) {
            static const uint64_t divsrc = 432000000llu * 524288;
            uint32_t rate = (newclock * 1000) / 16 * post_div / 2;
            uint32_t lpf = (uint32_t)(divsrc / rate);
            volatile uint16_t* p16 = (uint16_t*)pll_map;
            uint32_t cur_post_div = (p16[0x232] & 0x0f) + 1;
            uint32_t tmp_post_div = cur_post_div;

            if (post_div > cur_post_div) {
                while (tmp_post_div != post_div) {
                    tmp_post_div <<= 1;
                    p16[0x232] = (p16[0x232] & 0xf0) | ((tmp_post_div - 1) & 0x0f);
                }
            }

            p16[0x2A8] = 0x0000;        // reg_lpf_enable = 0
            p16[0x2AE] = 0x000f;        // reg_lpf_update_cnt = 32
            p16[0x2A4] = lpf & 0xffff;  // set target freq to LPF high
            p16[0x2A6] = lpf >> 16;     // set target freq to LPF high
            p16[0x2B0] = 0x0001;        // switch to LPF control
            p16[0x2B2]|= 0x1000;        // from low to high
            p16[0x2A8] = 0x0001;        // reg_lpf_enable = 1
            while(!(p16[0x2ba] & 1));   // polling done
            p16[0x2A0] = lpf & 0xffff;  // store freq to LPF low
            p16[0x2A2] = lpf >> 16;     // store freq to LPF low

            if (post_div < cur_post_div) {
                while (tmp_post_div != post_div) {
                    tmp_post_div >>= 1;
                    p16[0x232] = (p16[0x232] & 0xf0) | ((tmp_post_div - 1) & 0x0f);
                }
            }
        }
        munmap(pll_map, PLL_SIZE);
    }
    close(fd_mem);
    return 0;
}
#endif

int get_dir_path(const char *path, int desire, char *buf)
{
    DIR *d = NULL;
    int count = 0, r = -1;
    struct dirent *dir = NULL;

    d = opendir(path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0) {
                continue;
            }

            if (strcmp(dir->d_name, "..") == 0) {
                continue;
            }

            if (dir->d_type != DT_DIR) {
                continue;
            }

            if (count == desire) {
                r = snprintf(buf, MAX_PATH, "%s/%s", path, dir->d_name) ? 0 : 1;
                break;
            }
            count+= 1;
        }
        closedir(d);
    }
    return r;
}

static int get_file_path(const char *path, int desire, char *buf, int add_path)
{
    DIR *d = NULL;
    int count = 0, r = -1;
    struct dirent *dir = NULL;

    d = opendir(path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (strcmp(dir->d_name, ".") == 0) {
                continue;
            }

            if (strcmp(dir->d_name, "..") == 0) {
                continue;
            }

            if (dir->d_type == DT_DIR) {
                continue;
            }

            if (count == desire) {
                if (add_path) {
                    r = snprintf(buf, MAX_PATH, "%s/%s", path, dir->d_name) ? 0 : 1;
                }
                else {
                    r = snprintf(buf, MAX_PATH, "%s", dir->d_name) ? 0 : 1;
                }
                break;
            }
            count+= 1;
        }
        closedir(d);
    }
    return r;
}

static int get_dir_count(const char *path)
{
    DIR *d = NULL;
    int count = 0;
    struct dirent *dir = NULL;

    d = opendir(path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type != DT_DIR) {
                continue;
            }
            if (strcmp(dir->d_name, ".") == 0) {
                continue;
            }
            if (strcmp(dir->d_name, "..") == 0) {
                continue;
            }
            count+= 1;
        }
        closedir(d);
    }
    return count;
}

static int get_file_count(const char *path)
{
    DIR *d = NULL;
    int count = 0;
    struct dirent *dir = NULL;

    d = opendir(path);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_type == DT_DIR) {
                continue;
            }
            if (strcmp(dir->d_name, ".") == 0) {
                continue;
            }
            if (strcmp(dir->d_name, "..") == 0) {
                continue;
            }
            count+= 1;
        }
        closedir(d);
    }
    return count;
}

static int get_theme_count(void)
{
    return get_dir_count(nds.theme.path);
}

static int get_menu_count(void)
{
    return get_dir_count(MENU_PATH);
}

static int get_pen_count(void)
{
    return get_file_count(nds.pen.path);
}

static int get_overlay_count(void)
{
    return get_file_count(nds.overlay.path);
}

#ifdef UNITTEST
int fb_init(void)
{
    return 0;
}

int fb_quit(void)
{
    return 0;
}
#endif

#ifdef QX1000
int fb_init(void)
{
    is_wl_thread_running = 1;
    wl.bg = SDL_malloc(LCD_W * LCD_H * 2);
    memset(wl.bg, 0, LCD_W * LCD_H * 2);
    pthread_create(&thread_id[0], NULL, wl_thread, NULL);
    pthread_create(&thread_id[1], NULL, draw_thread, NULL);
    while (wl.init == 0) {
        usleep(100000);
    }

    wl.flip = 0;
    wl.ready = 0;
    wl.data = SDL_malloc(LCD_W * LCD_H * 2);
    memset(wl.data, 0, LCD_W * LCD_H *2);
    update_wayland_res(NDS_W * 2, NDS_H);
    wl.ready = 1;
    return 0;
}

int fb_quit(void)
{
    return 0;
}
#endif

#ifdef PANDORA
int fb_init(void)
{
    gfx.fb_dev[0] = open("/dev/fb0", O_RDWR);
    gfx.fb_dev[1] = open("/dev/fb1", O_RDWR);
    ioctl(gfx.fb_dev[1], OMAPFB_QUERY_PLANE, &gfx.pi);
    ioctl(gfx.fb_dev[1], OMAPFB_QUERY_MEM, &gfx.mi);
    if(gfx.pi.enabled){
        gfx.pi.enabled = 0;
        ioctl(gfx.fb_dev[1], OMAPFB_SETUP_PLANE, &gfx.pi);
    }
    gfx.mi.size = FB_SIZE;
    ioctl(gfx.fb_dev[1], OMAPFB_SETUP_MEM, &gfx.mi);

    gfx.pi.enabled = 1;
    gfx.pi.pos_x = 0;
    gfx.pi.pos_y = 0;
    gfx.pi.out_width = FB_W;
    gfx.pi.out_height = FB_H;
    ioctl(gfx.fb_dev[1], OMAPFB_SETUP_PLANE, &gfx.pi);

    ioctl(gfx.fb_dev[0], FBIOGET_VSCREENINFO, &gfx.vinfo);
    ioctl(gfx.fb_dev[0], FBIOGET_FSCREENINFO, &gfx.finfo);
    gfx.hw.mem[0] = mmap(0, FB_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, gfx.fb_dev[0], 0);
    memset(gfx.hw.mem[0], 0, FB_SIZE);

    ioctl(gfx.fb_dev[1], FBIOGET_VSCREENINFO, &gfx.vinfo);
    ioctl(gfx.fb_dev[1], FBIOGET_FSCREENINFO, &gfx.finfo);
    gfx.hw.mem[1] = mmap(0, FB_SIZE, PROT_WRITE | PROT_READ, MAP_SHARED, gfx.fb_dev[1], 0);
    memset(gfx.hw.mem[1], 0, FB_SIZE);
    return 0;
}

int fb_quit(void)
{
    ioctl(gfx.fb_dev[1], OMAPFB_QUERY_PLANE, &gfx.pi);
    gfx.pi.enabled = 0;
    ioctl(gfx.fb_dev[1], OMAPFB_SETUP_PLANE, &gfx.pi);
    munmap(gfx.hw.mem[0], FB_SIZE);
    munmap(gfx.hw.mem[1], FB_SIZE);
    close(gfx.fb_dev[0]);
    close(gfx.fb_dev[1]);
    gfx.fb_dev[0] = -1;
    gfx.fb_dev[1] = -1;
    return 0;
}
#endif

#ifdef TRIMUI
static int ion_alloc(int ion_fd, ion_alloc_info_t* info)
{
    struct ion_allocation_data iad;
    struct ion_fd_data ifd;
    struct ion_custom_data icd;
    sunxi_phys_data spd;

    iad.len = info->size;
    iad.align = sysconf(_SC_PAGESIZE);
    iad.heap_id_mask = ION_HEAP_TYPE_DMA_MASK;
    iad.flags = 0;
    if (ioctl(ion_fd, ION_IOC_ALLOC, &iad) < 0) {
        printf(PREFIX"Failed to call ION_IOC_ALLOC\n");
        return -1;
    }

    icd.cmd = ION_IOC_SUNXI_PHYS_ADDR;
    icd.arg = (uintptr_t)&spd;
    spd.handle = iad.handle;
    if (ioctl(ion_fd, ION_IOC_CUSTOM, &icd) < 0) {
        printf(PREFIX"Failed to call ION_IOC_CUSTOM\n");
        return -1;
    }
    ifd.handle = iad.handle;
    if (ioctl(ion_fd, ION_IOC_MAP, &ifd) < 0) {
        printf(PREFIX"Failed to call ION_IOC_MAP\n");
    }

    info->handle = iad.handle;
    info->fd = ifd.fd;
    info->padd = (void*)spd.phys_addr;
    info->vadd = mmap(0, info->size, PROT_READ | PROT_WRITE, MAP_SHARED, info->fd, 0);
    printf(PREFIX"Mmap padd:0x%x, vadd:0x%x, size:%d\n", (uintptr_t)info->padd, (uintptr_t)info->vadd, info->size);
    return 0;
}

static void ion_free(int ion_fd, ion_alloc_info_t* info)
{
    struct ion_handle_data ihd;

    munmap(info->vadd, info->size);
    close(info->fd);
    ihd.handle = info->handle;
    if (ioctl(ion_fd, ION_IOC_FREE, &ihd) < 0) {
        printf(PREFIX"Failed to call ION_ION_FREE\n");
    }
}

int fb_init(void)
{
    int r = 0;
    uint32_t args[4] = {0, (uintptr_t)&gfx.hw.disp, 1, 0};

    gfx.fb_dev = open("/dev/fb0", O_RDWR);
    gfx.ion_dev = open("/dev/ion", O_RDWR);
    gfx.mem_dev = open("/dev/mem", O_RDWR);
    gfx.disp_dev = open("/dev/disp", O_RDWR);
    if (gfx.fb_dev < 0) {
        printf(PREFIX"Failed to open /dev/fb0\n");
        return -1;
    }

    memset(&gfx.hw.disp, 0, sizeof(disp_layer_config));
    memset(&gfx.hw.buf, 0, sizeof(disp_layer_config));
    gfx.hw.mem = mmap(0, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_SHARED, gfx.mem_dev, OVL_V);

    ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &r);

    gfx.hw.disp.channel = DEF_FB_CH;
    gfx.hw.disp.layer_id = DEF_FB_LAYER;
    ioctl(gfx.disp_dev, DISP_LAYER_GET_CONFIG, args);

    gfx.hw.disp.enable = 0;
    ioctl(gfx.disp_dev, DISP_LAYER_SET_CONFIG, args);

    gfx.hw.ion.size = ION_W * ION_H * FB_BPP * 2;
    ion_alloc(gfx.ion_dev, &gfx.hw.ion);

    gfx.hw.buf.channel = SCALER_CH;
    gfx.hw.buf.layer_id = SCALER_LAYER;
    gfx.hw.buf.enable = 1;
    gfx.hw.buf.info.fb.format = DISP_FORMAT_ARGB_8888;
    gfx.hw.buf.info.fb.addr[0] = (uintptr_t)gfx.hw.ion.padd;
    gfx.hw.buf.info.fb.size[0].width = FB_H;
    gfx.hw.buf.info.fb.size[0].height = FB_W;
    gfx.hw.buf.info.mode = LAYER_MODE_BUFFER;
    gfx.hw.buf.info.zorder = SCALER_ZORDER;
    gfx.hw.buf.info.alpha_mode = 0;
    gfx.hw.buf.info.alpha_value = 0;
    gfx.hw.buf.info.screen_win.x = 0;
    gfx.hw.buf.info.screen_win.y = 0;
    gfx.hw.buf.info.screen_win.width  = FB_H;
    gfx.hw.buf.info.screen_win.height = FB_W;
    gfx.hw.buf.info.fb.pre_multiply = 0;
    gfx.hw.buf.info.fb.crop.x = (uint64_t)0 << 32;
    gfx.hw.buf.info.fb.crop.y = (uint64_t)0 << 32;
    gfx.hw.buf.info.fb.crop.width  = (uint64_t)FB_H << 32;
    gfx.hw.buf.info.fb.crop.height = (uint64_t)FB_W << 32;
    args[1] = (uintptr_t)&gfx.hw.buf;
    ioctl(gfx.disp_dev, DISP_LAYER_SET_CONFIG, args);
    ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &r);
    return 0;
}

int fb_quit(void)
{
    uint32_t args[4] = {0, (uintptr_t)&gfx.hw.disp, 1, 0};

    gfx.hw.disp.enable = gfx.hw.buf.enable = 0;
    ioctl(gfx.disp_dev, DISP_LAYER_SET_CONFIG, args);

    args[1] = (uintptr_t)&gfx.hw.buf;
    ioctl(gfx.disp_dev, DISP_LAYER_SET_CONFIG, args);

    gfx.hw.disp.enable = 1;
    gfx.hw.disp.channel = DEF_FB_CH;
    gfx.hw.disp.layer_id = DEF_FB_LAYER;
    args[1] = (uintptr_t)&gfx.hw.disp;
    ioctl(gfx.disp_dev, DISP_LAYER_SET_CONFIG, args);

    ion_free(gfx.ion_dev, &gfx.hw.ion);
    munmap(gfx.hw.mem, sysconf(_SC_PAGESIZE));

    close(gfx.fb_dev);
    close(gfx.ion_dev);
    close(gfx.mem_dev);
    close(gfx.disp_dev);

    gfx.fb_dev = -1;
    gfx.ion_dev = -1;
    gfx.mem_dev = -1;
    gfx.disp_dev = -1;
    return 0;
}

void disp_resize(void)
{
    int r = 0;
    uint32_t args[4] = {0, (uintptr_t)&gfx.hw.buf, 1, 0};

    ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &r);
    if (nds.dis_mode == NDS_DIS_MODE_S0) {
        gfx.hw.buf.info.fb.crop.width  = ((uint64_t)FB_H) << 32;
        gfx.hw.buf.info.fb.crop.height = ((uint64_t)FB_W) << 32;
    }
    else {
        gfx.hw.buf.info.fb.crop.width  = ((uint64_t)NDS_H) << 32;
        gfx.hw.buf.info.fb.crop.height = ((uint64_t)NDS_W) << 32;
    }
    ioctl(gfx.disp_dev, DISP_LAYER_SET_CONFIG, args);
    ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &r);
}
#endif

#ifdef RG28XX
static int get_core(int index)
{
    FILE *fd = NULL;
    char buf[255] = {0};

    sprintf(buf, "cat /sys/devices/system/cpu/cpu%d/online", index % 4);
    fd = popen(buf, "r");
    if (fd == NULL) {
        return -1;
    }
    fgets(buf, sizeof(buf), fd);
    pclose(fd);
    return atoi(buf);
}

static void check_before_set(int num, int v)
{
    char buf[255] = {0};

    if (get_core(num) != v) {
        sprintf(buf, "echo %d > /sys/devices/system/cpu/cpu%d/online", v ? 1 : 0, num);
        system(buf);
    }
}

static void set_core(int n)
{
    if (n <= 1) {
        printf(PREFIX"New CPU Core: 1\n");
        check_before_set(0, 1);
        check_before_set(1, 0);
        check_before_set(2, 0);
        check_before_set(3, 0);
    }
    else if (n == 2) {
        printf(PREFIX"New CPU Core: 2\n");
        check_before_set(0, 1);
        check_before_set(1, 1);
        check_before_set(2, 0);
        check_before_set(3, 0);
    }
    else if (n == 3) {
        printf(PREFIX"New CPU Core: 3\n");
        check_before_set(0, 1);
        check_before_set(1, 1);
        check_before_set(2, 1);
        check_before_set(3, 0);
    }
    else {
        printf(PREFIX"New CPU Core: 4\n");
        check_before_set(0, 1);
        check_before_set(1, 1);
        check_before_set(2, 1);
        check_before_set(3, 1);
    }
}

uint64_t page_size = 4096;
uint64_t aligned_offset = 0;
size_t map_size = 0;
static int drm_create_fb(struct drm_device *bo)
{
	/* create a dumb-buffer, the pixel format is XRGB888 */
	bo->create.width = bo->width;
	bo->create.height = bo->height;
	bo->create.bpp = 32;

	/* handle, pitch, size will be returned */
	drmIoctl(dri_fd, DRM_IOCTL_MODE_CREATE_DUMB, &bo->create);

	/* bind the dumb-buffer to an FB object */
	bo->pitch = bo->create.pitch;
	bo->size = bo->create.size;
	bo->handle = bo->create.handle;
	drmModeAddFB(dri_fd, bo->width, bo->height, 24, 32, bo->pitch,
			   bo->handle, &bo->fb_id);

	printf("pitch = %d ,size = %d, handle = %d \n",bo->pitch,bo->size,bo->handle);

	/* map the dumb-buffer to userspace */
	bo->map.handle = bo->create.handle;
	drmIoctl(dri_fd, DRM_IOCTL_MODE_MAP_DUMB, &bo->map);


     	//Solve: -D_FILE_OFFSET_BITS=64
	bo->vaddr = (uint32_t *)mmap(0, bo->create.size, PROT_READ | PROT_WRITE,
			MAP_SHARED, dri_fd, bo->map.offset);
     	//mmap failed: Invalid argument

        printf("Parameters: size=%d, offset=%llx, fd=%d\n", 
               bo->size, bo->map.offset, dri_fd);
    	if (bo->vaddr == MAP_FAILED) {
        	printf("mmap failed: %s\n", strerror(errno));
        	printf("Parameters: size=%d, offset=%llx, fd=%d\n", 
          	     bo->size, bo->map.offset, dri_fd);
       		return -1;
    	}
	/* initialize the dumb-buffer with white-color */
	memset(bo->vaddr, 0x0,bo->size);

	return 0;
}

static void drm_destroy_fb(struct drm_device *bo)
{
	struct drm_mode_destroy_dumb destroy = {};
	drmModeRmFB(dri_fd, bo->fb_id);
	munmap(bo->vaddr, bo->size);
	destroy.handle = bo->handle;
	drmIoctl(dri_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

int drm_init()
{
	// Open Dri
	dri_fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
	if(dri_fd < 0){
		printf("dri wrong\n");
		return -1;
	}
        printf("fd flags: 0x%x\n", fcntl(dri_fd, F_GETFL));

	res = drmModeGetResources(dri_fd);
	crtc_id = res->crtcs[0];
	conn_id = res->connectors[1];
	printf("crtc = %d , conneter = %d\n",crtc_id,conn_id);

	conn = drmModeGetConnector(dri_fd, conn_id);
	drm_buf.width = conn->modes[0].hdisplay;
	drm_buf.height = conn->modes[0].vdisplay;

	printf("width = %d , height = %d\n",drm_buf.width,drm_buf.height);

	drm_create_fb(&drm_buf);
	
	// Init GBM Device
	drm_buf.gbm = gbm_create_device(dri_fd);
	if (!drm_buf.gbm) {
		fprintf(stderr, "Cannot create gbm device\n");
		return -1;
	}
	drm_buf.gbm_surface = gbm_surface_create(
		drm_buf.gbm,
		640,
		480,
		GBM_FORMAT_ARGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
	);

	if (!drm_buf.gbm_surface) {
		fprintf(stderr, "Cannot create gbm surface\n");
		return -1;
	}
	//Set CRTCS
	drmModeSetCrtc(dri_fd, crtc_id, drm_buf.fb_id,
			0, 0, &conn_id, 1, &conn->modes[0]);

	return 0;
}
void drm_exit()
{
	drm_destroy_fb(&drm_buf);
	drmModeFreeConnector(conn);
	drmModeFreeResources(res);
	close(dri_fd);
}
int fb_init(void)
{    
    drm_init();
    
    //kmsdrm

    gfx.fb.virAddr = drm_buf.vaddr;

    if (gfx.fb.virAddr == (void *)-1) {
        close(gfx.fb_dev);
        gfx.fb_dev = -1;
        printf(PREFIX"Failed to mmap fb\n");
        return -1;
    }
    return 0;
}

int fb_quit(void)
{
    set_core(DEINIT_CPU_CORE);
    system("echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

    if (gfx.fb.virAddr) {
        munmap(gfx.fb.virAddr, FB_SIZE);
        gfx.fb.virAddr = NULL;
    }

    if (gfx.fb_dev > 0) {
        close(gfx.fb_dev);
        gfx.fb_dev = -1;
    }

    if (vid.mem_fd > 0) {
        close(vid.mem_fd);
        vid.mem_fd = -1;
    }
    drm_destroy_fb(&drm_buf);
    drmModeFreeConnector(conn);
    drmModeFreeResources(res);
    close(dri_fd);
    return 0;
}
#endif

#ifdef A30
static int get_core(int index)
{
    FILE *fd = NULL;
    char buf[255] = {0};

    sprintf(buf, "cat /sys/devices/system/cpu/cpu%d/online", index % 4); 
    fd = popen(buf, "r");
    if (fd == NULL) {
        return -1;
    }       
    fgets(buf, sizeof(buf), fd);
    pclose(fd);
    return atoi(buf);
}

static void check_before_set(int num, int v)
{
    char buf[255] = {0};

    if (get_core(num) != v) {
        sprintf(buf, "echo %d > /sys/devices/system/cpu/cpu%d/online", v ? 1 : 0, num);
        system(buf);
    }       
}   

static void set_core(int n)
{           
    if (n <= 1) {
        printf(PREFIX"New CPU Core: 1\n");
        check_before_set(0, 1);
        check_before_set(1, 0);
        check_before_set(2, 0);
        check_before_set(3, 0);
    }       
    else if (n == 2) {
        printf(PREFIX"New CPU Core: 2\n");
        check_before_set(0, 1);
        check_before_set(1, 1);
        check_before_set(2, 0);
        check_before_set(3, 0);
    }       
    else if (n == 3) {
        printf(PREFIX"New CPU Core: 3\n");
        check_before_set(0, 1);
        check_before_set(1, 1);
        check_before_set(2, 1);
        check_before_set(3, 0);
    }
    else {
        printf(PREFIX"New CPU Core: 4\n");
        check_before_set(0, 1);
        check_before_set(1, 1);
        check_before_set(2, 1);
        check_before_set(3, 1);
    }
}

static int set_best_match_cpu_clock(int clk)
{
    int cc = 0;

    system("echo performance > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");
    for (cc = 0; cc < max_cpu_item; cc++) {
        if (cpu_clock[cc].clk >= clk) {
            printf(PREFIX"Set Best Match CPU %dMHz (0x%08x)\n", cpu_clock[cc].clk, cpu_clock[cc].reg);
            *vid.cpu_ptr = cpu_clock[cc].reg;
            return cc;
        }
    }
    return -1;
}

int fb_init(void)
{
    gfx.fb_dev = open("/dev/fb0", O_RDWR, 0);
    if (gfx.fb_dev < 0) {
        printf(PREFIX"Failed to open /dev/fb0\n");
        return -1;
    }

    if (ioctl(gfx.fb_dev, FBIOGET_VSCREENINFO, &gfx.vinfo) < 0) {
        printf(PREFIX"Failed to get fb info\n");
        return -1;
    }

    gfx.fb.virAddr = mmap(NULL, FB_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, gfx.fb_dev, 0);
    if (gfx.fb.virAddr == (void *)-1) {
        close(gfx.fb_dev);
        gfx.fb_dev = -1;
        printf(PREFIX"Failed to mmap fb\n");
        return -1;
    }
    printf(PREFIX"FB virAddr %p (size:%d)\n", gfx.fb.virAddr, FB_SIZE);
    memset(gfx.fb.virAddr, 0 , FB_SIZE);

    gfx.vinfo.yres_virtual = gfx.vinfo.yres * 2;
    ioctl(gfx.fb_dev, FBIOPUT_VSCREENINFO, &gfx.vinfo);

    vid.mem_fd = open("/dev/mem", O_RDWR);
    if (vid.mem_fd < 0) { 
        printf("Failed to open /dev/mem\n");
        return -1;
    }    
    vid.ccu_mem = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, vid.mem_fd, CCU_BASE);
    if (vid.ccu_mem == MAP_FAILED) {
        printf("Failed to map memory\n");
        return -1;
    }    
    printf(PREFIX"CCU MMap %p\n", vid.ccu_mem);
    vid.cpu_ptr = (uint32_t *)&vid.ccu_mem[0x00];

    vid.dac_mem = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, vid.mem_fd, DAC_BASE);
    if (vid.dac_mem == MAP_FAILED) {
        printf("Failed to map memory\n");
        return -1;
    }    
    printf(PREFIX"DAC MMap %p\n", vid.dac_mem);
    vid.vol_ptr = (uint32_t *)(&vid.dac_mem[0xc00 + 0x258]);

    set_best_match_cpu_clock(INIT_CPU_CLOCK);
    set_core(INIT_CPU_CORE);
    return 0;
}

int fb_quit(void)
{
    set_best_match_cpu_clock(DEINIT_CPU_CLOCK);
    set_core(DEINIT_CPU_CORE);
    system("echo ondemand > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor");

    if (gfx.fb.virAddr) {
        munmap(gfx.fb.virAddr, FB_SIZE);
        gfx.fb.virAddr = NULL;
    }

    if (gfx.fb_dev > 0) {
        close(gfx.fb_dev);
        gfx.fb_dev = -1;
    }

    if (vid.ccu_mem != MAP_FAILED) {
        munmap(vid.ccu_mem, 4096);
        vid.ccu_mem = NULL;
    }

    if (vid.dac_mem != MAP_FAILED) {
        munmap(vid.dac_mem, 4096);
        vid.dac_mem = NULL;
    }

    if (vid.mem_fd > 0) {
        close(vid.mem_fd);
        vid.mem_fd = -1;
    }
    return 0;
}
#endif

#ifdef MMIYOO
int fb_init(void)
{
#if USE_MASK
    int c0 = 0;
    int c1 = 0;
    uint32_t *p = NULL;
#endif

    MI_SYS_Init();
    MI_GFX_Open();

    gfx.fb_dev = open("/dev/fb0", O_RDWR);
    if (gfx.fb_dev < 0) {
        printf(PREFIX"Failed to open /dev/fb0\n");
        return -1;
    }
    ioctl(gfx.fb_dev, FBIOGET_FSCREENINFO, &gfx.finfo);
    ioctl(gfx.fb_dev, FBIOGET_VSCREENINFO, &gfx.vinfo);
    gfx.vinfo.yoffset = 0;
    gfx.vinfo.yres_virtual = gfx.vinfo.yres * 2;
    ioctl(gfx.fb_dev, FBIOPUT_VSCREENINFO, &gfx.vinfo);

    gfx.fb.phyAddr = gfx.finfo.smem_start;
    MI_SYS_MemsetPa(gfx.fb.phyAddr, 0, FB_SIZE);
    MI_SYS_Mmap(gfx.fb.phyAddr, gfx.finfo.smem_len, &gfx.fb.virAddr, TRUE);
    memset(&gfx.hw.opt, 0, sizeof(gfx.hw.opt));

    MI_SYS_MMA_Alloc(NULL, TMP_SIZE, &gfx.tmp.phyAddr);
    MI_SYS_Mmap(gfx.tmp.phyAddr, TMP_SIZE, &gfx.tmp.virAddr, TRUE);

    MI_SYS_MMA_Alloc(NULL, TMP_SIZE, &gfx.overlay.phyAddr);
    MI_SYS_Mmap(gfx.overlay.phyAddr, TMP_SIZE, &gfx.overlay.virAddr, TRUE);

    MI_SYS_MMA_Alloc(NULL, SCREEN_DMA_SIZE, &gfx.lcd.phyAddr[0][0]);
    MI_SYS_MMA_Alloc(NULL, SCREEN_DMA_SIZE, &gfx.lcd.phyAddr[0][1]);
    MI_SYS_MMA_Alloc(NULL, SCREEN_DMA_SIZE, &gfx.lcd.phyAddr[1][0]);
    MI_SYS_MMA_Alloc(NULL, SCREEN_DMA_SIZE, &gfx.lcd.phyAddr[1][1]);
    MI_SYS_Mmap(gfx.lcd.phyAddr[0][0], SCREEN_DMA_SIZE, &gfx.lcd.virAddr[0][0], TRUE);
    MI_SYS_Mmap(gfx.lcd.phyAddr[0][1], SCREEN_DMA_SIZE, &gfx.lcd.virAddr[0][1], TRUE);
    MI_SYS_Mmap(gfx.lcd.phyAddr[1][0], SCREEN_DMA_SIZE, &gfx.lcd.virAddr[1][0], TRUE);
    MI_SYS_Mmap(gfx.lcd.phyAddr[1][1], SCREEN_DMA_SIZE, &gfx.lcd.virAddr[1][1], TRUE);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[0][0]);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[0][1]);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[1][0]);
    printf(PREFIX"Ping-pong Buffer %p\n", gfx.lcd.virAddr[1][1]);

#if USE_MASK
    MI_SYS_MMA_Alloc(NULL, MASK_SIZE, &gfx.mask.phyAddr[0]);
    MI_SYS_MMA_Alloc(NULL, MASK_SIZE, &gfx.mask.phyAddr[1]);
    MI_SYS_Mmap(gfx.mask.phyAddr[0], MASK_SIZE, &gfx.mask.virAddr[0], TRUE);
    MI_SYS_Mmap(gfx.mask.phyAddr[1], MASK_SIZE, &gfx.mask.virAddr[1], TRUE);

    p = (uint32_t *)gfx.mask.virAddr[0];
    for (c0 = 0; c0 < NDS_Hx3; c0++) {
        for (c1 = 0; c1 < NDS_Wx3; c1++) {
            if (((c0 % 3) == 0) || (c1 % 3) == 0) {
                *p++ = 0xff000000;
            }
            else {
                *p++ = 0x00000000;
            }
        }
    }
    MI_SYS_FlushInvCache(gfx.mask.virAddr[0], MASK_SIZE);
#endif

    vid.sar_fd = open("/dev/sar", O_RDWR);
    return 0;
}

int fb_quit(void)
{
    MI_SYS_Munmap(gfx.fb.virAddr, TMP_SIZE);

    MI_SYS_Munmap(gfx.tmp.virAddr, TMP_SIZE);
    MI_SYS_MMA_Free(gfx.tmp.phyAddr);

    MI_SYS_Munmap(gfx.overlay.virAddr, TMP_SIZE);
    MI_SYS_MMA_Free(gfx.overlay.phyAddr);

#if USE_MASK
    MI_SYS_Munmap(gfx.mask.virAddr[0], MASK_SIZE);
    MI_SYS_Munmap(gfx.mask.virAddr[1], MASK_SIZE);
    MI_SYS_MMA_Free(gfx.mask.phyAddr[0]);
    MI_SYS_MMA_Free(gfx.mask.phyAddr[1]);
#endif

    MI_SYS_Munmap(gfx.lcd.virAddr[0][0], SCREEN_DMA_SIZE);
    MI_SYS_Munmap(gfx.lcd.virAddr[0][1], SCREEN_DMA_SIZE);
    MI_SYS_Munmap(gfx.lcd.virAddr[1][0], SCREEN_DMA_SIZE);
    MI_SYS_Munmap(gfx.lcd.virAddr[1][1], SCREEN_DMA_SIZE);
    MI_SYS_MMA_Free(gfx.lcd.phyAddr[0][0]);
    MI_SYS_MMA_Free(gfx.lcd.phyAddr[0][1]);
    MI_SYS_MMA_Free(gfx.lcd.phyAddr[1][0]);
    MI_SYS_MMA_Free(gfx.lcd.phyAddr[1][1]);

    MI_GFX_Close();
    MI_SYS_Exit();

    gfx.vinfo.yoffset = 0;
    ioctl(gfx.fb_dev, FBIOPUT_VSCREENINFO, &gfx.vinfo);
    close(gfx.fb_dev);
    gfx.fb_dev = -1;

    close(vid.sar_fd);
    vid.sar_fd = -1;
    return 0;
}
#endif

void GFX_Init(void)
{
    struct stat st = {0};
    char buf[MAX_PATH << 1] = {0};

#ifdef TRIMUI
    int x = 0;
    int y = 0;
    int ox = 32;
    int oy = 24;
    int cc = 0;
    uint32_t *dst = NULL;
#endif

    fb_init();
    memset(nds.pen.path, 0, sizeof(nds.pen.path));
    if (getcwd(nds.pen.path, sizeof(nds.pen.path))) {
        strcat(nds.pen.path, "/");
        strcat(nds.pen.path, PEN_PATH);
    }

    strcpy(nds.shot.path, SHOT_PATH);
    if (stat(nds.shot.path, &st) == -1) {
        mkdir(nds.shot.path, 0755);
    }

    memset(nds.theme.path, 0, sizeof(nds.theme.path));
    if (getcwd(nds.theme.path, sizeof(nds.theme.path))) {
        strcat(nds.theme.path, "/");
        strcat(nds.theme.path, THEME_PATH);
#ifdef PANDORA
        strcat(nds.theme.path, "_800");
#else
        strcat(nds.theme.path, nds.enable_752x560 ? "_752" : "_640");
#endif
    }

    memset(nds.overlay.path, 0, sizeof(nds.overlay.path));
    if (getcwd(nds.overlay.path, sizeof(nds.overlay.path))) {
        strcat(nds.overlay.path, "/");
        strcat(nds.overlay.path, OVERLAY_PATH);
    }

    memset(nds.lang.path, 0, sizeof(nds.lang.path));
    if (getcwd(nds.lang.path, sizeof(nds.lang.path))) {
        strcat(nds.lang.path, "/");
        strcat(nds.lang.path, LANG_PATH);
    }

    memset(nds.menu.path, 0, sizeof(nds.menu.path));
    if (getcwd(nds.menu.path, sizeof(nds.menu.path))) {
        strcat(nds.menu.path, "/");
        strcat(nds.menu.path, MENU_PATH);
    }

    memset(nds.bios.path, 0, sizeof(nds.bios.path));
    if (getcwd(nds.bios.path, sizeof(nds.bios.path))) {
        strcat(nds.bios.path, "/");
        strcat(nds.bios.path, BIOS_PATH);

        sprintf(buf, "%s/drastic_bios_arm7.bin", nds.bios.path);
        write_file(buf, drastic_bios_arm7, sizeof(drastic_bios_arm7));

        sprintf(buf, "%s/drastic_bios_arm9.bin", nds.bios.path);
        write_file(buf, drastic_bios_arm9, sizeof(drastic_bios_arm9));

        sprintf(buf, "%s/nds_bios_arm7.bin", nds.bios.path);
        write_file(buf, nds_bios_arm7, sizeof(nds_bios_arm7));

        sprintf(buf, "%s/nds_bios_arm9.bin", nds.bios.path);
        write_file(buf, nds_bios_arm9, sizeof(nds_bios_arm9));

        sprintf(buf, "%s/nds_firmware.bin", nds.bios.path);
        write_file(buf, nds_firmware, sizeof(nds_firmware));
    }

#ifdef QX1000
    cvt = SDL_CreateRGBSurface(SDL_SWSURFACE, IMG_W, IMG_H, 32, 0, 0, 0, 0);
#else
    cvt = SDL_CreateRGBSurface(SDL_SWSURFACE, FB_W, FB_H, 32, 0, 0, 0, 0);
#endif

    nds.pen.sel = 0;
    nds.pen.max = get_pen_count();

    nds.theme.sel = 0;
    nds.theme.max = get_theme_count();

    nds.overlay.sel = nds.overlay.max = get_overlay_count();

    nds.menu.sel = 0;
    nds.menu.max = get_menu_count();

#ifdef QX1000
    nds.menu.drastic.main = SDL_CreateRGBSurface(SDL_SWSURFACE, IMG_W, IMG_H, 32, 0, 0, 0, 0);
#else
    nds.menu.drastic.main = SDL_CreateRGBSurface(SDL_SWSURFACE, FB_W, FB_H, 32, 0, 0, 0, 0);
#endif
    if (nds.menu.drastic.main) {
        if (nds.menu.drastic.bg0) {
            SDL_SoftStretch(nds.menu.drastic.bg0, NULL, nds.menu.drastic.main, NULL);
        }
    }

    getcwd(nds.cfg.path, sizeof(nds.cfg.path));
    strcat(nds.cfg.path, "/");
    strcat(nds.cfg.path, CFG_PATH);

    TTF_Init();
    nds.font = TTF_OpenFont(FONT_PATH, FONT_SIZE);
    if (nds.enable_752x560) {
        //TTF_SetFontStyle(nds.font, TTF_STYLE_BOLD);
    }

#ifdef TRIMUI
    cc = 0;
    for (y = 0; y < NDS_H; y++) {
        for (x = 0; x < NDS_W; x++) {
            dst = (uint32_t *)gfx.hw.ion.vadd;
            LUT_256x192_S00[cc] = (uint32_t)(dst + ((((NDS_W - 1) - x) + ox) * FB_H) + y + oy);
            LUT_256x192_S10[cc] = (uint32_t)(dst + ((((NDS_W - 1) - x)) * FB_H) + y);

            dst = (uint32_t *)gfx.hw.ion.vadd + (FB_W * FB_H);
            LUT_256x192_S01[cc] = (uint32_t)(dst + ((((NDS_W - 1) - x) + ox) * FB_H) + y + oy);
            LUT_256x192_S11[cc] = (uint32_t)(dst + ((((NDS_W - 1) - x)) * FB_H) + y);
            cc+= 1;
        }
    }
#endif

    is_video_thread_running = 1;
    pthread_create(&thread, NULL, video_handler, (void *)NULL);
}

void GFX_Quit(void)
{
    void *ret = NULL;

    printf(PREFIX"Wait for video_handler exit\n");
    is_video_thread_running = 0;
    pthread_join(thread, &ret);

    GFX_Clear();
    printf(PREFIX"Free FB resources\n");
    fb_quit();

    if (cvt) {
        SDL_FreeSurface(cvt);
        cvt = NULL;
    }
}

void GFX_Clear(void)
{
#ifdef MMIYOO
    MI_SYS_MemsetPa(gfx.fb.phyAddr, 0, FB_SIZE);
    MI_SYS_MemsetPa(gfx.tmp.phyAddr, 0, TMP_SIZE);
    MI_SYS_MemsetPa(gfx.lcd.phyAddr[0][0], 0, SCREEN_DMA_SIZE);
    MI_SYS_MemsetPa(gfx.lcd.phyAddr[0][1], 0, SCREEN_DMA_SIZE);
    MI_SYS_MemsetPa(gfx.lcd.phyAddr[1][0], 0, SCREEN_DMA_SIZE);
    MI_SYS_MemsetPa(gfx.lcd.phyAddr[1][1], 0, SCREEN_DMA_SIZE);
#endif
}

int draw_pen(void *pixels, int width, int pitch)
{
#ifndef UNITTEST
    int c0 = 0;
    int c1 = 0;
    int w = 28;
    int h = 28;
    int sub = 0;
    int sw = NDS_W;
    int sh = NDS_H;
    int x0 = 0, y0 = 0;
    int x1 = 0, y1 = 0;
    int x = 0, y = 0, is_565 = 0, scale = 1;
    uint32_t r = 0, g = 0, b = 0;
    uint32_t *s = hex_pen;
    uint16_t *d_565 = (uint16_t*)pixels;
    uint32_t *d_888 = (uint32_t*)pixels;

    if ((pitch / width) == 2) {
        is_565 = 1;
    }

    if (width == NDS_Wx2) {
        sw = NDS_Wx2;
        sh = NDS_Hx2;
        scale = 2;
    }

    x = (evt.mouse.x * sw) / evt.mouse.maxx;
    y = (evt.mouse.y * sh) / evt.mouse.maxy;

    if (nds.pen.img) {
        w = nds.pen.img->w;
        h = nds.pen.img->h;
        s = nds.pen.img->pixels;
    }

    switch(nds.pen.type) {
    case PEN_LT:
        break;
    case PEN_LB:
        y -= (h * scale);
        break;
    case PEN_RT:
        x -= (w * scale);
        break;
    case PEN_RB:
        x -= (w * scale);
        y -= (h * scale);
        break;
    case PEN_CP:
        x -= ((w * scale) >> 1);
        y -= ((h * scale) >> 1);
        break;
    }

#if 0
    vVertices[0] = ((((float)x) / NDS_W) - 0.5) * 2.0;
    vVertices[1] = ((((float)y) / NDS_H) - 0.5) * -2.0;

    vVertices[5] = vVertices[0];
    vVertices[6] = ((((float)(y + nds.pen.img->h)) / NDS_H) - 0.5) * -2.0;

    vVertices[10] = ((((float)(x + nds.pen.img->w)) / NDS_W) - 0.5) * 2.0;
    vVertices[11] = vVertices[6];

    vVertices[15] = vVertices[10];
    vVertices[16] = vVertices[1];

    glUniform1f(vid.alphaLoc, 1.0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vid.texID[TEX_PEN]);
    glVertexAttribPointer(vid.posLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), vVertices);
    glVertexAttribPointer(vid.texLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &vVertices[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    glDisable(GL_BLEND);
    glUniform1f(vid.alphaLoc, 0.0);
#endif

    asm ("PLD [%0, #128]"::"r" (s));
    for (c1=0; c1<h; c1++) {
        asm ("PLD [%0, #128]"::"r" (d_565));
        asm ("PLD [%0, #128]"::"r" (d_888));
        for (c0=0; c0<w; c0++) {
            x0 = x1 = (c0 * scale) + x;
            y0 = y1 = (c1 * scale) + (y - sub);

            if (scale == 2) {
                x0 += 1;
                y0 += 1;
            }
            if ((y0 >= 0) && (y0 < sh) && (x0 < sw) && (x0 >= 0)) {
                if (*s) {
                    if (is_565) {
                        r = (*s & 0xf80000) >> 8;
                        g = (*s & 0x00f800) >> 5;
                        b = (*s & 0x0000f8) >> 3;
                        d_565[(y1 * width) + x1] = r | g | b;
                        if (scale == 2) {
                            d_565[((y1 + 0) * width) + (x1 + 1)] = r | g | b;
                            d_565[((y1 + 1) * width) + (x1 + 0)] = r | g | b;
                            d_565[((y1 + 1) * width) + (x1 + 1)] = r | g | b;
                        }
                    }
                    else {
                        d_888[(y1 * width) + x1] = *s;
                        if (scale == 2) {
                            d_888[((y1 + 0) * width) + (x1 + 1)] = *s;
                            d_888[((y1 + 1) * width) + (x1 + 0)] = *s;
                            d_888[((y1 + 1) * width) + (x1 + 1)] = *s;
                        }
                    }
                }
            }
            s+= 1;
        }
    }
#endif
    return 0;
}

int GFX_Copy(int id, const void *pixels, SDL_Rect srcrect, SDL_Rect dstrect, int pitch, int alpha, int rotate)
{
#if defined(A30) || defined(RG28XX)
    int tex = (id >= 0) ? id : TEX_TMP;

    if ((id != -1) && ((nds.dis_mode == NDS_DIS_MODE_HH1) || (nds.dis_mode == NDS_DIS_MODE_HH3))) {
        vVertices[5] = ((((float)dstrect.x) / 640.0) - 0.5) * 2.0;
        vVertices[6] = ((((float)dstrect.y) / 480.0) - 0.5) * -2.0;

        vVertices[10] = vVertices[5];
        vVertices[11] = ((((float)(dstrect.y + dstrect.w)) / 480.0) - 0.5) * -2.0;

        vVertices[15] = ((((float)(dstrect.x + dstrect.h)) / 640.0) - 0.5) * 2.0;
        vVertices[16] = vVertices[11];

        vVertices[0] = vVertices[15];
        vVertices[1] = vVertices[6];
    }
    else if ((id != -1) && ((nds.dis_mode == NDS_DIS_MODE_HH0) || (nds.dis_mode == NDS_DIS_MODE_HH2))) {
        vVertices[15] = ((((float)dstrect.x) / 640.0) - 0.5) * 2.0;
        vVertices[16] = ((((float)dstrect.y) / 480.0) - 0.5) * -2.0;

        vVertices[0] = vVertices[15];
        vVertices[1] = ((((float)(dstrect.y + dstrect.w)) / 480.0) - 0.5) * -2.0;

        vVertices[5] = ((((float)(dstrect.x + dstrect.h)) / 640.0) - 0.5) * 2.0;
        vVertices[6] = vVertices[1];

        vVertices[10] = vVertices[5];
        vVertices[11] = vVertices[16];
    }
    else {
        vVertices[0] = ((((float)dstrect.x) / 640.0) - 0.5) * 2.0;
        vVertices[1] = ((((float)dstrect.y) / 480.0) - 0.5) * -2.0;

        vVertices[5] = vVertices[0];
        vVertices[6] = ((((float)(dstrect.y + dstrect.h)) / 480.0) - 0.5) * -2.0;

        vVertices[10] = ((((float)(dstrect.x + dstrect.w)) / 640.0) - 0.5) * 2.0;
        vVertices[11] = vVertices[6];

        vVertices[15] = vVertices[10];
        vVertices[16] = vVertices[1];
    }

    if (tex == TEX_TMP) {
        glBindTexture(GL_TEXTURE_2D, vid.texID[tex]);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        if (pixel_filter) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        else {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, srcrect.w, srcrect.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    if (((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1)) && (tex == TEX_SCR0)) {
        glUniform1f(vid.alphaLoc, 1.0 - ((float)nds.alpha.val / 10.0));
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_BLEND);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, vid.texID[tex]);
    glVertexAttribPointer(vid.posLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), vVertices);
    glVertexAttribPointer(vid.texLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &vVertices[3]);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

    if (((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1)) && (tex == TEX_SCR0)) {
        glUniform1f(vid.alphaLoc, 0.0);
        glDisable(GL_BLEND);
    }
#endif

#ifdef QX1000
    int x = 0;
    int y = 0;
    const uint32_t *src = pixels;
    uint32_t *dst = (uint32_t *)wl.pixels[wl.flip];

    if (srcrect.w == NDS_W) {
        dst += (dstrect.y ? 0 : NDS_W);
        asm volatile (
            "0:  vldmia %0!, {q0-q7}    ;"
            "    vldmia %0!, {q8-q15}   ;"
            "    vstmia %1!, {q0-q7}    ;"
            "    vstmia %1!, {q8-q15}   ;"
            "1:  vldmia %0!, {q0-q7}    ;"
            "    vldmia %0!, {q8-q15}   ;"
            "    vstmia %1!, {q0-q7}    ;"
            "    vstmia %1!, {q8-q15}   ;"
            "2:  vldmia %0!, {q0-q7}    ;"
            "    vldmia %0!, {q8-q15}   ;"
            "    vstmia %1!, {q0-q7}    ;"
            "    vstmia %1!, {q8-q15}   ;"
            "3:  vldmia %0!, {q0-q7}    ;"
            "    vldmia %0!, {q8-q15}   ;"
            "    vstmia %1!, {q0-q7}    ;"
            "    vstmia %1!, {q8-q15}   ;"
            "    add %1, %1, %2         ;"
            "    subs %3, #1            ;"
            "    bne 0b                 ;"
            :
            : "r"(pixels), "r"(dst), "r"((FB_W - srcrect.w) * 4), "r"(NDS_H)
            : "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "memory", "cc"
        );
    }
    else {
        for (y = 0; y < srcrect.h; y++) {
            for (x = 0; x < srcrect.w; x++) {
                *dst++ = *src++;
            }
            dst+= (FB_W - srcrect.w);
        }
    }
#endif

#ifdef PANDORA
    if ((pitch == 1024) && (srcrect.w == NDS_W) && (srcrect.h == NDS_H)) {
        uint32_t *dst = (uint32_t *)gfx.hw.mem[(gfx.vinfo.yoffset == 0) ? 0 : 1];

        if (dstrect.y == 0) {
            dst += 16;
            dst += (((FB_H - NDS_Hx2) >> 1) * FB_W);
            asm volatile (
                "0:  add r8, %1, %2         ;"
                "1:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "2:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "3:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "4:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "5:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "6:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "7:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "8:  vldmia %0!, {q0-q3}    ;"
                "    vldmia %0!, {q8-q11}   ;"
                "    vdup.32 d15, d7[1]     ;"
                "    vdup.32 d14, d7[0]     ;"
                "    vdup.32 d13, d6[1]     ;"
                "    vdup.32 d12, d6[0]     ;"
                "    vdup.32 d11, d5[1]     ;"
                "    vdup.32 d10, d5[0]     ;"
                "    vdup.32 d9, d4[1]      ;"
                "    vdup.32 d8, d4[0]      ;"
                "    vdup.32 d7, d3[1]      ;"
                "    vdup.32 d6, d3[0]      ;"
                "    vdup.32 d5, d2[1]      ;"
                "    vdup.32 d4, d2[0]      ;"
                "    vdup.32 d3, d1[1]      ;"
                "    vdup.32 d2, d1[0]      ;"
                "    vdup.32 d1, d0[1]      ;"
                "    vdup.32 d0, d0[0]      ;"
                "    vdup.32 d31, d23[1]    ;"
                "    vdup.32 d30, d23[0]    ;"
                "    vdup.32 d29, d22[1]    ;"
                "    vdup.32 d28, d22[0]    ;"
                "    vdup.32 d27, d21[1]    ;"
                "    vdup.32 d26, d21[0]    ;"
                "    vdup.32 d25, d20[1]    ;"
                "    vdup.32 d24, d20[0]    ;"
                "    vdup.32 d23, d19[1]    ;"
                "    vdup.32 d22, d19[0]    ;"
                "    vdup.32 d21, d18[1]    ;"
                "    vdup.32 d20, d18[0]    ;"
                "    vdup.32 d19, d17[1]    ;"
                "    vdup.32 d18, d17[0]    ;"
                "    vdup.32 d17, d16[1]    ;"
                "    vdup.32 d16, d16[0]    ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    vstmia r8!, {q0-q7}    ;"
                "    vstmia r8!, {q8-q15}   ;"
                "    add %1, %1, #1152      ;"
                "    add %1, %1, %2         ;"
                "    subs %3, #1            ;"
                "    bne 0b                 ;"
                :
                : "r"(pixels), "r"(dst), "r"(800 * 4), "r"(NDS_H)
                : "r8", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "memory", "cc"
            );
        }
        else {
            dst += (((FB_H - NDS_H) >> 1) * FB_W) + 1;
            asm volatile (
                "0:  add %1, %1, #2112      ;"
                "1:  vldmia %0!, {q0-q7}    ;"
                "    vldmia %0!, {q8-q15}   ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "2:  vldmia %0!, {q0-q7}    ;"
                "    vldmia %0!, {q8-q15}   ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "3:  vldmia %0!, {q0-q7}    ;"
                "    vldmia %0!, {q8-q15}   ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "4:  vldmia %0!, {q0-q7}    ;"
                "    vldmia %0!, {q8-q15}   ;"
                "    vstmia %1!, {q0-q7}    ;"
                "    vstmia %1!, {q8-q15}   ;"
                "    add %1, %1, #64        ;"
                "    subs %2, #1            ;"
                "    bne 0b                 ;"
                :
                : "r"(pixels), "r"(dst), "r"(NDS_H)
                : "r8", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "memory", "cc"
            );
        }
    }
    else {
        int x = 0;
        int y = 0;
        const uint32_t *src = pixels;
        uint32_t *dst = (uint32_t *)gfx.hw.mem[(gfx.vinfo.yoffset == 0) ? 0 : 1];

        for (y = 0; y < srcrect.h; y++) {
            for (x = 0; x < srcrect.w; x++) {
                *dst++ = *src++;
            }
            dst+= (FB_W - srcrect.w);
        }
    }
#endif

#ifdef TRIMUI
    int x = 0;
    int y = 0;
    int ox = 0;
    int oy = 0;
    int sw = srcrect.w;
    int sh = srcrect.h;
    uint32_t *dst = NULL;
    uint32_t *src = (uint32_t *)pixels;

    if (pixels == NULL) {
        return -1;
    }

    if ((pitch / srcrect.w) != 4) {
        printf(PREFIX"Only support 32bits (%dx%dx%d)\n", srcrect.w, srcrect.h, (pitch / srcrect.w));
        return -1;
    }

    if (nds.dis_mode == NDS_DIS_MODE_S0) {
        ox = 32;
        oy = 24;
    }

    if((srcrect.w == NDS_W) && (srcrect.h == NDS_H)) {
        if (nds.dis_mode == NDS_DIS_MODE_S0) {
            dst = gfx.fb.flip ? LUT_256x192_S01 : LUT_256x192_S00;
        }
        else {
            dst = gfx.fb.flip ? LUT_256x192_S11 : LUT_256x192_S10;
        }

        asm volatile (
            "1:  vldmia %0!, {q0-q7}   ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s0, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s1, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s2, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s3, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s4, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s5, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s6, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s7, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s8, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s9, [r8]         ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s10, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s11, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s12, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s13, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s14, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s15, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s16, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s17, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s18, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s19, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s20, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s21, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s22, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s23, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s24, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s25, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s26, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s27, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s28, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s29, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s30, [r8]        ;"
            "    ldr r8, [%1], #4      ;"
            "    vstr s31, [r8]        ;"
            "    subs %2, #1            ;"
            "    bne 1b                 ;"
            :
            : "r"(src), "r"(dst), "r"(8 * NDS_H)
            : "r8", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "memory", "cc"
        );
    }
    else {
        if ((srcrect.w >= 320) || (srcrect.h >= 240)) {
            ox = 0;
            oy = 0;
            sw = FB_W;
            sh = FB_H;
        }

        dst = (uint32_t *)gfx.hw.ion.vadd + (FB_W * FB_H * gfx.fb.flip);
        for (y = 0; y < sh; y++) {
            for (x = 0; x < sw; x++) {
                dst[((((sw - 1) - x) + ox) * FB_H) + y + oy] = *src++;
            }
        }
    }
#endif

#ifdef MMIYOO
    int cc = 0;
    int copy_it = 1;
    int dma_found = 0;
    MI_U16 u16Fence = 0;
    int is_rgb565 = (pitch / srcrect.w) == 2 ? 1 : 0;

    if (pixels == NULL) {
        return -1;
    }

    for (cc = 0; cc < 2; cc++) {
        if (pixels == gfx.lcd.virAddr[cc][0]) {
            dma_found = 1;
            gfx.hw.src.surf.phyAddr = gfx.lcd.phyAddr[cc][0];
            break;
        }
        if (pixels == gfx.lcd.virAddr[cc][1]) {
            dma_found = 1;
            gfx.hw.src.surf.phyAddr = gfx.lcd.phyAddr[cc][1];
            break;
        }
    }

    if (alpha != 0) {
        if (nds.alpha.val > NDS_ALPHA_MAX) {
            nds.alpha.val = 0;
        }

        if (nds.alpha.val > 0) {
            float m0 = (float)nds.alpha.val / 10;
            float m1 = 1.0 - m0;
            uint32_t *d = gfx.tmp.virAddr;
            uint32_t r0 = 0, g0 = 0, b0 = 0;
            uint32_t r1 = 0, g1 = 0, b1 = 0;
            int x = 0, y = 0, ax = 0, ay = 0, sw = 0, sh = 0;
            const uint32_t *s0 = gfx.fb.virAddr + (FB_W * gfx.vinfo.yoffset * FB_BPP);
            const uint16_t *s1_565 = pixels;
            const uint32_t *s1_888 = pixels;
            uint32_t col[] = {
                0x000000, 0xa0a0a0, 0x400000, 0x004000, 0x000040, 0x000000, 0xa0a000, 0x00a0a0
            };

            switch (nds.dis_mode) {
            case NDS_DIS_MODE_VH_T0:
                sw = 170;
                sh = 128;
                break;
            case NDS_DIS_MODE_VH_T1:
                sw = srcrect.w;
                sh = srcrect.h;
                break;
            }

            ay = 0;
            for (y=0; y<sh; y++) {
                switch (nds.dis_mode) {
                case NDS_DIS_MODE_VH_T0:
                    if (y && ((y % 2) == 0)) {
                        ay+= 1;
                    }
                    break;
                }

                ax = 0;
                for (x=0; x<sw; x++) {
                    asm ("PLD [%0, #128]"::"r" (s0));
                    if ((nds.alpha.border > 0) && ((y == 0) || (y == (sh - 1)) || (x == 0) || (x == (sw - 1)))) {
                        *d++ = col[nds.alpha.border];
                    }
                    else {
                        switch (nds.dis_mode) {
                        case NDS_DIS_MODE_VH_T0:
                            if (x && ((x % 2) == 0)) {
                                ax+= 1;
                            }
                            break;
                        }

                        if (is_rgb565) {
                            asm ("PLD [%0, #128]"::"r" (s1_565));
                            r1 = (s1_565[((y + ay) * srcrect.w) + x + ax] & 0xf800) >> 8;
                            g1 = (s1_565[((y + ay) * srcrect.w) + x + ax] & 0x07e0) >> 3;
                            b1 = (s1_565[((y + ay) * srcrect.w) + x + ax] & 0x001f) << 3;
                        }
                        else {
                            asm ("PLD [%0, #128]"::"r" (s1_888));
                            r1 = (s1_888[((y + ay) * srcrect.w) + x + ax] & 0xff0000) >> 16;
                            g1 = (s1_888[((y + ay) * srcrect.w) + x + ax] & 0x00ff00) >> 8;
                            b1 = (s1_888[((y + ay) * srcrect.w) + x + ax] & 0x0000ff) >> 0;
                        }

                        switch (nds.alpha.pos % 4) {
                        case 0:
                            r0 = (s0[((sh - y + (FB_H - sh) - 1) * FB_W) + (sw - x - 1)] & 0xff0000) >> 16;
                            g0 = (s0[((sh - y + (FB_H - sh) - 1) * FB_W) + (sw - x - 1)] & 0x00ff00) >> 8;
                            b0 = (s0[((sh - y + (FB_H - sh) - 1) * FB_W) + (sw - x - 1)] & 0x0000ff) >> 0;
                            break;
                        case 1:
                            r0 = (s0[((sh - y + (FB_H - sh) - 1) * FB_W) + (sw - x + (FB_W - sw) - 1)] & 0xff0000) >> 16;
                            g0 = (s0[((sh - y + (FB_H - sh) - 1) * FB_W) + (sw - x + (FB_W - sw) - 1)] & 0x00ff00) >> 8;
                            b0 = (s0[((sh - y + (FB_H - sh) - 1) * FB_W) + (sw - x + (FB_W - sw) - 1)] & 0x0000ff) >> 0;
                            break;
                        case 2:
                            r0 = (s0[((sh - y - 1) * FB_W) + (sw - x + (FB_W - sw) - 1)] & 0xff0000) >> 16;
                            g0 = (s0[((sh - y - 1) * FB_W) + (sw - x + (FB_W - sw) - 1)] & 0x00ff00) >> 8;
                            b0 = (s0[((sh - y - 1) * FB_W) + (sw - x + (FB_W - sw) - 1)] & 0x0000ff) >> 0;
                            break;
                        case 3:
                            r0 = (s0[((sh - y - 1) * FB_W) + (sw - x - 1)] & 0xff0000) >> 16;
                            g0 = (s0[((sh - y - 1) * FB_W) + (sw - x - 1)] & 0x00ff00) >> 8;
                            b0 = (s0[((sh - y - 1) * FB_W) + (sw - x - 1)] & 0x0000ff) >> 0;
                            break;
                        }

                        r0 = (uint8_t)((r0 * m0) + (r1 * m1));
                        g0 = (uint8_t)((g0 * m0) + (g1 * m1));
                        b0 = (uint8_t)((b0 * m0) + (b1 * m1));
                        *d++ = ((r0 << 16) | (g0 << 8) | b0);
                    }
                }
            }
            copy_it = 0;
        }

        switch (nds.dis_mode) {
        case NDS_DIS_MODE_VH_T0:
            dstrect.w = 170;
            dstrect.h = 128;
            if (nds.alpha.val > 0) {
                srcrect.w = dstrect.w;
                srcrect.h = dstrect.h;
                pitch = srcrect.w * 4;
            }
            break;
        case NDS_DIS_MODE_VH_T1:
            dstrect.w = NDS_W;
            dstrect.h = NDS_H;
            if (nds.alpha.val > 0) {
                srcrect.w = dstrect.w;
                srcrect.h = dstrect.h;
                pitch = srcrect.w * 4;
            }
            break;
        }

        switch (nds.alpha.pos % 4) {
        case 0:
            dstrect.x = 0;
            dstrect.y = FB_H - dstrect.h;
            break;
        case 1:
            dstrect.x = FB_W - dstrect.w;
            dstrect.y = FB_H - dstrect.h;
            break;
        case 2:
            dstrect.x = FB_W - dstrect.w;
            dstrect.y = 0;
            break;
        case 3:
            dstrect.x = 0;
            dstrect.y = 0;
            break;
        }
    }

    if (copy_it && pixel_filter) {
        do {
            if (nds.hres_mode != 0) {
                break;
            }

            if ((srcrect.w != NDS_W) || (srcrect.h != NDS_H)) {
                break;
            }

            if (pitch == 1024) {
#if USE_MASK
                if (dma_found) {
                    gfx.hw.opt.u32GlobalSrcConstColor = 0;
                    gfx.hw.opt.eRotate = 0;
                    gfx.hw.opt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
                    gfx.hw.opt.eDstDfbBldOp = 0;
                    gfx.hw.opt.eDFBBlendFlag = 0;

                    gfx.hw.src.rt.s32Xpos = 0;
                    gfx.hw.src.rt.s32Ypos = 0;
                    gfx.hw.src.rt.u32Width = NDS_W;
                    gfx.hw.src.rt.u32Height = NDS_H;
                    gfx.hw.src.surf.u32Width = NDS_W;
                    gfx.hw.src.surf.u32Height = NDS_H;
                    gfx.hw.src.surf.u32Stride = pitch;
                    gfx.hw.src.surf.eColorFmt = is_rgb565 ? E_MI_GFX_FMT_RGB565 : E_MI_GFX_FMT_ARGB8888;

                    gfx.hw.dst.rt.s32Xpos = 0;
                    gfx.hw.dst.rt.s32Ypos = 0;
                    gfx.hw.dst.rt.u32Width = NDS_Wx3;
                    gfx.hw.dst.rt.u32Height = NDS_Hx3;
                    gfx.hw.dst.surf.u32Width = NDS_Wx3;
                    gfx.hw.dst.surf.u32Height = NDS_Hx3;
                    gfx.hw.dst.surf.u32Stride = NDS_Wx3 * FB_BPP;
                    gfx.hw.dst.surf.eColorFmt = E_MI_GFX_FMT_ARGB8888;
                    gfx.hw.dst.surf.phyAddr = gfx.mask.phyAddr[1];

                    MI_GFX_BitBlit(&gfx.hw.src.surf, &gfx.hw.src.rt, &gfx.hw.dst.surf, &gfx.hw.dst.rt, &gfx.hw.opt, &u16Fence);
                    MI_GFX_WaitAllDone(FALSE, u16Fence);

                    gfx.hw.overlay.surf.phyAddr = gfx.mask.phyAddr[0];
                    gfx.hw.overlay.surf.eColorFmt = E_MI_GFX_FMT_ARGB8888;
                    gfx.hw.overlay.surf.u32Width = NDS_Wx3;
                    gfx.hw.overlay.surf.u32Height = NDS_Hx3;
                    gfx.hw.overlay.surf.u32Stride = NDS_Wx3 * FB_BPP;
                    gfx.hw.overlay.rt.s32Xpos = 0;
                    gfx.hw.overlay.rt.s32Ypos = 0;
                    gfx.hw.overlay.rt.u32Width = NDS_Wx3;
                    gfx.hw.overlay.rt.u32Height = NDS_Hx3;

                    gfx.hw.dst.rt.s32Xpos = 0;
                    gfx.hw.dst.rt.s32Ypos = 0;
                    gfx.hw.dst.rt.u32Width = NDS_Wx3;
                    gfx.hw.dst.rt.u32Height = NDS_Hx3;
                    gfx.hw.dst.surf.phyAddr = gfx.mask.phyAddr[1];

                    gfx.hw.opt.u32GlobalSrcConstColor = 0xff000000;
                    gfx.hw.opt.eRotate = 0;
                    gfx.hw.opt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
                    gfx.hw.opt.eDstDfbBldOp = E_MI_GFX_DFB_BLD_INVSRCALPHA;
                    gfx.hw.opt.eDFBBlendFlag = E_MI_GFX_DFB_BLEND_SRC_PREMULTIPLY | E_MI_GFX_DFB_BLEND_COLORALPHA | E_MI_GFX_DFB_BLEND_ALPHACHANNEL;
                    MI_GFX_BitBlit(&gfx.hw.overlay.surf, &gfx.hw.overlay.rt, &gfx.hw.dst.surf, &gfx.hw.dst.rt, &gfx.hw.opt, &u16Fence);
                    MI_GFX_WaitAllDone(FALSE, u16Fence);

                    copy_it = 0;
                    srcrect.x = 0;
                    srcrect.y = 0;
                    srcrect.w = NDS_Wx3;
                    srcrect.h = NDS_Hx3;
                    pitch = srcrect.w * 4;
                }
                else
#endif
                {
                    asm volatile (
                        "0:  add r8, %1, %2         ;"
                        "1:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "2:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "3:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "4:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "5:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "6:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "7:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "8:  vldmia %0!, {q0-q3}    ;"
                        "    vldmia %0!, {q8-q11}   ;"
                        "    vdup.32 d15, d7[1]     ;"
                        "    vdup.32 d14, d7[0]     ;"
                        "    vdup.32 d13, d6[1]     ;"
                        "    vdup.32 d12, d6[0]     ;"
                        "    vdup.32 d11, d5[1]     ;"
                        "    vdup.32 d10, d5[0]     ;"
                        "    vdup.32 d9, d4[1]      ;"
                        "    vdup.32 d8, d4[0]      ;"
                        "    vdup.32 d7, d3[1]      ;"
                        "    vdup.32 d6, d3[0]      ;"
                        "    vdup.32 d5, d2[1]      ;"
                        "    vdup.32 d4, d2[0]      ;"
                        "    vdup.32 d3, d1[1]      ;"
                        "    vdup.32 d2, d1[0]      ;"
                        "    vdup.32 d1, d0[1]      ;"
                        "    vdup.32 d0, d0[0]      ;"
                        "    vdup.32 d31, d23[1]    ;"
                        "    vdup.32 d30, d23[0]    ;"
                        "    vdup.32 d29, d22[1]    ;"
                        "    vdup.32 d28, d22[0]    ;"
                        "    vdup.32 d27, d21[1]    ;"
                        "    vdup.32 d26, d21[0]    ;"
                        "    vdup.32 d25, d20[1]    ;"
                        "    vdup.32 d24, d20[0]    ;"
                        "    vdup.32 d23, d19[1]    ;"
                        "    vdup.32 d22, d19[0]    ;"
                        "    vdup.32 d21, d18[1]    ;"
                        "    vdup.32 d20, d18[0]    ;"
                        "    vdup.32 d19, d17[1]    ;"
                        "    vdup.32 d18, d17[0]    ;"
                        "    vdup.32 d17, d16[1]    ;"
                        "    vdup.32 d16, d16[0]    ;"
                        "    vstmia %1!, {q0-q7}    ;"
                        "    vstmia %1!, {q8-q15}   ;"
                        "    vstmia r8!, {q0-q7}    ;"
                        "    vstmia r8!, {q8-q15}   ;"
                        "    add %1, %1, %2         ;"
                        "    subs %3, #1            ;"
                        "    bne 0b                 ;"
                        :
                        : "r"(pixels), "r"(gfx.tmp.virAddr), "r"(NDS_Wx2 * 4), "r"(NDS_H)
                        : "r8", "q0", "q1", "q2", "q3", "q4", "q5", "q6", "q7", "q8", "q9", "q10", "q11", "q12", "q13", "q14", "q15", "memory", "cc"
                    );

                    copy_it = 0;
                    srcrect.x = 0;
                    srcrect.y = 0;
                    srcrect.w = NDS_Wx2;
                    srcrect.h = NDS_Hx2;
                    pitch = srcrect.w * 4;
                }
            }
            else {
                int x = 0, y = 0;
                uint16_t *s0 = NULL;
                uint16_t *s1 = (uint16_t*)pixels;
                uint16_t *d = (uint16_t*)gfx.tmp.virAddr;

                for (y=0; y<srcrect.h; y++) {
                    s0 = d;
                    for (x=0; x<srcrect.w; x++) {
                        *d++ = *s1;
                        *d++ = *s1++;
                    }
                    neon_memcpy(d, s0, 1024);
                    d+= NDS_Wx2;
                }

                copy_it = 0;
                srcrect.x = 0;
                srcrect.y = 0;
                srcrect.w = NDS_Wx2;
                srcrect.h = NDS_Hx2;
                pitch = srcrect.w * 2;
            }
        } while(0);
    }

    if (copy_it) {
        if (dma_found == 0) {
            neon_memcpy(gfx.tmp.virAddr, pixels, srcrect.h * pitch);
            gfx.hw.src.surf.phyAddr = gfx.tmp.phyAddr;
            MI_SYS_FlushInvCache(gfx.tmp.virAddr, pitch * srcrect.h);
        }
    }
    else {
#if USE_MASK
        gfx.hw.src.surf.phyAddr = gfx.mask.phyAddr[1];
        MI_SYS_FlushInvCache(gfx.tmp.virAddr, pitch * srcrect.h);
#else
        gfx.hw.src.surf.phyAddr = gfx.tmp.phyAddr;
        MI_SYS_FlushInvCache(gfx.tmp.virAddr, pitch * srcrect.h);
#endif
    }

    gfx.hw.opt.u32GlobalSrcConstColor = 0;
    gfx.hw.opt.eRotate = rotate;
    gfx.hw.opt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
    gfx.hw.opt.eDstDfbBldOp = 0;
    gfx.hw.opt.eDFBBlendFlag = 0;

    gfx.hw.src.rt.s32Xpos = srcrect.x;
    gfx.hw.src.rt.s32Ypos = srcrect.y;
    gfx.hw.src.rt.u32Width = srcrect.w;
    gfx.hw.src.rt.u32Height = srcrect.h;
    gfx.hw.src.surf.u32Width = srcrect.w;
    gfx.hw.src.surf.u32Height = srcrect.h;
    gfx.hw.src.surf.u32Stride = pitch;
    gfx.hw.src.surf.eColorFmt = is_rgb565 ? E_MI_GFX_FMT_RGB565 : E_MI_GFX_FMT_ARGB8888;

    gfx.hw.dst.rt.s32Xpos = dstrect.x;
    gfx.hw.dst.rt.s32Ypos = dstrect.y;
    gfx.hw.dst.rt.u32Width = dstrect.w;
    gfx.hw.dst.rt.u32Height = dstrect.h;
    gfx.hw.dst.surf.u32Width = FB_W;
    gfx.hw.dst.surf.u32Height = FB_H;
    gfx.hw.dst.surf.u32Stride = FB_W * FB_BPP;
    gfx.hw.dst.surf.eColorFmt = E_MI_GFX_FMT_ARGB8888;
    gfx.hw.dst.surf.phyAddr = gfx.fb.phyAddr + (FB_W * gfx.vinfo.yoffset * FB_BPP);

    MI_GFX_BitBlit(&gfx.hw.src.surf, &gfx.hw.src.rt, &gfx.hw.dst.surf, &gfx.hw.dst.rt, &gfx.hw.opt, &u16Fence);
    MI_GFX_WaitAllDone(FALSE, u16Fence);

    if ((nds.menu.enable == 0) && (srcrect.w != 800) && ((srcrect.w == NDS_W) || (srcrect.w == NDS_Wx2)) && (nds.overlay.sel < nds.overlay.max)) {
        gfx.hw.overlay.surf.phyAddr = gfx.overlay.phyAddr;
        gfx.hw.overlay.surf.eColorFmt = E_MI_GFX_FMT_ARGB8888;
        gfx.hw.overlay.surf.u32Width = FB_W;
        gfx.hw.overlay.surf.u32Height = FB_H;
        gfx.hw.overlay.surf.u32Stride = FB_W * FB_BPP;
        gfx.hw.overlay.rt.s32Xpos = 0;
        gfx.hw.overlay.rt.s32Ypos = 0;
        gfx.hw.overlay.rt.u32Width = FB_W;
        gfx.hw.overlay.rt.u32Height = FB_H;

        gfx.hw.dst.rt.s32Xpos = 0;
        gfx.hw.dst.rt.s32Ypos = 0;
        gfx.hw.dst.rt.u32Width = FB_W;
        gfx.hw.dst.rt.u32Height = FB_H;
        gfx.hw.dst.surf.phyAddr = gfx.fb.phyAddr + (FB_W * gfx.vinfo.yoffset * FB_BPP);

        gfx.hw.opt.u32GlobalSrcConstColor = 0xff000000;
        gfx.hw.opt.eRotate = E_MI_GFX_ROTATE_180;
        gfx.hw.opt.eSrcDfbBldOp = E_MI_GFX_DFB_BLD_ONE;
        gfx.hw.opt.eDstDfbBldOp = E_MI_GFX_DFB_BLD_INVSRCALPHA;
        gfx.hw.opt.eDFBBlendFlag = E_MI_GFX_DFB_BLEND_SRC_PREMULTIPLY | E_MI_GFX_DFB_BLEND_COLORALPHA | E_MI_GFX_DFB_BLEND_ALPHACHANNEL;
        MI_GFX_BitBlit(&gfx.hw.overlay.surf, &gfx.hw.overlay.rt, &gfx.hw.dst.surf, &gfx.hw.dst.rt, &gfx.hw.opt, &u16Fence);
        MI_GFX_WaitAllDone(FALSE, u16Fence);
    }
#endif
    return 0;
}

void GFX_Flip(void)
{
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(drm_buf.gbm_surface);
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t pitch = gbm_bo_get_stride(bo);
    uint32_t fb;
#ifdef QX1000
    wl.flip ^= 1;
#endif

#ifdef PANDORA
    int arg = 0;

    ioctl(gfx.fb_dev[1], FBIOPAN_DISPLAY, &gfx.vinfo);
    ioctl(gfx.fb_dev[1], FBIO_WAITFORVSYNC, &arg);
    gfx.vinfo.yoffset ^= FB_H;
#endif

#if defined(A30) || defined(RG28XX)
    eglSwapBuffers(vid.eglDisplay, vid.eglSurface);

    // GBM Buffer Swap
    bo = gbm_surface_lock_front_buffer(drm_buf.gbm_surface);
    handle = gbm_bo_get_handle(bo).u32;
    pitch = gbm_bo_get_stride(bo);

    drmModeAddFB(dri_fd, drm_buf.width, drm_buf.height,
                 24, 32, pitch, handle, &fb);

    drmModeSetCrtc(dri_fd, crtc_id, fb,
			0, 0, &conn_id, 1, &conn->modes[0]);

    if (drm_buf.previous_bo) {
        drmModeRmFB(dri_fd, drm_buf.fb_id);
        gbm_surface_release_buffer(drm_buf.gbm_surface, drm_buf.previous_bo);
    }

    drm_buf.fb_id = fb;
    drm_buf.previous_bo = bo;

    if (nds.theme.img) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vid.texID[TEX_BG]);
        glVertexAttribPointer(vid.posLoc, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), bgVertices);
        glVertexAttribPointer(vid.texLoc, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(GLfloat), &bgVertices[3]);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    }
#endif

#ifdef MMIYOO
    ioctl(gfx.fb_dev, FBIOPAN_DISPLAY, &gfx.vinfo);
    gfx.vinfo.yoffset ^= FB_H;
#endif

#ifdef TRIMUI
    int r = 0;

    gfx.hw.buf.info.fb.addr[0] = (uintptr_t)((uint32_t *)gfx.hw.ion.padd + (FB_W * FB_H * gfx.fb.flip));
    gfx.hw.mem[OVL_V_TOP_LADD0 / 4] = gfx.hw.buf.info.fb.addr[0];
    ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &r);
    gfx.fb.flip^= 1;
#endif
}

int get_font_width(const char *info)
{
    int w = 0, h = 0;

    if (nds.font && info) {
        TTF_SizeUTF8(nds.font, info, &w, &h);
    }
    return w;
}

int get_font_height(const char *info)
{
    int w = 0, h = 0;

    if (nds.font && info) {
        TTF_SizeUTF8(nds.font, info, &w, &h);
    }
    return h;
}

const char *to_lang(const char *p)
{
    const char *info = p;
    char buf[MAX_PATH] = {0};
    int cc = 0, r = 0, len = 0;
    
    if (!strcmp(nds.lang.trans[DEF_LANG_SLOT], DEF_LANG_LANG) || (p == NULL)) {
        return p;
    }

    strcpy(buf, p);
    strcat(buf, "=");
    len = strlen(buf);
    if ((len == 0) || (len >= MAX_PATH)) {
        return 0;
    }

    for (cc=0; translate[cc]; cc++) {
        if (memcmp(buf, translate[cc], len) == 0) {
            r = 1;
            info = &translate[cc][len];
            //printf(PREFIX"Translate \'%s\' as \'%s\'\n", p, info);
            break;
        }
    }

    if (r == 0) {
        //printf(PREFIX"Failed to find the translation: \'%s\'(len=%d)\n", p, len);
        info = p;
    }
    return info;
}

int draw_info(SDL_Surface *dst, const char *info, int x, int y, uint32_t fgcolor, uint32_t bgcolor)
{
    int w = 0, h = 0;
    SDL_Color fg = {0};
    SDL_Rect rt = {0, 0, 0, 0};
    SDL_Surface *t0 = NULL;
    SDL_Surface *t1 = NULL;
    SDL_Surface *t2 = NULL;

    h = strlen(info);
    if ((nds.font == NULL) || (h == 0) || (h >= MAX_PATH)) {
        return -1;
    }

    fg.r = (fgcolor >> 16) & 0xff;
    fg.g = (fgcolor >> 8) & 0xff;
    fg.b = (fgcolor >> 0) & 0xff;
    TTF_SizeUTF8(nds.font, info, &w, &h);
    t0 = TTF_RenderUTF8_Solid(nds.font, info, fg);
    if (t0) {
        if (dst == NULL) {
            t1 = SDL_CreateRGBSurface(SDL_SWSURFACE, t0->w, t0->h, 32, 0, 0, 0, 0);
            if (t1) {
                SDL_FillRect(t1, &t1->clip_rect, bgcolor);
                SDL_BlitSurface(t0, NULL, t1, NULL);

                t2 = SDL_ConvertSurface(t1, cvt->format, 0);
                if (t2) {
                    rt.x = x;
                    rt.y = y;
                    rt.w = t2->w;
                    rt.h = t2->h;
                    GFX_Copy(-1, t2->pixels, t2->clip_rect, rt, t2->pitch, 0, E_MI_GFX_ROTATE_180);
                    SDL_FreeSurface(t2);
                }
                SDL_FreeSurface(t1);
            }
        }
        else {
            rt.x = x;
            rt.y = y;
            SDL_BlitSurface(t0, NULL, dst, &rt);
        }
        SDL_FreeSurface(t0);
    }
    return 0;
}

int reload_pen(void)
{
    static int pre_sel = -1;

    char buf[MAX_PATH] = {0};
    SDL_Surface *t = NULL;

    if (pre_sel != nds.pen.sel) {
        pre_sel = nds.pen.sel;

        if (nds.pen.img) {
            SDL_FreeSurface(nds.pen.img);
            nds.pen.img = NULL;
        }

        nds.pen.type = PEN_LB;
        if (get_file_path(nds.pen.path, nds.pen.sel, buf, 1) == 0) {
            t = IMG_Load(buf);
            if (t) {
#if defined(A30) || defined(RG28XX)
                int x = 0;
                int y = 0;
                uint32_t *p = malloc(t->pitch * t->h);
                uint32_t *src = t->pixels;
                uint32_t *dst = p;

                for (y = 0; y < t->h; y++) {
                    for (x = 0; x < t->w; x++) {
                        *dst++ = *src++;
                    }
                }
                glBindTexture(GL_TEXTURE_2D, vid.texID[TEX_PEN]);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->w, t->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, p);
                free(p);
#endif
                nds.pen.img = SDL_ConvertSurface(t, cvt->format, 0);
                SDL_FreeSurface(t);

                if (strstr(buf, "_lt")) {
                    nds.pen.type = PEN_LT;
                }
                else if (strstr(buf, "_rt")) {
                    nds.pen.type = PEN_RT;
                }
                else if (strstr(buf, "_rb")) {
                    nds.pen.type = PEN_RB;
                }
                else if (strstr(buf, "_lb")) {
                    nds.pen.type = PEN_LB;
                }
                else {
                    nds.pen.type = PEN_CP;
                }
            }
            else {
                printf(PREFIX"Failed to load pen (%s)\n", buf);
            }
        }
    }
    return 0;
}

int reload_menu(void)
{
    SDL_Surface *t = NULL;
    char folder[MAX_PATH] = {0};
    char buf[MAX_PATH << 1] = {0};

    if (get_dir_path(nds.menu.path, nds.menu.sel, folder) != 0) {
        return -1;
    }

    sprintf(buf, "%s/%s", folder, MENU_BG_FILE);
    t = IMG_Load(buf);
    if (t) {
        if (nds.menu.bg) {
            SDL_FreeSurface(nds.menu.bg);
        }
        nds.menu.bg = SDL_ConvertSurface(t, cvt->format, 0);
        SDL_FreeSurface(t);
    }

    sprintf(buf, "%s/%s", folder, MENU_CURSOR_FILE);
    nds.menu.cursor = IMG_Load(buf);

    sprintf(buf, "%s/%s", folder, DRASTIC_MENU_BG0_FILE);
    t = IMG_Load(buf);
    if (t) {
        if (nds.menu.drastic.bg0) {
            SDL_FreeSurface(nds.menu.drastic.bg0);
        }
        nds.menu.drastic.bg0 = SDL_ConvertSurface(t, cvt->format, 0);
        SDL_FreeSurface(t);
    }

    sprintf(buf, "%s/%s", folder, DRASTIC_MENU_BG1_FILE);
    t = IMG_Load(buf);
    if (t) {
        if (nds.menu.drastic.bg1) {
            SDL_FreeSurface(nds.menu.drastic.bg1);
        }
        nds.menu.drastic.bg1 = SDL_ConvertSurface(t, cvt->format, 0);
        SDL_FreeSurface(t);
    }

#if defined(MMIYOO) || defined(A30) || defined(RG28XX) || defined(UNITTEST)
    sprintf(buf, "%s/%s", folder, DRASTIC_MENU_CURSOR_FILE);
    nds.menu.drastic.cursor = IMG_Load(buf);
#endif

#ifdef TRIMUI
    nds.menu.drastic.cursor = NULL;
#endif

    sprintf(buf, "%s/%s", folder, DRASTIC_MENU_YES_FILE);
    t = IMG_Load(buf);
    if (t) {
#if defined(MMIYOO) || defined(QX1000) || defined(A30) || defined(RG28XX) || defined(UNITTEST)
        SDL_Rect nrt = {0, 0, LINE_H - 2, LINE_H - 2};
#endif
#if defined(TRIMUI) || defined(PANDORA)
        SDL_Rect nrt = {0, 0, t->w >> 1, t->h >> 1};
#endif
        if (nds.menu.drastic.yes) {
            SDL_FreeSurface(nds.menu.drastic.yes);
        }
        nds.menu.drastic.yes = SDL_CreateRGBSurface(SDL_SWSURFACE, nrt.w, nrt.h, 32, t->format->Rmask, t->format->Gmask, t->format->Bmask, t->format->Amask);
        if (nds.menu.drastic.yes) {
            SDL_SoftStretch(t, NULL, nds.menu.drastic.yes, NULL);
        }
        SDL_FreeSurface(t);
    }

    sprintf(buf, "%s/%s", folder, DRASTIC_MENU_NO_FILE);
    t = IMG_Load(buf);
    if (t) {
#if defined(MMIYOO) || defined(QX1000) || defined(A30) || defined(RG28XX) || defined(UNITTEST)
        SDL_Rect nrt = {0, 0, LINE_H - 2, LINE_H - 2};
#endif
#if defined(TRIMUI) || defined(PANDORA)
        SDL_Rect nrt = {0, 0, t->w >> 1, t->h >> 1};
#endif
        if (nds.menu.drastic.no) {
            SDL_FreeSurface(nds.menu.drastic.no);
        }
        nds.menu.drastic.no = SDL_CreateRGBSurface(SDL_SWSURFACE, nrt.w, nrt.h, 32, t->format->Rmask, t->format->Gmask, t->format->Bmask, t->format->Amask);
        if (nds.menu.drastic.no) {
            SDL_SoftStretch(t, NULL, nds.menu.drastic.no, NULL);
        }
        SDL_FreeSurface(t);
    }

    return 0;
}

int reload_bg(void)
{
#if !defined(PANDORA) && !defined(QX1000) && !defined(UNITTEST)
    static int pre_sel = -1;
#endif

#if !defined(PANDORA) && !defined(QX1000) && !defined(UNITTEST)
    static int pre_mode = -1;
#endif

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
    char buf[MAX_PATH] = {0};
    SDL_Surface *t = NULL;
    SDL_Rect srt = {0, 0, IMG_W, IMG_H};
    SDL_Rect drt = {0, 0, FB_W, FB_H};

    if (nds.enable_752x560) {
        srt.w = FB_W;
        srt.h = FB_H;
    }

    if (nds.overlay.sel >= nds.overlay.max) {
        if ((pre_sel != nds.theme.sel) || (pre_mode != nds.dis_mode)) {
            pre_mode = nds.dis_mode;
            pre_sel = nds.theme.sel;

            if (nds.theme.img) {
                SDL_FreeSurface(nds.theme.img);
                nds.theme.img = NULL;
            }

            nds.theme.img = SDL_CreateRGBSurface(SDL_SWSURFACE, srt.w, srt.h, 32, 0, 0, 0, 0);
            if (nds.theme.img) {
                SDL_FillRect(nds.theme.img, &nds.theme.img->clip_rect, SDL_MapRGB(nds.theme.img->format, 0x00, 0x00, 0x00));

                if (get_dir_path(nds.theme.path, nds.theme.sel, buf) == 0) {
                    switch (nds.dis_mode) {
                    case NDS_DIS_MODE_VH_T0:
                    case NDS_DIS_MODE_VH_T1:
                        return 0;
                    case NDS_DIS_MODE_S0:
                        strcat(buf, "/bg_s0.png");
                        break;
                    case NDS_DIS_MODE_S1:
                        return 0;
                    case NDS_DIS_MODE_V0:
                        strcat(buf, "/bg_v0.png");
                        break;
                    case NDS_DIS_MODE_V1:
                        strcat(buf, "/bg_v1.png");
                        break;
                    case NDS_DIS_MODE_H0:
                        strcat(buf, "/bg_h0.png");
                        break;
                    case NDS_DIS_MODE_H1:
                        strcat(buf, "/bg_h1.png");
                        break;
                    case NDS_DIS_MODE_VH_S0:
                        strcat(buf, "/bg_vh_s0.png");
                        break;
                    case NDS_DIS_MODE_VH_S1:
                        strcat(buf, "/bg_vh_s1.png");
                        break;
                    case NDS_DIS_MODE_VH_S2:
                        strcat(buf, "/bg_vh_s2.png");
                        break;
                    case NDS_DIS_MODE_VH_C0:
                        strcat(buf, "/bg_vh_c0.png");
                        break;
                    case NDS_DIS_MODE_VH_C1:
                        strcat(buf, "/bg_vh_c1.png");
                        break;
                    case NDS_DIS_MODE_HH0:
                    case NDS_DIS_MODE_HH1:
                        strcat(buf, "/bg_hh0.png");
                        break;
                    case NDS_DIS_MODE_HRES0:
                        strcat(buf, "/bg_hres0.png");
                        break;
                    case NDS_DIS_MODE_HRES1:
                        return 0;
                    }
                    
                    t = IMG_Load(buf);
                    if (t) {
                        SDL_BlitSurface(t, NULL, nds.theme.img, NULL);
                        SDL_FreeSurface(t);
#if !defined(A30) && !defined(RG28XX)
                        GFX_Copy(-1, nds.theme.img->pixels, nds.theme.img->clip_rect, drt, nds.theme.img->pitch, 0, E_MI_GFX_ROTATE_180);
#endif
                    }
                    else {
                        printf(PREFIX"Failed to load wallpaper (%s)\n", buf);
                    }
                }
            }
        }
        else {
            if (nds.theme.img) {
#if !defined(A30) && !defined(RG28XX)
                GFX_Copy(-1, nds.theme.img->pixels, nds.theme.img->clip_rect, drt, nds.theme.img->pitch, 0, E_MI_GFX_ROTATE_180);
#else
                glBindTexture(GL_TEXTURE_2D, vid.texID[TEX_BG]);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, nds.theme.img->w, nds.theme.img->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nds.theme.img->pixels);
#endif
            }
        }
    }
    else {
        t = SDL_CreateRGBSurface(SDL_SWSURFACE, IMG_W, IMG_H, 32, 0, 0, 0, 0);
        if (t) {
            SDL_FillRect(t, &t->clip_rect, SDL_MapRGB(t->format, 0x00, 0x00, 0x00));
            GFX_Copy(-1, t->pixels, t->clip_rect, drt, t->pitch, 0, E_MI_GFX_ROTATE_180);
            SDL_FreeSurface(t);
        }
    }
#endif

#ifdef TRIMUI
    char buf[MAX_PATH] = {0};
    SDL_Surface *t = NULL;

    if (nds.dis_mode == NDS_DIS_MODE_S1) {
        return 0;
    }

    if (pre_mode != nds.dis_mode) {
        pre_mode = nds.dis_mode;
        disp_resize();
    }

    if (pre_sel != nds.theme.sel) {
        pre_sel = nds.theme.sel;

        if (nds.theme.img) {
            SDL_FreeSurface(nds.theme.img);
            nds.theme.img = NULL;
        }

        nds.theme.img = SDL_CreateRGBSurface(SDL_SWSURFACE, IMG_W, IMG_H, 32, 0, 0, 0, 0);
        if (nds.theme.img) {
            SDL_FillRect(nds.theme.img, &nds.theme.img->clip_rect, SDL_MapRGB(nds.theme.img->format, 0x00, 0x00, 0x00));

            if (get_dir_path(nds.theme.path, nds.theme.sel, buf) == 0) {
                strcat(buf, "/bg_s0.png");
                t = IMG_Load(buf);
                if (t) {
                    SDL_BlitSurface(t, NULL, nds.theme.img, NULL);
                    SDL_FreeSurface(t);
                }
                else {
                    printf(PREFIX"Failed to load wallpaper (%s)\n", buf);
                }
            }
        }
    }
    
    if (nds.theme.img) {
        int x = 0;
        int y = 0;
        int z = 0;
        uint32_t *dst = NULL;
        uint32_t *src = NULL;

        ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &z);
        for (z=0; z<2; z++) {
            src = (uint32_t *)nds.theme.img->pixels;
            dst = (uint32_t *)gfx.hw.ion.vadd + (FB_W * FB_H * z);
            for (y = 0; y < FB_H; y++) {
                for (x = 0; x < FB_W; x++) {
                    dst[(((FB_W - 1) - x) * FB_H) + y] = *src;
                    src+= 2;
                }
                src+= IMG_W;
            }
        }
        ioctl(gfx.fb_dev, FBIO_WAITFORVSYNC, &z);
    }
#endif

#ifdef PANDORA
    SDL_Surface *t = NULL;
    char buf[MAX_PATH] = {0};

    if (pre_sel != nds.theme.sel) {
        pre_sel = nds.theme.sel;

        if (nds.theme.img) {
            SDL_FreeSurface(nds.theme.img);
            nds.theme.img = NULL;
        }

        nds.theme.img = SDL_CreateRGBSurface(SDL_SWSURFACE, IMG_W, IMG_H, 32, 0, 0, 0, 0);
        if (nds.theme.img) {
            SDL_FillRect(nds.theme.img, &nds.theme.img->clip_rect, SDL_MapRGB(nds.theme.img->format, 0x00, 0x00, 0x00));

            if (get_dir_path(nds.theme.path, nds.theme.sel, buf) == 0) {
                strcat(buf, "/bg.png");
                t = IMG_Load(buf);
                if (t) {
                    SDL_Rect r0 = {0};

                    SDL_BlitSurface(t, NULL, nds.theme.img, NULL);
                    SDL_FreeSurface(t);

                    r0.x = 16 - 1;
                    r0.y = 48 - 1;
                    r0.w = NDS_Wx2 + 2;
                    r0.h = NDS_Hx2 + 2;
                    SDL_FillRect(nds.theme.img, &r0, SDL_MapRGB(nds.theme.img->format, 0, 0, 0));

                    r0.x = (NDS_Wx2 + 16) - 0;
                    r0.y = ((FB_H - NDS_H) >> 1) - 1;
                    r0.w = NDS_W + 2;
                    r0.h = NDS_H + 2;
                    SDL_FillRect(nds.theme.img, &r0, SDL_MapRGB(nds.theme.img->format, 0, 0, 0));
                }
                else {
                    printf(PREFIX"Failed to load wallpaper (%s)\n", buf);
                }
            }
        }
    }

    if (nds.theme.img) {
        neon_memcpy(gfx.hw.mem[(gfx.vinfo.yoffset == 0) ? 0 : 1], nds.theme.img->pixels, FB_W * FB_H * 4);
    }
#endif
    return 0;
}

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
int reload_overlay(void)
{
    static int pre_sel = -1;

    char buf[MAX_PATH] = {0};
    SDL_Surface *t = NULL;

    if ((nds.overlay.sel < nds.overlay.max) && (pre_sel != nds.overlay.sel)) {
        pre_sel = nds.overlay.sel;

        if (nds.overlay.img) {
            SDL_FreeSurface(nds.overlay.img);
            nds.overlay.img = NULL;
        }

        nds.overlay.img = SDL_CreateRGBSurface(SDL_SWSURFACE, FB_W, FB_H, 32, 0, 0, 0, 0);
        if (nds.overlay.img) {
            SDL_FillRect(nds.overlay.img, &nds.overlay.img->clip_rect, SDL_MapRGB(nds.overlay.img->format, 0x00, 0x00, 0x00));

            if (get_file_path(nds.overlay.path, nds.overlay.sel, buf, 1) == 0) {
                t = IMG_Load(buf);
                if (t) {
                    SDL_BlitSurface(t, NULL, nds.overlay.img, NULL);
                    SDL_FreeSurface(t);

#ifdef MMIYOO
                    neon_memcpy(gfx.overlay.virAddr, nds.overlay.img->pixels, FB_W * FB_H * 4);
                    MI_SYS_FlushInvCache(gfx.overlay.virAddr, FB_W * FB_H * FB_BPP);
#endif
                }
                else {
                    printf(PREFIX"Failed to load overlay (%s)\n", buf);
                }
            }
        }
    }
    return 0;
}
#endif

static int MMIYOO_Available(void)
{
    const char *envr = SDL_getenv("SDL_VIDEODRIVER");
    if((envr) && (SDL_strcmp(envr, MMIYOO_DRIVER_NAME) == 0)) {
        return 1;
    }
    return 0;
}

static void MMIYOO_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

int MMIYOO_CreateWindow(_THIS, SDL_Window *window)
{
    vid.window = window;
    SDL_SetMouseFocus(window);
    SDL_SetKeyboardFocus(window);
    printf(PREFIX"Window:%p, Width:%d, Height:%d\n", window, window->w, window->h);
    return 0;
}

int MMIYOO_CreateWindowFrom(_THIS, SDL_Window *window, const void *data)
{
    return SDL_Unsupported();
}

static SDL_VideoDevice *MMIYOO_CreateDevice(int devindex)
{
    SDL_VideoDevice *device = NULL;

    if(!MMIYOO_Available()) {
        return (0);
    }

    device = (SDL_VideoDevice *) SDL_calloc(1, sizeof(SDL_VideoDevice));
    if(!device) {
        SDL_OutOfMemory();
        return (0);
    }
    device->is_dummy = SDL_TRUE;

    device->VideoInit = MMIYOO_VideoInit;
    device->VideoQuit = MMIYOO_VideoQuit;
    device->SetDisplayMode = MMIYOO_SetDisplayMode;
    device->CreateSDLWindow = MMIYOO_CreateWindow;
    device->CreateSDLWindowFrom = MMIYOO_CreateWindowFrom;
    device->free = MMIYOO_DeleteDevice;
    device->PumpEvents = MMIYOO_PumpEvents;
    return device;
}

VideoBootStrap MMIYOO_bootstrap = {MMIYOO_DRIVER_NAME, "MMIYOO VIDEO DRIVER", MMIYOO_CreateDevice};

int MMIYOO_VideoInit(_THIS)
{
#ifdef MMIYOO
    FILE *fd = NULL;
    char buf[MAX_PATH] = {0};
#endif

    SDL_DisplayMode mode = {0};
    SDL_VideoDisplay display = {0};

    //printf(PREFIX"MMIYOO_VideoInit\n");
#ifndef UNITTEST
    signal(SIGTERM, sigterm_handler);
#endif

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 800;
    mode.h = 480;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 800;
    mode.h = 480;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 800;
    mode.h = 600;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 800;
    mode.h = 600;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 480;
    mode.h = 272;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 480;
    mode.h = 272;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_RGB565;
    mode.w = 320;
    mode.h = 240;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_ARGB8888;
    mode.w = 320;
    mode.h = 240;
    mode.refresh_rate = 60;
    SDL_AddDisplayMode(&display, &mode);

    SDL_AddVideoDisplay(&display, SDL_FALSE);

    LINE_H = 30;
    FONT_SIZE = DEF_FONT_SIZE;

    FB_W = DEF_FB_W;
    FB_H = DEF_FB_H;
    FB_SIZE = FB_W * FB_H * FB_BPP * 2;
    TMP_SIZE = FB_W * FB_H * FB_BPP;

#ifdef MMIYOO
    fd = popen("fbset | grep \"mode \"", "r");
    if (fd) {
        fgets(buf, sizeof(buf), fd);
        pclose(fd);

        if (strstr(buf, "752")) {
            FONT_SIZE = 27;
            LINE_H = FONT_SIZE + 8;

            FB_W = 752;
            FB_H = 560;
            FB_SIZE = (FB_W * FB_H * FB_BPP * 2);
            TMP_SIZE = (FB_W * FB_H * FB_BPP);
            nds.enable_752x560 = 1;
        }
    }
#endif

    GFX_Init();
    read_config();
    MMIYOO_EventInit();

    detour_init(sysconf(_SC_PAGESIZE), nds.states.path);
    printf(PREFIX"Installed hooking for drastic functions\n");
    detour_hook(FUN_PRINT_STRING, (intptr_t)sdl_print_string);
    detour_hook(FUN_SAVESTATE_PRE, (intptr_t)sdl_savestate_pre);
    detour_hook(FUN_SAVESTATE_POST, (intptr_t)sdl_savestate_post);
    detour_hook(FUN_BLIT_SCREEN_MENU, (intptr_t)sdl_blit_screen_menu);
    detour_hook(FUN_UPDATE_SCREEN, (intptr_t)sdl_update_screen);
#ifndef UNITTEST
    detour_hook(FUN_RENDER_POLYGON_SETUP_PERSPECTIVE_STEPS, (intptr_t)render_polygon_setup_perspective_steps);
#endif

#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
    printf(PREFIX"Installed hooking for libc functions\n");
    detour_hook(FUN_MALLOC, (intptr_t)sdl_malloc);
    detour_hook(FUN_REALLOC, (intptr_t)sdl_realloc);
    detour_hook(FUN_FREE, (intptr_t)sdl_free);
#endif
    return 0;
}

static int MMIYOO_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    return 0;
}

void MMIYOO_VideoQuit(_THIS)
{
    //printf(PREFIX"MMIYOO_VideoQuit\n");
    printf(PREFIX"Wait for savestate complete\n");
    while (savestate_busy) {
        usleep(1000000);
    }
    system("sync");
    detour_quit();
    write_config();

    if (fps_info) {
        SDL_FreeSurface(fps_info);
        fps_info = NULL;
    }

    if (nds.pen.img) {
        SDL_FreeSurface(nds.pen.img);
        nds.pen.img = NULL;
    }

    if (nds.theme.img) {
        SDL_FreeSurface(nds.theme.img);
        nds.theme.img = NULL;
    }

    if (nds.overlay.img) {
        SDL_FreeSurface(nds.overlay.img);
        nds.overlay.img = NULL;
    }

    if (nds.menu.bg) {
        SDL_FreeSurface(nds.menu.bg);
        nds.menu.bg = NULL;
    }

    if (nds.menu.cursor) {
        SDL_FreeSurface(nds.menu.cursor);
        nds.menu.cursor = NULL;
    }

    if (nds.menu.drastic.bg0) {
        SDL_FreeSurface(nds.menu.drastic.bg0);
        nds.menu.drastic.bg0 = NULL;
    }

    if (nds.menu.drastic.bg1) {
        SDL_FreeSurface(nds.menu.drastic.bg1);
        nds.menu.drastic.bg1 = NULL;
    }

    if (nds.menu.drastic.cursor) {
        SDL_FreeSurface(nds.menu.drastic.cursor);
        nds.menu.drastic.cursor = NULL;
    }

    if (nds.menu.drastic.main) {
        SDL_FreeSurface(nds.menu.drastic.main);
        nds.menu.drastic.main = NULL;
    }

    if (nds.menu.drastic.yes) {
        SDL_FreeSurface(nds.menu.drastic.yes);
        nds.menu.drastic.yes = NULL;
    }

    if (nds.menu.drastic.no) {
        SDL_FreeSurface(nds.menu.drastic.no);
        nds.menu.drastic.no = NULL;
    }

    if (nds.font) {
        TTF_CloseFont(nds.font);
        nds.font = NULL;
    }

    printf(PREFIX"Free TTF resources\n");
    TTF_Quit();

    printf(PREFIX"Free GFX resources\n");
    GFX_Quit();

    printf(PREFIX"Free Event resources\n");
    MMIYOO_EventDeinit();

    printf(PREFIX"Free Lang resources\n");
    lang_unload();
}

#if defined(MMIYOO) || defined(QX1000) || defined(A30) || defined(RG28XX)
static const char *DIS_MODE0_640[] = {
    "640*480",
    "640*480",
    "512*384",
    "640*480",
    "256*192",
    "320*240",
    "256*192",
    "320*240",
    "480*360",
    "384*288",
    "384*288",
    "384*288",
    "384*288",
    "427*320",
    "427*320",
    "480*320",
    "480*320",
};

static const char *DIS_MODE1_640[] = {
    "170*128",
    "256*192",
    "",
    "",
    "256*192",
    "320*240",
    "256*192",
    "320*240",
    "160*120",
    "256*192",
    "256*192",
    "256*192",
    "256*192",
    "427*320",
    "427*320",
    "480*320",
    "480*320",
};

static const char *DIS_MODE0_752[] = {
    "752*560",
    "752*560",
    "512*384",
    "752*560",
    "256*192",
    "373*280",
    "256*192",
    "373*280",
    "592*440",
    "496*368",
    "496*368",
    "496*368",
    "496*368",
    "501*376",
    "501*376",
    "560*376",
    "560*376",
};

static const char *DIS_MODE1_752[] = {
    "170*128",
    "256*192",
    "",
    "",
    "256*192",
    "373*280",
    "256*192",
    "373*280",
    "160*120",
    "256*192",
    "256*192",
    "256*192",
    "256*192",
    "501*376",
    "501*376",
    "560*376",
    "560*376",
};

static const char *POS[] = {
    "Top-Right", "Top-Left", "Bottom-Left", "Bottom-Right"
};

static const char *BORDER[] = {
    "None", "White", "Red", "Green", "Blue", "Black", "Yellow", "Cyan"
};

static const char *DPAD[] = {
    "0°", "90°", "270°"
};

static const char *HOTKEY[] = {
    "MENU", "SELECT"
};

#ifdef A30
static const char *JOY_MODE[] = {
    "Disable", "D-Pad", "Stylus", "Customized Key"
};

static const char *JOY_CUSKEY[] = {
    "UP", "DOWN", "LEFT", "RIGHT", "A", "B", "X", "Y", "L1", "R1", "L2", "R2", "SELECT", "START"
};
#endif

static int lang_next(void)
{
    int cc = 0;

    for (cc=1; cc<(MAX_LANG_FILE-1); cc++) {
        if (!strcmp(nds.lang.trans[DEF_LANG_SLOT], nds.lang.trans[cc])) {
            if (strcmp(nds.lang.trans[cc + 1], "")) {
                strcpy(nds.lang.trans[DEF_LANG_SLOT], nds.lang.trans[cc + 1]);
                return 0;
            }
        }
    }
    return -1;
}

static int lang_prev(void)
{
    int cc = 0;

    for (cc=(MAX_LANG_FILE-1); cc>1; cc--) {
        if (!strcmp(nds.lang.trans[DEF_LANG_SLOT], nds.lang.trans[cc])) {
            if (strcmp(nds.lang.trans[cc - 1], "")) {
                strcpy(nds.lang.trans[DEF_LANG_SLOT], nds.lang.trans[cc - 1]);
                return 0;
            }
        }
    }
    return -1;
}

enum {
    MENU_LANG = 0,
#if defined(A30) || defined(RG28XX)
    MENU_CPU_CORE,
#ifdef A30
    MENU_CPU_CLOCK,
#endif
#else
    MENU_CPU,
#endif
    MENU_OVERLAY,
    MENU_DIS,
    MENU_DIS_ALPHA,
    MENU_DIS_BORDER,
    MENU_DIS_POSITION,
    MENU_ALT,
    MENU_KEYS,
    MENU_HOTKEY,
    MENU_SWAP_L1L2,
    MENU_SWAP_R1R2,
    MENU_PEN_XV,
    MENU_PEN_YV,
    MENU_CURSOR,
    MENU_FAST_FORWARD,
#ifdef A30
    MENU_JOY_MODE,
    MENU_JOY_CUSKEY0,
    MENU_JOY_CUSKEY1,
    MENU_JOY_CUSKEY2,
    MENU_JOY_CUSKEY3,
    MENU_JOY_DZONE,
#endif

#if defined(MMIYOO) || defined(A30)
    MENU_CHK_BAT,
#endif
    MENU_LAST,
};

static const char *MENU_ITEM[] = {
    "Language",
#if defined(A30) || defined(RG28XX)
    "CPU Core",
#ifdef A30
    "CPU Clock",
#endif
#else
    "CPU",
#endif
    "Overlay",
    "Display",
    "Alpha",
    "Border",
    "Position",
    "Alt. Display",
    "Keys",
    "Hotkey",
    "Swap L1-L2",
    "Swap R1-R2",
    "Pen X Speed",
    "Pen Y Speed",
    "Cursor",
    "Fast Forward",
#ifdef A30
    "Joy Mode",
    "  Joy Up",
    "  Joy Down",
    "  Joy Left",
    "  Joy Right",
    "Joy Dead Zone",
#endif
    "Check Battery",
};

int handle_menu(int key)
{
    static int pre_ff = 0;
    static int cur_sel = 0;
#if defined(A30) || defined(RG28XX)
    static uint32_t cur_cpucore = INIT_CPU_CORE;
    static uint32_t pre_cpucore = INIT_CPU_CORE;
#ifdef A30
    static uint32_t cur_cpuclock = INIT_CPU_CLOCK;
    static uint32_t pre_cpuclock = INIT_CPU_CLOCK;
#endif
#else
    static uint32_t cur_cpuclock = 0;
    static uint32_t pre_cpuclock = 0;
#endif
    static char pre_lang[LANG_FILE_LEN] = {0};

    const int SX = nds.enable_752x560 ? 200 : 150;
    const int SY = nds.enable_752x560 ? 120 : 107;
    const int SSX = nds.enable_752x560 ? 410 : 385;

    int cc = 0;
    int sx = 0;
    int sy = 0;
    int s0 = 0;
    int s1 = 0;
    int idx = 0;
    int h = LINE_H;
    int dis_mode = -1;
    SDL_Rect rt = {0};
    char buf[MAX_PATH] = {0};
    uint32_t sel_col = nds.menu.c0;
    uint32_t unsel_col = nds.menu.c1;
    uint32_t dis_col = 0x808080;
    uint32_t val_col = nds.menu.c0;
    uint32_t col0 = 0, col1 = 0;

    if (pre_lang[0] == 0) {
        strcpy(pre_lang, nds.lang.trans[DEF_LANG_SLOT]);
    }

#ifndef RG28XX
    if (pre_cpuclock == 0) {
#ifdef MMIYOO
        cur_cpuclock = pre_cpuclock = get_cpuclock();
#endif
    }
#endif

    switch (key) {
    case MYKEY_UP:
        if (cur_sel > 0) {
            cur_sel-= 1;
        }
        break;
    case MYKEY_DOWN:
        if (cur_sel < (MENU_LAST - 1)) {
            cur_sel+= 1;
        }
        break;
    case MYKEY_LEFT:
        switch(cur_sel) {
        case MENU_LANG:
            lang_prev();
            break;
#if defined(A30) || defined(RG28XX)
        case MENU_CPU_CORE:
            if (cur_cpucore > nds.mincore) {
                cur_cpucore-= 1;
            }
            break;
#ifdef A30
        case MENU_CPU_CLOCK:
            if (cur_cpuclock > nds.mincpu) {
                cur_cpuclock-= 50;
            }
            break;
#endif
#else
        case MENU_CPU:
            if (cur_cpuclock > nds.mincpu) {
                cur_cpuclock-= 50;
            }
            break;
#endif
        case MENU_HOTKEY:
            nds.hotkey = HOTKEY_BIND_MENU;
            break;
        case MENU_SWAP_L1L2:
            nds.swap_l1l2 = 0;
            break;
        case MENU_SWAP_R1R2:
            nds.swap_r1r2 = 0;
            break;
        case MENU_OVERLAY:
            if (nds.overlay.sel < nds.overlay.max) {
                nds.overlay.sel+= 1;
            }
            break;
        case MENU_DIS:
            if (nds.hres_mode == 0) {
                if (nds.dis_mode > 0) {
                    nds.dis_mode-= 1;
                }
            }
            else {
                nds.dis_mode = NDS_DIS_MODE_HRES0;
            }
            break;
        case MENU_DIS_ALPHA:
            if (((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                if (nds.alpha.val > 0) {
                    nds.alpha.val-= 1;
                }
            }
            break;
        case MENU_DIS_BORDER:
            if ((nds.alpha.val > 0) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                if (nds.alpha.border > 0) {
                    nds.alpha.border-= 1;
                }
            }
            break;
        case MENU_DIS_POSITION:
            if (((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                if (nds.alpha.pos > 0) {
                    nds.alpha.pos-= 1;
                }
            }
            break;
        case MENU_ALT:
            if ((nds.hres_mode == 0) && (nds.alt_mode > 0)) {
                nds.alt_mode-= 1;
            }
            break;
        case MENU_KEYS:
            if (nds.keys_rotate > 0) {
                nds.keys_rotate-= 1;
            }
            break;
        case MENU_PEN_XV:
            if (nds.pen.xv > PEN_XV_MIN) {
                nds.pen.xv-= PEN_XV_DEC;
            }
            break;
        case MENU_PEN_YV:
            if (nds.pen.yv > PEN_YV_MIN) {
                nds.pen.yv-= PEN_YV_DEC;
            }
            break;
        case MENU_CURSOR:
            nds.menu.show_cursor = 0;
            break;
        case MENU_FAST_FORWARD:
            if (nds.fast_forward > 1) {
                nds.fast_forward -= 1;
            }
            break;
#ifdef A30
        case MENU_JOY_MODE:
            if (nds.joy.mode > 0) {
                nds.joy.mode -= 1;
            }
            break;
        case MENU_JOY_CUSKEY0:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[0] > 0) {
                    nds.joy.cuskey[0] -= 1;
                }
            }
            break;
        case MENU_JOY_CUSKEY1:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[1] > 0) {
                    nds.joy.cuskey[1] -= 1;
                }
            }
            break;
        case MENU_JOY_CUSKEY2:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[2] > 0) {
                    nds.joy.cuskey[2] -= 1;
                }
            }
            break;
        case MENU_JOY_CUSKEY3:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[3] > 0) {
                    nds.joy.cuskey[3] -= 1;
                }
            }
            break;
        case MENU_JOY_DZONE:
            if (nds.joy.dzone > 0) {
                nds.joy.dzone -= 1;
            }
            break;
#endif
#if defined(MMIYOO) || defined(A30)
        case MENU_CHK_BAT:
            nds.chk_bat = 0;
            break;
#endif
        default:
            break;
        }
        break;
    case MYKEY_RIGHT:
        switch(cur_sel) {
        case MENU_LANG:
            lang_next();
            break;
#if defined(A30) || defined(RG28XX)
        case MENU_CPU_CORE:
            if (cur_cpucore < nds.maxcore) {
                cur_cpucore+= 1;
            }
            break;
#ifdef A30
        case MENU_CPU_CLOCK:
            if (cur_cpuclock < nds.maxcpu) {
                cur_cpuclock+= 50;
            }
            break;
#endif
#else
        case MENU_CPU:
            if (cur_cpuclock < nds.maxcpu) {
                cur_cpuclock+= 50;
            }
            break;
#endif
        case MENU_HOTKEY:
            nds.hotkey = HOTKEY_BIND_SELECT;
            break;
        case MENU_SWAP_L1L2:
            nds.swap_l1l2 = 1;
            break;
        case MENU_SWAP_R1R2:
            nds.swap_r1r2 = 1;
            break;
        case MENU_OVERLAY:
            if (nds.overlay.sel > 0) {
                nds.overlay.sel-= 1;
            }
            break;
        case MENU_DIS:
            if (nds.hres_mode == 0) {
                if (nds.dis_mode < NDS_DIS_MODE_LAST) {
                    nds.dis_mode+= 1;
                }
            }
            else {
                nds.dis_mode = NDS_DIS_MODE_HRES1;
            }
            break;
        case MENU_DIS_ALPHA:
            if (((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                if (nds.alpha.val < NDS_ALPHA_MAX) {
                    nds.alpha.val+= 1;
                }
            }
            break;
        case MENU_DIS_BORDER:
            if ((nds.alpha.val > 0) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                if (nds.alpha.border < NDS_BORDER_MAX) {
                    nds.alpha.border+= 1;
                }
            }
            break;
        case MENU_DIS_POSITION:
            if (((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                if (nds.alpha.pos < 3) {
                    nds.alpha.pos+= 1;
                }
            }
            break;
        case MENU_ALT:
            if ((nds.hres_mode == 0) && (nds.alt_mode < NDS_DIS_MODE_LAST)) {
                nds.alt_mode+= 1;
            }
            break;
        case MENU_KEYS:
            if (nds.keys_rotate < 2) {
                nds.keys_rotate+= 1;
            }
            break;
        case MENU_PEN_XV:
            if (nds.pen.xv < PEN_XV_MAX) {
                nds.pen.xv+= PEN_XV_INC;
            }
            break;
        case MENU_PEN_YV:
            if (nds.pen.yv < PEN_YV_MAX) {
                nds.pen.yv+= PEN_YV_INC;
            }
            break;
        case MENU_CURSOR:
            nds.menu.show_cursor = 1;
            break;
        case MENU_FAST_FORWARD:
            if (nds.fast_forward < 255) {
                nds.fast_forward += 1;
            }
            break;
#ifdef A30
        case MENU_JOY_MODE:
            if (nds.joy.mode < MYJOY_MODE_LAST) {
                nds.joy.mode += 1;
            }
            if (nds.joy.mode == MYJOY_MODE_STYLUS) {
                if (evt.mode == MMIYOO_MOUSE_MODE) {
                    evt.mode = MMIYOO_KEYPAD_MODE;
                }
            }
            break;
        case MENU_JOY_CUSKEY0:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[0] < 13) {
                    nds.joy.cuskey[0] += 1;
                }
            }
            break;
        case MENU_JOY_CUSKEY1:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[1] < 13) {
                    nds.joy.cuskey[1] += 1;
                }
            }
            break;
        case MENU_JOY_CUSKEY2:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[2] < 13) {
                    nds.joy.cuskey[2] += 1;
                }
            }
            break;
        case MENU_JOY_CUSKEY3:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                if (nds.joy.cuskey[3] < 13) {
                    nds.joy.cuskey[3] += 1;
                }
            }
            break;
        case MENU_JOY_DZONE:
            if (nds.joy.dzone < 255) {
                nds.joy.dzone += 1;
            }
            break;
#endif
#if defined(MMIYOO) || defined(A30)
        case MENU_CHK_BAT:
            nds.chk_bat = 1;
            break;
#endif
        default:
            break;
        }
        break;
    case MYKEY_B:
#ifndef RG28XX
        if (cur_cpuclock != pre_cpuclock) {
#ifdef MMIYOO
            set_cpuclock(cur_cpuclock);
#endif

#ifdef A30
            set_best_match_cpu_clock(cur_cpuclock);
#endif
            pre_cpuclock = cur_cpuclock;
        }
#endif

#if defined(A30) || defined(RG28XX)
        if (cur_cpucore != pre_cpucore) {
            set_core(cur_cpucore);
            pre_cpucore = cur_cpucore;
        }
#endif

        if (strcmp(pre_lang, nds.lang.trans[DEF_LANG_SLOT])) {
            lang_unload();
            lang_load(nds.lang.trans[DEF_LANG_SLOT]);
            memset(pre_lang, 0, sizeof(pre_lang));
        }

        if (pre_ff != nds.fast_forward) {
            dtr_fastforward(nds.fast_forward);
            pre_ff = nds.fast_forward;
        }
        nds.menu.enable = 0;
#ifdef QX1000
        update_wayland_res(NDS_W * 2, NDS_H);
#endif
        return 0;
    default:
        break;
    }

    if (cur_sel == MENU_DIS) {
        dis_mode = nds.dis_mode;
    }
    if (cur_sel == MENU_ALT) {
        dis_mode = nds.alt_mode;
    }
    SDL_SoftStretch(nds.menu.bg, NULL, cvt, NULL);

    if (cur_sel < 3) {
        s0 = 0;
        s1 = 7;
    }
    else if (cur_sel >= (MENU_LAST - 4)) {
        s0 = MENU_LAST - 8;
        s1 = MENU_LAST - 1;
    }
    else {
        s0 = cur_sel - 3;
        s1 = cur_sel + 4;
    }

    for (cc=0, idx=0; cc<MENU_LAST; cc++) {
        if ((cc < s0) || (cc > s1)) {
            continue;
        }

        sx = 0;
        col0 = (cur_sel == cc) ? sel_col : unsel_col;
        col1 = (cur_sel == cc) ? val_col : unsel_col;
        switch (cc) {
        case MENU_DIS_ALPHA:
            sx = 20;
            if ((cur_sel == MENU_DIS_ALPHA) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                col1 = val_col;
            }
            else {
                if ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1)) {
                    col1 = unsel_col;
                }
                else {
                    col1 = dis_col;
                }
            }
            break;
        case MENU_DIS_BORDER:
            sx = 20;
            if ((cur_sel == MENU_DIS_BORDER) && (nds.alpha.val > 0) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                col1 = val_col;
            }
            else {
                if ((nds.alpha.val > 0) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                    col1 = unsel_col;
                }
                else {
                    col1 = dis_col;
                }
            }
            break;
        case MENU_DIS_POSITION:
            sx = 20;
            if ((cur_sel == MENU_DIS_POSITION) && ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1))) {
                col1 = val_col;
            }
            else {
                if ((nds.dis_mode == NDS_DIS_MODE_VH_T0) || (nds.dis_mode == NDS_DIS_MODE_VH_T1)) {
                    col1 = unsel_col;
                }
                else {
                    col1 = dis_col;
                }
            }
            break;
        case MENU_ALT:
            if (nds.hres_mode == 0) {
                col1 = (cur_sel == cc) ? val_col : unsel_col;
            }
            else {
                col1 = unsel_col;
            }
            break;
#ifdef A30
        case MENU_JOY_CUSKEY0:
        case MENU_JOY_CUSKEY1:
        case MENU_JOY_CUSKEY2:
        case MENU_JOY_CUSKEY3:
            if (nds.joy.mode == MYJOY_MODE_CUSKEY) {
                col1 = (cur_sel == cc) ? val_col : unsel_col;
            }
            else {
                col1 = dis_col;
            }
            break;
#endif
        default:
            break;
        }

        if (col0 == sel_col) {
            if (nds.menu.show_cursor) {
                rt.x = SX - (nds.enable_752x560 ? 43 : 10);
                rt.w = 461 + (nds.enable_752x560 ? 41 : -40);
            }
            else {
                rt.x = SX - (nds.enable_752x560 ? 83 : 50);
                rt.w = 461 + (nds.enable_752x560 ? 81 : 0);
            }
            rt.y = SY + (h * idx) - 2;
            rt.h = FONT_SIZE + 3;

            if ((cc == MENU_DIS_ALPHA) || (cc == MENU_KEYS)) {
                rt.w-= (nds.enable_752x560 ? 179 : 121);
            }
            if (col1 == dis_col) {
                col1 = val_col;
                SDL_FillRect(cvt, &rt, SDL_MapRGB(nds.menu.drastic.main->format, 0x80, 0x80, 0x80));
            }
            else {
                SDL_FillRect(cvt, &rt, SDL_MapRGB(nds.menu.drastic.main->format, (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
            }

            if (nds.menu.show_cursor && nds.menu.cursor) {
                rt.x -= (nds.menu.cursor->w + 5);
                rt.y -= ((nds.menu.cursor->h - LINE_H) / 2);
                rt.w = 0;
                rt.h = 0;
                SDL_BlitSurface(nds.menu.cursor, NULL, cvt, &rt);
            }

            if ((cc == MENU_DIS) || (cc == MENU_ALT)) {
                rt.x = nds.enable_752x560 ? 480 : 440;
                rt.y = SY + (h * (idx + 1)) - 7;
                rt.w = nds.enable_752x560 ? 179 : 121;
                rt.h = FONT_SIZE + 8;
                SDL_FillRect(cvt, &rt, SDL_MapRGB(nds.menu.drastic.main->format, (nds.menu.c2 >> 16) & 0xff, (nds.menu.c2 >> 8) & 0xff, nds.menu.c2 & 0xff));
            }
        }
        draw_info(cvt, to_lang(MENU_ITEM[cc]), SX + sx, SY + (h * idx), col0, 0);

        sx = 0;
        switch (cc) {
        case MENU_LANG:
            sprintf(buf, "%s", nds.lang.trans[DEF_LANG_SLOT]);
            break;
#if defined(A30) || defined(RG28XX)
        case MENU_CPU_CORE:
            sprintf(buf, "%d", cur_cpucore);
            break;
#ifdef A30
        case MENU_CPU_CLOCK:
            sprintf(buf, "%dMHz", cur_cpuclock);
            break;
#endif
#else
        case MENU_CPU:
            sprintf(buf, "%dMHz", cur_cpuclock);
            break;
#endif
        case MENU_HOTKEY:
            sprintf(buf, "%s", to_lang(HOTKEY[nds.hotkey]));
            break;
        case MENU_SWAP_L1L2:
            sprintf(buf, "%s", to_lang(nds.swap_l1l2 ? "Yes" : "No"));
            break;
        case MENU_SWAP_R1R2:
            sprintf(buf, "%s", to_lang(nds.swap_r1r2 ? "Yes" : "No"));
            break;
        case MENU_OVERLAY:
            if (nds.overlay.sel < nds.overlay.max) {
                get_file_path(nds.overlay.path, nds.overlay.sel, buf, 0);
#if defined(MMIYOO) || defined(A30) || defined(RG28XX)
                reload_overlay();
#endif
            }
            else {
                sprintf(buf, to_lang("None"));
            }
            break;
        case MENU_DIS:
            if (nds.hres_mode == 0) {
                sprintf(buf, "[%d]   %s", nds.dis_mode, nds.enable_752x560 ? DIS_MODE0_752[nds.dis_mode] : DIS_MODE0_640[nds.dis_mode]);
            }
            else {
                if (nds.enable_752x560) {
                    sprintf(buf, "[%d]   %s", nds.dis_mode, nds.dis_mode == NDS_DIS_MODE_HRES0 ? "512*384" : "752*560");
                }
                else {
                    sprintf(buf, "[%d]   %s", nds.dis_mode, nds.dis_mode == NDS_DIS_MODE_HRES0 ? "" : "");
                }
            }
            break;
        case MENU_DIS_ALPHA:
            if (nds.hres_mode == 0) {
                sprintf(buf, "[%d]   ", nds.dis_mode);
                sx = get_font_width(buf);
                sprintf(buf, "%s", nds.enable_752x560 ? DIS_MODE1_752[nds.dis_mode] : DIS_MODE1_640[nds.dis_mode]);
                draw_info(cvt, buf, SSX + sx, SY + (h * idx), (cur_sel == MENU_DIS) ? nds.menu.c0 : nds.menu.c1, 0);
            }

            sx = 0;
            sprintf(buf, "%d", nds.alpha.val);
            break;
        case MENU_DIS_BORDER:
            sprintf(buf, "%s", to_lang(BORDER[nds.alpha.border]));
            break;
        case MENU_DIS_POSITION:
            sprintf(buf, "%s", to_lang(POS[nds.alpha.pos]));
            break;
        case MENU_ALT:
            sprintf(buf, "[%d]   %s", nds.alt_mode, nds.enable_752x560 ? DIS_MODE0_752[nds.alt_mode] : DIS_MODE0_640[nds.alt_mode]);
            break;
        case MENU_KEYS:
            sprintf(buf, "[%d]   ", nds.alt_mode);
            sx = get_font_width(buf);
            sprintf(buf, "%s", nds.enable_752x560 ? DIS_MODE1_752[nds.alt_mode] : DIS_MODE1_640[nds.alt_mode]);
            draw_info(cvt, buf, SSX + sx, SY + (h * idx), (nds.hres_mode == 0) && (cur_sel == MENU_ALT) ? val_col : unsel_col, 0);

            sx = 0;
            sprintf(buf, "%s", DPAD[nds.keys_rotate % 3]);
            break;
        case MENU_PEN_XV:
#ifdef A30
            sprintf(buf, "%d (80000)", nds.pen.xv);
#else
            sprintf(buf, "%d (30000)", nds.pen.xv);
#endif
            break;
        case MENU_PEN_YV:
#ifdef A30
            sprintf(buf, "%d (85000)", nds.pen.yv);
#else
            sprintf(buf, "%d (35000)", nds.pen.yv);
#endif
            break;
        case MENU_CURSOR:
            sprintf(buf, "%s", to_lang(nds.menu.show_cursor ? "Show" : "Hide"));
            break;
        case MENU_FAST_FORWARD:
            sprintf(buf, "%d (6)", nds.fast_forward);
            break;
#ifdef A30
        case MENU_JOY_MODE:
            sprintf(buf, "%s", to_lang(JOY_MODE[nds.joy.mode]));
            break;
        case MENU_JOY_CUSKEY0:
            sprintf(buf, "Miyoo %s", to_lang(JOY_CUSKEY[nds.joy.cuskey[0]]));
            break;
        case MENU_JOY_CUSKEY1:
            sprintf(buf, "Miyoo %s", to_lang(JOY_CUSKEY[nds.joy.cuskey[1]]));
            break;
        case MENU_JOY_CUSKEY2:
            sprintf(buf, "Miyoo %s", to_lang(JOY_CUSKEY[nds.joy.cuskey[2]]));
            break;
        case MENU_JOY_CUSKEY3:
            sprintf(buf, "Miyoo %s", to_lang(JOY_CUSKEY[nds.joy.cuskey[3]]));
            break;
        case MENU_JOY_DZONE:
            sprintf(buf, "%d (65)", nds.joy.dzone);
            break;
#endif
#if defined(MMIYOO) || defined(A30)
        case MENU_CHK_BAT:
            sprintf(buf, "%s", to_lang(nds.chk_bat ? "Yes" : "No"));
            break;
#endif
        }
        draw_info(cvt, buf, SSX + sx, SY + (h * idx), col1, 0);
        idx+= 1;
    }

    sx = nds.enable_752x560 ? 540 : 450;
    sy = nds.enable_752x560 ? 430 : 360;
    if ((cur_sel == MENU_OVERLAY) && (nds.overlay.sel < nds.overlay.max) && (nds.overlay.img)) {
        rt.x = sx;
        rt.y = sy;
        rt.w = 128;
        rt.h = 96;
        SDL_SoftStretch(nds.overlay.img, NULL, cvt, &rt);
    }
    else if(dis_mode >= 0) {
/*
        switch (dis_mode) {
        case NDS_DIS_MODE_VH_T0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
           
            rt.w = 34;
            rt.h = 26;
            switch (nds.alpha.pos) {
            case 0:
                rt.x = (sx + 128) - rt.w;
                rt.y = sy;
                break;
            case 1:
                rt.x = sx;
                rt.y = sy;
                break;
            case 2:
                rt.x = sx;
                rt.y = (sy + 96) - rt.h;
                break;
            case 3:
                rt.x = (sx + 128) - rt.w;
                rt.y = (sy + 96) - rt.h;
                break;
            }
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, (30 * nds.alpha.val)));
            break;
        case NDS_DIS_MODE_VH_T1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
           
            rt.w = 51;
            rt.h = 38;
            switch (nds.alpha.pos) {
            case 0:
                rt.x = (sx + 128) - rt.w;
                rt.y = sy;
                break;
            case 1:
                rt.x = sx;
                rt.y = sy;
                break;
            case 2:
                rt.x = sx;
                rt.y = (sy + 96) - rt.h;
                break;
            case 3:
                rt.x = (sx + 128) - rt.w;
                rt.y = (sy + 96) - rt.h;
                break;
            }
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, (30 * nds.alpha.val)));
            break;
        case NDS_DIS_MODE_S0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 102;
            rt.h = 76;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            break;
        case NDS_DIS_MODE_S1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            break;
        case NDS_DIS_MODE_V0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + ((96 - (rt.h * 2)) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + ((96 - (rt.h * 2)) / 2) + rt.h;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_V1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 64;
            rt.h = 48;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + ((96 - (rt.h * 2)) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 64;
            rt.h = 48;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + ((96 - (rt.h * 2)) / 2) + rt.h;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_H0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + ((128 - (rt.w * 2)) / 2);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + ((128 - (rt.w * 2)) / 2) + rt.w;
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_H1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 64;
            rt.h = 48;
            rt.x = sx + ((128 - (rt.w * 2)) / 2);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 64;
            rt.h = 48;
            rt.x = sx + ((128 - (rt.w * 2)) / 2) + rt.w;
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_VH_S0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 96;
            rt.h = 72;
            rt.x = sx;
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 32;
            rt.h = 24;
            rt.x = sx + (128 - rt.w);
            rt.y = sy + (96 - rt.h);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_VH_S1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 77;
            rt.h = 58;
            rt.x = sx;
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + (128 - rt.w);
            rt.y = sy + (96 - rt.h);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_VH_S2:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 96;
            rt.h = 72;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 32;
            rt.h = 24;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + (96 - rt.h);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_VH_C0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 77;
            rt.h = 58;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + (96 - rt.h);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_VH_C1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 77;
            rt.h = 58;
            rt.x = sx;
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 51;
            rt.h = 38;
            rt.x = sx + (128 - rt.w);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_HH0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 64;
            rt.h = 85;
            rt.x = sx;
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            
            rt.w = 64;
            rt.h = 85;
            rt.x = sx + (128 - rt.w);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_HH1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));

            rt.w = 64;
            rt.h = 85;
            rt.x = sx;
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));

            rt.w = 64;
            rt.h = 85;
            rt.x = sx + (128 - rt.w);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            break;
        case NDS_DIS_MODE_HH2:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));

            rt.w = 64;
            rt.h = 96;
            rt.x = sx;
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));

            rt.w = 64;
            rt.h = 96;
            rt.x = sx + (128 - rt.w);
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));
            break;
        case NDS_DIS_MODE_HH3:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));

            rt.w = 64;
            rt.h = 96;
            rt.x = sx;
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x00, 0x80));

            rt.w = 64;
            rt.h = 96;
            rt.x = sx + (128 - rt.w);
            rt.y = sy;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            break;
        case NDS_DIS_MODE_HRES0:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x00, 0x80, 0x00));
            
            rt.w = 102;
            rt.h = 76;
            rt.x = sx + ((128 - rt.w) / 2);
            rt.y = sy + ((96 - rt.h) / 2);
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            break;
        case NDS_DIS_MODE_HRES1:
            rt.x = sx;
            rt.y = sy;
            rt.w = 128;
            rt.h = 96;
            SDL_FillRect(cvt, &rt, SDL_MapRGB(cvt->format, 0x80, 0x00, 0x00));
            break;
        }
*/
    }

#if defined(A30) || defined(RG28XX)
    nds.update_menu = 1;
#else
    GFX_Copy(-1, cvt->pixels, cvt->clip_rect, cvt->clip_rect, cvt->pitch, 0, E_MI_GFX_ROTATE_180);
    GFX_Flip();
#endif
    need_reload_bg = RELOAD_BG_COUNT;
    return 0;
}
#endif

#ifdef UNITTEST
#include "unity_fixture.h"

TEST_GROUP(sdl2_video_mmiyoo);

TEST_SETUP(sdl2_video_mmiyoo)
{
}

TEST_TEAR_DOWN(sdl2_video_mmiyoo)
{
}

TEST(sdl2_video_mmiyoo, get_current_menu_layer)
{
    TEST_ASSERT_EQUAL(get_current_menu_layer(), -1);
}

TEST_GROUP_RUNNER(sdl2_video_mmiyoo)
{
    RUN_TEST_CASE(sdl2_video_mmiyoo, get_current_menu_layer);
}
#endif

