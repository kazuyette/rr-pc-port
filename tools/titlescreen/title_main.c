/* title_main.c -- reconstructs Ridge Racer 1 (PS1)'s TITLE SCREEN as a
 * 320x240 image, from the user's own legally-extracted TEX3.TMS (the
 * menu/UI texture bank -- never committed to this repo).
 *
 * Asset provenance (identified by dumping every page of every TEX bank
 * with texparse_tool and inspecting them):
 *   - TEX3.TMS page 36 (240x96, 4bpp) = the "RIDGE RACER (tm)" title
 *     logo itself: red speed-lined letters over the green-scribble +
 *     grey checkered-flag art. This is the real title art, not the
 *     small 96x64 in-game billboard copy (that one lives in TEX1
 *     page 112).
 *   - TEX3.TMS page 90 (144x16) = the red "START BUTTON" prompt.
 *   - TEX3.TMS page 40 (112x16) = the dark-red "namcot (r)" logo.
 *   (For the record: TEX4.TMS holds the boot sequence -- "NOW
 *   LOADING!", the Galaxian mini-game sprites, and the "namco (r)"
 *   logo. TEX0 = in-race HUD/liveries, TEX1/TEX2 = per-track scenery.)
 *
 * Layout note: the on-screen positions used here are a RECONSTRUCTION
 * (logo centered upper-third, prompt below center, namcot at the
 * bottom -- matching how the real title screen reads), NOT yet a
 * byte-exact trace of the original's title-state draw calls. Finding
 * those exact coordinates in the decompiled state machine is a future
 * RE task; the ASSETS themselves are the real ones, byte-decoded.
 *
 * [ROUND 39] TRACED the original's title-logo renderer in the decomp:
 * the logo CLUT id 0x7E8C (computed from TEX3 p36's CLUT VRAM position
 * (192,506): ((506&0x1FF)<<6)|(192>>4)) has exactly ONE consumer in the
 * whole codebase -- func_800266B8. That function builds a 28x20 grid
 * (0x1C x 0x14 loops) of 0x34-byte textured-quad primitives in a
 * double buffer at D_8012E4C0/D_801510B4 (init: func_80026794, which
 * also zeroes the animation frame counter D_801D77A8), every quad
 * getting clut=0x7E8C at prim+0xE and a texpage from
 * func_8004788C(0,0,0x2C0,0x100) -- i.e. GetTPage-equivalent pointing
 * at VRAM (704,256), the logo's exact pixmap position. The per-frame
 * animator func_800267E4 then rebuilds a 112-entry (0x70) wave table
 * of (x,y) pairs in the PS1 scratchpad (0x1F800000 region), each entry
 * from cos/sin (func_80044D0C/func_80044E2C, the round-16-confirmed
 * BAM12 trig pair) of a phase that advances with the frame counter
 * (traveling wave: phase ~ i-dependent term minus frame*7-derived
 * term, amplitude ~0x90 gently modulated by cos(frame*4), y biased
 * +0x80), and deforms the quad mesh through it (via func_80043B3C /
 * func_80047A40 per quad row). CONCLUSION, CONFIRMED: the original
 * title logo is a WAVING-FLAG MESH ANIMATION -- 560 quads rippling
 * through a scratchpad sine table -- not a static blit. The --wave
 * mode below reproduces that structure (per-column traveling sine
 * deformation, breathing amplitude, blinking START BUTTON prompt);
 * its exact coefficients are APPROXIMATED to read like the original,
 * not yet byte-matched to the scratchpad math.
 *
 * Usage:
 *   rr_title_tool <path/to/TEX3.TMS> [--out title.ppm]
 *   rr_title_tool <path/to/TEX3.TMS> --wave N --prefix frame_
 *     (writes frame_000.ppm .. frame_NNN.ppm of the animated title)
 *
 * Writes 320x240 binary PPMs. Blitting uses pure-black color-keying
 * (the classic PS1 "raw 0x0000 = transparent" convention; tim.c
 * decodes everything opaque, so keying on black at blit time is
 * equivalent for these pages on a black background).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tim.h"

#define SCR_W 320
#define SCR_H 240

/* Pages within TEX3.TMS (see provenance block above). */
#define PAGE_LOGO 36
#define PAGE_START_BUTTON 90
#define PAGE_NAMCOT 40

/* BAM12 sine via the same convention as the original (0x1000 = full
 * turn), table-free double-precision stand-in -- the PC port's
 * physics code has the real LUT path; this display tool only needs
 * the shape. */
static int bam_sin(int bam)
{
    static const double PI2 = 6.28318530717958647692;
    double t = (double)(bam & 0xFFF) / 4096.0;
    double s = 0.0;
    /* cheap sine: use library-free 5th-order approx to avoid -lm */
    double x = t * PI2;
    if (x > 3.14159265358979) x -= PI2;
    {
        double x2 = x * x;
        s = x * (1.0 - x2 / 6.0 * (1.0 - x2 / 20.0 * (1.0 - x2 / 42.0)));
    }
    return (int)(s * 4096.0);
}

/* Waving-flag blit: per-column vertical displacement from a traveling
 * sine, mirroring the traced structure of func_800267E4 (traveling
 * phase advancing with the frame counter, ~0x90-scale amplitude with
 * a slow cos() breathing term). Coefficients approximated. */
static void blit_wave(uint32_t *fb, const TimPage *p, int dst_x, int dst_y,
                      int frame)
{
    int x, y;
    if (!p || !p->rgba)
        return;
    for (x = 0; x < p->width; x++) {
        /* two ripples across the logo, traveling left, amplitude
         * breathing slightly and ramping toward the free (right) edge */
        int phase = ((x << 13) / p->width) - frame * 96;
        int amp = 10 + ((6 * x) / p->width)
                + ((2 * bam_sin(frame * 24)) >> 12);
        int dy = (bam_sin(phase) * amp) >> 12;
        int fx = dst_x + x; /* vertical-only ripple: per-column x shifts
                             * would tear the columns apart in this
                             * column-blit -- the original avoids that
                             * because its mesh quads stretch BETWEEN
                             * wave samples instead of shifting columns */
        if (fx < 0 || fx >= SCR_W)
            continue;
        for (y = 0; y < p->height; y++) {
            int fy = dst_y + y + dy;
            uint32_t t;
            if (fy < 0 || fy >= SCR_H)
                continue;
            t = p->rgba[y * p->width + x];
            if ((t & 0x00FFFFFFu) == 0)
                continue;
            fb[fy * SCR_W + fx] = t;
        }
    }
}

static void blit(uint32_t *fb, const TimPage *p, int dst_x, int dst_y)
{
    int x, y;
    if (!p || !p->rgba)
        return;
    for (y = 0; y < p->height; y++) {
        int fy = dst_y + y;
        if (fy < 0 || fy >= SCR_H)
            continue;
        for (x = 0; x < p->width; x++) {
            int fx = dst_x + x;
            uint32_t t;
            if (fx < 0 || fx >= SCR_W)
                continue;
            t = p->rgba[y * p->width + x];
            if ((t & 0x00FFFFFFu) == 0) /* black color key */
                continue;
            fb[fy * SCR_W + fx] = t;
        }
    }
}

static int write_ppm(const char *path, const uint32_t *fb)
{
    int x, y;
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (y = 0; y < SCR_H; y++)
        for (x = 0; x < SCR_W; x++) {
            uint32_t t = fb[y * SCR_W + x];
            fputc((int)(t & 0xFF), f);
            fputc((int)((t >> 8) & 0xFF), f);
            fputc((int)((t >> 16) & 0xFF), f);
        }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *out_path = "title.ppm";
    const char *prefix = "frame_";
    int wave_frames = 0;
    FILE *f;
    long sz;
    uint8_t *buf;
    TimFile tf;
    uint32_t *fb;
    int i;

    if (argc < 2) {
        fprintf(stderr,
                "usage: %s <path/to/TEX3.TMS> [--out title.ppm]\n"
                "       %s <path/to/TEX3.TMS> --wave N [--prefix frame_]\n",
                argv[0], argv[0]);
        return 1;
    }
    for (i = 2; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--out") == 0)
            out_path = argv[i + 1];
        else if (strcmp(argv[i], "--wave") == 0)
            wave_frames = atoi(argv[i + 1]);
        else if (strcmp(argv[i], "--prefix") == 0)
            prefix = argv[i + 1];
    }

    f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "read failed\n");
        fclose(f);
        free(buf);
        return 1;
    }
    fclose(f);

    if (tim_parse(buf, (size_t)sz, &tf) != TIM_OK) {
        fprintf(stderr, "tim_parse failed\n");
        free(buf);
        return 1;
    }
    if (tf.page_count <= PAGE_START_BUTTON) {
        fprintf(stderr, "only %zu pages -- is this really TEX3.TMS?\n",
                tf.page_count);
        tim_free(&tf);
        free(buf);
        return 1;
    }

    fb = calloc(SCR_W * SCR_H, sizeof(uint32_t)); /* black background */
    if (!fb) {
        tim_free(&tf);
        free(buf);
        return 1;
    }

    if (wave_frames > 0) {
        /* Animated title: waving-flag logo (traced structure, see the
         * ROUND 39 provenance block), blinking START BUTTON, static
         * namcot. One PPM per frame. */
        int fr;
        char path[512];
        for (fr = 0; fr < wave_frames; fr++) {
            memset(fb, 0, SCR_W * SCR_H * sizeof(uint32_t));
            blit_wave(fb, &tf.pages[PAGE_LOGO],
                      (SCR_W - tf.pages[PAGE_LOGO].width) / 2, 46, fr);
            if ((fr / 15) % 2 == 0) /* prompt blinks ~0.5s at 30fps */
                blit(fb, &tf.pages[PAGE_START_BUTTON],
                     (SCR_W - tf.pages[PAGE_START_BUTTON].width) / 2, 170);
            blit(fb, &tf.pages[PAGE_NAMCOT],
                 (SCR_W - tf.pages[PAGE_NAMCOT].width) / 2, 214);
            snprintf(path, sizeof(path), "%s%03d.ppm", prefix, fr);
            if (write_ppm(path, fb) != 0) {
                fprintf(stderr, "cannot write %s\n", path);
                break;
            }
        }
        printf("wrote %d wave frames (%s000.ppm ..)\n", wave_frames, prefix);
        free(fb);
        tim_free(&tf);
        free(buf);
        return 0;
    }

    /* Logo centered horizontally, upper third. */
    blit(fb, &tf.pages[PAGE_LOGO],
         (SCR_W - tf.pages[PAGE_LOGO].width) / 2, 46);
    /* START BUTTON prompt, below center. */
    blit(fb, &tf.pages[PAGE_START_BUTTON],
         (SCR_W - tf.pages[PAGE_START_BUTTON].width) / 2, 170);
    /* namcot(r) logo, bottom. */
    blit(fb, &tf.pages[PAGE_NAMCOT],
         (SCR_W - tf.pages[PAGE_NAMCOT].width) / 2, 214);

    if (write_ppm(out_path, fb) != 0) {
        fprintf(stderr, "cannot write %s\n", out_path);
        free(fb);
        tim_free(&tf);
        free(buf);
        return 1;
    }
    printf("wrote %s (320x240) -- logo=TEX3 p%d, prompt=p%d, namcot=p%d\n",
           out_path, PAGE_LOGO, PAGE_START_BUTTON, PAGE_NAMCOT);

    free(fb);
    tim_free(&tf);
    free(buf);
    return 0;
}
