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
 *   5. Real-track demo mode: if invoked with `--track <MAP.RRM>
 *      <IDX.HED>`, parses both files (tools/mapparse/map_rrm.c +
 *      idx_hed.c) and draws the real track using gpu_draw_quad_flat()
 *      -- one filled quad per type-B (road surface) record, translated
 *      to its section's IDX.HED grid-cell anchor (the round 8-10
 *      confirmed, byte/algebra-verified translation-only placement
 *      model; see project memory rr_pc_port_round8.md..round10.md and
 *      tools/mapparse/worldmap_main.c, whose standalone-PPM version of
 *      this exact approach this mode mirrors, now flowing through the
 *      real rasterizer instead of a raw PPM writer). Sections with <=2
 *      total records are skipped (round 10 finding: markers/junction
 *      nodes, not road geometry). Two view modes, toggled with V:
 *        - Top (default): orthographic top-down debug view, arrow keys
 *          pan / +/- zoom / R resets.
 *        - Drive: a perspective camera you steer through the track --
 *          up/down move forward/back, left/right turn, +/- raise/lower
 *          the camera, R resets to a spawn point over the track's
 *          bounding-box center. KNOWN SIMPLIFICATION: this treats the
 *          track as a flat plane at Y=0 and ignores each vertex's real
 *          MAP.RRM Y (height) field -- that field has not been verified
 *          the way X/Z were in rounds 8-10, so using it for real
 *          elevation is future work, not done here. Occlusion is a
 *          back-to-front depth sort, exact for a flat non-overlapping
 *          plane. P toggles autopilot: an automatic lap around a path
 *          built from each real-road section's centroid, visited in
 *          section-index order (round 10 proved this order IS the real
 *          track traversal order) -- pure reuse of already-confirmed
 *          data, no new reverse engineering. Any direction key hands
 *          control back to the player. Autopilot speed scales down
 *          through corners based on local path curvature instead of
 *          holding one fixed speed (round 12c).
 *      Optional 4th argument: a TEX*.TMS path. When given, the road
 *      surface (both view modes) is drawn with that file's first usable
 *      decoded page (same page-picking heuristic as texture demo mode
 *      above) instead of a flat color, tiled every
 *      TRACK_TEXTURE_TILE_WORLD_UNITS world units via a simple planar
 *      (world X/Z) UV mapping. KNOWN SIMPLIFICATION: MAP.RRM's real
 *      per-record texture/material selection has never been decoded
 *      (map_rrm.h's MapRrmRecord field comments list group_id/flags as
 *      unconfirmed candidates) and doing so would need a live-debugging
 *      round like rounds 8-10 used for the placement transform -- so
 *      this paints the whole road with one uniform tiled texture rather
 *      than the game's real per-section material, and (gpu_soft.c's
 *      gpu_draw_quad_textured has no color-tint input) textured quads
 *      skip the drive view's distance fog that flat-colored quads get.
 *      Both honestly documented shortcuts, not silent inaccuracies.
 *      Without this argument, behavior is unchanged (flat-colored road).
 *      Both MAP.RRM/IDX.HED files (and the optional texture file) are
 *      never bundled/committed -- point this at your own local
 *      extraction, e.g.:
 *        ./rr_pc_port --track /path/to/MAP.RRM /path/to/IDX.HED [/path/to/TEX0.TMS]
 *
 * This is scaffolding -- no asm-locked function, no audio, and the
 * track demo above is a debug view (top-down or a simplified flat-
 * ground drive camera), not the real game's actual driving/rendering
 * code -- that's a later phase. See ROADMAP.md.
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
#include "psx_vram.h"
#include "map_rrm.h"
#include "idx_hed.h"
#include "physics.h"
#include "physics_psx.h"
#include "psx_track_bridge.h"
#include "psx_ai.h"
#include "vab.h"

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

/* Holds the two parsed files for track demo mode, plus whether loading
 * succeeded -- mirrors the load_demo_texture_page()/TimPage pattern
 * above. Owned/freed by main() via track_demo_free(). */
typedef struct {
    MapRrmFile mf;
    IdxHedFile idxf;
    int ready;
    /* Optional road texture (round 12d). Borrowed -- points into a
     * TimFile owned and freed separately by main(), NOT by
     * track_demo_free() below. NULL/0 means "no texture", i.e. the
     * original flat-colored road. */
    const uint32_t *tex_rgba;
    int tex_w, tex_h;
} TrackDemo;

/* World-space planar UV tiling for the road texture: wraps a world
 * coordinate into a [0,1) texture coordinate every
 * TRACK_TEXTURE_TILE_WORLD_UNITS units. Deliberately simple (no
 * per-record orientation/scale) -- see the file header comment for why
 * a real per-material UV isn't attempted here. */
#define TRACK_TEXTURE_TILE_WORLD_UNITS 512.0
static float track_tile_uv(double world_coord) {
    double t = world_coord / TRACK_TEXTURE_TILE_WORLD_UNITS;
    double frac = t - floor(t);
    return (float)frac;
}

static void track_demo_free(TrackDemo *td) {
    if (td->ready) {
        map_rrm_free(&td->mf);
    }
    memset(td, 0, sizeof(*td));
}

/* Loads and parses MAP.RRM + IDX.HED for track demo mode. On success
 * returns 1 with td->ready=1; on any failure prints a diagnostic,
 * leaves td zeroed/ready=0, and returns 0 (caller falls back to the
 * animated demo, same graceful-degradation pattern as texture mode). */
static int load_demo_track(const char *map_path, const char *idx_path, TrackDemo *td) {
    uint8_t *map_buf, *idx_buf;
    size_t map_size = 0, idx_size = 0;
    int rc;

    memset(td, 0, sizeof(*td));

    map_buf = read_whole_file(map_path, &map_size);
    idx_buf = read_whole_file(idx_path, &idx_size);
    if (map_buf == NULL || idx_buf == NULL) {
        free(map_buf);
        free(idx_buf);
        return 0;
    }

    rc = map_rrm_parse(map_buf, map_size, &td->mf);
    free(map_buf);
    if (rc != MAP_RRM_OK) {
        printf("track demo: MAP.RRM parse failed (%d)\n", rc);
        free(idx_buf);
        return 0;
    }

    rc = idx_hed_parse(idx_buf, idx_size, &td->idxf);
    free(idx_buf);
    if (rc != IDX_HED_OK) {
        printf("track demo: IDX.HED parse failed (%d)\n", rc);
        map_rrm_free(&td->mf);
        memset(td, 0, sizeof(*td));
        return 0;
    }

    printf("track demo: parsed %u sections / %zu records from '%s' + '%s'\n",
           (unsigned)td->mf.section_count, td->mf.record_count, map_path, idx_path);
    td->ready = 1;
    return 1;
}

/* A section is only drawn if it has more than 2 total records --
 * round 10 finding: <=2-record sections are very likely markers/
 * junction nodes rather than ordinary road geometry, and including
 * them adds noise without adding real track shape (matches
 * tools/mapparse/worldmap_main.c's IS_REAL_ROAD_SECTION filter). */
static int track_demo_is_real_road_section(const MapRrmFile *mf, uint16_t section_index) {
    const MapRrmSectionDir *d;
    if (section_index >= mf->section_count) return 0;
    d = &mf->sections[section_index];
    return ((int)d->count_a + (int)d->count_b + (int)d->count_c) > 2;
}

/* Interactive view state for track demo mode: `zoom` multiplies the
 * auto-fit scale (1.0 = whole track fits on screen), `pan_x`/`pan_z`
 * shift the view center in WORLD units (not screen pixels, so panning
 * feels consistent at any zoom level). Arrow keys pan, +/- zoom, R
 * resets -- wired up in run_sdl_loop's event loop below. File-scope
 * since there is only ever one track demo view per process. */
static double s_track_zoom = 1.0;
static double s_track_pan_x = 0.0, s_track_pan_z = 0.0;

/* Which of the two track-demo view modes is active; toggled with V in
 * run_sdl_loop's event loop. TRACK_VIEW_TOP is the original round-10
 * orthographic debug view (unchanged); TRACK_VIEW_DRIVE is the newer
 * perspective camera (see file header comment for its simplifications). */
#define TRACK_VIEW_TOP   0
#define TRACK_VIEW_DRIVE 1
static int s_track_view_mode = TRACK_VIEW_TOP;

/* Drive-camera state: position in world X/Z, height above the (flat,
 * simplified) Y=0 ground plane, and yaw in radians (0 = facing +Z,
 * increasing yaw turns the facing direction toward +X -- see
 * draw_track_drive_scene's comment for the exact rotation convention). */
static double s_cam_x = 0.0, s_cam_z = 0.0, s_cam_height = 300.0, s_cam_yaw = 0.0;

#define TRACK_DRIVE_MOVE_STEP   60.0
#define TRACK_DRIVE_TURN_STEP   0.08
#define TRACK_DRIVE_HEIGHT_STEP 30.0
#define TRACK_DRIVE_HEIGHT_MIN  20.0
#define TRACK_DRIVE_HEIGHT_MAX  4000.0

/* "Real physics" driving mode (C toggles): drives s_cam_x/s_cam_z/
 * s_cam_yaw from src/physics.c's gearbox + integration model instead of
 * the fixed-step free-cam movement above. Uses this file's own (sin,
 * cos) forward convention, which src/physics.c's header comment
 * documents as chosen to match -- confirmed identical here, so no
 * conversion is needed when syncing the two. See src/physics.h for what
 * in this model is a confirmed port of the original PS1 game's formulas
 * (gear-shift thresholds, track off-track test) vs. an original
 * approximation (accel/steering feel) -- this integration doesn't yet
 * load the real course geometry (tools/trackdata), so off-track
 * detection isn't wired up here, only the gearbox + movement model. */
static int s_physics_mode = 0;
static PhysicsCar s_physics_car;
static double s_physics_hud_timer = 0.0;

/* Optional real course geometry (tools/trackdata), loaded from the
 * user's own PSX.EXE via --physicsdata -- see main()'s arg parsing and
 * ROADMAP.md Phase 7. Purely additive: physics mode works fine without
 * it (movement only, no off-track feedback), same graceful-fallback
 * convention this file already uses for --track/--track TEX. */
static TrackData s_physics_trackdata;
static int s_physics_trackdata_loaded = 0;
/* ROUND 43: the AUTHENTIC fixed-point core (physics_psx.c, rounds
 * 40-42) drives the interactive mode whenever the real course geometry
 * is loaded (--physicsdata) -- the float model above stays as the
 * fallback without it. s_psx_accum implements a fixed 30Hz step (the
 * original's frame rate) decoupled from the render rate. */
/* ROUND 45: real PS1 VRAM recreation + per-(tpage,clut) baked page
 * cache -- see tools/texparse/psx_vram.h for the MAP.RRM texture-field
 * breakthrough this feeds on. Loaded via --texdir <dir with TEX*.TMS>. */
static PsxVram s_vram;
static int s_vram_loaded = 0;
typedef struct { uint16_t tpage, clut; uint32_t *rgba; } VramPageCacheEnt;
static VramPageCacheEnt s_vram_cache[192];
static int s_vram_cache_n = 0;

static const uint32_t *vram_page_get(uint16_t tpage, uint16_t clut)
{
    int i;
    for (i = 0; i < s_vram_cache_n; i++)
        if (s_vram_cache[i].tpage == tpage && s_vram_cache[i].clut == clut)
            return s_vram_cache[i].rgba;
    if (s_vram_cache_n >= (int)(sizeof(s_vram_cache) / sizeof(s_vram_cache[0])))
        return NULL;
    {
        uint32_t *pg = (uint32_t *)malloc(256 * 256 * sizeof(uint32_t));
        if (pg == NULL) return NULL;
        psx_vram_bake_page(&s_vram, tpage, clut, pg);
        s_vram_cache[s_vram_cache_n].tpage = tpage;
        s_vram_cache[s_vram_cache_n].clut = clut;
        s_vram_cache[s_vram_cache_n].rgba = pg;
        s_vram_cache_n++;
        return pg;
    }
}

/* ROUND 51: OBJ.RRO car models in the port. Corrected directory
 * layout (round 50): 6 int16 prim counts at +0x0..+0xA (sizes
 * 40/48/32/64/72/56) + data pointer slot at +0xC (computed at load
 * here, like func_80012670 does in-place). Type-64 prims = car body:
 * 4 model verts + 4 Q12 normals + the standard 16-byte texture tail. */
static uint8_t *s_obj_buf = NULL;
static long s_obj_size = 0;
static uint32_t s_obj_count = 0;
static uint32_t *s_obj_data_off = NULL; /* [count] absolute offsets */
static double s_car_model_scale = 1.0;
/* camera ground sample, hoisted to file scope (round 52 -- the drone
 * spawner in the selfdrive loop needs it too) */
static double s_ground_y = 0.0;
static int s_ground_seeded = 0;

/* ROUND 52: N-car draw list (player + opponents).
 * ROUND 53: opponents are now the real AI (psx_ai.c, ported from
 * func_80025268 + callees); `model` is a MODEL ID (0..12) resolved
 * through the D_80059228 piece-kit table when it's loaded, and the
 * cars carry the authentic corner-lean angle (+0x28). */
typedef struct {
    int on;
    double x, y, z;
    int32_t heading, wheel, roll;
    int model;
    int y_seeded; /* per-car ground continuity (see ground_sample_at) */
} CarDraw;
#define MAX_DRAW_CARS 13
static CarDraw s_draw_cars[MAX_DRAW_CARS];
/* legacy aliases used by the older single-car code paths */
#define s_car_draw_on (s_draw_cars[0].on)
#define s_car_draw_x (s_draw_cars[0].x)
#define s_car_draw_y (s_draw_cars[0].y)
#define s_car_draw_z (s_draw_cars[0].z)
#define s_car_draw_heading (s_draw_cars[0].heading)
#define s_car_draw_wheel (s_draw_cars[0].wheel)
#define s_car_draw_model (s_draw_cars[0].model)

static int obj_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint32_t i, off, cur;
    if (!f) return 0;
    fseek(f, 0, SEEK_END); s_obj_size = ftell(f); fseek(f, 0, SEEK_SET);
    s_obj_buf = (uint8_t *)malloc((size_t)s_obj_size);
    if (!s_obj_buf || fread(s_obj_buf, 1, (size_t)s_obj_size, f) != (size_t)s_obj_size) {
        fclose(f); free(s_obj_buf); s_obj_buf = NULL; return 0;
    }
    fclose(f);
    s_obj_count = (uint32_t)s_obj_buf[0] | ((uint32_t)s_obj_buf[1] << 8) |
                  ((uint32_t)s_obj_buf[2] << 16) | ((uint32_t)s_obj_buf[3] << 24);
    s_obj_data_off = (uint32_t *)malloc(s_obj_count * sizeof(uint32_t));
    if (!s_obj_data_off) return 0;
    off = 4; cur = 4 + s_obj_count * 16;
    for (i = 0; i < s_obj_count; i++) {
        static const int psz[6] = { 40, 48, 32, 64, 72, 56 };
        int k; long sz = 0;
        s_obj_data_off[i] = cur;
        for (k = 0; k < 6; k++) {
            int16_t c = (int16_t)(s_obj_buf[off + k * 2] |
                                  (s_obj_buf[off + k * 2 + 1] << 8));
            sz += (long)c * psz[k];
        }
        cur += (uint32_t)sz;
        off += 16;
    }
    /* ROUND 53 -- model scale CONFIRMED by tracing func_800129AC: the
     * GTE translation is the camera-relative world position SHIFTED
     * LEFT BY 2 (out+0x2C/0x30/0x34 = pos<<2) while model vertices
     * pass through 1:1, so one model unit = 1/4 world-position unit.
     * Cross-check: the kit table stores the rear-axle offset both
     * ways -- -83 world units (+0x8) vs -335 model units (+0xA),
     * ratio 4.04. Our empirical 0.30 (round 51) was this 0.25 seen
     * through a slightly-off bbox guess. */
    s_car_model_scale = 0.25;
    printf("--objfile: %u objects, %ld bytes (closes at %u)\n",
           s_obj_count, s_obj_size, cur);
    return cur == (uint32_t)s_obj_size;
}

static int16_t obj_cnt(uint32_t oi, int type)
{
    uint32_t off = 4 + oi * 16;
    return (int16_t)(s_obj_buf[off + type * 2] |
                     (s_obj_buf[off + type * 2 + 1] << 8));
}

/* ROUND 53: the real opponents. Stepped once per 30Hz physics frame
 * from BOTH the interactive loop and the selfdrive capture, then
 * mirrored into the draw list slots 1..PSX_AI_SLOTS. */
static PsxAiCar s_ai_cars[PSX_AI_SLOTS];
static int s_ai_ready = 0;
static int s_hud_lap = 0; /* player lap (ROUND 61: hoisted here so the
                             position calc below can see it) */
static int s_race_pos = 12;    /* live position, 1..12 */
static int s_race_over = 0;    /* set when the player finishes lap 3 */
static int s_title_active = 1; /* ROUND 61: interactive boot state --
                                  title screen until Enter (needs VRAM) */
static int s_title_frame = 0;
static int s_select_active = 0; /* ROUND 62: car-select state (after
                                   title, before the race) */

static void ai_step_and_fill(PsxBridge *br, double player_progress,
                             int obj_ready)
{
    int di;
    if (!s_ai_ready) {
        for (di = 0; di < PSX_AI_SLOTS; di++)
            psx_ai_init(&s_ai_cars[di], di, br);
        s_ai_ready = 1;
    }
    for (di = 0; di < PSX_AI_SLOTS && 1 + di < MAX_DRAW_CARS; di++) {
        PsxAiCar *ac = &s_ai_cars[di];
        CarDraw *cd = &s_draw_cars[1 + di];
        psx_ai_frame(ac, br, player_progress);
        cd->on = obj_ready;
        cd->x = ac->x;
        cd->z = ac->z;
        cd->y = s_ground_y; /* camera-deck sample; wrong on far slopes
                               (known limitation, see ROADMAP) */
        cd->heading = ac->heading;
        cd->roll = ac->roll;
        cd->wheel = ac->wheel;
        cd->model = ac->model;
    }
    /* ROUND 61: live race position -- total progress (laps * course +
     * section) player vs each active AI. The AI slots 5-10 start most
     * of a lap ahead (rolling field), which is why you start P12. */
    {
        int nsec = (int)br->td.count;
        double ptotal = (double)s_hud_lap * nsec + player_progress;
        int ahead = 0;
        for (di = 0; di < PSX_AI_SLOTS; di++) {
            const PsxAiCar *ac2 = &s_ai_cars[di];
            double atotal;
            if (!ac2->active)
                continue;
            atotal = (double)ac2->lap * nsec + ac2->progress
                     + psx_ai_setup_loaded * 0.0;
            /* the strung-out slots grid ahead WITHOUT a lap credit --
             * their start_rel already places them ahead in progress */
            if (atotal > ptotal)
                ahead++;
        }
        s_race_pos = 1 + ahead;
        if (s_hud_lap >= 3)
            s_race_over = 1;
    }
}


/* ROUND 54: race HUD -- speed / gear / lap, PS1-arcade style (big
 * yellow digits with a hard drop shadow, top-right km/h readout like
 * the original layout). Typography is a built-in 4x6 microfont for
 * now (PLACEHOLDER -- the real TEX0 HUD sprite sheet is a future
 * round); the VALUES are the authentic ones: km/h = speed * 33/100
 * (0x2C3 top speed ~ 232 km/h, matching the original's readout),
 * gear from the authentic gearbox, lap from the section wrap. */
static const uint8_t k_hudfont[16][6] = {
    { 0xF, 0x9, 0x9, 0x9, 0x9, 0xF }, /* 0 */
    { 0x2, 0x6, 0x2, 0x2, 0x2, 0x7 }, /* 1 */
    { 0xF, 0x1, 0xF, 0x8, 0x8, 0xF }, /* 2 */
    { 0xF, 0x1, 0x7, 0x1, 0x1, 0xF }, /* 3 */
    { 0x9, 0x9, 0xF, 0x1, 0x1, 0x1 }, /* 4 */
    { 0xF, 0x8, 0xF, 0x1, 0x1, 0xF }, /* 5 */
    { 0xF, 0x8, 0xF, 0x9, 0x9, 0xF }, /* 6 */
    { 0xF, 0x1, 0x2, 0x2, 0x4, 0x4 }, /* 7 */
    { 0xF, 0x9, 0xF, 0x9, 0x9, 0xF }, /* 8 */
    { 0xF, 0x9, 0xF, 0x1, 0x1, 0xF }, /* 9 */
    { 0x9, 0xA, 0xC, 0xC, 0xA, 0x9 }, /* K */
    { 0x9, 0xF, 0xF, 0x9, 0x9, 0x9 }, /* M */
    { 0x9, 0x9, 0xF, 0x9, 0x9, 0x9 }, /* H */
    { 0xF, 0x8, 0x8, 0x8, 0x8, 0xF }, /* C (unused) */
    { 0x8, 0x8, 0x8, 0x8, 0x8, 0xF }, /* L */
    { 0x0, 0x0, 0x0, 0x6, 0x6, 0x0 }, /* . */
};

static void hud_glyph(int gx, int gy, int glyph, int scale, uint32_t col)
{
    int r, c, sy, sx;
    for (r = 0; r < 6; r++)
        for (c = 0; c < 4; c++) {
            if (!(k_hudfont[glyph][r] & (8 >> c)))
                continue;
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++) {
                    int px = gx + c * scale + sx, py = gy + r * scale + sy;
                    if (px >= 0 && px < GPU_FB_WIDTH && py >= 0 && py < GPU_FB_HEIGHT)
                        gpu_framebuffer[py * GPU_FB_WIDTH + px] = col;
                }
        }
}

/* ROUND 55: REAL glyphs -- TEX0 page 0 is the game's HUD sprite
 * sheet ("winner", "1st/2nd/3rd", "TIME IS UP", the tachometer, and
 * several digit fonts). It loads at VRAM x=320 (halfwords), so the
 * 4bpp indices are read straight out of the recreated VRAM; the
 * medium digit row lives at y=152..175 with the per-digit x-runs
 * measured below. The glyph SHAPES are the authentic sprites; only
 * the tint is ours until the HUD CLUT id is traced. */
static int hud_tex_texel(int u, int v)
{
    uint16_t hw;
    if (!s_vram_loaded || u < 0 || u > 255 || v < 0 || v > 255)
        return 0;
    hw = s_vram.hw[v * PSX_VRAM_W + 320 + (u >> 2)];
    return (hw >> ((u & 3) * 4)) & 0xF;
}

static const struct { int x0, x1; } k_hud_digit[10] = {
    { 4, 20 }, { 30, 39 }, { 51, 68 }, { 75, 92 }, { 100, 117 },
    { 125, 142 }, { 149, 166 }, { 173, 190 }, { 197, 213 }, { 221, 237 }
};
#define HUD_DIGIT_Y0 152
#define HUD_DIGIT_Y1 175

/* ROUND 56: the HUD digit CLUT is 0x7EC8 -- found by scoring every
 * CLUT row TEX0 declares against the digit-row texels: it decodes
 * them to the authentic chrome-silver gradient of the original speed
 * readout (runner-up 0x7F82 is a flatter grey; 0x7802 a yellow/red
 * variant, likely the highlight state). Page 0 sits at VRAM x=320 ->
 * tpage id 5. Full-color path below; the mask+tint fallback stays
 * for when the page cache can't bake. */
#define HUD_TPAGE 5
#define HUD_CLUT 0x7EC8
/* ROUND 60: the yellow/red CLUT variant 0x7802 (found in the round-56
 * scoring) is the REDLINE highlight -- applied to the km/h digits
 * when the authentic rpm sits in the dial's red zone (>= 9000). */
#define HUD_CLUT_HOT 0x7802
static uint16_t s_hud_digit_clut = HUD_CLUT;

/* Draws one authentic digit glyph, integer-scaled; returns advance. */
static int hud_sprite_digit(int dst_x, int dst_y, int digit, int scale,
                            uint32_t col)
{
    int u, v, sy, sx;
    int w = k_hud_digit[digit].x1 - k_hud_digit[digit].x0 + 1;
    const uint32_t *pg = s_vram_loaded
        ? vram_page_get(HUD_TPAGE, s_hud_digit_clut) : NULL;
    for (v = HUD_DIGIT_Y0; v <= HUD_DIGIT_Y1; v++)
        for (u = k_hud_digit[digit].x0; u <= k_hud_digit[digit].x1; u++) {
            uint32_t texcol = col;
            if (!hud_tex_texel(u, v))
                continue;
            if (pg != NULL) {
                uint32_t t = pg[v * 256 + u];
                if ((t & 0xFF000000u) == 0)
                    continue;
                /* page cache is ABGR (R low byte) -- swap to the
                 * framebuffer's RGB layout */
                texcol = ((t & 0xFFu) << 16) | (t & 0xFF00u) |
                         ((t >> 16) & 0xFFu);
            }
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++) {
                    int px = dst_x + (u - k_hud_digit[digit].x0) * scale + sx;
                    int py = dst_y + (v - HUD_DIGIT_Y0) * scale + sy;
                    if (px >= 0 && px < GPU_FB_WIDTH && py >= 0 && py < GPU_FB_HEIGHT)
                        gpu_framebuffer[py * GPU_FB_WIDTH + px] = texcol;
                }
        }
    return (w + 3) * scale;
}

static void hud_number(int x, int y, int value, int scale, uint32_t col)
{
    char buf[12];
    int i, n = 0;
    if (value < 0) value = 0;
    do { buf[n++] = (char)(value % 10); value /= 10; } while (value && n < 11);
    if (s_vram_loaded) {
        /* authentic TEX0 sprite digits (scale halved: they are 24px
         * tall natively where the microfont was 6) */
        int sc = scale >= 2 ? scale / 2 : 1;
        int gx = x;
        for (i = n - 1; i >= 0; i--)
            gx += hud_sprite_digit(gx, y, buf[i], sc, col);
        return;
    }
    for (i = 0; i < n; i++) {
        int gx = x + (n - 1 - i) * (5 * scale);
        hud_glyph(gx + scale, y + scale, buf[i], scale, 0xFF000000u); /* shadow */
        hud_glyph(gx, y, buf[i], scale, col);
    }
}

/* ROUND 57: the tachometer -- the dial face (ticks, digits, red
 * zone, "X1000 R/MIN") lives on the same TEX0 HUD page at
 * (60,64)-(152,151); it decodes cleanly with the page's own declared
 * CLUT 0x7984. The needle is drawn programmatically from the hub at
 * ~(105,107): the printed scale runs 0 at the bottom (270 deg,
 * screen) counter-clockwise to 10 at the right (0 deg), so
 * angle = 270 - rpm/0x2710 * 270. */
#define HUD_DIAL_X0 64
#define HUD_DIAL_Y0 73
#define HUD_DIAL_X1 152
#define HUD_DIAL_Y1 151
#define HUD_DIAL_CLUT 0x7984

static void draw_tachometer(int dst_x, int dst_y, int32_t rpm)
{
    const uint32_t *pg = s_vram_loaded ? vram_page_get(HUD_TPAGE, HUD_DIAL_CLUT)
                                       : NULL;
    int u, v;
    double cx, cy, ang, ca, sa2;
    int i;
    if (pg == NULL)
        return;
    for (v = HUD_DIAL_Y0; v <= HUD_DIAL_Y1; v++)
        for (u = HUD_DIAL_X0; u <= HUD_DIAL_X1; u++) {
            uint32_t t = pg[v * 256 + u];
            int px = dst_x + (u - HUD_DIAL_X0);
            int py = dst_y + (v - HUD_DIAL_Y0);
            if ((t & 0xFF000000u) == 0)
                continue;
            if (px >= 0 && px < GPU_FB_WIDTH && py >= 0 && py < GPU_FB_HEIGHT)
                gpu_framebuffer[py * GPU_FB_WIDTH + px] =
                    ((t & 0xFFu) << 16) | (t & 0xFF00u) | ((t >> 16) & 0xFFu);
        }
    /* needle */
    if (rpm < 0) rpm = 0;
    if (rpm > 0x2710) rpm = 0x2710;
    cx = dst_x + (105 - HUD_DIAL_X0);
    cy = dst_y + (107 - HUD_DIAL_Y0);
    ang = (270.0 - (double)rpm / 10000.0 * 270.0) * 3.14159265358979 / 180.0;
    ca = cos(ang);
    sa2 = -sin(ang); /* screen y-down */
    for (i = 4; i < 40; i++) {
        int px = (int)(cx + ca * i);
        int py = (int)(cy + sa2 * i);
        int t2;
        for (t2 = 0; t2 < 2; t2++) {
            int qx = px, qy = py + t2;
            if (qx >= 0 && qx < GPU_FB_WIDTH && qy >= 0 && qy < GPU_FB_HEIGHT)
                gpu_framebuffer[qy * GPU_FB_WIDTH + qx] = 0x00FF6020;
        }
    }
}

/* ROUND 61: generic VRAM sprite blit (any tpage/clut/rect), used by
 * the race-position ordinals, the finish overlay and the boot title
 * screen. Transparent texels (alpha 0 in the baked page) skip. */
static void hud_sprite_blit(int dst_x, int dst_y, uint16_t tpage,
                            uint16_t clut, int u0, int v0, int u1,
                            int v1, int scale)
{
    const uint32_t *pg = s_vram_loaded ? vram_page_get(tpage, clut) : NULL;
    int u, v, sy, sx;
    if (pg == NULL)
        return;
    for (v = v0; v <= v1; v++)
        for (u = u0; u <= u1; u++) {
            uint32_t t = pg[(v & 255) * 256 + (u & 255)];
            uint32_t col;
            if ((t & 0xFF000000u) == 0)
                continue;
            col = ((t & 0xFFu) << 16) | (t & 0xFF00u) | ((t >> 16) & 0xFFu);
            for (sy = 0; sy < scale; sy++)
                for (sx = 0; sx < scale; sx++) {
                    int px = dst_x + (u - u0) * scale + sx;
                    int py = dst_y + (v - v0) * scale + sy;
                    if (px >= 0 && px < GPU_FB_WIDTH && py >= 0 && py < GPU_FB_HEIGHT)
                        gpu_framebuffer[py * GPU_FB_WIDTH + px] = col;
                }
        }
}

/* Result/ordinal sprites on the HUD sheet (page 0 = tpage 5), rects
 * measured round 61: cursive "winner" plus the italic 1st/2nd/3rd and
 * the bare "th" ordinal for positions 4+. */
#define SPR_WINNER 144, 24, 246, 57
#define SPR_1ST 186, 55, 255, 90
#define SPR_2ND 150, 88, 255, 120
#define SPR_3RD 150, 118, 255, 150
#define SPR_TH 160, 58, 186, 90

/* ROUND 61: the boot TITLE SCREEN, in-engine -- the real "RIDGE
 * RACER" logo (TEX3 page 36, 240x96 at VRAM (704,256) -> tpage 0x1B,
 * its dedicated CLUT 0x7E8C, decoded rounds 38-39) over a night
 * gradient, with a pulsing PRESS-START line. Shown at boot in the
 * interactive build (Enter starts the race) and as the intro of
 * selfdrive captures. */
#define TITLE_TPAGE 0x1B
#define TITLE_CLUT 0x7E8C

static void draw_title_screen(int frame)
{
    int x, y;
    for (y = 0; y < GPU_FB_HEIGHT; y++) {
        int shade = 12 + y * 40 / GPU_FB_HEIGHT;
        uint32_t c = ((uint32_t)(shade / 3) << 16) |
                     ((uint32_t)(shade / 2) << 8) | (uint32_t)shade;
        for (x = 0; x < GPU_FB_WIDTH; x++)
            gpu_framebuffer[y * GPU_FB_WIDTH + x] = c;
    }
    hud_sprite_blit((GPU_FB_WIDTH - 240) / 2, 48, TITLE_TPAGE, TITLE_CLUT,
                    0, 0, 239, 95, 1);
    if ((frame / 20) & 1) {
        /* the real START-BUTTON prompt strip (TEX3 page 37, 72x16 at
         * VRAM (704,352) -> same tpage, v offset 96) */
        hud_sprite_blit((GPU_FB_WIDTH - 96) / 2, 176, TITLE_TPAGE,
                        TITLE_CLUT, 72, 96, 119, 111, 2);
    }
    (void)frame;
}

/* ROUND 62: the CAR SELECT screen -- the chosen model's real OBJ.RRO
 * body (type-64 prims: verts + Q12 normals + texture tail) spinning
 * on a dark stage, front axle included. Left/right cycles the 12
 * consumer models, Enter races it. Painter-sorted quads, same z-
 * mirror and 0.25 GTE scale conventions as the in-race car path. */
static int s_select_model = 0;

typedef struct { double depth; int x[4], y[4]; const uint32_t *pg;
                 float u[4], v[4]; uint32_t mod; } SelQuad;

static void draw_car_select(int frame, int model)
{
    static SelQuad q[512];
    int nq = 0, x, y, k, pi;
    double yaw = (double)(frame % 360) * 3.14159265358979 / 90.0;
    double cy = cos(yaw), sy = sin(yaw);
    if (s_obj_buf == NULL || !psx_ai_kit_loaded)
        return;
    for (y = 0; y < GPU_FB_HEIGHT; y++) {
        int shade = 8 + (GPU_FB_HEIGHT - y) * 22 / GPU_FB_HEIGHT;
        uint32_t c = ((uint32_t)(shade / 2) << 16) |
                     ((uint32_t)(shade / 2) << 8) | (uint32_t)shade;
        for (x = 0; x < GPU_FB_WIDTH; x++)
            gpu_framebuffer[y * GPU_FB_WIDTH + x] = c;
    }
    for (pi = 0; pi < 3; pi++) {
        const PsxCarKit *kt = &psx_ai_kit[model];
        int obj = pi == 0 ? kt->body_obj : kt->axle_obj;
        int dz = pi == 2 ? kt->axle_dz_model : 0;
        int16_t n64;
        uint32_t base, o;
        if (obj < 0 || (uint32_t)obj >= s_obj_count)
            continue;
        base = s_obj_data_off[obj];
        n64 = obj_cnt((uint32_t)obj, 3);
        o = base + (uint32_t)obj_cnt(obj, 0) * 40u
                 + (uint32_t)obj_cnt(obj, 1) * 48u
                 + (uint32_t)obj_cnt(obj, 2) * 32u;
        for (k = 0; k < n64 && nq < 512; k++) {
            const uint8_t *rec = s_obj_buf + o + (uint32_t)k * 64u;
            uint16_t tail[8];
            int c2, t;
            SelQuad *sq = &q[nq];
            double avg = 0.0;
            int16_t nx0 = (int16_t)(rec[24] | (rec[25] << 8));
            int16_t ny0 = (int16_t)(rec[26] | (rec[27] << 8));
            int16_t nz0 = (int16_t)-(int16_t)(rec[28] | (rec[29] << 8));
            double wnx = (nz0 / 4096.0) * cy - (nx0 / 4096.0) * sy;
            double d = wnx * -0.5 + (ny0 / 4096.0) * -0.8;
            int sh = (int)(140.0 + 115.0 * (d > 0 ? d : 0));
            if (sh > 255) sh = 255;
            sq->mod = ((uint32_t)sh << 16) | ((uint32_t)sh << 8) | (uint32_t)sh;
            for (t = 0; t < 8; t++)
                tail[t] = (uint16_t)(rec[48 + t * 2] | (rec[49 + t * 2] << 8));
            sq->pg = s_vram_loaded ? vram_page_get(tail[3], tail[1]) : NULL;
            for (c2 = 0; c2 < 4; c2++) {
                int vi = (c2 == 0) ? 0 : (c2 == 1) ? 1 : (c2 == 2) ? 3 : 2;
                int uvi = (c2 == 3) ? 2 : (c2 == 2) ? 3 : c2;
                int16_t mx = (int16_t)(rec[vi * 6] | (rec[vi * 6 + 1] << 8));
                int16_t my = (int16_t)(rec[vi * 6 + 2] | (rec[vi * 6 + 3] << 8));
                int16_t mz = (int16_t)(rec[vi * 6 + 4] | (rec[vi * 6 + 5] << 8));
                double pmz = -(double)mz + dz, pmy = (double)my, pmx = (double)mx;
                double rx = pmz * cy - pmx * sy;
                double rz = pmz * sy + pmx * cy;
                double depth = 1600.0 + rz;
                sq->x[c2] = GPU_FB_WIDTH / 2 + (int)(rx * 340.0 / depth);
                sq->y[c2] = 150 + (int)((pmy + 130.0) * 340.0 / depth);
                sq->u[c2] = (float)(tail[uvi * 2] & 0xFF) / 255.0f;
                sq->v[c2] = (float)(tail[uvi * 2] >> 8) / 255.0f;
                avg += depth;
            }
            sq->depth = avg / 4.0;
            nq++;
        }
    }
    /* painter sort, far first */
    for (k = 0; k < nq; k++) {
        int m2 = k, j2;
        SelQuad tmp;
        for (j2 = k + 1; j2 < nq; j2++)
            if (q[j2].depth > q[m2].depth) m2 = j2;
        tmp = q[k]; q[k] = q[m2]; q[m2] = tmp;
        if (q[k].pg != NULL)
            gpu_draw_quad_textured_mod(q[k].x[0], q[k].y[0], q[k].u[0], q[k].v[0],
                                       q[k].x[1], q[k].y[1], q[k].u[1], q[k].v[1],
                                       q[k].x[2], q[k].y[2], q[k].u[2], q[k].v[2],
                                       q[k].x[3], q[k].y[3], q[k].u[3], q[k].v[3],
                                       q[k].pg, 256, 256, q[k].mod);
    }
    /* header: CAR + number */
    hud_number(GPU_FB_WIDTH / 2 - 10, 16, model + 1, 4, 0x00FFD830);
}

static int hud_number_width(int value, int scale)
{
    int w = 0, n = 0;
    char buf[12];
    if (value < 0) value = 0;
    do { buf[n++] = (char)(value % 10); value /= 10; } while (value && n < 11);
    if (s_vram_loaded) {
        int sc = scale >= 2 ? scale / 2 : 1, i;
        for (i = 0; i < n; i++)
            w += (k_hud_digit[(int)buf[i]].x1 -
                  k_hud_digit[(int)buf[i]].x0 + 4) * sc;
        return w;
    }
    return n * 5 * scale;
}

static void draw_race_hud(int32_t speed_raw, int gear, int lap, int32_t rpm)
{
    /* speed, top-right, big yellow -- kmh = speed*33/100 (authentic
     * ~232 km/h ceiling); digits are the real TEX0 HUD sprites when
     * the texture banks are loaded (see hud_sprite_digit). */
    int kmh = (int)((long)speed_raw * 33 / 100);
    uint32_t yellow = 0x00FFD830, white = 0x00F0F0F0;
    int sx = GPU_FB_WIDTH - 12 - hud_number_width(kmh, 4);
    s_hud_digit_clut = rpm >= 9000 ? HUD_CLUT_HOT : HUD_CLUT;
    hud_number(sx, 10, kmh, 4, yellow);
    s_hud_digit_clut = HUD_CLUT;
    /* "KMH" tag under it */
    hud_glyph(GPU_FB_WIDTH - 14 - 3 * 10, 62, 10, 2, white);
    hud_glyph(GPU_FB_WIDTH - 14 - 2 * 10, 62, 11, 2, white);
    hud_glyph(GPU_FB_WIDTH - 14 - 1 * 10, 62, 12, 2, white);
    /* gear, bottom-right, white */
    hud_number(GPU_FB_WIDTH - 14 - hud_number_width(gear, 4),
               GPU_FB_HEIGHT - 40, gear, 4, white);
    /* lap counter "L n", top-left */
    hud_glyph(12, 12, 14, 3, white);
    hud_number(12 + 18, 10, lap, 3, yellow);
    /* ROUND 57: the real tachometer, bottom-left like the original */
    draw_tachometer(8, GPU_FB_HEIGHT - 96, rpm);
    /* ROUND 61: live race position, RR style -- big digit + ordinal
     * sprite, above the gear readout. */
    if (s_race_pos >= 1 && s_race_pos <= 12) {
        int px = GPU_FB_WIDTH - 110;
        int py = GPU_FB_HEIGHT - 100;
        if (s_race_pos == 1)
            hud_sprite_blit(px, py, HUD_TPAGE, HUD_CLUT, SPR_1ST, 1);
        else if (s_race_pos == 2)
            hud_sprite_blit(px, py, HUD_TPAGE, HUD_CLUT, SPR_2ND, 1);
        else if (s_race_pos == 3)
            hud_sprite_blit(px, py, HUD_TPAGE, HUD_CLUT, SPR_3RD, 1);
        else
            hud_number(px, py, s_race_pos, 3, yellow);
    }
    /* ROUND 61: finish overlay -- the real result sprites. */
    if (s_race_over) {
        if (s_race_pos == 1) {
            hud_sprite_blit(60, 60, HUD_TPAGE, HUD_CLUT, SPR_WINNER, 2);
            hud_sprite_blit(90, 140, HUD_TPAGE, HUD_CLUT, SPR_1ST, 2);
        } else if (s_race_pos == 2) {
            hud_sprite_blit(50, 90, HUD_TPAGE, HUD_CLUT, SPR_2ND, 2);
        } else if (s_race_pos == 3) {
            hud_sprite_blit(50, 90, HUD_TPAGE, HUD_CLUT, SPR_3RD, 2);
        } else {
            hud_number(110, 90, s_race_pos, 6, yellow);
        }
    }
}

static int s_selfdrive_frames = 0;
static const char *s_selfdrive_prefix = NULL;
static PsxCar s_psx_car;
static PsxBridge s_psx_bridge;
static PsxTrackIface s_psx_iface;
static int s_psx_active = 0;
static double s_psx_accum = 0.0;
/* Persists across HUD updates so physics_find_section_local_walk (round
 * 11 -- see physics.h) can do its cheap incremental walk instead of a
 * fresh O(n) search every time; -1 means "not seeded yet", which makes
 * the first call cold-start via physics_find_nearest_section. */
static int s_physics_section_index = -1;

static void track_view_reset_top(void) {
    s_track_zoom = 1.0;
    s_track_pan_x = 0.0;
    s_track_pan_z = 0.0;
}


/* ROUND 59: the CONFIRMED frame conversion. idx_hed now returns the
 * game's MESH-frame cell origin ((30 - file_col)*2048, row*2048); our
 * whole port (physics, cars, camera) lives in the trackdata/physics
 * frame, which the game itself maps as x_phys = 61440 - x_mesh
 * (D_801733A0 = 0xF000, func_800181C8/func_80015CD4). So every MAP
 * vert converts as PHYS_X = 61440 - (origin + vx); z passes through.
 * Validated on the 12 real grid anchors + 232/256 section centers
 * (see idx_hed.c). */
#define MAP_PHYS_X(ox, vx) (61440.0 - (double)(ox) - (double)(vx))

/* ROUND 53: generic deck sampler (extracted from the round-46 camera
 * ground sampler): point-in-quad over the real-road B records, deck
 * disambiguation by continuity against *prev when seeded, nearest-
 * corner fallback. Returns the smoothed deck height. */
static double ground_sample_at(const TrackDemo *td, double smp_x,
                               double smp_z, double prev, int *seeded)
{
    const MapRrmFile *mf = &td->mf;
    const IdxHedFile *idxf = &td->idxf;
    double best_contained = 1e30, contained_y = 0.0;
    int have_contained = 0;
    double best_near = 1e30, near_y = prev;
    size_t rr;
    for (rr = 0; rr < mf->record_count; rr++) {
        const MapRrmTaggedRecord *tr2 = &mf->records[rr];
        int32_t ox2, oz2;
        double qx[4], qz[4], qy;
        double dx2, dz2, d2;
        int c2, pos = 0, neg = 0;
        if (tr2->type != MAP_RRM_RECORD_TYPE_B) continue;
        /* ROUND 54: no section filter (see the draw loop note) */
        if (idx_hed_section_world_origin(idxf, tr2->section_index, &ox2, &oz2) != IDX_HED_OK) continue;
        qx[0] = MAP_PHYS_X(ox2, tr2->rec.v0[0]); qz[0] = oz2 + tr2->rec.v0[2];
        qx[1] = MAP_PHYS_X(ox2, tr2->rec.v1[0]); qz[1] = oz2 + tr2->rec.v1[2];
        qx[2] = MAP_PHYS_X(ox2, tr2->rec.v3[0]); qz[2] = oz2 + tr2->rec.v3[2];
        qx[3] = MAP_PHYS_X(ox2, tr2->rec.v2[0]); qz[3] = oz2 + tr2->rec.v2[2];
        qy = ((double)tr2->rec.v0[1] + tr2->rec.v1[1] +
              tr2->rec.v2[1] + tr2->rec.v3[1]) * 0.25;
        dx2 = qx[0] - smp_x; dz2 = qz[0] - smp_z;
        d2 = dx2 * dx2 + dz2 * dz2;
        if (*seeded) {
            double dy2 = qy - prev;
            d2 += dy2 * dy2 * 16.0;
        }
        if (d2 < best_near) { best_near = d2; near_y = qy; }
        for (c2 = 0; c2 < 4; c2++) {
            int n2 = (c2 + 1) & 3;
            double cr = (qx[n2] - qx[c2]) * (smp_z - qz[c2])
                      - (qz[n2] - qz[c2]) * (smp_x - qx[c2]);
            if (cr >= 0) pos++;
            if (cr <= 0) neg++;
        }
        if (pos == 4 || neg == 4) {
            double dy2 = *seeded ? (qy - prev) : 0.0;
            double score = dy2 * dy2;
            if (score < best_contained) {
                best_contained = score;
                contained_y = qy;
                have_contained = 1;
            }
        }
    }
    {
        double target = have_contained ? contained_y : near_y;
        if (!*seeded) { *seeded = 1; return target; }
        return prev + (target - prev) * 0.35;
    }
}

/* Pass over the same record set draw_track_demo_scene()/
 * draw_track_drive_scene() draw (real-road sections, type B only) to
 * get a world-space bounding box -- shared so the top-down auto-fit
 * and the drive camera's spawn point agree on what "the track" spans. */
static int track_demo_world_bbox(const TrackDemo *td, double *ominx, double *omaxx,
                                  double *ominz, double *omaxz) {
    const MapRrmFile *mf = &td->mf;
    const IdxHedFile *idxf = &td->idxf;
    double minx = 1e18, maxx = -1e18, minz = 1e18, maxz = -1e18;
    size_t r;

    for (r = 0; r < mf->record_count; r++) {
        const MapRrmTaggedRecord *tr = &mf->records[r];
        int32_t ox, oz;
        int c;
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (!track_demo_is_real_road_section(mf, tr->section_index)) continue;
        if (idx_hed_section_world_origin(idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;
        for (c = 0; c < 4; c++) {
            const int16_t *v = (c == 0) ? tr->rec.v0 : (c == 1) ? tr->rec.v1 : (c == 2) ? tr->rec.v2 : tr->rec.v3;
            double wx = MAP_PHYS_X(ox, v[0]), wz = oz + v[2];
            if (wx < minx) minx = wx;
            if (wx > maxx) maxx = wx;
            if (wz < minz) minz = wz;
            if (wz > maxz) maxz = wz;
        }
    }
    if (maxx <= minx || maxz <= minz) return 0;
    *ominx = minx; *omaxx = maxx; *ominz = minz; *omaxz = maxz;
    return 1;
}

/* Spawns the drive camera at the track's bounding-box center, facing
 * +Z, at a fixed default height. Falls back to the origin if the track
 * isn't loaded/ready (e.g. called before load_demo_track succeeds). */
static void track_view_reset_drive(const TrackDemo *td) {
    double minx, maxx, minz, maxz;
    s_cam_yaw = 0.0;
    s_cam_height = 300.0;
    if (td != NULL && td->ready && track_demo_world_bbox(td, &minx, &maxx, &minz, &maxz)) {
        s_cam_x = (minx + maxx) / 2.0;
        s_cam_z = (minz + maxz) / 2.0;
    } else {
        s_cam_x = 0.0;
        s_cam_z = 0.0;
    }
}

static void track_view_reset(const TrackDemo *td) {
    track_view_reset_top();
    track_view_reset_drive(td);
}

/* Autopilot: flies the drive camera around a lap automatically, using
 * the round-10 confirmed finding that MAP.RRM's section index order IS
 * the real track traversal order (the rainbow-gradient render proved
 * this -- a single, perfectly continuous loop with zero scrambling).
 * The path is just each real-road section's centroid, visited in
 * section-index order and looped back to the start; no new reverse
 * engineering needed, this is built entirely from already-confirmed
 * data. Toggled with P; any manual direction key while it's running
 * hands control back to the player. */
#define TRACK_AUTOPILOT_MAX_WAYPOINTS 2048
static double s_autopilot_wp_x[TRACK_AUTOPILOT_MAX_WAYPOINTS];
static double s_autopilot_wp_z[TRACK_AUTOPILOT_MAX_WAYPOINTS];
/* Per-waypoint speed multiplier derived from local path curvature --
 * see the comment inside track_view_build_autopilot_path below for how
 * it's computed. 1.0 = full speed (straight), down to
 * TRACK_AUTOPILOT_MIN_SPEED_SCALE (sharpest hairpin). */
static double s_autopilot_wp_speed_scale[TRACK_AUTOPILOT_MAX_WAYPOINTS];
static int s_autopilot_wp_count = 0;
static int s_autopilot_on = 0;
static double s_autopilot_t = 0.0;         /* position along the path, in waypoint units */
static const double s_autopilot_base_speed = 0.5; /* waypoints per second, on a dead-straight stretch */
#define TRACK_AUTOPILOT_MIN_SPEED_SCALE 0.35 /* floor speed multiplier through the sharpest hairpins */

/* Returns waypoint index `i` modulo the (looped) waypoint count,
 * handling negative `i` correctly (needed for the Catmull-Rom "point
 * before the segment start" sample, and for the curvature lookbehind
 * below). Declared ahead of track_view_build_autopilot_path because
 * that function now uses it too (curvature precompute). */
static int autopilot_wp_index(int i) {
    int n = s_autopilot_wp_count;
    int m;
    if (n <= 0) return 0;
    m = i % n;
    if (m < 0) m += n;
    return m;
}

static void track_view_build_autopilot_path(const TrackDemo *td) {
    const MapRrmFile *mf = &td->mf;
    const IdxHedFile *idxf = &td->idxf;
    double *sum_x, *sum_z;
    int *count;
    uint16_t s;
    size_t r;
    int i;

    s_autopilot_wp_count = 0;
    if (mf->section_count == 0) return;

    sum_x = (double *)calloc(mf->section_count, sizeof(double));
    sum_z = (double *)calloc(mf->section_count, sizeof(double));
    count = (int *)calloc(mf->section_count, sizeof(int));
    if (sum_x == NULL || sum_z == NULL || count == NULL) {
        free(sum_x); free(sum_z); free(count);
        return;
    }

    for (r = 0; r < mf->record_count; r++) {
        const MapRrmTaggedRecord *tr = &mf->records[r];
        int32_t ox, oz;
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (tr->section_index >= mf->section_count) continue;
        if (idx_hed_section_world_origin(idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;
        sum_x[tr->section_index] += MAP_PHYS_X(ox, tr->rec.v0[0]);
        sum_z[tr->section_index] += oz + tr->rec.v0[2];
        count[tr->section_index]++;
    }

    for (s = 0; s < mf->section_count && s_autopilot_wp_count < TRACK_AUTOPILOT_MAX_WAYPOINTS; s++) {
        if (!track_demo_is_real_road_section(mf, s)) continue;
        if (count[s] == 0) continue;
        s_autopilot_wp_x[s_autopilot_wp_count] = sum_x[s] / count[s];
        s_autopilot_wp_z[s_autopilot_wp_count] = sum_z[s] / count[s];
        s_autopilot_wp_count++;
    }

    free(sum_x); free(sum_z); free(count);

    /* Curvature-based speed precompute (new this round): rounds 12/12b
     * drove the whole lap at one fixed speed, which reads as unnatural
     * now that the Catmull-Rom smoothing (round 12b) makes cornering
     * visually obvious -- a real driving line slows for corners. For
     * each waypoint, compare the incoming heading (wp[i-1] -> wp[i])
     * against the outgoing heading (wp[i] -> wp[i+1]) via their unit
     * dot product: 1.0 = dead straight, -1.0 = a full reversal
     * (tightest possible hairpin). Linearly remapped onto
     * [TRACK_AUTOPILOT_MIN_SPEED_SCALE, 1.0] so the car eases into and
     * out of corners instead of an abrupt per-waypoint speed step --
     * track_view_autopilot_update below blends between two consecutive
     * waypoints' scale by the same `t` used for position, so the speed
     * itself is C0-continuous along the lap. No new reverse engineering
     * here: purely a function of the already-confirmed waypoint
     * centroids from round 10/12. */
    for (i = 0; i < s_autopilot_wp_count; i++) {
        int ip = autopilot_wp_index(i - 1);
        int in = autopilot_wp_index(i + 1);
        double inx = s_autopilot_wp_x[i] - s_autopilot_wp_x[ip];
        double inz = s_autopilot_wp_z[i] - s_autopilot_wp_z[ip];
        double outx = s_autopilot_wp_x[in] - s_autopilot_wp_x[i];
        double outz = s_autopilot_wp_z[in] - s_autopilot_wp_z[i];
        double in_len = sqrt(inx * inx + inz * inz);
        double out_len = sqrt(outx * outx + outz * outz);
        double dot, blend;
        if (in_len < 1e-6 || out_len < 1e-6) {
            s_autopilot_wp_speed_scale[i] = 1.0;
            continue;
        }
        dot = (inx * outx + inz * outz) / (in_len * out_len);
        if (dot > 1.0) dot = 1.0;
        if (dot < -1.0) dot = -1.0;
        blend = (dot + 1.0) * 0.5; /* 0 = hairpin reversal, 1 = dead straight */
        s_autopilot_wp_speed_scale[i] = TRACK_AUTOPILOT_MIN_SPEED_SCALE +
            (1.0 - TRACK_AUTOPILOT_MIN_SPEED_SCALE) * blend;
    }

    printf("autopilot: built a %d-waypoint lap path from real-road section centroids "
           "(curvature-based speed scaling active)\n", s_autopilot_wp_count);
}

/* Advances the camera along the autopilot path by `dt` seconds. No-op
 * if autopilot is off or the path hasn't been built yet. */

/* Catmull-Rom spline through the looped waypoint list -- round 12
 * originally used a straight lerp between consecutive waypoints, which
 * visibly cuts corners where waypoints are sparse (round-10 style
 * section spacing is uneven). This is a drop-in smoother replacement:
 * same waypoint data, same `s_autopilot_t` progress variable, just a
 * curved path through the same points instead of straight segments
 * between them, plus the analytic tangent for a smoothly-turning
 * facing direction instead of a per-segment constant heading. */
static void track_view_autopilot_update(double dt) {
    int i0, i1, im1, i2;
    double t, t2, t3;
    double p_m1x, p0x, p1x, p2x;
    double p_m1z, p0z, p1z, p2z;
    double dx, dz;
    double speed_scale, speed;

    if (!s_autopilot_on || s_autopilot_wp_count < 2) return;

    /* Curvature-scaled speed: blend this waypoint's and the next one's
     * precomputed scale by the current fractional position `t`, so the
     * speed itself ramps smoothly into/out of corners instead of
     * stepping at each waypoint crossing. Uses the position from
     * *before* this frame's advance (last frame's settled s_autopilot_t),
     * same as the position/tangent computation below does after the
     * advance -- one frame of lag on the speed scale is imperceptible. */
    i0 = (int)s_autopilot_t;
    t = s_autopilot_t - (double)i0;
    i1 = autopilot_wp_index(i0 + 1);
    speed_scale = s_autopilot_wp_speed_scale[i0] * (1.0 - t) + s_autopilot_wp_speed_scale[i1] * t;
    speed = s_autopilot_base_speed * speed_scale;

    s_autopilot_t += speed * dt;
    while (s_autopilot_t >= (double)s_autopilot_wp_count) s_autopilot_t -= (double)s_autopilot_wp_count;

    i0 = (int)s_autopilot_t;
    t = s_autopilot_t - (double)i0;
    im1 = autopilot_wp_index(i0 - 1);
    i1 = autopilot_wp_index(i0 + 1);
    i2 = autopilot_wp_index(i0 + 2);

    p_m1x = s_autopilot_wp_x[im1]; p_m1z = s_autopilot_wp_z[im1];
    p0x = s_autopilot_wp_x[i0];    p0z = s_autopilot_wp_z[i0];
    p1x = s_autopilot_wp_x[i1];    p1z = s_autopilot_wp_z[i1];
    p2x = s_autopilot_wp_x[i2];    p2z = s_autopilot_wp_z[i2];

    t2 = t * t;
    t3 = t2 * t;

    /* Standard centripetal-free (uniform) Catmull-Rom position: */
    s_cam_x = 0.5 * ((2.0 * p0x) + (-p_m1x + p1x) * t +
                      (2.0 * p_m1x - 5.0 * p0x + 4.0 * p1x - p2x) * t2 +
                      (-p_m1x + 3.0 * p0x - 3.0 * p1x + p2x) * t3);
    s_cam_z = 0.5 * ((2.0 * p0z) + (-p_m1z + p1z) * t +
                      (2.0 * p_m1z - 5.0 * p0z + 4.0 * p1z - p2z) * t2 +
                      (-p_m1z + 3.0 * p0z - 3.0 * p1z + p2z) * t3);

    /* Analytic derivative (tangent) at the same t -- gives a smoothly
     * rotating facing direction along the curve instead of a heading
     * that snaps at each waypoint. */
    dx = 0.5 * ((-p_m1x + p1x) +
                2.0 * (2.0 * p_m1x - 5.0 * p0x + 4.0 * p1x - p2x) * t +
                3.0 * (-p_m1x + 3.0 * p0x - 3.0 * p1x + p2x) * t2);
    dz = 0.5 * ((-p_m1z + p1z) +
                2.0 * (2.0 * p_m1z - 5.0 * p0z + 4.0 * p1z - p2z) * t +
                3.0 * (-p_m1z + 3.0 * p0z - 3.0 * p1z + p2z) * t2);
    if (dx != 0.0 || dz != 0.0) {
        s_cam_yaw = atan2(dx, dz); /* matches this file's yaw convention: forward = (sin(yaw), cos(yaw)) */
    }
}

/* Top-down render of the real track: one filled quad per type-B (road
 * surface) record, world position = local record vertex + IDX.HED
 * grid-cell anchor (translation-only, no rotation -- see file header
 * comment). `s_track_zoom`==1.0 and no pan fits the whole track on
 * screen preserving aspect ratio; arrow keys/+/- let the user explore
 * closer, since round 10's small residual notches (very-high-record
 * junction sections) are easiest to inspect zoomed in. */
static void draw_track_demo_scene(const TrackDemo *td) {
    const MapRrmFile *mf = &td->mf;
    const IdxHedFile *idxf = &td->idxf;
    double minx, maxx, minz, maxz;
    double fit_scale, scale, cx, cz;
    size_t r;
    int margin = 6;

    gpu_clear(0x00080810);

    /* Pass 1: world-space bbox over the same record set we're about to
     * draw (real-road sections, type B only) -- used both to compute
     * the auto-fit scale and as the default view center. */
    if (!track_demo_world_bbox(td, &minx, &maxx, &minz, &maxz)) {
        return; /* nothing to draw (e.g. IDX.HED had zero occupied cells) */
    }

    fit_scale = (double)(GPU_FB_WIDTH - 2 * margin) / (maxx - minx + 1);
    {
        double scale_z = (double)(GPU_FB_HEIGHT - 2 * margin) / (maxz - minz + 1);
        if (scale_z < fit_scale) fit_scale = scale_z;
    }
    scale = fit_scale * s_track_zoom;
    cx = (minx + maxx) / 2.0 + s_track_pan_x;
    cz = (minz + maxz) / 2.0 + s_track_pan_z;

    /* Pass 2: fill each qualifying type-B record as a solid quad --
     * corners ordered v0,v1,v3,v2 around the perimeter (matches
     * gpu_draw_quad_flat's expected winding and worldmap_main.c's
     * confirmed-correct ordering, avoiding a bowtie). Projection is
     * centered on (cx,cz) rather than the old min-corner+margin
     * convention, since pan/zoom need a stable center to zoom toward. */
    for (r = 0; r < mf->record_count; r++) {
        const MapRrmTaggedRecord *tr = &mf->records[r];
        int32_t ox, oz;
        int x0, y0, x1, y1, x2, y2, x3, y3;
        double wx0, wz0, wx1, wz1, wx2, wz2, wx3, wz3;
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (!track_demo_is_real_road_section(mf, tr->section_index)) continue;
        if (idx_hed_section_world_origin(idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;

        /* v0,v1,v3,v2 perimeter order (note v3 before v2). */
        wx0 = MAP_PHYS_X(ox, tr->rec.v0[0]); wz0 = oz + tr->rec.v0[2];
        wx1 = MAP_PHYS_X(ox, tr->rec.v1[0]); wz1 = oz + tr->rec.v1[2];
        wx2 = MAP_PHYS_X(ox, tr->rec.v3[0]); wz2 = oz + tr->rec.v3[2];
        wx3 = MAP_PHYS_X(ox, tr->rec.v2[0]); wz3 = oz + tr->rec.v2[2];

        x0 = (int)((wx0 - cx) * scale) + GPU_FB_WIDTH / 2;
        y0 = (int)((wz0 - cz) * scale) + GPU_FB_HEIGHT / 2;
        x1 = (int)((wx1 - cx) * scale) + GPU_FB_WIDTH / 2;
        y1 = (int)((wz1 - cz) * scale) + GPU_FB_HEIGHT / 2;
        x2 = (int)((wx2 - cx) * scale) + GPU_FB_WIDTH / 2;
        y2 = (int)((wz2 - cz) * scale) + GPU_FB_HEIGHT / 2;
        x3 = (int)((wx3 - cx) * scale) + GPU_FB_WIDTH / 2;
        y3 = (int)((wz3 - cz) * scale) + GPU_FB_HEIGHT / 2;

        if (td->tex_rgba != NULL) {
            gpu_draw_quad_textured(x0, y0, track_tile_uv(wx0), track_tile_uv(wz0),
                                    x1, y1, track_tile_uv(wx1), track_tile_uv(wz1),
                                    x2, y2, track_tile_uv(wx2), track_tile_uv(wz2),
                                    x3, y3, track_tile_uv(wx3), track_tile_uv(wz3),
                                    td->tex_rgba, td->tex_w, td->tex_h);
        } else {
            gpu_draw_quad_flat(x0, y0, x1, y1, x2, y2, x3, y3, 0x003C965A);
        }
    }
}

/* One projected, depth-sortable quad, staged by draw_track_drive_scene
 * before the actual draw calls so the whole frame's worth of quads can
 * be painter's-algorithm sorted back-to-front first. */
typedef struct {
    double depth;
    int x0, y0, x1, y1, x2, y2, x3, y3;
    uint32_t color;
    /* Per-vertex UVs, only meaningful/used when the scene has a road
     * texture (td->tex_rgba != NULL) -- see draw_track_drive_scene. */
    float u0, v0, u1, v1, u2, v2, u3, v3;
    /* ROUND 45: real per-quad texture page (from the record's OWN
     * tpage/clut/UV fields) -- NULL means draw flat with .color. */
    const uint32_t *page_rgba;
    uint32_t mod; /* ROUND 52: 0xRRGGBB texel modulation (fog/lighting) */
} TrackDriveQuadJob;

#define TRACK_DRIVE_MAX_QUADS 12288
static TrackDriveQuadJob s_track_drive_jobs[TRACK_DRIVE_MAX_QUADS];

static int track_drive_job_cmp(const void *pa, const void *pb) {
    const TrackDriveQuadJob *a = (const TrackDriveQuadJob *)pa;
    const TrackDriveQuadJob *b = (const TrackDriveQuadJob *)pb;
    /* Farthest first (descending depth) -- painter's algorithm. */
    if (a->depth > b->depth) return -1;
    if (a->depth < b->depth) return 1;
    return 0;
}

/* Perspective drive-camera render -- see the file header comment for
 * the flat-ground-plane simplification this makes. Camera-space axes:
 * `right` = dx*cos(yaw) - dz*sin(yaw), `forward` (depth) = dx*sin(yaw)
 * + dz*cos(yaw), where (dx,dz) = world position minus camera position.
 * At yaw==0 this reduces to right=dx, forward=dz (camera faces +Z),
 * and increasing yaw rotates the facing direction from +Z toward +X.
 * Screen projection is a standard pinhole model (screen = focal *
 * lateral / depth), with a fixed camera height above the Y=0 ground
 * plane standing in for real elevation data. */
static void draw_track_drive_scene(const TrackDemo *td) {
    const MapRrmFile *mf = &td->mf;
    const IdxHedFile *idxf = &td->idxf;
    size_t r;
    int njobs = 0;
    double cos_yaw = cos(s_cam_yaw), sin_yaw = sin(s_cam_yaw);
    /* ~80 degree horizontal FOV; focal length in pixels for a
     * GPU_FB_WIDTH-wide framebuffer. */
    double focal = (GPU_FB_WIDTH / 2.0) / tan(80.0 * 3.14159265358979 / 180.0 / 2.0);
    const double near_plane = 20.0;   /* world units; vertices closer than this are dropped */
    const double far_fog = 9000.0;    /* world units; distance fog fades to background by here */
    /* Screen coords are clamped to this before handing to
     * gpu_draw_quad_flat -- keeps the int edge-function math in
     * gpu_soft.c (see gpu_draw_triangle_flat) well inside int32 range
     * even for near-camera geometry that projects far off-screen,
     * while still being well outside the visible framebuffer. */
    const int coord_clamp = 20000;
    /* ROUND 44: camera ground height from the road records under the
     * camera -- MAP.RRM type-B records carry REAL per-corner heights
     * (v[1], PS1 y-down: negative = raised road), previously ignored
     * (flat-plane simplification). Sampled from the nearest section's
     * B-run so hills/tunnel dips move the horizon like they should. */
    {
        /* ROUND 46 ground sampler v2 -- ROUND 53: extracted into
         * ground_sample_at() (also used per-AI-car below); sampled at
         * the CAR when it exists, since the chase camera legitimately
         * hangs past the road edge in corners. */
        double smp_x = s_car_draw_on ? s_car_draw_x : s_cam_x;
        double smp_z = s_car_draw_on ? s_car_draw_z : s_cam_z;
        s_ground_y = ground_sample_at(td, smp_x, smp_z, s_ground_y,
                                      &s_ground_seeded);
    }

    /* ROUND 44: sky -- vertical gradient bands above the horizon
     * (deep blue high, warm haze at the horizon line), drawn first so
     * every road quad paints over it. Horizon sits at mid-screen
     * (flat-projection convention of this camera). */
    {
        int band, nbands = 12, horizon = GPU_FB_HEIGHT / 2;
        for (band = 0; band < nbands; band++) {
            int y0b = band * horizon / nbands;
            int y1b = (band + 1) * horizon / nbands;
            double t = (double)band / (nbands - 1);
            int rr8 = (int)(24 + t * (110 - 24));
            int gg8 = (int)(40 + t * (140 - 40));
            int bb8 = (int)(96 + t * (176 - 96));
            uint32_t c = ((uint32_t)rr8 << 16) | ((uint32_t)gg8 << 8) | (uint32_t)bb8;
            gpu_draw_quad_flat(0, y0b, GPU_FB_WIDTH, y0b, GPU_FB_WIDTH, y1b, 0, y1b, c);
        }
        /* ground below the horizon: dark neutral */
        gpu_draw_quad_flat(0, horizon, GPU_FB_WIDTH, horizon,
                           GPU_FB_WIDTH, GPU_FB_HEIGHT, 0, GPU_FB_HEIGHT,
                           0x00202428);
    }

    for (r = 0; r < mf->record_count && njobs < TRACK_DRIVE_MAX_QUADS; r++) {
        const MapRrmTaggedRecord *tr = &mf->records[r];
        int32_t ox, oz;
        int c, behind = 0;
        double rightv[4], depthv[4], heightv[4];
        float uv_u[4], uv_v[4];
        double avg_depth = 0.0;
        int px[4], py[4];
        int fog;
        uint32_t color;

        /* ROUND 53: MAP.RRM record types A and C turn out to be the
         * SAME 40-byte textured-quad format as type B (4 verts + the
         * round-45 texture tail; verified by field-range scan on the
         * real file: the tail words decode to valid CLUT/TPAGE ids).
         * They are the SCENERY -- cliff faces, building sides, tunnel
         * walls -- that the drive view was silently skipping. Draw
         * all three streams; the ground sampler stays B-only (roads). */
        if (tr->type != MAP_RRM_RECORD_TYPE_A &&
            tr->type != MAP_RRM_RECORD_TYPE_B &&
            tr->type != MAP_RRM_RECORD_TYPE_C) continue;
        /* ROUND 54: the ">2 records" section filter is GONE here --
         * it excluded 61 of 258 sections (a simple straight is a
         * single road quad), which was the "dark zone" around
         * sections 40-42 and every other missing patch of road. */
        if (idx_hed_section_world_origin(idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;

        /* ROUND 53: cheap distance cull -- a quad whose first corner
         * sits beyond the fog wall can only rasterize as background
         * anyway; skipping it keeps the quad budget for what's
         * visible now that the A/C scenery streams are in. */
        {
            double ddx = MAP_PHYS_X(ox, tr->rec.v0[0]) - s_cam_x;
            double ddz = (oz + tr->rec.v0[2]) - s_cam_z;
            if (ddx * ddx + ddz * ddz > (far_fog + 1200.0) * (far_fog + 1200.0))
                continue;
        }

        for (c = 0; c < 4; c++) {
            /* v0,v1,v3,v2 perimeter order, same convention as the
             * top-down view's pass 2. */
            const int16_t *v = (c == 0) ? tr->rec.v0 : (c == 1) ? tr->rec.v1 : (c == 2) ? tr->rec.v3 : tr->rec.v2;
            double wx = MAP_PHYS_X(ox, v[0]), wz = oz + v[2];
            double dx = wx - s_cam_x, dz = wz - s_cam_z;
            rightv[c] = dx * cos_yaw - dz * sin_yaw;
            depthv[c] = dx * sin_yaw + dz * cos_yaw;
            /* ROUND 44: real height relative to the camera's ground
             * sample (PS1 y-down), camera riding s_cam_height above. */
            heightv[c] = ((double)v[1] - s_ground_y) + s_cam_height;
            if (depthv[c] <= near_plane) behind++;
            avg_depth += depthv[c];
            uv_u[c] = track_tile_uv(wx);
            uv_v[c] = track_tile_uv(wz);
        }
        /* ROUND 47: REAL near-plane polygon clipping (Sutherland-
         * Hodgman against depth == near_plane, camera space, UVs
         * interpolated) -- replaces the any-vertex-behind cull that
         * left a gray hole in the immediate foreground. Produces up to
         * 5 vertices; drawn as a triangle fan below (a "quad" job with
         * its last vertex duplicated rasterizes as a triangle). */
        {
            double cr[8], ch[8], cd[8];
            float cu[8], cv[8];
            int n_out = 0;
            /* fetch this record's real UVs up front (or tiling UVs) so
             * clipping interpolates the right values */
            float ru[4], rv[4];
            const uint32_t *pg = NULL;
            if (s_vram_loaded) {
                uint16_t rclut = tr->rec.heading;
                uint16_t rtpage = (uint16_t)tr->rec.unk_1e;
                pg = vram_page_get(rtpage, rclut);
            }
            if (pg != NULL) {
                uint16_t w0 = (uint16_t)tr->rec.unk_18;
                uint16_t w1 = (uint16_t)tr->rec.unk_1c;
                uint16_t w2 = (uint16_t)tr->rec.unk_20;
                uint16_t w3 = (uint16_t)tr->rec.unk_24;
                /* perimeter order v0,v1,v3,v2 <-> uv0,uv1,uv3,uv2 */
                ru[0] = (float)(w0 & 0xFF) / 255.0f; rv[0] = (float)(w0 >> 8) / 255.0f;
                ru[1] = (float)(w1 & 0xFF) / 255.0f; rv[1] = (float)(w1 >> 8) / 255.0f;
                ru[2] = (float)(w2 & 0xFF) / 255.0f; rv[2] = (float)(w2 >> 8) / 255.0f;
                ru[3] = (float)(w3 & 0xFF) / 255.0f; rv[3] = (float)(w3 >> 8) / 255.0f;
            } else {
                ru[0] = uv_u[0]; rv[0] = uv_v[0];
                ru[1] = uv_u[1]; rv[1] = uv_v[1];
                ru[2] = uv_u[2]; rv[2] = uv_v[2];
                ru[3] = uv_u[3]; rv[3] = uv_v[3];
            }
            if (behind == 4)
                continue;
            if (behind == 0) {
                for (c = 0; c < 4; c++) {
                    cr[c] = rightv[c]; ch[c] = heightv[c]; cd[c] = depthv[c];
                    cu[c] = ru[c]; cv[c] = rv[c];
                }
                n_out = 4;
            } else {
                for (c = 0; c < 4; c++) {
                    int nxt = (c + 1) & 3;
                    int ain = depthv[c] > near_plane;
                    int bin = depthv[nxt] > near_plane;
                    if (ain) {
                        cr[n_out] = rightv[c]; ch[n_out] = heightv[c];
                        cd[n_out] = depthv[c];
                        cu[n_out] = ru[c]; cv[n_out] = rv[c];
                        n_out++;
                    }
                    if (ain != bin) {
                        double t = (near_plane - depthv[c]) /
                                   (depthv[nxt] - depthv[c]);
                        cr[n_out] = rightv[c] + (rightv[nxt] - rightv[c]) * t;
                        ch[n_out] = heightv[c] + (heightv[nxt] - heightv[c]) * t;
                        cd[n_out] = near_plane;
                        cu[n_out] = ru[c] + (float)((ru[nxt] - ru[c]) * t);
                        cv[n_out] = rv[c] + (float)((rv[nxt] - rv[c]) * t);
                        n_out++;
                    }
                }
                if (n_out < 3)
                    continue;
            }
            avg_depth = 0.0;
            for (c = 0; c < n_out; c++)
                avg_depth += cd[c];
            avg_depth /= n_out;

            /* fog + material tint (unchanged from rounds 44-45) */
            fog = (int)(255.0 * (1.0 - avg_depth / far_fog));
            if (fog < 110) fog = 110;
            if (fog > 255) fog = 255;
            {
                int g = (int)(int16_t)tr->rec.group_id;
                int t1 = ((g * 73) & 0x1F) - 16;
                int t2 = ((g * 131) & 0x1F) - 16;
                int rb = 0x74 + t1, gb = 0x78 + t2, bb = 0x7C + (t1 + t2) / 2;
                if (rb < 24) rb = 24;
                if (rb > 200) rb = 200;
                if (gb < 24) gb = 24;
                if (gb > 200) gb = 200;
                if (bb < 24) bb = 24;
                if (bb > 200) bb = 200;
                color = ((uint32_t)((rb * fog) / 255) << 16) |
                        ((uint32_t)((gb * fog) / 255) << 8) |
                        (uint32_t)((bb * fog) / 255);
            }

            /* project + emit triangle fan */
            {
                int sx[8], sy[8], k;
                double sort_depth = avg_depth
                    + (double)(int16_t)tr->rec.group_id * 8.0;
                for (c = 0; c < n_out; c++) {
                    sx[c] = (int)((cr[c] / cd[c]) * focal) + GPU_FB_WIDTH / 2;
                    sy[c] = (int)((ch[c] / cd[c]) * focal) + GPU_FB_HEIGHT / 2;
                    if (sx[c] < -coord_clamp) sx[c] = -coord_clamp;
                    if (sx[c] > coord_clamp) sx[c] = coord_clamp;
                    if (sy[c] < -coord_clamp) sy[c] = -coord_clamp;
                    if (sy[c] > coord_clamp) sy[c] = coord_clamp;
                }
                for (k = 1; k + 1 < n_out && njobs < TRACK_DRIVE_MAX_QUADS; k++) {
                    TrackDriveQuadJob *jb = &s_track_drive_jobs[njobs];
                    jb->mod = ((uint32_t)fog << 16) | ((uint32_t)fog << 8)
                            | (uint32_t)fog; /* distance haze */
                    jb->depth = sort_depth;
                    jb->x0 = sx[0]; jb->y0 = sy[0];
                    jb->x1 = sx[k]; jb->y1 = sy[k];
                    jb->x2 = sx[k + 1]; jb->y2 = sy[k + 1];
                    jb->x3 = sx[k + 1]; jb->y3 = sy[k + 1]; /* degenerate = triangle */
                    jb->color = color;
                    jb->page_rgba = pg;
                    jb->u0 = cu[0]; jb->v0 = cv[0];
                    jb->u1 = cu[k]; jb->v1 = cv[k];
                    jb->u2 = cu[k + 1]; jb->v2 = cv[k + 1];
                    jb->u3 = cu[k + 1]; jb->v3 = cv[k + 1];
                    njobs++;
                }
            }
            continue; /* fan emitted -- skip the legacy single-quad path */
        }

        fog = (int)(255.0 * (1.0 - avg_depth / far_fog));
        if (fog < 110) fog = 110;
        if (fog > 255) fog = 255;
        /* ROUND 44: material tint from the record's group_id (the
         * stepped "material id" candidate field -- 30 distinct values
         * in the shipped course). Base = asphalt gray; the id hashes
         * to a subtle stable tint so different road materials (bridge,
         * tunnel, beach front...) read as different surfaces. Mapping
         * group_id to its REAL texture page is the documented next
         * step (needs the PS1 render-loop consumer trace). */
        {
            int g = (int)(int16_t)tr->rec.group_id;
            int t1 = ((g * 73) & 0x1F) - 16; /* -16..15 stable tint */
            int t2 = ((g * 131) & 0x1F) - 16;
            int rb = 0x74 + t1, gb = 0x78 + t2, bb = 0x7C + (t1 + t2) / 2;
            if (rb < 24) rb = 24;
            if (rb > 200) rb = 200;
            if (gb < 24) gb = 24;
            if (gb > 200) gb = 200;
            if (bb < 24) bb = 24;
            if (bb > 200) bb = 200;
            color = ((uint32_t)((rb * fog) / 255) << 16) |
                    ((uint32_t)((gb * fog) / 255) << 8) |
                    (uint32_t)((bb * fog) / 255);
        }

        /* ROUND 46: fold in the record's own ordering-table bias
         * (bytes 34-35, decoded round 45) -- the game's way of
         * layering decks/bridges; one OT step ~ a few world units. */
        s_track_drive_jobs[njobs].depth = avg_depth
            + (double)(int16_t)tr->rec.group_id * 8.0;
        s_track_drive_jobs[njobs].x0 = px[0]; s_track_drive_jobs[njobs].y0 = py[0];
        s_track_drive_jobs[njobs].x1 = px[1]; s_track_drive_jobs[njobs].y1 = py[1];
        s_track_drive_jobs[njobs].x2 = px[2]; s_track_drive_jobs[njobs].y2 = py[2];
        s_track_drive_jobs[njobs].x3 = px[3]; s_track_drive_jobs[njobs].y3 = py[3];
        s_track_drive_jobs[njobs].color = color;
        s_track_drive_jobs[njobs].page_rgba = NULL;
        if (s_vram_loaded) {
            /* ROUND 45: the record's OWN texture reference -- bytes
             * 24-39 decoded via func_8003486C's POLY_FT4 copy (see
             * psx_vram.h). Corner order: record v0,v1,v3,v2 is this
             * renderer's perimeter order, so UVs pair up the same way:
             * uv0,uv1,uv3,uv2. */
            uint16_t rclut = tr->rec.heading;          /* bytes 26-27 */
            uint16_t rtpage = (uint16_t)tr->rec.unk_1e;/* bytes 30-31 */
            const uint32_t *pg = vram_page_get(rtpage, rclut);
            if (pg != NULL) {
                uint16_t w0 = (uint16_t)tr->rec.unk_18; /* u0,v0 */
                uint16_t w1 = (uint16_t)tr->rec.unk_1c; /* u1,v1 */
                uint16_t w2 = (uint16_t)tr->rec.unk_20; /* u2,v2 */
                uint16_t w3 = (uint16_t)tr->rec.unk_24; /* u3,v3 */
                s_track_drive_jobs[njobs].page_rgba = pg;
                s_track_drive_jobs[njobs].u0 = (float)(w0 & 0xFF) / 255.0f;
                s_track_drive_jobs[njobs].v0 = (float)(w0 >> 8) / 255.0f;
                s_track_drive_jobs[njobs].u1 = (float)(w1 & 0xFF) / 255.0f;
                s_track_drive_jobs[njobs].v1 = (float)(w1 >> 8) / 255.0f;
                s_track_drive_jobs[njobs].u3 = (float)(w2 & 0xFF) / 255.0f;
                s_track_drive_jobs[njobs].v3 = (float)(w2 >> 8) / 255.0f;
                s_track_drive_jobs[njobs].u2 = (float)(w3 & 0xFF) / 255.0f;
                s_track_drive_jobs[njobs].v2 = (float)(w3 >> 8) / 255.0f;
            }
        }
        if (s_track_drive_jobs[njobs].page_rgba == NULL) {
            s_track_drive_jobs[njobs].u0 = uv_u[0]; s_track_drive_jobs[njobs].v0 = uv_v[0];
            s_track_drive_jobs[njobs].u1 = uv_u[1]; s_track_drive_jobs[njobs].v1 = uv_v[1];
            s_track_drive_jobs[njobs].u2 = uv_u[2]; s_track_drive_jobs[njobs].v2 = uv_v[2];
            s_track_drive_jobs[njobs].u3 = uv_u[3]; s_track_drive_jobs[njobs].v3 = uv_v[3];
        }
        njobs++;
    }

    /* ROUND 51: the visible car -- object model type-64 prims (4 verts
     * + 4 Q12 normals + texture tail), rotated by the car heading,
     * scaled (approximated factor, see obj_load), placed at the car
     * pose, and pushed through the SAME projection/clip/texture path
     * as the track quads. */
    if (s_obj_buf != NULL) {
        int ci;
        for (ci = 0; ci < MAX_DRAW_CARS; ci++) {
        CarDraw *cd = &s_draw_cars[ci];
        if (!cd->on || (uint32_t)cd->model >= s_obj_count)
            continue;
        /* ROUND 53: per-car deck height -- each AI car samples the
         * ground at ITS OWN position (with its own overpass
         * continuity), instead of inheriting the camera's deck. */
        if (ci != 0)
            cd->y = ground_sample_at(td, cd->x, cd->z, cd->y,
                                     &cd->y_seeded);
        {
        /* ROUND 52: the car is drawn as PIECES -- body + two full
         * AXLE objects spinning around X with the authentic wheel
         * angle (car+0x38). ROUND 53: the pieces now come from the
         * game's own per-model kit table D_80059228 (traced in the
         * car renderer func_8002128C): body object, axle object
         * (36/37/38 per model class) drawn once at the car origin and
         * once at the rear offset the table stores, the far LOD
         * object beyond Manhattan camera distance 0xD00, and a full
         * cull past 0x2500. */
        struct { int obj; int dz; int dy; int spin; } pieces[3];
        int pi, npieces;
        /* ROUND 53: model z runs NOSE-BACKWARD relative to our world
         * forward -- drawing it raw showed the nose to the chase
         * camera WITH mirrored livery text, the signature of a z-axis
         * reflection (the game's renderer bakes the equivalent into
         * its 0x800-minus-heading matrix convention). Mirror model z
         * (verts and normals) and keep the heading direct. */
        double hb = (double)(cd->heading & 0xFFF) / 4096.0 * 6.283185307179586;
        double ch2 = cos(hb), sh2 = sin(hb);
        double rb = (double)((cd->roll << 20) >> 20) / 4096.0 * 6.283185307179586;
        double crl = cos(rb), srl = sin(rb);
        double man = fabs(cd->x - s_cam_x) + fabs(cd->z - s_cam_z);
        if (psx_ai_kit_loaded && cd->model >= 0 &&
            cd->model < PSX_AI_KIT_MODELS) {
            const PsxCarKit *kt = &psx_ai_kit[cd->model];
            if (ci != 0 && man > 9472.0)
                continue; /* 0x2500: cull (func_8002128C) */
            if (ci != 0 && man > 3328.0) {
                /* 0xD00: single-piece LOD */
                pieces[0].obj = kt->lod_obj; pieces[0].dz = 0;
                pieces[0].dy = 0; pieces[0].spin = 0;
                npieces = 1;
            } else {
                pieces[0].obj = kt->body_obj; pieces[0].dz = 0;
                pieces[0].dy = 0; pieces[0].spin = 0;
                /* ROUND 56: axles ride slightly high in model space
                 * (-14, y-down) so the wheel rims meet the ground
                 * instead of sinking under the body line. */
                pieces[1].obj = kt->axle_obj; pieces[1].dz = 0;
                pieces[1].dy = -14; pieces[1].spin = 1;
                pieces[2].obj = kt->axle_obj;
                pieces[2].dz = kt->axle_dz_model; /* rear (-335 etc.) */
                pieces[2].dy = -14; pieces[2].spin = 1;
                npieces = 3;
            }
        } else {
            /* no kit (no --physicsdata): round-52 fallback */
            pieces[0].obj = cd->model; pieces[0].dz = 0; pieces[0].dy = 0;
            pieces[0].spin = 0;
            pieces[1].obj = 36; pieces[1].dz = 40; pieces[1].dy = -14;
            pieces[1].spin = 1;
            pieces[2].obj = 36; pieces[2].dz = -350; pieces[2].dy = -14;
            pieces[2].spin = 1;
            npieces = 3;
        }
        for (pi = 0; pi < npieces; pi++) {
        int piece_model = pieces[pi].obj;
        uint32_t base;
        if (piece_model < 0 || (uint32_t)piece_model >= s_obj_count)
            continue;
        base = s_obj_data_off[piece_model];
        int16_t n64 = obj_cnt((uint32_t)piece_model, 3);
        uint32_t o = base + (uint32_t)obj_cnt(piece_model, 0) * 40u
                          + (uint32_t)obj_cnt(piece_model, 1) * 48u
                          + (uint32_t)obj_cnt(piece_model, 2) * 32u;
        double wb = (double)(cd->wheel & 0xFFF) / 4096.0 * 6.283185307179586;
        double cw2 = cos(wb), sw2 = sin(wb);
        int k;
        for (k = 0; k < n64 && njobs < TRACK_DRIVE_MAX_QUADS; k++) {
            const uint8_t *rec = s_obj_buf + o + (uint32_t)k * 64u;
            double wr[4], wh[4], wd[4];
            float wu[4], wv[4];
            uint16_t tail[8];
            const uint32_t *pg = NULL;
            int c2, behind2 = 0;
            double avg2 = 0.0;
            uint32_t car_mod = 0xFFFFFFu;
            int t;
            for (t = 0; t < 8; t++)
                tail[t] = (uint16_t)(rec[48 + t * 2] | (rec[49 + t * 2] << 8));
            if (s_vram_loaded)
                pg = vram_page_get(tail[3], tail[1]);
            {
                /* ROUND 52: gouraud-style lighting from the prim's Q12
                 * normals (per-quad, normal 0), sun from high front-left
                 * (y-down world). Shade range keeps blacks readable. */
                int16_t nx0 = (int16_t)(rec[24] | (rec[25] << 8));
                int16_t ny0 = (int16_t)(rec[26] | (rec[27] << 8));
                int16_t nz0 = (int16_t)-(int16_t)(rec[28] | (rec[29] << 8));
                double wnx = (nz0 / 4096.0) * ch2 - (nx0 / 4096.0) * sh2;
                double wnz = (nz0 / 4096.0) * sh2 + (nx0 / 4096.0) * ch2;
                double wny = ny0 / 4096.0;
                double d = wnx * -0.35 + wny * -0.85 + wnz * 0.20;
                int sh = (int)(150.0 + 105.0 * (d > 0 ? d : 0));
                if (sh > 255) sh = 255;
                car_mod = ((uint32_t)sh << 16) | ((uint32_t)sh << 8) | (uint32_t)sh;
            }
            for (c2 = 0; c2 < 4; c2++) {
                /* perimeter order 0,1,3,2 like the track quads */
                int vi = (c2 == 0) ? 0 : (c2 == 1) ? 1 : (c2 == 2) ? 3 : 2;
                int16_t mx = (int16_t)(rec[vi * 6] | (rec[vi * 6 + 1] << 8));
                int16_t my = (int16_t)(rec[vi * 6 + 2] | (rec[vi * 6 + 3] << 8));
                int16_t mz = (int16_t)(rec[vi * 6 + 4] | (rec[vi * 6 + 5] << 8));
                double pmx = (double)mx, pmy = (double)my, pmz = -(double)mz;
                if (pieces[pi].spin) {
                    /* axle spin about X with the authentic wheel angle */
                    double ry = pmy * cw2 - pmz * sw2;
                    double rz = pmy * sw2 + pmz * cw2;
                    pmy = ry; pmz = rz;
                }
                pmz += (double)pieces[pi].dz;
                pmy += (double)pieces[pi].dy;
                /* ROUND 53: corner lean -- the AI renderer feeds +0x28
                 * into a Z-rotation (roll about the forward axis). */
                if (cd->roll != 0) {
                    double rx = pmx * crl - pmy * srl;
                    double ry2 = pmx * srl + pmy * crl;
                    pmx = rx; pmy = ry2;
                }
                /* model +z = forward; rotate about Y (y-down world) */
                double lx = pmx * s_car_model_scale;
                double ly = pmy * s_car_model_scale;
                double lz = pmz * s_car_model_scale;
                double wx = cd->x + lz * ch2 - lx * sh2;
                double wz = cd->z + lz * sh2 + lx * ch2;
                double wy = (ci == 0 ? s_ground_y : cd->y) + ly - 6.0;
                double dx = wx - s_cam_x, dz = wz - s_cam_z;
                int uvi = (c2 == 3) ? 2 : (c2 == 2) ? 3 : c2;
                wr[c2] = dx * cos_yaw - dz * sin_yaw;
                wd[c2] = dx * sin_yaw + dz * cos_yaw;
                wh[c2] = (wy - s_ground_y) + s_cam_height;
                if (wd[c2] <= near_plane) behind2 = 1;
                avg2 += wd[c2];
                wu[c2] = (float)(tail[uvi * 2] & 0xFF) / 255.0f;
                wv[c2] = (float)(tail[uvi * 2] >> 8) / 255.0f;
            }
            if (behind2) continue;
            avg2 /= 4.0;
            {
                TrackDriveQuadJob *jb = &s_track_drive_jobs[njobs];
                int c3;
                int qx[4], qy[4];
                for (c3 = 0; c3 < 4; c3++) {
                    qx[c3] = (int)((wr[c3] / wd[c3]) * focal) + GPU_FB_WIDTH / 2;
                    qy[c3] = (int)((wh[c3] / wd[c3]) * focal) + GPU_FB_HEIGHT / 2;
                }
                jb->depth = avg2 - 2.0; /* nudge in front of coplanar road */
                jb->x0 = qx[0]; jb->y0 = qy[0];
                jb->x1 = qx[1]; jb->y1 = qy[1];
                jb->x2 = qx[2]; jb->y2 = qy[2];
                jb->x3 = qx[3]; jb->y3 = qy[3];
                jb->page_rgba = pg;
                jb->mod = car_mod;
                jb->color = 0x00C03030;
                jb->u0 = wu[0]; jb->v0 = wv[0];
                jb->u1 = wu[1]; jb->v1 = wv[1];
                jb->u2 = wu[2]; jb->v2 = wv[2];
                jb->u3 = wu[3]; jb->v3 = wv[3];
                njobs++;
            }
        }
        } /* pieces */
        }
        } /* cars */
    }

    qsort(s_track_drive_jobs, (size_t)njobs, sizeof(s_track_drive_jobs[0]), track_drive_job_cmp);
    for (r = 0; r < (size_t)njobs; r++) {
        const TrackDriveQuadJob *j = &s_track_drive_jobs[r];
        if (td->tex_rgba != NULL) {
            /* No fog/tint on textured quads -- gpu_draw_quad_textured
             * has no color-tint input, see file header comment. */
            gpu_draw_quad_textured(j->x0, j->y0, j->u0, j->v0,
                                    j->x1, j->y1, j->u1, j->v1,
                                    j->x2, j->y2, j->u2, j->v2,
                                    j->x3, j->y3, j->u3, j->v3,
                                    td->tex_rgba, td->tex_w, td->tex_h);
        } else if (j->page_rgba != NULL) {
            /* ROUND 45: the quad's real texture page */
            gpu_draw_quad_textured(j->x0, j->y0, j->u0, j->v0,
                                    j->x1, j->y1, j->u1, j->v1,
                                    j->x2, j->y2, j->u2, j->v2,
                                    j->x3, j->y3, j->u3, j->v3,
                                    j->page_rgba, 256, 256);
        } else {
            gpu_draw_quad_flat(j->x0, j->y0, j->x1, j->y1, j->x2, j->y2, j->x3, j->y3, j->color);
        }
    }
}

/* Dispatches to whichever track-demo view mode is active. */
static void draw_track_view(const TrackDemo *td) {
    if (s_track_view_mode == TRACK_VIEW_DRIVE) {
        /* ROUND 61: boot title (real logo) until Enter is pressed */
        if (s_psx_active && s_title_active && s_vram_loaded) {
            draw_title_screen(s_title_frame++);
            return;
        }
        /* ROUND 62: car select (left/right, Enter races it) */
        if (s_psx_active && s_select_active && s_vram_loaded) {
            draw_car_select(s_title_frame++, s_select_model);
            return;
        }
        draw_track_drive_scene(td);
        if (s_psx_active)
            draw_race_hud(s_psx_car.speed, (int)s_psx_car.gear, s_hud_lap + 1,
                          s_psx_car.rpm);
    } else {
        draw_track_demo_scene(td);
    }
}

#ifdef HAVE_SDL2
/* ROUND 57: first AUDIO -- an engine tone driven by the authentic
 * rpm. This is a synthesized placeholder (two detuned saws + a sub
 * square, pitch 55..280 Hz over the 0..0x2710 rpm range, plus a
 * white-noise breath scaled by drift slip); the REAL sound will come
 * from RR.VH/RR.VB (VAB) in a future round. Kept wholly inside the
 * SDL build; the headless binary stays silent and identical. */
static SDL_AudioDeviceID s_audio_dev = 0;
/* ROUND 59: the REAL engine sample -- --vabfiles <RR.VH> <RR.VB>
 * decodes the player model's own engine VAG (program = model id,
 * programs 0-16 are the per-car engine family, round 58) into PCM at
 * load; the callback then LOOPS it with an rpm-driven resample step
 * instead of synthesizing. Synth remains the no-VAB fallback. */
static int16_t *s_vag_pcm = NULL;
static size_t s_vag_len = 0;
/* ROUND 60: the skid voice. Programs 17-27 are multi-tone mixes; VAGs
 * 19/20 appear in nearly every one of them (the shared tire/scrub
 * components) -- VAG 20 looped at fixed pitch, mixed in at a volume
 * proportional to the authentic drift slip, is the skid layer. */
static int16_t *s_skid_pcm = NULL;
static size_t s_skid_len = 0;
static volatile int32_t s_audio_rpm = 0;
static volatile int32_t s_audio_slip = 0;
static volatile int s_audio_on = 0;
static uint32_t s_audio_noise = 0x12345678u;

static void engine_audio_cb(void *userdata, Uint8 *stream, int len)
{
    static double ph1 = 0.0, ph2 = 0.0, ph3 = 0.0;
    int16_t *out = (int16_t *)stream;
    int n = len / 2, i;
    double rpm = (double)s_audio_rpm;
    double slip = (double)(s_audio_slip < 0 ? -s_audio_slip : s_audio_slip);
    double f = 55.0 + rpm / 10000.0 * 225.0;
    double amp = s_audio_on ? 5000.0 : 0.0;
    double nz = slip > 60.0 ? 1200.0 : slip * 20.0;
    (void)userdata;
    if (s_vag_pcm != NULL) {
        /* real engine loop: resample step ~ rpm (the sample sounds
         * near idle at ~0.55x and near redline at ~1.9x -- the exact
         * center-89/shift-70 pitch law is a future refinement) */
        static double pos = 0.0, spos = 0.0;
        double step = (0.55 + rpm / 10000.0 * 1.35) * 0.5;
        double skid_amp = slip > 40.0 ? (slip - 40.0) / 160.0 : 0.0;
        if (skid_amp > 0.9) skid_amp = 0.9;
        for (i = 0; i < n; i++) {
            double v;
            size_t i0;
            s_audio_noise = s_audio_noise * 1664525u + 1013904223u;
            pos += step;
            if (pos >= (double)s_vag_len) pos -= (double)s_vag_len;
            i0 = (size_t)pos;
            v = (double)s_vag_pcm[i0] * (s_audio_on ? 0.8 : 0.0)
              + ((double)(s_audio_noise >> 16) / 65535.0 - 0.5) * nz;
            if (s_skid_pcm != NULL && skid_amp > 0.0 && s_audio_on) {
                spos += 0.5;
                if (spos >= (double)s_skid_len) spos -= (double)s_skid_len;
                v += (double)s_skid_pcm[(size_t)spos] * skid_amp;
            }
            if (v > 32000.0) v = 32000.0;
            if (v < -32000.0) v = -32000.0;
            out[i] = (int16_t)v;
        }
        return;
    }
    for (i = 0; i < n; i++) {
        double v;
        ph1 += f / 44100.0;
        ph2 += f * 1.011 / 44100.0;
        ph3 += f * 0.5 / 44100.0;
        if (ph1 >= 1.0) ph1 -= 1.0;
        if (ph2 >= 1.0) ph2 -= 1.0;
        if (ph3 >= 1.0) ph3 -= 1.0;
        s_audio_noise = s_audio_noise * 1664525u + 1013904223u;
        v = (ph1 - 0.5) * amp + (ph2 - 0.5) * amp * 0.6
          + (ph3 < 0.5 ? -1.0 : 1.0) * amp * 0.25
          + ((double)(s_audio_noise >> 16) / 65535.0 - 0.5) * nz;
        if (v > 32000.0) v = 32000.0;
        if (v < -32000.0) v = -32000.0;
        out[i] = (int16_t)v;
    }
}

static void engine_audio_start(void)
{
    SDL_AudioSpec want, have;
    if (s_audio_dev != 0)
        return;
    SDL_memset(&want, 0, sizeof want);
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 1024;
    want.callback = engine_audio_cb;
    s_audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (s_audio_dev != 0)
        SDL_PauseAudioDevice(s_audio_dev, 0);
}

static char s_vab_vh[512], s_vab_vb[512]; /* ROUND 62: kept for the
    car-select reload (engine sample follows the chosen model) */

static void engine_vag_load(const char *vh_path, const char *vb_path,
                            int model)
{
    FILE *f;
    uint8_t *vh = NULL, *vb = NULL;
    long vhn, vbn;
    VabHeader h;
    int vag;
    snprintf(s_vab_vh, sizeof s_vab_vh, "%s", vh_path);
    snprintf(s_vab_vb, sizeof s_vab_vb, "%s", vb_path);
    f = fopen(vh_path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END); vhn = ftell(f); fseek(f, 0, SEEK_SET);
    vh = malloc((size_t)vhn);
    if (!vh || fread(vh, 1, (size_t)vhn, f) != (size_t)vhn) { fclose(f); free(vh); return; }
    fclose(f);
    f = fopen(vb_path, "rb");
    if (!f) { free(vh); return; }
    fseek(f, 0, SEEK_END); vbn = ftell(f); fseek(f, 0, SEEK_SET);
    vb = malloc((size_t)vbn);
    if (!vb || fread(vb, 1, (size_t)vbn, f) != (size_t)vbn) { fclose(f); free(vh); free(vb); return; }
    fclose(f);
    if (vab_parse(vh, (size_t)vhn, &h) == 0) {
        if (model < 0 || model > 16) model = 0;
        vag = h.tone[model][0].vag;
        if (vag >= 1) {
            size_t cap = (size_t)h.vag_len[vag] / 16 * 28 + 28;
            int16_t *pcm = malloc(cap * sizeof(int16_t));
            size_t n = pcm ? vab_decode_vag(&h, vag, vb, (size_t)vbn, pcm, cap) : 0;
            if (n > 1000) {
                s_vag_pcm = pcm;
                s_vag_len = n;
                printf("--vabfiles: engine VAG %d decoded (%zu samples) "
                       "for model %d\n", vag, n, model);
            } else {
                free(pcm);
            }
        }
        /* skid layer: VAG 20 (see note at s_skid_pcm) */
        {
            size_t cap = (size_t)h.vag_len[20] / 16 * 28 + 28;
            int16_t *pcm = cap > 28 ? malloc(cap * sizeof(int16_t)) : NULL;
            size_t n = pcm ? vab_decode_vag(&h, 20, vb, (size_t)vbn, pcm, cap) : 0;
            if (n > 1000) {
                s_skid_pcm = pcm;
                s_skid_len = n;
                printf("--vabfiles: skid VAG 20 decoded (%zu samples)\n", n);
            } else {
                free(pcm);
            }
        }
    }
    free(vh);
    free(vb);
}

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

/* `tex_page` and `track` together select which scene the loop draws
 * every frame: tex_page non-NULL takes priority (real-texture demo),
 * else track->ready draws the real-track demo, else the original
 * phase 1-3 animated demo runs unchanged. Window title reflects
 * whichever mode is active. */
static int run_sdl_loop(const TimPage *tex_page, const TrackDemo *track) {
    const char *title = (tex_page != NULL)
        ? "rr-pc-port (real texture demo -- decoded from a TEX*.TMS file)"
        : (track != NULL && track->ready)
        ? "rr-pc-port (real track demo -- decoded from MAP.RRM/IDX.HED)"
        : "rr-pc-port (phase 3 -- software rasterizer)";

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
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
        if (track != NULL && track->ready) {
            printf("track demo controls: V toggles top-down/drive view; "
                   "top-down: arrow keys pan, +/- zoom; "
                   "drive: up/down move, left/right turn, +/- height, "
                   "P toggles autopilot (auto-drives a lap using the "
                   "confirmed section-order path -- any direction key "
                   "hands control back), "
                   "C toggles real-physics driving mode (src/physics.c's "
                   "gearbox + integration model -- up/down = throttle/"
                   "brake, left/right = steer, hold shift for manual "
                   "gear changes; see src/physics.h for what's a "
                   "confirmed RE'd formula vs. an approximation); "
                   "R resets the active view\n");
            s_track_view_mode = TRACK_VIEW_TOP;
            s_autopilot_on = 0;
            s_physics_mode = 0;
            track_view_reset(track);
        }
    }

    Uint32 start = SDL_GetTicks();
    Uint32 last_ticks = start;
    int running = 1;
    while (running) {
        Uint32 now_ticks;
        double dt;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN) {
                switch (ev.key.keysym.sym) {
                    case SDLK_ESCAPE: running = 0; break;
                    case SDLK_RETURN:
                        if (s_title_active) {
                            s_title_active = 0;
                            s_select_active = 1;
                        } else if (s_select_active) {
                            s_select_active = 0;
                            /* race with the chosen model (ROUND 62):
                             * physics stats, drawn body AND the
                             * model's own engine sample */
                            psx_car_set_model(&s_psx_car, s_select_model);
                            s_draw_cars[0].model = s_select_model;
                            if (s_vab_vh[0] != 0) {
                                int16_t *oldp = s_vag_pcm;
                                s_vag_pcm = NULL;
                                s_vag_len = 0;
                                free(oldp);
                                engine_vag_load(s_vab_vh, s_vab_vb,
                                                s_select_model);
                            }
                        }
                        break;
                    case SDLK_LEFT:
                        if (s_select_active)
                            s_select_model = (s_select_model + 11) % 12;
                        break;
                    case SDLK_RIGHT:
                        if (s_select_active)
                            s_select_model = (s_select_model + 1) % 12;
                        break;
                    default: break;
                }
                if (track != NULL && track->ready) {
                    if (ev.key.keysym.sym == SDLK_v) {
                        s_track_view_mode = (s_track_view_mode == TRACK_VIEW_TOP)
                            ? TRACK_VIEW_DRIVE : TRACK_VIEW_TOP;
                    } else if (s_track_view_mode == TRACK_VIEW_DRIVE) {
                        switch (ev.key.keysym.sym) {
                            case SDLK_p:
                                if (!s_autopilot_on && s_autopilot_wp_count == 0) {
                                    track_view_build_autopilot_path(track);
                                }
                                s_autopilot_on = !s_autopilot_on;
                                s_physics_mode = 0;
                                break;
                            case SDLK_c:
                                s_autopilot_on = 0;
                                s_physics_mode = !s_physics_mode;
                                if (s_physics_mode) {
                                    /* Hand off the current free-cam pose to the
                                     * physics car so toggling on doesn't jump
                                     * the view. */
                                    physics_car_init(&s_physics_car, s_cam_x, s_cam_z, s_cam_yaw);
                                    s_psx_active = s_physics_trackdata_loaded;
                                    if (s_psx_active) {
                                        /* AUTHENTIC core (rounds 40-42): convert the
                                         * cam yaw (radians, forward=(sin,cos)) into
                                         * the core's BAM frame (forward=(cos,sin)). */
                                        long bam = lround(atan2(cos(s_cam_yaw), sin(s_cam_yaw))
                                                          / (2.0 * M_PI) * 4096.0) & PSX_BAM_MASK;
                                        s_psx_bridge.td = s_physics_trackdata;
                                        psx_bridge_iface(&s_psx_bridge, &s_psx_iface);
                                        psx_car_init(&s_psx_car,
                                                     (int32_t)lround(s_cam_x),
                                                     (int32_t)lround(s_cam_z),
                                                     (int32_t)bam);
                                        psx_bridge_seed(&s_psx_bridge, s_psx_car.pos_x, s_psx_car.pos_z);
                                        s_psx_accum = 0.0;
                                        printf("AUTHENTIC PS1 physics mode ON (physics_psx.c, the "
                                               "fixed-point core traced from func_8001C490 -- rounds "
                                               "40-42): up = throttle, down = brake, left/right = "
                                               "steer, shift+up/down = manual shift; real course "
                                               "widths as walls\n");
                                        break;
                                    }
                                    printf("real-physics driving mode ON (float fallback model from "
                                           "src/physics.c -- load --physicsdata <PSX.EXE> for the "
                                           "authentic fixed-point core); "
                                           "up/down = throttle/brake, left/right = steer, "
                                           "shift+up/down = manual gear change\n");
                                } else {
                                    printf("real-physics driving mode off\n");
                                }
                                break;
                            case SDLK_UP:
                                if (!s_physics_mode) {
                                    s_autopilot_on = 0;
                                    s_cam_x += sin(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                    s_cam_z += cos(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                }
                                break;
                            case SDLK_DOWN:
                                if (!s_physics_mode) {
                                    s_autopilot_on = 0;
                                    s_cam_x -= sin(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                    s_cam_z -= cos(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                }
                                break;
                            case SDLK_LEFT:
                                if (!s_physics_mode) { s_autopilot_on = 0; s_cam_yaw -= TRACK_DRIVE_TURN_STEP; }
                                break;
                            case SDLK_RIGHT:
                                if (!s_physics_mode) { s_autopilot_on = 0; s_cam_yaw += TRACK_DRIVE_TURN_STEP; }
                                break;
                            case SDLK_EQUALS: case SDLK_KP_PLUS:
                                s_cam_height += TRACK_DRIVE_HEIGHT_STEP;
                                if (s_cam_height > TRACK_DRIVE_HEIGHT_MAX) s_cam_height = TRACK_DRIVE_HEIGHT_MAX;
                                break;
                            case SDLK_MINUS: case SDLK_KP_MINUS:
                                s_cam_height -= TRACK_DRIVE_HEIGHT_STEP;
                                if (s_cam_height < TRACK_DRIVE_HEIGHT_MIN) s_cam_height = TRACK_DRIVE_HEIGHT_MIN;
                                break;
                            case SDLK_r:
                                s_autopilot_on = 0;
                                s_physics_mode = 0;
                                track_view_reset_drive(track);
                                break;
                            default: break;
                        }
                    } else {
                        /* Pan step is in world units, scaled down as zoom
                         * increases so a keypress always moves about the
                         * same fraction of the visible view, not a fixed
                         * world distance that would fly off-screen at high
                         * zoom or crawl at low zoom. */
                        double pan_step = 300.0 / s_track_zoom;
                        switch (ev.key.keysym.sym) {
                            case SDLK_LEFT:  s_track_pan_x -= pan_step; break;
                            case SDLK_RIGHT: s_track_pan_x += pan_step; break;
                            case SDLK_UP:    s_track_pan_z -= pan_step; break;
                            case SDLK_DOWN:  s_track_pan_z += pan_step; break;
                            case SDLK_EQUALS: case SDLK_KP_PLUS:
                                s_track_zoom *= 1.2;
                                if (s_track_zoom > 40.0) s_track_zoom = 40.0;
                                break;
                            case SDLK_MINUS: case SDLK_KP_MINUS:
                                s_track_zoom /= 1.2;
                                if (s_track_zoom < 0.2) s_track_zoom = 0.2;
                                break;
                            case SDLK_r:
                                track_view_reset_top();
                                break;
                            default: break;
                        }
                    }
                }
            }
        }

        now_ticks = SDL_GetTicks();
        dt = (double)(now_ticks - last_ticks) / 1000.0;
        last_ticks = now_ticks;
        if (track != NULL && track->ready) {
            track_view_autopilot_update(dt);
        }

        /* Real-physics driving mode (C toggles, see the SDLK_c case
         * above): continuous held-key polling rather than the discrete
         * per-keydown steps the free-cam controls use above, since a
         * gearbox/throttle car needs to keep accelerating while a key
         * stays down, not just nudge once per press. Clamp dt the same
         * defensive way a physics step normally would, in case of a
         * long hitch (window drag, breakpoint, etc.) -- avoids a single
         * huge dt flinging the car across the map. */
        if (s_physics_mode && s_psx_active && track != NULL && track->ready) {
            /* AUTHENTIC core: digital pad input (as the original), fixed
             * 30Hz steps accumulated from wall-clock dt. */
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            int shift_mod = (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
            PsxInput pin;
            double step_dt = dt > 0.1 ? 0.1 : dt;
            memset(&pin, 0, sizeof pin);
            pin.throttle = keys[SDL_SCANCODE_UP] && !shift_mod;
            pin.brake = keys[SDL_SCANCODE_DOWN] && !shift_mod;
            pin.steer_left = keys[SDL_SCANCODE_LEFT] != 0;
            pin.steer_right = keys[SDL_SCANCODE_RIGHT] != 0;
            s_psx_car.manual = shift_mod;
            s_psx_accum += step_dt;
            while (s_psx_accum >= 1.0 / 30.0) {
                static int up_was = 0, dn_was = 0;
                s_psx_accum -= 1.0 / 30.0;
                pin.shift_up = shift_mod && keys[SDL_SCANCODE_UP] && !up_was;
                pin.shift_down = shift_mod && keys[SDL_SCANCODE_DOWN] && !dn_was;
                up_was = shift_mod && keys[SDL_SCANCODE_UP];
                dn_was = shift_mod && keys[SDL_SCANCODE_DOWN];
                psx_car_frame(&s_psx_car, &pin, &s_psx_iface);
                psx_bridge_resolve(&s_psx_bridge, &s_psx_car);
                s_audio_rpm = s_psx_car.rpm;   /* ROUND 57: engine tone */
                s_audio_slip = s_psx_car.slip_last;
                s_audio_on = 1;
                engine_audio_start();
                {
                    static int lap_prev2 = 0;
                    int nsec2 = (int)s_psx_bridge.td.count;
                    if (lap_prev2 > nsec2 - 8 && s_psx_bridge.cur < 8)
                        s_hud_lap++;
                    lap_prev2 = s_psx_bridge.cur;
                }
                /* ROUND 53: the AI opponents race too (visible ahead
                 * through the windshield -- the player car itself
                 * stays undrawn in this first-person mode). */
                ai_step_and_fill(&s_psx_bridge, (double)s_psx_bridge.cur,
                                 s_obj_buf != NULL);
            }
            /* camera follows the car: BAM (forward=(cos,sin)) back to
             * the render convention (forward=(sin,cos)). */
            s_cam_x = (double)s_psx_car.pos_x;
            s_cam_z = (double)s_psx_car.pos_z;
            s_cam_yaw = atan2(psx_cos(s_psx_car.heading) / 4096.0,
                              psx_sin(s_psx_car.heading) / 4096.0);
            s_physics_hud_timer += dt;
            if (s_physics_hud_timer >= 0.5) {
                s_physics_hud_timer = 0.0;
                printf("[psx] gear=%d speed=0x%X rpm=%d %s | sec=%d slip=%d%s%s\n",
                       (int)s_psx_car.gear, (unsigned)s_psx_car.speed,
                       (int)s_psx_car.rpm,
                       s_psx_car.manual ? "(manual)" : "(auto)",
                       s_psx_bridge.cur, (int)s_psx_car.slip_last,
                       (s_psx_car.wheel_rot & 0x1000) ? " [wheel-blur]" : "",
                       s_psx_car.spin_state ? " [SPIN]" : "");
            }
        } else if (s_physics_mode && track != NULL && track->ready) {
            const Uint8 *keys = SDL_GetKeyboardState(NULL);
            double throttle = keys[SDL_SCANCODE_UP] ? 1.0 : 0.0;
            double brake = keys[SDL_SCANCODE_DOWN] ? 1.0 : 0.0;
            double steer = 0.0;
            int shift_mod = (SDL_GetModState() & (KMOD_LSHIFT | KMOD_RSHIFT)) != 0;
            double step_dt = dt > 0.1 ? 0.1 : dt;

            if (keys[SDL_SCANCODE_LEFT]) steer -= 1.0;
            if (keys[SDL_SCANCODE_RIGHT]) steer += 1.0;
            s_physics_car.manual_transmission = shift_mod;

            physics_gearbox_update(&s_physics_car, throttle,
                                    shift_mod && keys[SDL_SCANCODE_UP],
                                    shift_mod && keys[SDL_SCANCODE_DOWN], step_dt);

            /* Round 15: off-track status now feeds the physics EVERY
             * FRAME, not just the 0.5s HUD readout. Uses
             * physics_find_section_local_walk seeded with last frame's
             * index -- O(1) amortized (a handful of steps at most), so
             * running it every frame instead of twice a second is cheap
             * (unlike physics_find_nearest_section's full O(n) scan,
             * which is exactly why round 11 ported the local-walk
             * version in the first place). Falls back to "on track" when
             * no course data was loaded via --physicsdata. */
            {
                int sec_idx = -1;
                double along = 0.0, lateral = 0.0;
                int offtrack = 0;
                double wall_gradient = 0.0;

                if (s_physics_trackdata_loaded) {
                    sec_idx = physics_find_section_local_walk(&s_physics_car, &s_physics_trackdata,
                                                                s_physics_section_index);
                    s_physics_section_index = sec_idx;
                    if (sec_idx >= 0) {
                        offtrack = physics_track_project(&s_physics_car,
                            &s_physics_trackdata.sections[sec_idx], &along, &lateral);
                        /* Round 17: continuous "grazing the wall" gradient,
                         * see physics_wall_probe_lateral_gradient's doc
                         * comment in physics.h -- computed against the
                         * same section already found above. */
                        wall_gradient = physics_wall_probe_lateral_gradient(&s_physics_car,
                            &s_physics_trackdata.sections[sec_idx]);
                    }
                }

                physics_car_integrate(&s_physics_car, throttle, brake, steer, offtrack,
                                       wall_gradient, step_dt);

                s_cam_x = s_physics_car.x;
                s_cam_z = s_physics_car.z;
                s_cam_yaw = s_physics_car.heading;

                s_physics_hud_timer += dt;
                if (s_physics_hud_timer >= 0.5) {
                    s_physics_hud_timer = 0.0;
                    if (s_physics_trackdata_loaded) {
                        printf("[physics] gear=%d speed=%.1f rpm=%.0f %s | section=%d lateral=%.1f %s\n",
                               s_physics_car.gear, s_physics_car.speed, s_physics_car.rpm,
                               s_physics_car.manual_transmission ? "(manual)" : "(auto)",
                               sec_idx, lateral, offtrack ? "OFF-TRACK" : "on track");
                    } else {
                        printf("[physics] gear=%d speed=%.1f rpm=%.0f %s\n",
                               s_physics_car.gear, s_physics_car.speed, s_physics_car.rpm,
                               s_physics_car.manual_transmission ? "(manual)" : "(auto)");
                    }
                }
            }
        }

        if (tex_page != NULL) {
            draw_texture_demo_scene(tex_page);
        } else if (track != NULL && track->ready) {
            draw_track_view(track);
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
    /* Optional demo modes, both purely additive over the default phase
     * 1-3 animated demo:
     *   rr_pc_port /path/to/TEX0.TMS                  -- texture demo
     *   rr_pc_port --track /path/to/MAP.RRM /path/to/IDX.HED -- track demo
     * See the file header comment and the load_demo_*()/draw_*_scene()
     * functions above for what each does. */
    const char *tex_path = NULL;
    const char *track_map_path = NULL, *track_idx_path = NULL, *track_tex_path = NULL;
    TimFile tex_file;
    TimFile track_tex_file;
    const TimPage *tex_page = NULL;
    const TimPage *track_tex_page = NULL;
    TrackDemo track;

    memset(&tex_file, 0, sizeof(tex_file));
    memset(&track_tex_file, 0, sizeof(track_tex_file));
    memset(&track, 0, sizeof(track));

    /* --physicsdata <PSX.EXE> can appear anywhere in argv (not
     * positional) so it composes freely with both the plain texture-demo
     * arg and --track's own positional args below -- scan and pull it
     * out (both tokens) BEFORE the positional parsing so it can never be
     * misread as one of --track's positional slots (map/idx/tex path). */
    {
        int i, j;
        const char *physicsdata_path = NULL;
        /* ROUND 43: --selfdrive <N> <prefix> -- headless capture mode:
         * the AUTHENTIC physics core (physics_psx.c) drives the real
         * course with the round-42 harness driver while the 3D drive
         * renderer draws each frame; every 2nd frame is dumped as
         * <prefix>NNNN.ppm. Requires --track and --physicsdata. */
        /* ROUND 51: --objfile <OBJ.RRO> -- loads the car/object models. */
        for (i = 1; i + 1 < argc; i++) {
            if (strcmp(argv[i], "--objfile") == 0) {
                if (!obj_load(argv[i + 1]))
                    printf("--objfile: load/parse failed for '%s'\n", argv[i + 1]);
                for (j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
                argc -= 2;
                break;
            }
        }
        /* ROUND 45: --texdir <dir> -- loads dir/TEX0.TMS..TEX4.TMS into
         * the recreated PS1 VRAM for real per-quad track textures. */
        for (i = 1; i + 1 < argc; i++) {
            if (strcmp(argv[i], "--texdir") == 0) {
                int bank;
                if (psx_vram_init(&s_vram) == 0) {
                    int total = 0;
                    for (bank = 0; bank <= 4; bank++) {
                        char pth[512];
                        FILE *tf;
                        snprintf(pth, sizeof(pth), "%s/TEX%d.TMS", argv[i + 1], bank);
                        tf = fopen(pth, "rb");
                        if (tf != NULL) {
                            long tsz;
                            uint8_t *tbuf;
                            fseek(tf, 0, SEEK_END); tsz = ftell(tf); fseek(tf, 0, SEEK_SET);
                            tbuf = (tsz > 0) ? (uint8_t *)malloc((size_t)tsz) : NULL;
                            if (tbuf != NULL && fread(tbuf, 1, (size_t)tsz, tf) == (size_t)tsz)
                                total += psx_vram_load_tms(&s_vram, tbuf, (size_t)tsz);
                            free(tbuf);
                            fclose(tf);
                        }
                    }
                    s_vram_loaded = total > 0;
                    printf("--texdir: %d TIM pages blitted into recreated VRAM%s\n",
                           total, s_vram_loaded ? "" : " (NONE -- check the dir)");
                }
                /* pull both tokens out of argv so the positional
                 * --track parsing below can't misread them */
                for (j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
                argc -= 2;
                break;
            }
        }
        /* scan --selfdrive first: the --physicsdata scan below breaks
         * out of its loop once found, which would skip any flag that
         * comes after it on the command line. */
        for (i = 1; i + 2 < argc; i++) {
            if (strcmp(argv[i], "--selfdrive") == 0) {
                s_selfdrive_frames = atoi(argv[i + 1]);
                s_selfdrive_prefix = argv[i + 2];
                break;
            }
        }
        /* ROUND 59: --vabfiles <RR.VH> <RR.VB> -- real engine sample.
         * Spliced out of argv like --texdir so positional parsing
         * can't misread the tokens. */
        for (i = 1; i + 2 < argc; i++) {
            if (strcmp(argv[i], "--vabfiles") == 0) {
#ifdef HAVE_SDL2
                engine_vag_load(argv[i + 1], argv[i + 2], 0);
#endif
                for (j = i; j < argc - 3; j++) argv[j] = argv[j + 3];
                argc -= 3;
                break;
            }
        }
        for (i = 1; i < argc - 1; i++) {
            if (strcmp(argv[i], "--physicsdata") == 0) {
                physicsdata_path = argv[i + 1];
                for (j = i; j < argc - 2; j++) argv[j] = argv[j + 2];
                argc -= 2;
                break;
            }
        }

        if (argc > 1 && strcmp(argv[1], "--track") == 0) {
            if (argc > 2) track_map_path = argv[2];
            if (argc > 3) track_idx_path = argv[3];
            if (argc > 4) track_tex_path = argv[4];
        } else if (argc > 1) {
            tex_path = argv[1];
        }

        /* Loads the real course section-geometry table (tools/trackdata)
         * from the user's own PSX.EXE for live off-track feedback in the
         * C (real-physics) drive mode. See s_physics_trackdata's
         * declaration above and ROADMAP.md Phase 7. */
        if (physicsdata_path != NULL) {
            FILE *f = fopen(physicsdata_path, "rb");
            if (f == NULL) {
                printf("--physicsdata: could not open '%s'\n", physicsdata_path);
            } else {
                long fsize;
                uint8_t *exe_buf;
                fseek(f, 0, SEEK_END);
                fsize = ftell(f);
                fseek(f, 0, SEEK_SET);
                exe_buf = (fsize > 0) ? (uint8_t *)malloc((size_t)fsize) : NULL;
                if (exe_buf != NULL && fread(exe_buf, 1, (size_t)fsize, f) == (size_t)fsize) {
                    int rc = trackdata_parse(exe_buf, (size_t)fsize, TRACKDATA_COURSE_A_RAM_ADDR,
                                              TRACKDATA_COURSE_A_COUNT, &s_physics_trackdata);
                    if (rc == TRACKDATA_OK) {
                        s_physics_trackdata_loaded = 1;
                        printf("--physicsdata: loaded course A geometry from '%s' (%zu sections)\n",
                               physicsdata_path, s_physics_trackdata.count);
                        /* ROUND 53: the per-model piece-kit table
                         * D_80059228 lives in the same EXE (bodies,
                         * LODs, axle objects + rear-axle offsets --
                         * see psx_ai.h). Extracted here, never
                         * committed anywhere. */
                        if (psx_ai_kit_from_exe(exe_buf, (size_t)fsize))
                            printf("--physicsdata: car piece-kit table "
                                   "D_80059228 extracted (13 models)\n");
                        /* ROUND 55: real race setup -- grid roster,
                         * start spread, per-car pace (psx_ai.h). */
                        if (psx_ai_race_from_exe(exe_buf, (size_t)fsize))
                            printf("--physicsdata: race setup extracted "
                                   "(roster/grid/pace, func_80021048)\n");
                    } else {
                        printf("--physicsdata: failed to parse course data from '%s' (rc=%d) -- "
                               "real-physics mode will still work, just without live off-track "
                               "feedback\n", physicsdata_path, rc);
                    }
                } else {
                    printf("--physicsdata: could not read '%s'\n", physicsdata_path);
                }
                free(exe_buf);
                fclose(f);
            }
        }
    }

    printf("rr-pc-port phase 1+2+3 vertical slice\n");

    exercise_ported_functions();
    exercise_ported_logic_round1();

    if (tex_path != NULL) {
        printf("-- texture demo mode: loading '%s' --\n", tex_path);
        load_demo_texture_page(tex_path, &tex_file, &tex_page);
        /* On failure, tex_page stays NULL -- falls back to the animated
         * demo below rather than crashing or exiting, so a bad path
         * doesn't break the rest of the vertical slice. */
    } else if (track_map_path != NULL && track_idx_path != NULL) {
        printf("-- track demo mode: loading '%s' + '%s' --\n", track_map_path, track_idx_path);
        load_demo_track(track_map_path, track_idx_path, &track);
        /* On failure, track.ready stays 0 -- same graceful fallback. */
        if (track_tex_path != NULL) {
            printf("-- track demo mode: loading road texture '%s' --\n", track_tex_path);
            if (load_demo_texture_page(track_tex_path, &track_tex_file, &track_tex_page)) {
                track.tex_rgba = track_tex_page->rgba;
                track.tex_w = track_tex_page->width;
                track.tex_h = track_tex_page->height;
            }
            /* On failure, track.tex_rgba stays NULL -- falls back to
             * the flat-colored road, same graceful-fallback convention
             * as everywhere else in this file. */
        }
    } else if (track_map_path != NULL) {
        printf("--track requires both <MAP.RRM> <IDX.HED> paths -- falling back to animated demo\n");
    }

    if (s_selfdrive_frames > 0 && s_selfdrive_prefix != NULL
        && track.ready && s_physics_trackdata_loaded) {
        /* ROUND 43 headless capture: authentic core + shared bridge +
         * round-42 driver, rendered through draw_track_drive_scene. */
        PsxTrackIface trk;
        PsxCar car;
        PsxInput in;
        int i, dumped = 0;
        s_psx_bridge.td = s_physics_trackdata;
        psx_bridge_iface(&s_psx_bridge, &trk);
        s_psx_bridge.cur = 0;
        {
            const TrackSection *s0 = &s_physics_trackdata.sections[0];
            psx_car_init(&car, (int32_t)lround(s0->x), (int32_t)lround(s0->z),
                         psx_bridge_road_dir(&s_psx_bridge, 0));
        }
        psx_bridge_seed(&s_psx_bridge, car.pos_x, car.pos_z);
        memset(&in, 0, sizeof in);
        s_cam_height = 45.0; /* chase-cam driving height, not the freecam default */
        for (i = 0; i < s_selfdrive_frames; i++) {
            int32_t steer;
            {
                static int32_t prev_lat = 0;
                int32_t road = psx_bridge_road_dir(&s_psx_bridge, s_psx_bridge.cur);
                int32_t lat = (int32_t)lround(psx_bridge_lat(&s_psx_bridge,
                        s_psx_bridge.cur, (double)car.pos_x, (double)car.pos_z));
                steer = psx_angdiff(car.vel_dir, road) * 24
                      - lat * 8 - (lat - prev_lat) * 40;
                prev_lat = lat;
                if (steer < -0x1000) steer = -0x1000;
                if (steer > 0x1000) steer = 0x1000;
            }
            {
                int32_t target = psx_bridge_corner_target(&s_psx_bridge);
                in.throttle = car.speed < target;
                in.brake = car.speed > target + 0x80;
            }
            psx_car_frame_steer(&car, &in, &trk, steer);
            psx_bridge_resolve(&s_psx_bridge, &car);
            {
                static int lap_prev = 0;
                int nsec = (int)s_psx_bridge.td.count;
                if (lap_prev > nsec - 8 && s_psx_bridge.cur < 8)
                    s_hud_lap++;
                lap_prev = s_psx_bridge.cur;
            }
            {
                /* ROUND 51: chase cam WITH a collision probe -- pull
                 * the camera in whenever its would-be position leaves
                 * the track width (checked through the same bridge
                 * lateral test the physics uses). */
                double fx = psx_cos(car.heading) / 4096.0;
                double fz = psx_sin(car.heading) / 4096.0;
                double dist = 300.0;
                int probe;
                int saved_cur = s_psx_bridge.cur; /* preserve the deck-
                    continuity walk state -- a full re-seed at the car
                    can snap to the WRONG DECK at the overpass and
                    derail the driver (found round 51) */
                /* ROUND 53 camera fix: the round-51 probe demanded the
                 * camera inside 55% of the width and cut the distance
                 * by 45% per try -- on any real corner the straight-
                 * line point 300 behind the car leaves the road, so
                 * the loop collapsed dist to ~9 and parked the camera
                 * INSIDE the car model (the mid-lap "red polygon
                 * soup" every static GIF since round 48 hid). Now:
                 * accept anywhere on the actual road width, back off
                 * gently, and never come closer than 140. */
                for (probe = 0; probe < 6; probe++) {
                    double cx2 = (double)car.pos_x - fx * dist;
                    double cz2 = (double)car.pos_z - fz * dist;
                    const TrackSection *sc2;
                    double lat2, w2;
                    psx_bridge_seed(&s_psx_bridge, (int32_t)cx2, (int32_t)cz2);
                    sc2 = &s_psx_bridge.td.sections[s_psx_bridge.cur];
                    lat2 = psx_bridge_lat(&s_psx_bridge, s_psx_bridge.cur, cx2, cz2);
                    w2 = (lat2 > 0 ? sc2->width_right : sc2->width_left) - 12.0;
                    if (fabs(lat2) <= w2 || dist * 0.78 < 140.0)
                        break;
                    dist *= 0.78;
                }
                if (dist < 140.0) dist = 140.0;
                s_cam_x = (double)car.pos_x - fx * dist;
                s_cam_z = (double)car.pos_z - fz * dist;
                s_cam_yaw = atan2(fx, fz);
                s_psx_bridge.cur = saved_cur; /* restore, no re-seed */
                s_car_draw_on = s_obj_buf != NULL;
                s_car_draw_x = (double)car.pos_x;
                s_car_draw_z = (double)car.pos_z;
                /* the draw path measures heights relative to the
                 * camera ground sample -- put the car ON that ground */
                s_car_draw_y = -1.0; /* sentinel: resolved in the draw */
                s_car_draw_heading = car.heading;
                s_car_draw_wheel = car.wheel_rot; /* authentic +0x38 */
                s_car_draw_model = 0;
                s_draw_cars[0].roll = 0;
                /* ROUND 53: the real AI opponents (func_80025268 port,
                 * see psx_ai.h's confirmed/approximated ledger). */
                ai_step_and_fill(&s_psx_bridge, (double)s_psx_bridge.cur,
                                 s_obj_buf != NULL);
            }
            if ((i & 1) == 0) {
                char path[512];
                FILE *pf;
                int px, py;
                /* ROUND 61: the first 60 dumped frames of a selfdrive
                 * capture are the boot title screen (real logo). */
                if (dumped < 60 && s_vram_loaded) {
                    draw_title_screen(dumped);
                } else if (dumped < 150 && s_vram_loaded) {
                    /* ROUND 62: car-select showcase, one model per
                     * 30 frames, spinning */
                    draw_car_select(dumped * 4, ((dumped - 60) / 30) % 12);
                } else {
                draw_track_drive_scene(&track);
                draw_race_hud(car.speed, (int)car.gear, s_hud_lap + 1, car.rpm);
                }
                snprintf(path, sizeof(path), "%s%04d.ppm", s_selfdrive_prefix, dumped);
                pf = fopen(path, "wb");
                if (pf) {
                    fprintf(pf, "P6\n%d %d\n255\n", GPU_FB_WIDTH, GPU_FB_HEIGHT);
                    for (py = 0; py < GPU_FB_HEIGHT; py++)
                        for (px = 0; px < GPU_FB_WIDTH; px++) {
                            uint32_t t = gpu_framebuffer[py * GPU_FB_WIDTH + px];
                            fputc((int)((t >> 16) & 0xFF), pf);
                            fputc((int)((t >> 8) & 0xFF), pf);
                            fputc((int)(t & 0xFF), pf);
                        }
                    fclose(pf);
                    dumped++;
                }
            }
        }
        printf("selfdrive capture: %d physics frames, %d ppm frames dumped "
               "(gear=%d speed=0x%X sec=%d)\n",
               s_selfdrive_frames, dumped, (int)car.gear,
               (unsigned)car.speed, s_psx_bridge.cur);
        trackdata_free(&s_physics_trackdata);
        track_demo_free(&track);
        if (tex_file.pages) tim_free(&tex_file);
        return 0;
    }

#ifdef HAVE_SDL2
    if (run_sdl_loop(tex_page, &track) != 0) {
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
    } else if (track.ready) {
        int px, distinct = 0;
        uint32_t first;
        draw_track_demo_scene(&track);
        first = gpu_framebuffer[0];
        for (px = 0; px < GPU_FB_WIDTH * GPU_FB_HEIGHT; px++) {
            if (gpu_framebuffer[px] != first) { distinct = 1; break; }
        }
        printf("(built without SDL2 -- headless track demo: drew the track into "
               "gpu_framebuffer[], %s)\n",
               distinct ? "framebuffer shows real variation (not solid color)"
                        : "WARNING: framebuffer is a single solid color");
    } else {
        printf("(built without SDL2 -- window/event loop skipped, logic-only run)\n");
    }
#endif

    tim_free(&tex_file);
    tim_free(&track_tex_file);
    track_demo_free(&track);
    if (s_physics_trackdata_loaded) trackdata_free(&s_physics_trackdata);

    printf("phase 1+2+3 vertical slice complete, exiting 0\n");
    return 0;
}
