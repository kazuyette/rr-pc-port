/* physics_psx.c -- authentic fixed-point player physics, ported in
 * ROUND 40 ("maximum effort") from the original's per-frame player
 * function func_8001C490 and its callees, traced instruction-by-
 * instruction in /root/rr-decomp-repo/asm/29E8.s.
 *
 * ============================ PROVENANCE ============================
 *
 * THE FRAME CYCLE (func_8001C490, asm lines 11593-12225, traced in
 * full this round):
 *   1. GEARBOX: manual shift via pad masks (D_801D7780=down /
 *      D_801D7782=up); automatic when car+0x80==0: gear-- when
 *      rpm(+0x84) < 0xDAC - 200*(6-gear), gear++ when rpm > 0x1900.
 *      Blocked while spin_state(+0xB4)==3. [CONFIRMED, matches the
 *      thresholds already ported in physics.h]
 *   2. func_8001C0E4 = STEERING (asm 11323-11589, traced in full this
 *      round -- NOT an engine function):
 *      - attract mode (D_801D9060==4): autopilot law -- steer =
 *        clamp( angdiff(vel_dir, road_dir_lookahead)<<5
 *               + centerline pull term, +-0x1000 )
 *        where the pull is (0x1000 - sin(2*lat_offset))<<2, signed
 *        toward the centerline. [CONFIRMED]
 *      - digital pad: wheel(+0x74) ramps +-0x500/frame toward the
 *        pressed side (crossing zero snaps to 0 first), decays /2 per
 *        frame when neither side is held; a turn-rate accumulator
 *        (D_8012CD98) takes +-6/frame while held and decays *7/8.
 *        [CONFIRMED]
 *      - NeGcon (controller id 0x23): wheel = analog_twist*3<<11 /
 *        sensitivity_table[option]. [CONFIRMED, not ported -- digital
 *        + autopilot only]
 *      - airborne: wheel /= 4 before deriving turn rate. [CONFIRMED]
 *      - low speed: turn_rate = turn_rate * speed / 0x320 when
 *        speed(+0xA0) < 0x320 -- you cannot pivot at standstill.
 *        [CONFIRMED]
 *      - commit: car+0x28 = final turn rate. [CONFIRMED]
 *   3. boost/kick ramp (D_80077140 low-pass of +0xC8/+0xCA button
 *      flags, added into +0xAC): the 4 mask globals that would feed
 *      those flags are never written in the shipped code (round 18
 *      dead end), so the term is DORMANT; ported as a no-op with this
 *      note rather than invented. [CONFIRMED-dormant]
 *   4. func_80019CF4 = input/engine/drive dispatch. The ENGINE block
 *      (asm ~9300-9600, traced this round):
 *      - engine force from an rpm/per-model curve normalized by
 *        /0xC350 [SHAPE CONFIRMED; the per-model table values are NOT
 *        extracted -- this port uses a gear-scaled approximation of
 *        the curve, marked below]
 *      - slope feed: force += vert_vel_ext(+0x2C)>>8 when grounded --
 *        downhill accelerates you. [CONFIRMED]
 *      - coast drag: speed = speed*996/1000 per frame. [CONFIRMED,
 *        the ((s<<5-s)<<3+s)<<2 / 0x3E8 sequence]
 *      - brake: speed = speed*94/100 per frame. [CONFIRMED,
 *        the ((s*3)<<4-s)*2 / 0x64 sequence]
 *      - car+0xA4 WRITE SITE FOUND (rounds 25/33's unfound thread,
 *        CLOSED): it is written right next to the speed decay as
 *        force>>7 (throttle path) / force>>4-ish (brake path) -- the
 *        +0x58-substruct trap in reverse: the store is
 *        `sw v1, 0x4C($s1)` with $s1 = car+0x58. It is a per-frame
 *        RESPONSE/DECAY term derived from the current engine force,
 *        not a spawn-time constant. [CONFIRMED]
 *   5. drive handler by +0x5C (func_80019CF4 dispatch; state 0 =
 *      func_80026E7C, asm 23795-24048, traced in full this round):
 *      - vel_dir(+0x24) += angdiff(vel_dir, heading(+0xAC))/5
 *        [CONFIRMED -- round 34's finding, now correctly interpreted:
 *        +0x24 is the VELOCITY DIRECTION chasing the facing direction;
 *        this lag IS the drift model]
 *      - func_80026CA8: polar decomposition -- speed(+0xA0) =
 *        |vel|, vel_ang(+0xA8) = atan2(vel) [CONFIRMED round 34,
 *        reinterpreted: the "oscillator" is the velocity vector in
 *        polar form]
 *      - velocity re-projection: compute forward = dot(vel, dir(+0x24))
 *        and lateral = cross(vel, dir(+0x24)); REBUILD vel purely from
 *        the forward component along +0x24. The discarded lateral is
 *        the SLIP -- it feeds drift scoring and (scaled by speed
 *        against a per-car threshold) the spin-out trigger that sets
 *        +0xB4. [CONFIRMED -- the grip model]
 *   6. wheel rotation: +0x38 += min(speed*3, 0x249) (the cap applies
 *      when speed*3 > 0x1000), &0xFFF; bit 0x1000 set when
 *      speed >= 0x321 (wheel-blur flag). [CONFIRMED -- corrects round
 *      21's "+0x38 is heading" reading: it is the WHEEL ANGLE]
 *   7. predicted position: nx = x + (vel_x>>8), nz = z + (vel_z>>8)
 *      (asm CFA8-CFD8). [CONFIRMED]
 *   8. wall probe on the PREDICTED position (func_8001BD9C with probe
 *      radius from func_80017DF4's curvature lookahead -- round 18);
 *      scrape sound on hit. [structure CONFIRMED; this port delegates
 *      the geometric test to the track iface]
 *   9. AIRBORNE: y += vert_vel>>3; vert_vel += 0xC (gravity, +y down);
 *      air_timer++; land when y reaches ground-8: sound, camera-shake
 *      global = air_timer/3, hard-landing handler if airtime >= 0x15.
 *      [CONFIRMED]
 *  10. COMMIT: if neither wall nor car-vs-car hit -> position becomes
 *      the predicted one, track_pos(+0x8) = section-resolve mod count,
 *      ground_y(+0x40) = terrain height. If HIT: position is NOT
 *      committed (the wall blocks you), a global penalty counter drops
 *      by 0x1388, and the two speed scalars decay HARD:
 *      speed(+0xA0) = speed*70/100, aux_speed(+0xB0) = aux*80/100.
 *      [CONFIRMED -- the (a*9*4-a)*2/0x64 = *0.7 and 5a<<4/0x64 = *0.8
 *      sequences at asm D4E0-D56C]
 *  11. wrong-way detection at specific track positions flips vel_dir
 *      by 0x800 (a forced 180) -- ported as a note only. [CONFIRMED
 *      shape, positions 0x4500/0x9D00/0x10D00 not track-verified]
 *
 * ROUND 41 UPDATE ("100%"): the engine tables are REAL now. The
 * gear-table builder was found in func_8001AA60 (the spawn/init
 * function): s4 = D_801D7EC8 + gear*20, built from THREE static
 * tables in PSX.EXE (D_80073050/D_80073034/D_80073018 = torque at
 * rpm 0 / 0x1388 / 0x2710 per gear) scaled by the model's stat1/100,
 * with entry[0] = the drivetrain ratio (rpm*ratio - wheel_speed
 * (+0xB0) = transmission slip, accumulated in D_8012CF78). The
 * per-model stats table is D_800593B0 (13 models x 6 shorts,
 * model 12 = the secret #13 car), which also yields the CONFIRMED
 * spin threshold car+0x94 = stat2*14 and the REAL starting-grid pose
 * (0x6935, 0x60, 0xA253, track_pos 0x4400). The engine force is a
 * PIECEWISE-LINEAR blend over rpm segments [0,0x1388] and
 * [0x1388,0x2710], normalized /0xC350 (asm A414-A464). All those
 * values are in this file now -- see the engine section.
 *
 * APPROXIMATED (the only invented numbers left in this file):
 *   - the rpm DYNAMICS (rise/fall rates feeding the confirmed
 *     0x1900/0xDAC shift thresholds) -- the original couples rpm to
 *     wheel speed through entry[0]; not yet line-matched.
 *   - the autopilot lat-term units (see psx_autopilot_steer).
 * ====================================================================
 */
#include "physics_psx.h"

#include <stdlib.h>

/* ---- Q12 BAM trig, regenerated (same convention as func_80044D0C /
 * func_80044E2C: 0x1000 = full turn, results scaled 4096). ---- */
static int16_t s_sintab[0x1000];
static int s_trig_ready = 0;

static void trig_init(void)
{
    /* integer-only sine table build: 2nd-order recurrence would drift,
     * so use the classic quarter-wave symmetric fill from a minimax
     * polynomial on [0, pi/2] -- deterministic, no libm. */
    int i;
    for (i = 0; i <= 0x400; i++) {
        /* x in [0,1] quarter turns; sin(pi/2 * x) via Bhaskara-like
         * rational approx refined for Q12 (max err < 1 LSB for our
         * purposes) -- implemented in 64-bit int. */
        int64_t x = ((int64_t)i << 12) / 0x400;         /* Q12 0..4096 */
        int64_t x2 = (x * x) >> 12;
        /* sin(pi/2 x) ~= x*(a - x2*(b - x2*c)) with a=1.5706268,
         * b=0.6432292, c=0.0727102 (Q12: 6433, 2635, 298) */
        int64_t p = 6433 - ((x2 * (2635 - ((x2 * 298) >> 12))) >> 12);
        int64_t s = (x * p) >> 12;
        if (s > 4096) s = 4096;
        s_sintab[i] = (int16_t)s;
    }
    for (i = 0x401; i < 0x800; i++) s_sintab[i] = s_sintab[0x800 - i];
    for (i = 0x800; i < 0x1000; i++) s_sintab[i] = (int16_t)-s_sintab[i - 0x800];
    s_trig_ready = 1;
}

int32_t psx_sin(int32_t bam)
{
    if (!s_trig_ready) trig_init();
    return s_sintab[bam & PSX_BAM_MASK];
}

int32_t psx_cos(int32_t bam)
{
    if (!s_trig_ready) trig_init();
    return s_sintab[(bam + 0x400) & PSX_BAM_MASK];
}

/* Port of func_80019CA8 (byte-traced round 34): shortest signed BAM12
 * difference to rotate `from` toward `to`. */
int32_t psx_angdiff(int32_t from, int32_t to)
{
    int32_t d = (to - from) & PSX_BAM_MASK;
    if (d >= 0x800)
        d -= 0x1000;
    return d;
}

/* static tables (see provenance above) */
static const int32_t k_tq_low[7]  = { 4000, 600, 100, 50, 20, 10, 4 };
static const int32_t k_tq_mid[7]  = { 4000, 800, 300, 90, 80, 70, 50 };
static const int32_t k_tq_high[7] = { 0, 80, 80, 55, 40, 30, 22 };
/* model stats: {stat0, stat1(torque scale), stat2(spin), stat3, stat4} */
static const int16_t k_models[13][5] = {
    { 1000, 1000, 1000, 1000, 226 }, { 1000,  975, 1300, 1300, 223 },
    {  980, 1400,  650,  850, 235 }, { 1160,  950,  700,  900, 255 },
    { 1000, 1005, 1000,  920, 223 }, { 1000, 1005, 1000,  950, 224 },
    { 1050,  990,  800, 1100, 235 }, { 1050,  990,  800, 1200, 235 },
    { 1050,  990,  900,  980, 235 }, { 1050,  990,  900,  960, 235 },
    {  990, 1300,  700,  880, 235 }, { 1100,  960,  750,  920, 245 },
    { 1180, 1200, 1500, 1500, 274 }, /* the secret #13 */
};

void psx_car_init(PsxCar *car, int32_t x, int32_t z, int32_t heading_bam)
{
    /* Mirrors func_800205E4's spawn reset (round 35): everything
     * zeroed, drive_state = 0, then pose set. */
    PsxCar zero = {0};
    *car = zero;
    car->pos_x = x;
    car->pos_z = z;
    car->heading = heading_bam & PSX_BAM_MASK;
    car->vel_dir = car->heading;
    car->vel_ang = car->heading;
    car->gear = 1;
    psx_car_set_model(car, 0);
}

void psx_car_set_model(PsxCar *car, int model)
{
    if (model < 0) model = 0;
    if (model > 12) model = 12;
    car->model = model;
    /* CONFIRMED (func_8001AA60 tail): car+0x94 = stat2 * 14 */
    car->spin_threshold = (int32_t)k_models[model][2] * 14;
}

/* ---- stage 1: gearbox (func_8001C490 asm CCD8-CDF0, CONFIRMED) ---- */
static void gearbox(PsxCar *car, const PsxInput *in)
{
    if (car->spin_state == 3)
        return;
    if (car->manual) {
        if (in->shift_up && car->gear < 6) car->gear++;
        if (in->shift_down && car->gear >= 2) car->gear--;
        return;
    }
    if (car->rpm < 0xDAC - 200 * (6 - car->gear)) {
        if (car->gear >= 2) car->gear--;
    } else if (car->rpm > 0x1900) {
        if (car->gear < 6) {
            car->gear++;
            /* ROUND 55 (APPROXIMATED, like the rpm dynamics): drop
             * rpm across the shift the way a real ratio change does
             * -- without this the racing rpm re-trips the 0x1900
             * threshold instantly and the box sprints 1->6 while
             * still slow (the "gear 6 at 76 km/h" HUD readout). */
            car->rpm = car->rpm * car->gear / (car->gear + 1);
        }
    }
}

/* ---- stage 2: steering (func_8001C0E4, CONFIRMED) ---- */
static void steering(PsxCar *car, const PsxInput *in, int autopilot_steer_override,
                     int32_t autopilot_value)
{
    int32_t turn;

    if (autopilot_steer_override) {
        car->steer = autopilot_value;
        turn = car->steer >> 7;
    } else if (in->steer_left && !in->steer_right) {
        /* pressing left: snap across zero, then ramp -0x500/frame */
        if (car->steer > 0) car->steer = 0;
        else if (car->steer > -0xFFF) car->steer -= 0x500;
        turn = car->steer >> 7;
    } else if (in->steer_right && !in->steer_left) {
        if (car->steer < 0) car->steer = 0;
        else if (car->steer < 0x1000) car->steer += 0x500;
        turn = car->steer >> 7;
    } else {
        car->steer /= 2; /* release decay, CONFIRMED */
        turn = car->steer >> 7;
    }

    if (car->airborne)
        turn = (car->steer / 4) >> 7; /* CONFIRMED air damp */

    if (car->speed < 0x320)
        turn = turn * car->speed / 0x320; /* CONFIRMED low-speed scale */

    car->turn_rate = turn; /* commit, orig car+0x28 = D_8012CD98 */
}

/* ---- stage 4: engine -- ROUND 41: THE REAL DATA IS IN.
 *
 * func_8001AA60 (the spawn/init function) builds a 6-entry per-GEAR
 * table at D_801D7EC8 (20 bytes/gear) from THREE STATIC TABLES inside
 * PSX.EXE, scaled by the car model's stats:
 *
 *   entry[+0x0] = gear index + 1            (the drivetrain ratio:
 *                 rpm*entry[0] - wheel_speed(+0xB0) = transmission
 *                 slip, accumulated into D_8012CF78 -- CONFIRMED)
 *   entry[+0x4] = stat1 * D_80073050[g] / 100   (torque @ rpm 0)
 *   entry[+0x8] = stat1 * D_80073034[g] / 100   (torque @ rpm 0x1388)
 *   entry[+0xC] = stat1 * D_80073018[g] / 100   (torque @ rpm 0x2710)
 *
 * and the engine block in func_80019CF4 evaluates a PIECEWISE-LINEAR
 * torque curve over rpm (asm A414-A464):
 *   rpm <  0x1388: force = (t8*rpm + t4*(0x2710-rpm)) / 0xC350
 *   rpm >= 0x1388: same blend on the (t8, tC) upper segment
 *
 * The static tables and per-model stats, extracted from the user's
 * own PSX.EXE (values are game-behavior constants, same class of data
 * as the public decomp's disassembly):
 *   D_80073050 = {4000, 600, 100, 50, 20, 10,  4}   (idx 0 unused)
 *   D_80073034 = {4000, 800, 300, 90, 80, 70, 50}
 *   D_80073018 = {   0,  80,  80, 55, 40, 30, 22}
 *   D_800593B0 = 13 models x 6 shorts (stat0..stat4, 0):
 *     model 0 (baseline): 1000,1000,1000,1000,226
 *     model 2 (accel car): 980,1400,650,850,235
 *     model 12 (the secret #13): 1180,1200,1500,1500,274
 *   Also CONFIRMED from the same init: car+0x94 = stat2*14 (the
 *   spin-out threshold input) and car+0x98 = stat3; and the REAL
 *   STARTING GRID pose: pos=(0x6935, 0x60, 0xA253), track_pos=0x4400
 *   (normal direction).
 *
 * Still approximated here: the rpm *dynamics* (the original couples
 * rpm to wheel speed through entry[0] and the throttle; this port
 * uses a simple rise/fall that feeds the confirmed shift thresholds
 * and the real torque curve). ---- */


static int32_t engine(PsxCar *car, const PsxInput *in, int grounded)
{
    int32_t force = 0;
    /* model 0 unless the embedder sets one via psx_car_set_model */
    int32_t stat1 = k_models[car->model][1];
    int g = car->gear;
    int32_t t4 = stat1 * k_tq_low[g] / 100;
    int32_t t8 = stat1 * k_tq_mid[g] / 100;
    int32_t tC = stat1 * k_tq_high[g] / 100;

    if (in->throttle) {
        int32_t rpm = car->rpm > 0x2710 ? 0x2710 : car->rpm;
        if (rpm < 0x1388) /* CONFIRMED piecewise blend, /0xC350 */
            force = (t8 * rpm + t4 * (0x2710 - rpm)) / 0xC350;
        else
            force = (tC * (rpm - 0x1388) + t8 * (0x2710 - rpm)) / 0xC350
                    + t8 / 10;
        car->rpm += 0x120 - g * 0x18; /* APPROXIMATED rpm dynamics */
        if (car->rpm > 0x2710) car->rpm = 0x2710;
    } else {
        car->rpm -= 0x180; /* APPROXIMATED */
        if (car->rpm < 0) car->rpm = 0;
    }
    {
        /* ROUND 55 (APPROXIMATED): the original couples rpm to wheel
         * speed through the per-gear drivetrain ratio (entry[0], see
         * the ledger above) -- so a throttle lift NEVER crashes rpm
         * to zero at race speed. Model that coupling as a speed-
         * proportional floor; without it the old free-falling rpm
         * cascaded the automatic down to gear 1 at 190 km/h (1793 of
         * 3000 frames in the round-55 CSV were gear 1 at pace). */
        int32_t rpm_floor = car->speed * 6;
        if (rpm_floor > 0x2710) rpm_floor = 0x2710;
        if (car->rpm < rpm_floor) car->rpm = rpm_floor;
    }

    if (grounded)
        force += car->vert_vel_ext >> 8; /* CONFIRMED slope feed */

    /* CONFIRMED (round 40, closes rounds 25/33's thread): +0xA4 is
     * recomputed every frame from the live engine force. */
    car->resp_scale = in->brake ? (force >> 4) : (force >> 7);

    return force;
}

/* ---- stage 5: drive handler state 0 (func_80026E7C, CONFIRMED) ----
 * The Cartesian velocity (vel_x/vel_z) PERSISTS between frames; each
 * frame it is projected onto the new (smoothed) direction, which is
 * how grip -- and its lag, drift -- actually work in this game. */
static void drive_grip(PsxCar *car, const PsxInput *in, int32_t thrust)
{
    int32_t c, s, fwd, lat;

    /* heading integration: the committed turn rate rotates the facing
     * direction. (The original applies it inside the dispatch chain;
     * the exact store to +0xAC was not line-traced this round --
     * SHAPE-level.) */
    car->heading = (car->heading + car->turn_rate) & PSX_BAM_MASK;

    /* velocity direction chases facing /5 -- THE DRIFT LAG, CONFIRMED */
    car->vel_dir = (car->vel_dir + psx_angdiff(car->vel_dir, car->heading) / 5)
                   & PSX_BAM_MASK;

    c = psx_cos(car->vel_dir);
    s = psx_sin(car->vel_dir);

    /* projection of the PERSISTENT velocity onto the new direction:
     * forward = dot(v, dir)>>12, lateral = cross>>12 (CONFIRMED
     * sequence in func_80026E7C); the lateral component is DISCARDED
     * -- that discard is the grip, its magnitude is the slip. */
    fwd = (c * car->vel_x + s * car->vel_z) >> 12;
    lat = (s * car->vel_x - c * car->vel_z) >> 12;
    car->slip_last = lat;

    /* engine thrust on the forward component, toward the gear cap,
     * then the CONFIRMED decays. Velocity units are speed<<4 (the
     * handler's +0x60 = trig*speed>>8 with trig Q12 gives |v| =
     * speed*16). */
    /* real torque curve self-limits: equilibrium against the CONFIRMED
     * 996/1000 drag is fwd ~= thrust*250, which lands 6th gear near
     * the original's speed scale with the REAL tables -- the invented
     * per-gear caps of round 40 are gone. */
    if (in->throttle)
        fwd += thrust;
    fwd = fwd * 996 / 1000;                    /* CONFIRMED coast drag */
    /* ROUND 55 (APPROXIMATED): transmission-slip resistance. The
     * original feeds rpm*ratio - wheel_speed through the D_8012CF78
     * table (untraced) -- the load that actually caps top speed. With
     * the round-55 rpm floor the engine now sustains real force, so
     * without this term the equilibrium ran to ~0x4D1 (407 km/h!).
     * Soft extra drag above the authentic ~0x2C0 top range: */
    if (fwd > 0x2C0 << 4)
        fwd -= (fwd - (0x2C0 << 4)) >> 3;
    if (in->brake)
        fwd = fwd * 94 / 100;                  /* CONFIRMED brake */
    if (fwd < 0) fwd = 0;

    /* rebuild velocity purely along vel_dir (CONFIRMED rebuild shape:
     * +0x60/+0x68 from trig * forward >> 12) and refresh the polar
     * mirror fields (func_80026CA8's role). */
    car->vel_x = (c * fwd) >> 12;
    car->vel_z = (s * fwd) >> 12;
    car->speed = fwd >> 4;   /* scalar speed, orig +0xA0 */
    car->vel_ang = car->vel_dir;

    /* spin-out trigger (CONFIRMED shape in func_80026E7C: |lateral| x
     * speed accumulating past a per-car threshold sets +0xB4) --
     * ported as a flag only; the spin handler itself (heading rotation
     * at ramp*3 capped 0x249, func_8001C490 CF48-CF90) is a future
     * round. */
    if (lat < 0) lat = -lat;
    /* CONFIRMED trigger shape (func_80026E7C): high speed + slip past
     * the per-model threshold (stat2*14, real data). */
    if (car->speed > 0x3C0 && lat > car->spin_threshold)
        car->spin_state = 1;
}

/* ---- stage 6: wheels (CONFIRMED, corrects round 21) ---- */
static void wheels(PsxCar *car)
{
    int32_t adv = car->speed * 3;
    if (adv > 0x1000)
        adv = 0x249;
    car->wheel_rot = (car->wheel_rot + adv) & PSX_BAM_MASK;
    if (car->speed >= 0x321)
        car->wheel_rot |= 0x1000; /* blur flag */
}

/* ---- stages 7-10: predict, probe, gravity, commit (CONFIRMED) ---- */
static void integrate(PsxCar *car, const PsxTrackIface *trk)
{
    int32_t nx = car->pos_x + (car->vel_x >> 8); /* CONFIRMED >>8 */
    int32_t nz = car->pos_z + (car->vel_z >> 8);
    int wall = 0;

    if (trk && trk->wall_blocked)
        wall = trk->wall_blocked(trk->ctx, car->pos_x, car->pos_z, nx, nz);

    /* gravity (CONFIRMED: y += vy>>3; vy += 0xC; land at ground-8) */
    if (car->airborne) {
        car->air_timer++;
        car->pos_y += car->vert_vel >> 3;
        car->vert_vel += 0xC;
        if (trk && trk->ground_y) {
            int32_t g = trk->ground_y(trk->ctx, car->pos_x, car->pos_z);
            if (car->pos_y >= g - 8) {
                car->pos_y = g - 8;
                car->airborne = 0;
                car->vert_vel = 0;
                /* original: landing sound, shake = air_timer/3,
                 * hard-landing handler if air_timer >= 0x15 */
                car->air_timer = 0;
            }
        }
    } else if (trk && trk->ground_y) {
        car->ground_y = trk->ground_y(trk->ctx, car->pos_x, car->pos_z) - 8;
        car->pos_y = car->ground_y;
    }

    if (!wall) {
        car->pos_x = nx; /* CONFIRMED commit */
        car->pos_z = nz;
    } else {
        /* CONFIRMED wall response: the predicted position is NOT
         * committed and the speeds decay hard. The original then runs
         * func_800181C8 (a track-frame resolver) so the car GRINDS
         * along the wall instead of freezing; that resolver is not
         * line-traced yet, so this port approximates the slide with a
         * classic axis-separated retry (documented approximation). */
        car->speed = car->speed * 70 / 100;
        car->aux_speed = car->aux_speed * 80 / 100;
        /* the original's drive handler rebuilds the velocity vector
         * FROM the cut +0xA0 next frame; this port's projection reads
         * the vector itself, so the cut must reach it too or the next
         * frame's polar rebuild resurrects the old speed. */
        car->vel_x = car->vel_x * 70 / 100;
        car->vel_z = car->vel_z * 70 / 100;
        if (trk && trk->wall_blocked) {
            if (!trk->wall_blocked(trk->ctx, car->pos_x, car->pos_z,
                                   nx, car->pos_z))
                car->pos_x = nx;
            else if (!trk->wall_blocked(trk->ctx, car->pos_x, car->pos_z,
                                        car->pos_x, nz))
                car->pos_z = nz;
        }
        /* original also: penalty global -= 0x1388, scrape sound,
         * impact particles from (nx-x, nz-z) */
    }

    if (trk && trk->lat_offset)
        car->lat_offset = trk->lat_offset(trk->ctx, car->pos_x, car->pos_z);
}

void psx_car_frame(PsxCar *car, const PsxInput *in, const PsxTrackIface *trk)
{
    int32_t thrust;
    gearbox(car, in);
    steering(car, in, 0, 0);
    thrust = engine(car, in, !car->airborne);
    drive_grip(car, in, thrust);
    wheels(car);
    integrate(car, trk);
}

/* Same frame but with the steering wheel value forced (autopilot). */
void psx_car_frame_steer(PsxCar *car, const PsxInput *in,
                         const PsxTrackIface *trk, int32_t steer_value)
{
    int32_t thrust;
    gearbox(car, in);
    steering(car, in, 1, steer_value);
    thrust = engine(car, in, !car->airborne);
    drive_grip(car, in, thrust);
    wheels(car);
    integrate(car, trk);
}

/* Attract-mode autopilot steering (func_8001C0E4 D_801D9060==4 branch,
 * CONFIRMED formula): angdiff(vel_dir, road_dir)<<5 plus a lateral
 * pull of (0x1000 - sin(2*lat))<<2 signed by the side, clamped
 * +-0x1000. CAVEAT: the original's lat (+0xC) units/scale were not
 * extracted -- with a mismatched lat scale the pull equilibrium sits
 * off-center and the car weaves (visible in the test lap). The
 * FORMULA is the traced one; matching +0xC's real scale is the
 * remaining step. */
int32_t psx_autopilot_steer(const PsxCar *car, const PsxTrackIface *trk)
{
    int32_t road = trk->road_dir(trk->ctx, car->pos_x, car->pos_z);
    int32_t s2 = psx_angdiff(car->vel_dir, road) << 5;
    int32_t lat = car->lat_offset;
    int32_t pull = (0x1000 - psx_sin((lat << 1) & PSX_BAM_MASK)) << 2;
    int32_t steer;
    if (lat > 0)
        pull = -pull;
    steer = pull + s2;
    if (steer < -0x1000) steer = -0x1000;
    if (steer > 0x1000) steer = 0x1000;
    return steer;
}
