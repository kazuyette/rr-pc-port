/* psx_ai.c -- see psx_ai.h for the provenance ledger (what is an
 * instruction-level trace of func_80025268 + callees vs. what is
 * approximated until func_80023C58 / the race-setup tables fall). */
#include "psx_ai.h"

#include <math.h>

PsxCarKit psx_ai_kit[PSX_AI_KIT_MODELS];
int psx_ai_kit_loaded = 0;

int psx_ai_kit_from_exe(const uint8_t *exe, size_t size)
{
    /* D_80059228: RAM 0x80059228, EXE load base 0x80010000, +0x800
     * header -> file offset 0x49A28. Validated by the round-53 dump:
     * bodies {0,5,22,23,6,11,12,13,14,15,16,21}, axles 36/37/38,
     * LODs 24..35, sentinel -32767 on every row. */
    size_t off = 0x49A28;
    int m, k;
    if (size < off + PSX_AI_KIT_MODELS * 16)
        return 0;
    for (m = 0; m < PSX_AI_KIT_MODELS; m++) {
        int16_t v[8];
        for (k = 0; k < 8; k++)
            v[k] = (int16_t)(exe[off + (size_t)m * 16 + (size_t)k * 2] |
                             (exe[off + (size_t)m * 16 + (size_t)k * 2 + 1] << 8));
        psx_ai_kit[m].axle_obj = v[0];
        psx_ai_kit[m].body_obj = v[1];
        psx_ai_kit[m].lod_obj = v[2];
        psx_ai_kit[m].aux_obj = v[3];
        psx_ai_kit[m].axle_dz = v[4];
        psx_ai_kit[m].axle_dz_model = v[5];
        psx_ai_kit[m].f6 = v[6];
        psx_ai_kit[m].sentinel = v[7];
    }
    /* sanity: every consumer row ends in the -32767 sentinel */
    for (m = 0; m < 12; m++)
        if (psx_ai_kit[m].sentinel != -32767)
            return 0;
    psx_ai_kit_loaded = 1;
    return 1;
}

PsxAiSlotSetup psx_ai_setup[12];
int psx_ai_setup_loaded = 0;

int psx_ai_race_from_exe(const uint8_t *exe, size_t size)
{
    /* RAM->file: -0x80010000 + 0x800. Roster D_80073130 (12 x int32),
     * grid D_800731C0 (12 x 16B, +0xC = start progress section<<8),
     * pace D_80073560 (12 x 24B, +0x4 = cruise limit / 8, AI units).
     * The player grids at section 68 (grid slot 0's progress). */
    size_t roster = 0x80073130 - 0x80010000 + 0x800;
    size_t grid = 0x800731C0 - 0x80010000 + 0x800;
    size_t pace = 0x80073560 - 0x80010000 + 0x800;
    int32_t player_start;
    int s;
    if (size < pace + 12 * 24)
        return 0;
    player_start = (int32_t)(exe[grid + 12] | (exe[grid + 13] << 8) |
                             (exe[grid + 14] << 16) | (exe[grid + 15] << 24));
    for (s = 0; s < 12; s++) {
        int32_t model = (int32_t)(exe[roster + s * 4] |
                                  (exe[roster + s * 4 + 1] << 8) |
                                  (exe[roster + s * 4 + 2] << 16) |
                                  (exe[roster + s * 4 + 3] << 24));
        int32_t prog = (int32_t)(exe[grid + s * 16 + 12] |
                                 (exe[grid + s * 16 + 13] << 8) |
                                 (exe[grid + s * 16 + 14] << 16) |
                                 (exe[grid + s * 16 + 15] << 24));
        int32_t lim = (int32_t)(exe[pace + s * 24 + 4] |
                                (exe[pace + s * 24 + 5] << 8) |
                                (exe[pace + s * 24 + 6] << 16) |
                                (exe[pace + s * 24 + 7] << 24));
        int32_t acc = (int32_t)(exe[pace + s * 24 + 12] |
                                (exe[pace + s * 24 + 13] << 8) |
                                (exe[pace + s * 24 + 14] << 16) |
                                (exe[pace + s * 24 + 15] << 24));
        int32_t gx = (int32_t)(exe[grid + s * 16] | (exe[grid + s * 16 + 1] << 8) |
                               (exe[grid + s * 16 + 2] << 16) | (exe[grid + s * 16 + 3] << 24));
        int32_t gz = (int32_t)(exe[grid + s * 16 + 4] | (exe[grid + s * 16 + 5] << 8) |
                               (exe[grid + s * 16 + 6] << 16) | (exe[grid + s * 16 + 7] << 24));
        psx_ai_setup[s].model = model;
        psx_ai_setup[s].start_rel = (double)(prog - player_start) / 256.0;
        psx_ai_setup[s].limit = lim * 64; /* AI units *8, x8 to player */
        psx_ai_setup[s].accel = acc;
        /* ROUND 56: grid x/z are in the game's MESH/GTE frame; the
         * physics frame is x_phys = 0xF000 - x_mesh (D_801733A0 =
         * 0xF000, func_80015CD4 -- and the slot-0 cross-check:
         * 61440 - sec68.x(34604) = 26836 ~ grid.x 27126, the delta
         * being exactly the grid lane). */
        psx_ai_setup[s].grid_x = 61440.0 - (double)gx;
        psx_ai_setup[s].grid_z = (double)gz;
    }
    /* sanity: roster slot 0 is model 2 on the retail EXE */
    if (psx_ai_setup[0].model != 2)
        return 0;
    psx_ai_setup_loaded = 1;
    return 1;
}

int32_t psx_ai_blend_angle(int32_t a, int32_t b, int32_t w)
{
    /* exact func_800177B8: circular lerp with +-0x800 wrap fixup */
    a &= 0xFFF; b &= 0xFFF;
    if (b < a) {
        if (a - b > 0x800) b += 0x1000;
    } else {
        if (b - a > 0x800) a += 0x1000;
    }
    return ((a * (256 - w) + b * w) >> 8) & 0xFFF;
}

void psx_ai_init(PsxAiCar *c, int slot, const PsxBridge *b)
{
    int n = (int)b->td.count;
    static const double lane_pat[4] = { -60.0, 55.0, -30.0, 80.0 };
    c->active = 1;
    if (psx_ai_setup_loaded && slot < 12 && psx_ai_setup[slot].model >= 0) {
        /* ROUND 55: the REAL race -- roster model, real start spread
         * (slots 0-4 grid with the player, 5-10 strung out ahead:
         * the rolling field RR1 has you chase down), real per-car
         * cruise limit (see psx_ai.h). */
        c->model = psx_ai_setup[slot].model;
        c->progress = 1.0 + slot * 0.35 + psx_ai_setup[slot].start_rel;
        c->limit = psx_ai_setup[slot].limit;
        c->accel = 4 + psx_ai_setup[slot].accel / 3; /* APPROX mapping */
        /* ROUND 56: REAL grid lane -- lateral offset of the grid
         * position from the centerline at that slot's start section. */
        {
            int gsec = (int)c->progress;
            double lat;
            while (gsec >= n) gsec -= n;
            while (gsec < 0) gsec += n;
            lat = psx_bridge_lat(b, gsec, psx_ai_setup[slot].grid_x,
                                 psx_ai_setup[slot].grid_z);
            if (lat > -200.0 && lat < 200.0)
                c->lane = lat;
            else
                c->lane = lane_pat[slot & 3]; /* far slots: keep spread */
        }
    } else if (psx_ai_setup_loaded && slot < 12) {
        c->active = 0; /* roster slot off (-1): authentic 11-car field */
        c->model = 0;
        c->progress = 0.0;
        c->limit = 0;
        c->accel = 0;
    } else {
        /* no EXE: round-53 fallback */
        c->model = 1 + (slot % 11);
        c->progress = 1.5 + slot * 1.1;
        c->limit = 0;
        c->accel = 7 + (slot % 5) * 2;
    }
    while (c->progress >= (double)n) c->progress -= (double)n;
    while (c->progress < 0.0) c->progress += (double)n;
    c->speed = 0;
    c->heading = 0;
    c->roll = 0;
    c->rubber = 0;
    c->wheel = (slot * 353) & 0xFFF;
    c->lane = lane_pat[slot & 3] * (0.6 + 0.05 * (slot >> 2));
    c->x = c->y = c->z = 0.0;
    c->lap = 0;
}

/* Curvature-derived per-section speed limit (APPROXIMATED stand-in
 * for the stored +0xF8 field the game reads; same lookahead math as
 * psx_bridge_corner_target but valid at any section). */
static int32_t section_limit(const PsxBridge *b, int sec)
{
    int n = (int)b->td.count;
    int32_t curdir = psx_bridge_road_dir(b, sec);
    int32_t worst = 0, k;
    for (k = 2; k <= 10; k += 2) {
        int32_t d = psx_angdiff(curdir, psx_bridge_road_dir(b, (sec + k) % n));
        if (d < 0) d = -d;
        d = d * 12 / k;
        if (d > worst) worst = d;
    }
    {
        int32_t t = 0xA00 - worst * 4;
        if (t < 0x260) t = 0x260;
        return t;
    }
}

void psx_ai_frame(PsxAiCar *c, PsxBridge *b, double player_progress)
{
    int n = (int)b->td.count;
    int sec, nxt;
    int32_t frac256, limit, target;
    double frac, fx, fz, len, cx, cz;
    const TrackSection *sa, *sb;

    if (!c->active || n < 2)
        return;

    sec = (int)c->progress;
    if (sec >= n) sec = n - 1;
    frac = c->progress - (double)sec;
    frac256 = (int32_t)(frac * 256.0);
    if (frac256 > 255) frac256 = 255;
    nxt = (sec + 1) % n;

    /* --- target speed: CONFIRMED structure limit*8/10 + accel.
     * ROUND 55: when the real pace table is loaded, the limit is the
     * AUTHENTIC per-car cruise pace (+0xF8 = pace[+4]*8, converted to
     * player units); the curvature stand-in remains the no-EXE
     * fallback only. --------------------------------------------- */
    limit = c->limit > 0 ? c->limit : section_limit(b, sec);
    target = limit * 8 / 10;
    {
        /* rubber band -- ROUND 54: func_80023C58 traced. The game
         * keeps a progress window [D_8012CD80, D_8007C510] around the
         * player; leaving it puts the car in catch-up state (+0xA2 =
         * 4) with a target progress of 9/10 of the crossed bound, and
         * slews a LEVEL (+0x60) by +-1/frame toward +-3, in +-5
         * section-wide distance bands (0 exactly on target). The
         * level->speed mapping lives in the untraced state handlers,
         * so ~5% per level here is the one APPROXIMATED constant. */
        double d = c->progress - player_progress;
        int32_t want;
        while (d > n / 2.0) d -= n;
        while (d < -n / 2.0) d += n;
        if (d < -4.0) want = 3;        /* far behind -> full catch-up */
        else if (d > 4.0) want = -3;   /* far ahead  -> full wait-up  */
        else if (d < -1.0) want = 1;
        else if (d > 1.0) want = -1;
        else want = 0;
        if (c->rubber < want) c->rubber++;
        else if (c->rubber > want) c->rubber--;
        target = target * (100 + 5 * c->rubber) / 100;
    }
    if (c->speed < target) {
        c->speed += c->accel;
        if (c->speed > target) c->speed = target;
    } else if (c->speed > target) {
        c->speed -= 24;
        if (c->speed < target) c->speed = target;
    }

    /* --- heading: CONFIRMED -- circular blend of the road direction
     * across the section boundary by the progress fraction ---------- */
    c->heading = psx_ai_blend_angle(psx_bridge_road_dir(b, sec),
                                    psx_bridge_road_dir(b, nxt), frac256);

    /* --- lean controller: exact func_80023FF8 (+-0x50 deadband,
     * +-2/frame slew, bounds +0x1E/-0xE) ---------------------------- */
    {
        int32_t d = psx_angdiff(psx_bridge_road_dir(b, sec),
                                psx_bridge_road_dir(b, (sec + 2) % n));
        if (d >= -0x50 && d <= 0x50) {
            if (c->roll > 0) c->roll -= 2;
            else if (c->roll < 0) c->roll += 2;
        } else if (d > 0x50) {
            if (c->roll >= -0xE) c->roll -= 2;
        } else {
            if (c->roll < 0x1E) c->roll += 2;
        }
    }

    /* --- wheel: CONFIRMED AI variant (+= speed, blur >= 0x321) ----- */
    c->wheel = (c->wheel + c->speed) & 0xFFF;
    if (c->speed >= 0x321)
        c->wheel |= 0x1000;

    /* --- integrate progress: world step = speed/16 (the player core
     * integrates pos += vel>>8 with |vel| = speed<<4) --------------- */
    sa = &b->td.sections[sec];
    sb = &b->td.sections[nxt];
    fx = sb->x - sa->x;
    fz = sb->z - sa->z;
    len = sqrt(fx * fx + fz * fz);
    if (len < 1e-9) len = 1.0;
    c->progress += ((double)c->speed / 16.0) / len;
    if (c->progress >= (double)n) {
        c->progress -= (double)n;
        c->lap++;
    }

    /* --- pose: centerline + lane (kept inside the section width) --- */
    sec = (int)c->progress;
    if (sec >= n) sec = n - 1;
    frac = c->progress - (double)sec;
    sa = &b->td.sections[sec];
    sb = &b->td.sections[(sec + 1) % n];
    fx = sb->x - sa->x;
    fz = sb->z - sa->z;
    len = sqrt(fx * fx + fz * fz);
    if (len < 1e-9) len = 1.0;
    fx /= len; fz /= len;
    {
        double lane = c->lane;
        double wl = (lane > 0.0 ? sa->width_right : sa->width_left) - 24.0;
        if (wl > 0.0 && fabs(lane) > wl)
            lane = lane > 0.0 ? wl : -wl;
        cx = sa->x + (sb->x - sa->x) * frac + fz * lane;
        cz = sa->z + (sb->z - sa->z) * frac - fx * lane;
    }
    c->x = cx;
    c->z = cz;
    /* y stays caller-resolved (ground sampler lives in the renderer) */
}
