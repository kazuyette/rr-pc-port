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
 *          control back to the player.
 *      Both files are never bundled/committed -- point this at your own
 *      local extraction, e.g.:
 *        ./rr_pc_port --track /path/to/MAP.RRM /path/to/IDX.HED
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
#include "map_rrm.h"
#include "idx_hed.h"

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
} TrackDemo;

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

static void track_view_reset_top(void) {
    s_track_zoom = 1.0;
    s_track_pan_x = 0.0;
    s_track_pan_z = 0.0;
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
            double wx = ox + v[0], wz = oz + v[2];
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
        sum_x[tr->section_index] += ox + tr->rec.v0[0];
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
        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (!track_demo_is_real_road_section(mf, tr->section_index)) continue;
        if (idx_hed_section_world_origin(idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;

        x0 = (int)(((ox + tr->rec.v0[0]) - cx) * scale) + GPU_FB_WIDTH / 2;
        y0 = (int)(((oz + tr->rec.v0[2]) - cz) * scale) + GPU_FB_HEIGHT / 2;
        x1 = (int)(((ox + tr->rec.v1[0]) - cx) * scale) + GPU_FB_WIDTH / 2;
        y1 = (int)(((oz + tr->rec.v1[2]) - cz) * scale) + GPU_FB_HEIGHT / 2;
        x2 = (int)(((ox + tr->rec.v3[0]) - cx) * scale) + GPU_FB_WIDTH / 2; /* note: v3 before v2 -- perimeter order */
        y2 = (int)(((oz + tr->rec.v3[2]) - cz) * scale) + GPU_FB_HEIGHT / 2;
        x3 = (int)(((ox + tr->rec.v2[0]) - cx) * scale) + GPU_FB_WIDTH / 2;
        y3 = (int)(((oz + tr->rec.v2[2]) - cz) * scale) + GPU_FB_HEIGHT / 2;

        gpu_draw_quad_flat(x0, y0, x1, y1, x2, y2, x3, y3, 0x003C965A);
    }
}

/* One projected, depth-sortable quad, staged by draw_track_drive_scene
 * before the actual draw calls so the whole frame's worth of quads can
 * be painter's-algorithm sorted back-to-front first. */
typedef struct {
    double depth;
    int x0, y0, x1, y1, x2, y2, x3, y3;
    uint32_t color;
} TrackDriveQuadJob;

#define TRACK_DRIVE_MAX_QUADS 8192
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
    const double far_fog = 6000.0;    /* world units; distance fog fades to background by here */
    /* Screen coords are clamped to this before handing to
     * gpu_draw_quad_flat -- keeps the int edge-function math in
     * gpu_soft.c (see gpu_draw_triangle_flat) well inside int32 range
     * even for near-camera geometry that projects far off-screen,
     * while still being well outside the visible framebuffer. */
    const int coord_clamp = 20000;

    gpu_clear(0x00080810);

    for (r = 0; r < mf->record_count && njobs < TRACK_DRIVE_MAX_QUADS; r++) {
        const MapRrmTaggedRecord *tr = &mf->records[r];
        int32_t ox, oz;
        int c, behind = 0;
        double rightv[4], depthv[4];
        double avg_depth = 0.0;
        int px[4], py[4];
        int fog;
        uint32_t color;

        if (tr->type != MAP_RRM_RECORD_TYPE_B) continue;
        if (!track_demo_is_real_road_section(mf, tr->section_index)) continue;
        if (idx_hed_section_world_origin(idxf, tr->section_index, &ox, &oz) != IDX_HED_OK) continue;

        for (c = 0; c < 4; c++) {
            /* v0,v1,v3,v2 perimeter order, same convention as the
             * top-down view's pass 2. */
            const int16_t *v = (c == 0) ? tr->rec.v0 : (c == 1) ? tr->rec.v1 : (c == 2) ? tr->rec.v3 : tr->rec.v2;
            double wx = ox + v[0], wz = oz + v[2];
            double dx = wx - s_cam_x, dz = wz - s_cam_z;
            rightv[c] = dx * cos_yaw - dz * sin_yaw;
            depthv[c] = dx * sin_yaw + dz * cos_yaw;
            if (depthv[c] <= near_plane) behind = 1;
            avg_depth += depthv[c];
        }
        if (behind) continue; /* simple near-plane cull, no clipping -- see file header comment */
        avg_depth /= 4.0;

        for (c = 0; c < 4; c++) {
            px[c] = (int)((rightv[c] / depthv[c]) * focal) + GPU_FB_WIDTH / 2;
            py[c] = (int)((s_cam_height / depthv[c]) * focal) + GPU_FB_HEIGHT / 2;
            if (px[c] < -coord_clamp) px[c] = -coord_clamp;
            if (px[c] > coord_clamp) px[c] = coord_clamp;
            if (py[c] < -coord_clamp) py[c] = -coord_clamp;
            if (py[c] > coord_clamp) py[c] = coord_clamp;
        }

        fog = (int)(255.0 * (1.0 - avg_depth / far_fog));
        if (fog < 40) fog = 40;
        if (fog > 255) fog = 255;
        color = ((uint32_t)((0x3C * fog) / 255) << 16) |
                ((uint32_t)((0x96 * fog) / 255) << 8) |
                (uint32_t)((0x5A * fog) / 255);

        s_track_drive_jobs[njobs].depth = avg_depth;
        s_track_drive_jobs[njobs].x0 = px[0]; s_track_drive_jobs[njobs].y0 = py[0];
        s_track_drive_jobs[njobs].x1 = px[1]; s_track_drive_jobs[njobs].y1 = py[1];
        s_track_drive_jobs[njobs].x2 = px[2]; s_track_drive_jobs[njobs].y2 = py[2];
        s_track_drive_jobs[njobs].x3 = px[3]; s_track_drive_jobs[njobs].y3 = py[3];
        s_track_drive_jobs[njobs].color = color;
        njobs++;
    }

    qsort(s_track_drive_jobs, (size_t)njobs, sizeof(s_track_drive_jobs[0]), track_drive_job_cmp);
    for (r = 0; r < (size_t)njobs; r++) {
        const TrackDriveQuadJob *j = &s_track_drive_jobs[r];
        gpu_draw_quad_flat(j->x0, j->y0, j->x1, j->y1, j->x2, j->y2, j->x3, j->y3, j->color);
    }
}

/* Dispatches to whichever track-demo view mode is active. */
static void draw_track_view(const TrackDemo *td) {
    if (s_track_view_mode == TRACK_VIEW_DRIVE) {
        draw_track_drive_scene(td);
    } else {
        draw_track_demo_scene(td);
    }
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
        if (track != NULL && track->ready) {
            printf("track demo controls: V toggles top-down/drive view; "
                   "top-down: arrow keys pan, +/- zoom; "
                   "drive: up/down move, left/right turn, +/- height, "
                   "P toggles autopilot (auto-drives a lap using the "
                   "confirmed section-order path -- any direction key "
                   "hands control back); "
                   "R resets the active view\n");
            s_track_view_mode = TRACK_VIEW_TOP;
            s_autopilot_on = 0;
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
                                break;
                            case SDLK_UP:
                                s_autopilot_on = 0;
                                s_cam_x += sin(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                s_cam_z += cos(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                break;
                            case SDLK_DOWN:
                                s_autopilot_on = 0;
                                s_cam_x -= sin(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                s_cam_z -= cos(s_cam_yaw) * TRACK_DRIVE_MOVE_STEP;
                                break;
                            case SDLK_LEFT:  s_autopilot_on = 0; s_cam_yaw -= TRACK_DRIVE_TURN_STEP; break;
                            case SDLK_RIGHT: s_autopilot_on = 0; s_cam_yaw += TRACK_DRIVE_TURN_STEP; break;
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
    const char *track_map_path = NULL, *track_idx_path = NULL;
    TimFile tex_file;
    const TimPage *tex_page = NULL;
    TrackDemo track;

    memset(&tex_file, 0, sizeof(tex_file));
    memset(&track, 0, sizeof(track));

    if (argc > 1 && strcmp(argv[1], "--track") == 0) {
        if (argc > 2) track_map_path = argv[2];
        if (argc > 3) track_idx_path = argv[3];
    } else if (argc > 1) {
        tex_path = argv[1];
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
    } else if (track_map_path != NULL) {
        printf("--track requires both <MAP.RRM> <IDX.HED> paths -- falling back to animated demo\n");
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
    track_demo_free(&track);

    printf("phase 1+2+3 vertical slice complete, exiting 0\n");
    return 0;
}
