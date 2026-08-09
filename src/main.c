/* rr-pc-port -- phase 1 + 2 + 3 vertical slice, + real-texture demo.
 *
 * Proves these things compile and run together on a normal host:
 *   1. Genuine ported decomp logic (src/globals.c, src/stubs.c,
 *      src/ported_logic.c), called through their original PS1 symbol
 *      names.
 *   2. An optional SDL2 window/event loop that degrades gracefully to a
 *      headless run when SDL2 isn't available at build time, or when
 *      there's no display at run time (this sandbox has neither).
 *   3. Phase 3: the software rasterizer (src/gpu/gpu_soft.c) drawing a
 *      small animated demo scene (a spinning gouraud-shaded triangle,
 *      two pulsing/color-cycling flat triangles, a background grid and
 *      crosshair via gpu_draw_line) into a raw framebuffer, blitted to
 *      the SDL2 window each frame via a streaming texture. Hardcoded
 *      shapes only -- no real game geometry yet, that's phase 5.
 *   4. Real-texture demo mode: if invoked with a path to one of the
 *      game's own TEX*.TMS files (argv[1]), decodes it via
 *      tools/texparse/tim.c and displays one of its real pages as a
 *      large centered quad via gpu_draw_quad_textured -- proof that
 *      authentic game art, not just synthetic shapes, can flow through
 *      this rasterizer. Entirely additive/optional: with no argv[1],
 *      behavior is unchanged from the phase 1-3 animated demo above.
 *      The TEX*.TMS file itself is never bundled/committed -- point
 *      this at your own local extraction, e.g.:
 *        ./rr_pc_port /path/to/TEX0.TMS
 *
 * This is scaffolding -- no asm-locked function, no audio, no real
 * geometry yet (real textures now flow through, real MAP.RRM/OBJ.RRO
 * geometry is still phase 5). See ROADMAP.md.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include "ported.h"
#include "ported_logic.h"
#include "gpu/gpu_soft.h"
#include "tim.h"

#ifdef HAVE_SDL2
#include <SDL2/SDL.h>
#endif

static void exercise_ported_functions(void) {
    printf("-- exercising ported logic functions --\n");

    printf("func_80021FA4(10, 3)  = %d (expect 7)\n", func_80021FA4(10, 3));
    printf("func_80021FA4(3, 10)  = %d (expect 7)\n", func_80021FA4(3, 10));

    printf("func_80019C6C(0x10, 0x20)   = %d (expect 16)\n", func_80019C6C(0x10, 0x20));
    printf("func_80019C6C(0x10, 0xFF0)  = %d (expect 32)\n", func_80019C6C(0x10, 0xFF0));

    func_8001CD90();
    func_8001CDA8();
    printf("func_8001CD90()+func_8001CDA8() ran (reset/reload pair, no visible return)\n");

    printf("func_80032DF8() = %d (expect 0)\n", func_80032DF8());

    unsigned char obj[8] = {0};
    func_80047B98(obj);
    printf("func_80047B98(obj): obj[3]=%d obj[7]=0x%02X (expect 5, 0x28)\n", obj[3], obj[7]);

    func_80047AF8(obj, 1);
    printf("func_80047AF8(obj, 1): obj[7]=0x%02X (expect 0x2A, bit 0x2 set)\n", obj[7]);

    func_8003FA94();
    printf("func_8003FA94() ran (no-op hook)\n");

    printf("-- ported logic OK --\n");
}

static void exercise_ported_logic_round1(void) {
    printf("-- exercising phase 2 round 1 ported logic (src/ported_logic.c) --\n");

    printf("func_80055800() = %d (expect 3)\n", func_80055800());
    printf("func_80055808() = %d (expect 1)\n", func_80055808());

    printf("func_80059040() = %d (expect 1, global starts at 0)\n", func_80059040());

    func_8004985C(0x1234);
    printf("func_8004985C(0x1234) ran, no direct getter (paired global set)\n");

    unsigned char obj[16] = {0};
    func_80047B48(obj);
    printf("func_80047B48(obj): obj[3]=%d obj[7]=0x%02X (expect 4, 0x20)\n", obj[3], obj[7]);

    func_80047B20(obj, 1);
    printf("func_80047B20(obj,1): obj[7]=0x%02X (expect 0x21, bit0 set)\n", obj[7]);
    func_80047B20(obj, 0);
    printf("func_80047B20(obj,0): obj[7]=0x%02X (expect 0x20, bit0 cleared)\n", obj[7]);

    printf("func_80045738(42) = %d (expect 0, old value)\n", func_80045738(42));
    printf("func_80045738(7)  = %d (expect 42, previous value)\n", func_80045738(7));

    printf("func_80047920(0x30, 0x2) = 0x%X (expect 0x83)\n", func_80047920(0x30, 2));

    printf("func_800554EC(0x10) = %d (expect 0, in range)\n", func_800554EC(0x10));
    printf("func_800554EC(0x30) = %d (expect 1, out of range)\n", func_800554EC(0x30));

    unsigned int word = 0;
    unsigned char cnt[8] = {0, 0, 0, 5, 0, 0, 0, 0};
    int rc = func_80047D24(cnt, &word);
    printf("func_80047D24: rc=%d cnt[3]=%d word=0x%X (expect 0, 6, 0)\n", rc, cnt[3], word);

    printf("-- phase 2 round 1 ported logic OK --\n");
}

/* Reads a whole file into a malloc'd buffer, same small helper pattern
 * as tools/mapparse/mapparse_main.c and tools/texparse/texparse_main.c
 * (kept as its own copy here rather than shared, to keep main.c's
 * build free of any extra tools/-only source files beyond tim.c
 * itself). Returns NULL and prints a diagnostic on any failure. */
static uint8_t *read_whole_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    size_t read_bytes;

    if (f == NULL) {
        printf("could not open '%s'\n", path);
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        printf("fseek failed on '%s'\n", path);
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size <= 0) {
        printf("ftell failed (or empty file) on '%s'\n", path);
        fclose(f);
        return NULL;
    }
    rewind(f);

    buf = (uint8_t *)malloc((size_t)size);
    if (buf == NULL) {
        printf("out of memory reading '%s' (%ld bytes)\n", path, size);
        fclose(f);
        return NULL;
    }
    read_bytes = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (read_bytes != (size_t)size) {
        printf("short read on '%s' (%zu of %ld bytes)\n", path, read_bytes, size);
        free(buf);
        return NULL;
    }
    *out_size = (size_t)size;
    return buf;
}

/* Loads a TEX*.TMS file at `path` and picks the first page that
 * actually decoded to real pixel data and is a reasonable size (skips
 * degenerate/tiny/undecoded pages -- e.g. mode-3/24bpp pages this
 * parser doesn't decode, or pages too small to be interesting on
 * screen). On success returns 1 and sets *tf_out (caller must
 * tim_free() it later) and *page_out to a pointer INTO *tf_out's
 * pages array (valid as long as *tf_out is alive). On failure returns
 * 0 and leaves *tf_out zeroed. */
static int load_demo_texture_page(const char *path, TimFile *tf_out, const TimPage **page_out) {
    uint8_t *buf;
    size_t buf_size;
    size_t i;

    memset(tf_out, 0, sizeof(*tf_out));
    *page_out = NULL;

    buf = read_whole_file(path, &buf_size);
    if (buf == NULL) {
        return 0;
    }

    if (tim_parse(buf, buf_size, tf_out) != TIM_OK) {
        printf("tim_parse failed on '%s'\n", path);
        free(buf);
        return 0;
    }
    free(buf); /* tim_parse copies out everything it needs into tf_out */

    for (i = 0; i < tf_out->page_count; i++) {
        const TimPage *p = &tf_out->pages[i];
        if (p->rgba != NULL && p->width >= 8 && p->height >= 8) {
            *page_out = p;
            printf("texture demo: using page %zu of '%s' (%dx%d, vram=(%d,%d)), %zu pages total in file\n",
                   i, path, p->width, p->height, p->vram_x, p->vram_y, tf_out->page_count);
            return 1;
        }
    }

    printf("texture demo: no usable (>=8x8, decoded) page found in '%s' (%zu pages total)\n",
           path, tf_out->page_count);
    return 0;
}

/* Renders one real, decoded TIM page as a large centered quad -- the
 * proof-of-concept: authentic game texture art, not a synthetic shape,
 * flowing through gpu_draw_quad_textured() into the same framebuffer
 * the animated demo scene uses. Static (no animation) -- the point is
 * showing the real art clearly, not motion. */
static void draw_texture_demo_scene(const TimPage *page) {
    int margin = 8;
    int x0 = margin, y0 = margin;
    int x1 = GPU_FB_WIDTH - 1 - margin, y1 = margin;
    int x2 = GPU_FB_WIDTH - 1 - margin, y2 = GPU_FB_HEIGHT - 1 - margin;
    int x3 = margin, y3 = GPU_FB_HEIGHT - 1 - margin;

    gpu_clear(0x00181818); /* neutral dark gray so texture colors read true */
    gpu_draw_quad_textured(x0, y0, 0.0f, 0.0f,
                            x1, y1, 1.0f, 0.0f,
                            x2, y2, 1.0f, 1.0f,
                            x3, y3, 0.0f, 1.0f,
                            page->rgba, page->width, page->height);
}

#ifdef HAVE_SDL2
/* Cosine-based color cycler: three sine waves 120 degrees apart give a
 * smooth, seamless-looping RGB cycle. `phase` offsets which point in
 * the cycle a given call starts at (used to desync the corner
 * triangles / gouraud vertices from each other), `t` is elapsed time
 * in seconds. Output stays in [64,255] per channel so nothing ever
 * dims all the way to black. */
static uint32_t cycle_color(double t, double phase) {
    double r = 0.62 + 0.38 * sin(t + phase);
    double g = 0.62 + 0.38 * sin(t + phase + 2.0943951); /* +120 deg */
    double b = 0.62 + 0.38 * sin(t + phase + 4.1887902); /* +240 deg */
    int ri = (int)(r * 255.0), gi = (int)(g * 255.0), bi = (int)(b * 255.0);
    if (ri < 0) { ri = 0; } else if (ri > 255) { ri = 255; }
    if (gi < 0) { gi = 0; } else if (gi > 255) { gi = 255; }
    if (bi < 0) { bi = 0; } else if (bi > 255) { bi = 255; }
    return ((uint32_t)ri << 16) | ((uint32_t)gi << 8) | (uint32_t)bi;
}

/* Phase 3: an animated demo scene into the software framebuffer every
 * frame -- proof that the gpu_soft rasterizer -> SDL2 texture blit
 * pipeline works end to end, and now exercises every primitive type
 * gpu_soft offers (flat triangle, gouraud triangle, line) instead of
 * three static shapes. Not real game geometry (that's phase 5, once
 * MAP.RRM/OBJ.RRO are decoded); just enough to look alive before
 * wiring anything real into it. `now_ms` is SDL_GetTicks() at the top
 * of the frame, used as the single time source for every animation
 * below so everything stays in sync. */
static void draw_animated_scene(Uint32 now_ms) {
    double t = now_ms / 1000.0;
    int i;

    gpu_clear(0x00202060); /* same dark-blue as the original solid-clear background */

    /* Faint background grid, purely to show off gpu_draw_line() and
     * give the rotation/pulsing something static to read against. */
    for (i = 40; i < GPU_FB_WIDTH; i += 40) {
        gpu_draw_line(i, 0, i, GPU_FB_HEIGHT - 1, 0x00303050);
    }
    for (i = 40; i < GPU_FB_HEIGHT; i += 40) {
        gpu_draw_line(0, i, GPU_FB_WIDTH - 1, i, 0x00303050);
    }

    /* Centered gouraud triangle, orbiting around the screen center and
     * spinning as it goes -- three vertices 120 degrees apart on a
     * circle, each with its own cycling color (phase-offset per vertex
     * so the shading itself animates, not just the position). This is
     * the real gpu_draw_triangle_gouraud() primitive, not a flat tri
     * tinted to look gradient-y. */
    {
        double cx = GPU_FB_WIDTH / 2.0, cy = GPU_FB_HEIGHT / 2.0;
        double radius = 55.0;
        double angle = t * 1.3; /* radians/sec spin rate */
        int vx[3], vy[3];
        uint32_t vc[3];

        for (i = 0; i < 3; i++) {
            double a = angle + i * (2.0 * M_PI / 3.0);
            vx[i] = (int)(cx + radius * cos(a));
            vy[i] = (int)(cy + radius * sin(a));
            vc[i] = cycle_color(t * 0.8, i * 2.0);
        }
        gpu_draw_triangle_gouraud(vx[0], vy[0], vx[1], vy[1], vx[2], vy[2],
                                   vc[0], vc[1], vc[2]);
    }

    /* Two corner flat triangles -- same shapes as the original
     * milestone-1 demo, still using gpu_draw_triangle_flat() (proving
     * that primitive still works unchanged), but now gently pulsing in
     * size (scaled from their own centroid) and color-cycling instead
     * of sitting static. */
    {
        /* {x0,y0, x1,y1, x2,y2} triples, same base shapes as before. */
        static const int tris[2][6] = {
            {20, 20, 20, 70, 80, 70},        /* top-left */
            {300, 220, 240, 220, 300, 170},  /* bottom-right */
        };
        double phases[2] = {0.0, 3.0};

        for (i = 0; i < 2; i++) {
            const int *v = tris[i];
            double scale = 1.0 + 0.18 * sin(t * 2.0 + phases[i]);
            double cx = (v[0] + v[2] + v[4]) / 3.0;
            double cy = (v[1] + v[3] + v[5]) / 3.0;
            int sx0 = (int)(cx + (v[0] - cx) * scale);
            int sy0 = (int)(cy + (v[1] - cy) * scale);
            int sx1 = (int)(cx + (v[2] - cx) * scale);
            int sy1 = (int)(cy + (v[3] - cy) * scale);
            int sx2 = (int)(cx + (v[4] - cx) * scale);
            int sy2 = (int)(cy + (v[5] - cy) * scale);
            uint32_t color = cycle_color(t * 0.5, phases[i]);
            gpu_draw_triangle_flat(sx0, sy0, sx1, sy1, sx2, sy2, color);
        }
    }

    /* Crosshair at dead center, drawn last so it's always on top --
     * another gpu_draw_line() exercise, and a fixed visual anchor to
     * judge the orbiting triangle's motion against. */
    {
        int cx = GPU_FB_WIDTH / 2, cy = GPU_FB_HEIGHT / 2;
        gpu_draw_line(cx - 8, cy, cx + 8, cy, 0x00FFFFFF);
        gpu_draw_line(cx, cy - 8, cx, cy + 8, 0x00FFFFFF);
    }
}

/* `tex_page` selects which scene the loop draws every frame: NULL runs
 * the original phase 1-3 animated demo unchanged; non-NULL switches to
 * the static real-texture demo (draw_texture_demo_scene) instead, and
 * the window title reflects which mode is active. */
static int run_sdl_loop(const TimPage *tex_page) {
    const char *title = (tex_page != NULL)
        ? "rr-pc-port (real texture demo -- decoded from a TEX*.TMS file)"
        : "rr-pc-port (phase 3 -- software rasterizer)";

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed (%s) -- continuing headless\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        title,
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        640, 480, SDL_WINDOW_SHOWN);

    if (!window) {
        printf("SDL_CreateWindow failed (%s) -- continuing headless\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        printf("SDL_CreateRenderer failed (%s)\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* Streaming texture that gpu_framebuffer[] gets copied into every
     * frame -- the standard SDL2 pattern for presenting a software-
     * rendered framebuffer. SDL scales it up to fill the (larger)
     * window automatically since we pass NULL src/dst rects below. */
    SDL_Texture *fb_texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING,
        GPU_FB_WIDTH, GPU_FB_HEIGHT);
    if (!fb_texture) {
        printf("SDL_CreateTexture failed (%s)\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    /* CI/headless runs force SDL_VIDEODRIVER=dummy (no real window ever
     * appears, nothing for a human to look at) -- keep the old 2s safety
     * cap there so the workflow can't hang. On a real display, stay open
     * until the user closes the window (or Escape), same as any normal
     * app -- flashing open/closed after 2s was confusing on a real
     * desktop, it looked like a crash rather than "working as intended". */
    const char *video_driver = getenv("SDL_VIDEODRIVER");
    int is_headless = video_driver && strcmp(video_driver, "dummy") == 0;
    if (!is_headless) {
        printf("window open -- close it (or press Escape) to exit\n");
    }

    Uint32 start = SDL_GetTicks();
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
        }

        if (tex_page != NULL) {
            draw_texture_demo_scene(tex_page);
        } else {
            draw_animated_scene(SDL_GetTicks() - start);
        }
        SDL_UpdateTexture(fb_texture, NULL, gpu_framebuffer,
                           GPU_FB_WIDTH * (int)sizeof(uint32_t));

        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, fb_texture, NULL, NULL);
        SDL_RenderPresent(renderer);

        if (is_headless && SDL_GetTicks() - start > 2000) running = 0; /* headless/CI safety cap */
    }

    printf("SDL loop exited cleanly after %u ms\n", SDL_GetTicks() - start);

    SDL_DestroyTexture(fb_texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif

int main(int argc, char **argv) {
    /* Optional real-texture demo mode: `rr_pc_port /path/to/TEX0.TMS`.
     * With no argument, behavior is exactly the original phase 1-3
     * animated demo -- this is purely additive. See the file header
     * comment and load_demo_texture_page()/draw_texture_demo_scene()
     * above for what this does. */
    const char *tex_path = (argc > 1) ? argv[1] : NULL;
    TimFile tex_file;
    const TimPage *tex_page = NULL;

    memset(&tex_file, 0, sizeof(tex_file));

    printf("rr-pc-port phase 1+2+3 vertical slice\n");

    exercise_ported_functions();
    exercise_ported_logic_round1();

    if (tex_path != NULL) {
        printf("-- texture demo mode: loading '%s' --\n", tex_path);
        load_demo_texture_page(tex_path, &tex_file, &tex_page);
        /* On failure, tex_page stays NULL -- falls back to the animated
         * demo below rather than crashing or exiting, so a bad path
         * doesn't break the rest of the vertical slice. */
    }

#ifdef HAVE_SDL2
    if (run_sdl_loop(tex_page) != 0) {
        printf("(no usable display -- window/event loop skipped, logic-only run)\n");
    }
#else
    if (tex_page != NULL) {
        /* No SDL2 at build time -- still prove the decode -> rasterize
         * pipeline works headlessly, same check
         * rr_pc_port_texdemo_test automates: draw the real page into
         * gpu_framebuffer[] and report whether it shows real variation. */
        int px, distinct = 0;
        uint32_t first;
        draw_texture_demo_scene(tex_page);
        first = gpu_framebuffer[0];
        for (px = 0; px < GPU_FB_WIDTH * GPU_FB_HEIGHT; px++) {
            if (gpu_framebuffer[px] != first) { distinct = 1; break; }
        }
        printf("(built without SDL2 -- headless texture demo: drew %dx%d page into "
               "gpu_framebuffer[], %s)\n",
               tex_page->width, tex_page->height,
               distinct ? "framebuffer shows real variation (not solid color)"
                        : "WARNING: framebuffer is a single solid color");
    } else {
        printf("(built without SDL2 -- window/event loop skipped, logic-only run)\n");
    }
#endif

    tim_free(&tex_file);

    printf("phase 1+2+3 vertical slice complete, exiting 0\n");
    return 0;
}
