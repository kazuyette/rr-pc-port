/* physics.c -- see physics.h for the confirmed-vs-approximated
 * breakdown per function. */
#include "physics.h"

#include <math.h>
#include <stddef.h>

void physics_car_init(PhysicsCar *car, double x, double z, double heading) {
    if (car == NULL) return;
    car->x = x;
    car->z = z;
    car->heading = heading;
    car->speed = 0.0;
    car->gear = PHYSICS_GEAR_MIN;
    car->rpm = 0.0;
    car->manual_transmission = 0;
}

void physics_gearbox_update(PhysicsCar *car, double throttle, int shift_up, int shift_down, double dt) {
    /* RPM accumulator: an ORIGINAL approximation (see physics.h -- the
     * original's exact per-gear accel/RPM-buildup curve was not
     * extracted this round). Rises toward a per-gear ceiling under
     * throttle, decays otherwise -- enough to drive the CONFIRMED
     * auto-shift thresholds below through a believable curve, without
     * claiming to be the original's numbers. */
    double rpm_ceiling = 3000.0 + 800.0 * (double)car->gear;
    double rpm_rate = 6000.0; /* units/second toward the target -- tuning knob, not RE'd */

    if (car == NULL) return;

    if (throttle > 0.0) {
        double target = rpm_ceiling * throttle;
        if (car->rpm < target) {
            car->rpm += rpm_rate * dt;
            if (car->rpm > target) car->rpm = target;
        }
    } else {
        car->rpm -= rpm_rate * 0.5 * dt;
        if (car->rpm < 0.0) car->rpm = 0.0;
    }

    /* Shift logic below mirrors FUN_8001c490's confirmed structure
     * exactly (see PHYSICS_DOWNSHIFT_RPM/PHYSICS_UPSHIFT_RPM in
     * physics.h for the literal threshold constants read out of the
     * decompiled game code). */
    if (car->manual_transmission) {
        if (shift_up && car->gear < PHYSICS_GEAR_MAX) car->gear++;
        if (shift_down && car->gear > PHYSICS_GEAR_MIN) car->gear--;
    } else {
        if (car->rpm < PHYSICS_DOWNSHIFT_RPM(car->gear) && car->gear > PHYSICS_GEAR_MIN) {
            car->gear--;
        } else if (car->rpm > PHYSICS_UPSHIFT_RPM && car->gear < PHYSICS_GEAR_MAX) {
            car->gear++;
        }
    }
}

void physics_car_integrate(PhysicsCar *car, double throttle, double brake, double steer,
                            int off_track, double wall_lateral_gradient, double dt) {
    /* ORIGINAL approximation, not bit-exact -- see physics.h. Top speed
     * and acceleration both scale with gear (a 6-speed gearbox should
     * feel progressively faster and less punchy in higher gears), and
     * steering authority falls off at speed (a simple, common arcade-
     * racer feel, not derived from FUN_80017DF4's exact turn-rate
     * subroutine which was not decompiled this round). */
    const double max_speed_per_gear = 15.0; /* world units/sec per gear step, tuning knob */
    const double accel_per_gear = 40.0;      /* world units/sec^2 per gear step, tuning knob */
    const double brake_decel = 60.0;
    const double coast_decel = 8.0;
    /* Round 15: off-track penalty. CONFIRMED that the original tracks
     * off-track state every frame via exactly the projection this port
     * already ports (physics_track_project/trackdata_project_point) --
     * see round 3/8's find of the along/lateral/width-compare formula.
     * NOT confirmed: what the original actually DOES in response (that
     * lives inside FUN_8001c490's surface/pitch-influenced accel path,
     * not yet traced -- see the DAT_80077140/struct-map notes in this
     * file's TODO). The scale-down below is this port's OWN arcade-feel
     * stand-in (grass slows you down, a near-universal racing-game
     * convention) until that's RE'd, not a ported formula. */
    const double off_track_speed_scale = 0.5;
    const double off_track_accel_scale = 0.6;
    double top_speed;
    double accel;
    double turn_rate;

    if (car == NULL) return;

    top_speed = max_speed_per_gear * (double)car->gear;
    accel = accel_per_gear * (double)car->gear;

    if (off_track) {
        top_speed *= off_track_speed_scale;
        accel *= off_track_accel_scale;
    }

    if (brake > 0.0) {
        car->speed -= brake_decel * brake * dt;
    } else if (throttle > 0.0) {
        car->speed += accel * throttle * dt;
        if (car->speed > top_speed) car->speed = top_speed;
    } else {
        car->speed -= coast_decel * dt;
    }
    /* Off-track top speed can be BELOW the car's current speed (e.g. it
     * ran off track at full pace in a high gear) -- bleed speed down to
     * the new cap on coast/throttle instead of only clamping on
     * throttle, so driving onto grass at speed actually feels like it
     * slows you down rather than just capping future acceleration. */
    if (car->speed > top_speed) {
        double decel_toward_cap = coast_decel * 2.0 * dt;
        car->speed -= decel_toward_cap;
        if (car->speed < top_speed) car->speed = top_speed;
    }
    if (car->speed < 0.0) car->speed = 0.0;

    /* Round 17: wall-proximity lateral gradient (see
     * physics_wall_probe_lateral_gradient). Structurally mirrors
     * func_80033584's CONFIRMED effect -- a lateral asymmetry between
     * the car's front-left/front-right/rear-left/rear-right probe points
     * drains speed even when the car hasn't crossed fully off-track yet
     * (a "grazing the wall" penalty). The SCALE constant below is a
     * tuning knob, not RE'd (the original's car+0xA4 multiplier and >>6
     * rounding weren't extracted -- see physics.h). Applied
     * unconditionally (not gated by throttle/off_track) since scraping a
     * wall should cost speed regardless of what the driver is doing. */
    {
        const double wall_lateral_decel_scale = 2.0;
        if (wall_lateral_gradient != 0.0) {
            car->speed -= wall_lateral_decel_scale * fabs(wall_lateral_gradient) * dt;
            if (car->speed < 0.0) car->speed = 0.0;
        }
    }

    /* Steering authority tapers off at higher speed so the car doesn't
     * feel twitchy flat-out -- an arcade-feel approximation, not the
     * original's exact curve. */
    turn_rate = 1.8 * (1.0 - 0.5 * (car->speed / (top_speed > 0.0 ? top_speed : 1.0)));
    car->heading += steer * turn_rate * dt;

    car->x += sin(car->heading) * car->speed * dt;
    car->z += cos(car->heading) * car->speed * dt;
}

double physics_wall_probe_lateral_gradient(const PhysicsCar *car, const TrackSection *sec) {
    /* CONFIRMED local probe offsets, round 17 (D_8007306E's real byte
     * values: shorts -32,32 / 32,32 / -32,-64 / 32,-64 read as
     * (x,z) pairs) -- car-local units, +X right, +Z forward, matching
     * this port's existing sin(heading)/cos(heading) rotation
     * convention (see physics_car_integrate). */
    static const double offsets[4][2] = {
        { -32.0,  32.0 }, /* front-left  */
        {  32.0,  32.0 }, /* front-right */
        { -32.0, -64.0 }, /* rear-left   */
        {  32.0, -64.0 }, /* rear-right  */
    };
    double excess[4];
    double c, s;
    int i;

    if (car == NULL || sec == NULL) return 0.0;

    c = cos(car->heading);
    s = sin(car->heading);

    for (i = 0; i < 4; i++) {
        double lx = offsets[i][0];
        double lz = offsets[i][1];
        /* Same local->world rotation as physics_car_integrate's own
         * sin(heading)/cos(heading) position update: heading 0 points
         * along +Z, increasing heading rotates forward toward +X. */
        double wx = car->x + lx * c + lz * s;
        double wz = car->z - lx * s + lz * c;
        double dx = wx - sec->x;
        double dz = wz - sec->z;
        double along, lateral, width;

        trackdata_project_point(sec, dx, dz, &along, &lateral);
        /* NOTE: a plain difference of raw `lateral` values here would be
         * WRONG -- it cancels out to a position-independent constant
         * (the fixed hull spread), never reflecting actual wall
         * proximity, since raw lateral grows linearly with the car's own
         * position and that dependence subtracts away identically for
         * every probe pair. What the real func_8001BD9C/func_80017B58
         * pair actually records per probe (traced this round) defaults
         * to 0 when that probe ISN'T past the track boundary, and only
         * becomes a nonzero (clamped) value when it IS -- so this port
         * instead computes each probe's signed EXCESS past its side's
         * width (0 if still within bounds), matching that on/off
         * shape rather than a raw offset. */
        width = (lateral > 0.0) ? sec->width_right : sec->width_left;
        if (lateral > width) {
            excess[i] = lateral - width;
        } else if (lateral < -width) {
            excess[i] = lateral + width;
        } else {
            excess[i] = 0.0;
        }
    }

    /* CONFIRMED combination shape (func_80033584, round 17): lateral
     * asymmetry between the front pair and the rear pair -- applied here
     * to each probe's excess-past-boundary rather than its raw lateral
     * (see note above). */
    return (excess[0] - excess[1]) + (excess[2] - excess[3]);
}

int physics_track_project(const PhysicsCar *car, const TrackSection *sec,
                           double *out_along, double *out_lateral) {
    double dx, dz;
    if (car == NULL || sec == NULL) return 0;
    dx = car->x - sec->x;
    dz = car->z - sec->z;
    return trackdata_project_point(sec, dx, dz, out_along, out_lateral);
}

double physics_blend_heading_bam(int16_t heading_a, int16_t heading_b, double t) {
    /* CONFIRMED formula shape (round 5/8, func_800177B8): take the short
     * way around the 4096-unit circle before blending. */
    double a = (double)((uint16_t)heading_a & 0xFFF);
    double b = (double)((uint16_t)heading_b & 0xFFF);
    double diff = b - a;
    double blended;
    const double bam_to_rad = 6.283185307179586 / 4096.0;

    if (diff > 2048.0) diff -= 4096.0;
    else if (diff < -2048.0) diff += 4096.0;

    blended = a + diff * t;
    if (blended < 0.0) blended += 4096.0;
    if (blended >= 4096.0) blended -= 4096.0;

    return blended * bam_to_rad;
}

int physics_find_nearest_section(const PhysicsCar *car, const TrackData *td) {
    size_t i;
    int best = -1;
    double best_dist2 = 0.0;

    if (car == NULL || td == NULL || td->count == 0) return -1;

    for (i = 0; i < td->count; i++) {
        double dx = car->x - td->sections[i].x;
        double dz = car->z - td->sections[i].z;
        double dist2 = dx * dx + dz * dz;
        if (best < 0 || dist2 < best_dist2) {
            best = (int)i;
            best_dist2 = dist2;
        }
    }
    return best;
}

/* Signed distance of (car - midpoint) projected onto (a->b): positive
 * means the car is on b's side of the midpoint between section a and
 * section b. Shared helper for the forward/backward walks below. */
static double section_boundary_side(const PhysicsCar *car, const TrackSection *a, const TrackSection *b) {
    double mx = (a->x + b->x) * 0.5;
    double mz = (a->z + b->z) * 0.5;
    double dirx = b->x - a->x;
    double dirz = b->z - a->z;
    return (car->x - mx) * dirx + (car->z - mz) * dirz;
}

int physics_find_section_local_walk(const PhysicsCar *car, const TrackData *td, int prev_index) {
    int idx, n, steps;

    if (car == NULL || td == NULL || td->count == 0) return -1;
    n = (int)td->count;

    idx = (prev_index >= 0 && prev_index < n) ? prev_index : physics_find_nearest_section(car, td);
    if (idx < 0) return -1;

    /* Walk forward while the car is past the midpoint toward the next
     * section. Bounded by `n` steps (one full lap) so a car that
     * somehow teleported (or a malformed course) can't spin this loop
     * forever. */
    for (steps = 0; steps < n; steps++) {
        int nxt = (idx + 1) % n;
        if (section_boundary_side(car, &td->sections[idx], &td->sections[nxt]) <= 0.0) break;
        idx = nxt;
    }
    /* Then walk backward the same way, in case the car is actually
     * behind the section we started from (e.g. prev_index is stale, or
     * the car reversed). */
    for (steps = 0; steps < n; steps++) {
        int prv = (idx - 1 + n) % n;
        if (section_boundary_side(car, &td->sections[idx], &td->sections[prv]) <= 0.0) break;
        idx = prv;
    }

    return idx;
}

double physics_steering_reference_raw(int16_t aux_heading_a_raw, int16_t aux_heading_b_raw,
                                       double t, double current_heading_rad) {
    /* See physics.h for the full derivation and what's confirmed vs.
     * not. Steps 1-2: circular blend of the two *8-scaled aux headings,
     * wrapped into roughly [-2048, 2047] BAM units. */
    const double bam_to_rad = 6.283185307179586 / 4096.0;
    double a8 = fmod((double)aux_heading_a_raw * 8.0, 4096.0);
    double b8 = fmod((double)aux_heading_b_raw * 8.0, 4096.0);
    double diff, blended, magnitude, angle_bam, current_heading_bam;

    if (a8 < 0.0) a8 += 4096.0;
    if (b8 < 0.0) b8 += 4096.0;

    diff = b8 - a8;
    if (diff > 2048.0) diff -= 4096.0;
    else if (diff < -2048.0) diff += 4096.0;

    blended = a8 + diff * t;
    if (blended < 0.0) blended += 4096.0;
    if (blended >= 4096.0) blended -= 4096.0;

    magnitude = blended;
    if (magnitude >= 2048.0) magnitude -= 4096.0;

    /* Step 3-4: angle = 2048 - current_heading - magnitude (BAM units),
     * output = magnitude * sin(angle). */
    current_heading_bam = current_heading_rad / bam_to_rad;
    angle_bam = 2048.0 - current_heading_bam - magnitude;

    return magnitude * sin(angle_bam * bam_to_rad);
}

/* Round 14 finding -- CONFIRMED per-car live-state struct field map (all
 * offsets relative to the car struct pointer FUN_8001c490 receives as
 * its own a0, i.e. the pointer this port's PhysicsCar loosely stands
 * in for). Correction to round 13: there are NOT two distinct pointers
 * into two different structs -- `$s2` in FUN_8001c490 is simply
 * `$s1 + 0x58` (one substruct nested inside the single car struct), so
 * everything below is one flat offset space:
 *   +0x80  int16  a mode/type flag -- when nonzero, REUSES the +0x82
 *          slot below for a decrementing counter instead of gear (see
 *          +0x82 entry). Not identified further.
 *   +0x82  int16  CONFIRMED gear (1..6) in the common case -- but when
 *          +0x80 is nonzero, the SAME two bytes are read/written as a
 *          plain decrementing counter (clamped to a minimum of 1)
 *          instead, in a completely different code path. A real
 *          field-reuse/union, not a tracing error -- flagged as a trap
 *          for future work that assumes +0x82 always means gear.
 *   +0x84  int32  CONFIRMED RPM-like accel accumulator (already used by
 *          PHYSICS_DOWNSHIFT_RPM/PHYSICS_UPSHIFT_RPM above).
 *   +0xB4  int32  a state/phase flag, compared against specific values
 *          in at least two different functions (FUN_8001c490 itself,
 *          and func_80019CF4 below) -- value 3 gates one branch in
 *          FUN_8001c490, value 1 gates a branch in func_80019CF4. Not
 *          identified (candidate: crashed/spinning/off-track-recovery
 *          state, given its proximity to the collision-adjacent code).
 *   +0x74  --     read as an input signal by the DAT_80077140 ramp
 *          below; not yet identified (candidate: throttle or a
 *          previous-frame steering value, unconfirmed).
 *   +0x98  int32  a per-car scale/multiplier, divided by 1000 in one
 *          branch of func_80019CF4's DAT_80077140 update; not identified.
 *   +0xA0  int32  a ramp/frame-timer accumulator, compared against fixed
 *          thresholds 0x100 (256) and 0x200 (512) to select between
 *          three blending regimes in the DAT_80077140 consumer below.
 *   +0xAC  int32  CONFIRMED accel accumulator (matches round 3's
 *          original finding) -- this is what the DAT_80077140 ramp
 *          below actually updates each frame.
 *   +0xC8, +0xCA  int16 each  boolean-ish flags (0 or 0x100/256), set in
 *          func_80019CF4 from ANDing pad/controller state against
 *          bitmask constants (D_801D777C/777E/778C/778E) gated by
 *          controller-port checks -- read as CONFIRMED to be
 *          button-press-driven, but WHICH button(s) they correspond to
 *          is not identified.
 *
 * TODO (future round, not RE'd yet):
 *   - [RESOLVED round 12] PS1 BAM trig fixed-point scale is confirmed
 *     Q12 (raw/4096.0), verified against the actual lookup table bytes
 *     -- see trackdata.h's file header for the derivation. No code
 *     change needed: trackdata_project_point's real sin()/cos() calls
 *     were already numerically equivalent.
 *   - [RESOLVED round 18, surprising answer] func_80017DF4's OUTPUT
 *     computation is traced and ported as physics_steering_reference_raw()
 *     above, formula-shape CONFIRMED (round 13). Round 18 traced the ONE
 *     call site inside FUN_8001c490 (of 7 total call sites to
 *     func_80017DF4 codebase-wide, the other 6 are unrelated functions)
 *     to answer what FUN_8001c490 does with the return value. It does
 *     NOT add it to the car's heading. The value is read straight back
 *     off the stack and fed DIRECTLY as the probe-radius argument to the
 *     very next call in the same block -- func_8001BD9C, the CONFIRMED
 *     wall-boundary probe (round 16/17). So despite the name this port
 *     gave it, "steering_reference" is not a turn-rate input in the
 *     original at all -- it's an ANTICIPATED-CURVATURE term that scales
 *     how far/wide the wall probe looks ahead, which then feeds
 *     func_80033584's soft accel-based wall response (round 17). Kept
 *     the function/parameter names as-is for continuity with rounds
 *     10-17's docs -- see physics.h for the full writeup. NOT ported
 *     this round: making physics_wall_probe_lateral_gradient's probe
 *     reach dynamic (curvature-scaled) instead of the fixed D_8007306E
 *     offsets it uses today -- a real future upgrade, not a quick
 *     follow-on, since it changes that function's whole shape.
 *   - [DEAD END, round 18] Identifying which button(s) trigger
 *     DAT_80077140's boost/kick mechanic. Traced func_80019CF4's actual
 *     button-check logic in full (the block computing car+0xC8/+0xCA):
 *     it ANDs the current pad-press bitmask (D_801D35AA) against one of
 *     two mask PAIRS -- D_801D777C/D_801D777E or D_801D778C/D_801D778E,
 *     selected by comparing a per-car controller-slot byte (D_801D35A9)
 *     against the literals 0x41 / 0x23. But grepping the ENTIRE
 *     decompiled codebase for those 4 mask globals turns up ONLY reads
 *     (8 occurrences, all in this one function) -- there is no write
 *     site anywhere in the matched code. They live in BSS (no static
 *     initializer), so their real values are either always 0 (silently
 *     disabling this whole path) or populated by something outside this
 *     executable's own code -- a resource file loaded from the disc, a
 *     memory-card/options-save value, or similar. This is a genuine
 *     limit of static disassembly, not a "didn't get to it yet": there
 *     is no further code to trace that would reveal these values. Ruled
 *     out as resolvable this way -- would need either a real captured
 *     runtime memory dump/trace of gameplay, or finding the resource
 *     file these get loaded from (out of scope for asm-only tracing).
 *   - [RESOLVED round 14, evidence-strengthened not code-changed]
 *     DAT_80077140's role: round 7 guessed "smoothed value, maybe
 *     visual pitch/dive, not a simple grip multiplier" -- round 14
 *     traced BOTH its write site (func_80019CF4) and its read site (the
 *     accel-accumulator ramp block in FUN_8001c490) fully. It's a
 *     GLOBAL (not per-car) one-pole low-pass filter: each frame,
 *     `new = (scaled(car.C8) + scaled(car.CA) [+ a mode-dependent
 *     offset/scale via car.B4/car.98] + old) / 2`, where car+0xC8/+0xCA
 *     are the button-press flags above -- i.e. this is a
 *     BUTTON-TRIGGERED KICK that ramps in on press and decays smoothly
 *     via the filter, not a friction/grip term. It then feeds EVERY
 *     car's own accel accumulator (+0xAC) through a 3-regime blend
 *     gated by that car's own ramp timer (+0xA0) and state flag (+0xB4)
 *     -- see the struct map above. Being a single GLOBAL fed by
 *     button state strongly suggests this is the game's
 *     boost/turbo-style special-input mechanic (with a shared
 *     screen-space/camera-pitch side effect, matching round 7's visual
 *     guess, PLUS a genuine subtle acceleration effect on every car via
 *     the ramp -- both could be true at once). Still NOT CONFIRMED:
 *     which specific button(s) (the D_801D777C/777E/778C/778E bitmask
 *     constants) trigger it -- and per round 18's dead-end finding below,
 *     this specific question is NOT resolvable via further static
 *     tracing (those 4 globals are never written anywhere in the matched
 *     code) -- nor is DAT_80077140's real-world scale/units, so still
 *     not ported into physics_car_integrate.
 *   - [STRUCTURALLY TRACED round 16] func_8001B374 and func_8001BD9C are
 *     two SEPARATE, SIBLING collision probes (neither calls the other;
 *     both are almost certainly called directly from FUN_8001c490, not
 *     traced from that side this round) -- not one "collision function"
 *     as earlier rounds' naming implied:
 *
 *     func_8001B374 = CAR-vs-CAR (or car-vs-other-entity) OBB test.
 *     Loops up to 12 "other slot" entries (a struct-of-arrays table,
 *     stride 0x114, global base D_801E92A8/D_801E9274/... -- one array
 *     per field, not a real C struct in this trace), skipping any slot
 *     whose "active" flag isn't 1. For each active slot, loops over 6
 *     fixed LOCAL hull points read from a compile-time constant table
 *     (D_80010128, 6 * 8-byte entries) -- this car's own simplified
 *     collision hull, always the same shape regardless of car model as
 *     far as this function is concerned. Each hull point is rotated by
 *     this car's heading (own func_80044E2C/func_80044D0C sin/cos calls,
 *     Q12 scale, same convention as round 12's trig-scale finding) and
 *     translated by this car's world position (car+0x10/+0x18), then
 *     transformed AGAIN into the OTHER slot's local frame (rotate by
 *     -other_heading, translate by -other_position) -- a textbook
 *     "transform point into other's OBB space" test. The transformed
 *     point is compared against CONFIRMED fixed thresholds: lateral in
 *     roughly [-25, +26), forward in roughly [-21, +80) (i.e. an
 *     asymmetric box biased forward -- consistent with a car-length
 *     bounding box, not a symmetric square), plus a separate scalar
 *     range check against another per-slot field +/-0xF. On a hit, the
 *     first 4 of the 6 hull points (probably the main box corners) take
 *     one path: compare heading difference between the two cars via
 *     func_80019C6C against a ~+/-112 degree (1280/4096 turn) threshold
 *     to pick between two sub-cases, write this car's own velocity
 *     (car+0x60..0x6C, 4 words) into the OTHER slot's response globals
 *     (for it to react next tick -- confirms collision response is
 *     APPLIED TO THE OTHER PARTY, not self, from this call), set a
 *     +/-2 "priority" value into another per-slot global based on
 *     comparing car+0xC (an unidentified per-car field, some kind of
 *     weight/class) against the other slot's own type field, and fire a
 *     sound effect via func_80032F50 -- the SAME function round 13 found
 *     in aux_a_raw's checkpoint-looking consumption path, so this is
 *     confirmed to be a generic "play effect N at this car" call, not
 *     collision-specific. The remaining 2 of 6 hull points (likely
 *     front/rear bumper probes) take a softer path: blend into a
 *     3-sample running average on the OTHER slot's own +0x3C/+0x40/+0x44
 *     fields -- structurally the same "one-pole-ish smoothing accumulator
 *     into another entity's state" shape as DAT_80077140's already-
 *     confirmed filter, just per-slot instead of global. Returns a
 *     0/1 "did any hull point hit anything" boolean.
 *
 *     func_8001BD9C = CAR-vs-TRACK-BOUNDARY (wall) probe -- takes a
 *     world position (a0/a1/a2) and a signed probe radius (a3), derives
 *     an angle-bucket index via a modulo against global D_801E90E0, and
 *     samples 4 points spaced around that position using a compile-time
 *     4-entry direction table (D_8007306E, read as interleaved
 *     sin/cos-like shorts) scaled by the probe radius. Each of the 4
 *     probe points is run through func_8001BAFC (not traced this round
 *     -- a ~0x2A0-byte helper, likely resolves the probe point to a
 *     nearby track section/edge) and then func_80017B58 (also not fully
 *     traced this round, but its call signature and the surrounding
 *     bit-flag consumption strongly match the SAME edge-crossing test
 *     family as the already-CONFIRMED func_80017838 from round 12 --
 *     i.e. this is the wall/boundary check reusing that primitive at a
 *     probe-point level instead of a hull-corner level). Each probe's
 *     2-bit result is OR'd into a running bitmask and its computed
 *     along/lateral-ish values are recorded into per-probe globals
 *     (D_801733E0/E4/E8, 4 slots of 0x10 bytes). After all 4 probes,
 *     bit1 of the combined mask triggers one sound-effect branch and
 *     bit0 (qualified by WHICH probe direction set it, remembered in a
 *     stack flag) triggers another, both via func_8003A958 (the same
 *     SFX trigger already used for round 15's off-track path) selecting
 *     between two adjacent effect IDs (0x11/0x12, 0x13/0x14, 0x15/0x16)
 *     based on a global (D_8007C2F8) compared against the SAME 0x321
 *     threshold that also appears in func_8001B374's fp+0xA0 check --
 *     likely a shared "distance/lap-progress" or "camera mode" gate that
 *     picks a stereo-pan or interior/exterior sound variant, not
 *     collision-physics-relevant. This function appears to return void
 *     (no meaningful v0 at exit) -- it's a side-effecting wall-probe,
 *     not a query.
 *
 *     What round 16 did NOT resolve, and round 17's status on each:
 *
 *   - [RESOLVED round 17] func_8001BAFC -- traced in full. It's the REAL,
 *     byte-exact version of physics_find_section_local_walk: a backward-
 *     then-forward walk using func_800178A0 (edge-point builder) +
 *     func_80017838 (the CONFIRMED cross-product gate test from round 12)
 *     exactly as round 12 had speculated for func_80017DF4's own walk.
 *     Operates on a Q8 FIXED-POINT section index (the original tracks
 *     "how far along this section" as a fraction, not just a discrete
 *     index -- a genuinely new structural finding), wrapping via
 *     D_801E90E0 which this confirms is the loaded course's total
 *     section count (used as a divisor/modulus). Still not ported here
 *     (this port's own local-walk stays the deliberate center-to-center
 *     approximation from round 11 -- see physics.h -- since porting the
 *     byte-exact version needs D_801733A0 settled first, see below).
 *   - [RESOLVED round 17] func_80017B58 -- traced in full. It's a second,
 *     independent per-section along/lateral projection+classification
 *     routine (the PS1-native sibling of this port's own
 *     trackdata_project_point), used by func_8001BD9C for each wall
 *     probe point. Confirms the in-RAM TrackSection layout used by THIS
 *     family of functions: +0x0 word X (Q14 fixed point, D_801733A0-
 *     biased), +0x4 word Z (Q14, unbiased), +0xA short heading (already
 *     known), +0xE/+0x10 shorts read depending on which side of center
 *     the point falls (width-like). NOTE: this is the engine's in-RAM
 *     working struct, not necessarily identical to the on-disk MAP.RRM
 *     record format decoded separately back in Phase 5 -- no claim these
 *     are the same offsets.
 *   - [RESOLVED round 17, constants extracted + CORRECTED round 19] The
 *     wall-hit response consumer -- grepped the whole codebase for
 *     readers of func_8001BD9C's output globals (D_801733E0/E4/E8) and
 *     found two: func_800397FC (a small "is any recent hit within 0x30
 *     units of this point" proximity query, most likely for spark/
 *     particle FX placement, not physics) and func_80033584, which IS
 *     the physics response. Round 19 re-read func_80033584 instruction-
 *     by-instruction to pin down its exact constants -- this CORRECTS
 *     round 17's summary, which conflated two of its four fields and
 *     missed a real, structurally important asymmetry. The precise
 *     breakdown (all fields below are offsets from the RAW car pointer,
 *     i.e. car+0x0 -- CORRECTED round 21: round 19 wrongly stated these
 *     were relative to car+0x58; tracing func_8001C490's own prologue
 *     confirms $s1 = the raw car argument and $s2 = $s1+0x58 are two
 *     DIFFERENT registers used for different field clusters -- func_
 *     80033584/func_80033438 are called with $s1 [the raw pointer], and
 *     every offset in this breakdown, including the confirmed car+0x80/
 *     +0x82/+0xB4/+0xA0/+0xAC/+0xC8/+0xCA fields from earlier rounds, is
 *     relative to that raw base; car+0x58 is a real, separate sub-
 *     pointer used for a smaller set of fields such as +0xC8 itself,
 *     which func_80033438 reaches as ($s1+0x58)+0x70):
 *
 *     Let gradient_lat = (slot0.f4-slot1.f4)+(slot2.f4-slot3.f4) (the
 *     lateral, front-vs-front/rear-vs-rear combination already described
 *     in physics.h) and gradient_lon = (slot0.f4-slot2.f4)+(slot1.f4-
 *     slot3.f4) (the longitudinal, front-vs-rear combination). Every
 *     call, UNCONDITIONALLY:
 *       car[0x28] -= gradient_lat * 4
 *       sum4 = slot0.f4+slot1.f4+slot2.f4+slot3.f4 (all 4, regardless of
 *              sign -- NOT gated by either gradient)
 *       weighted = sum4 * 30 / 100 (i.e. sum4 * 0.3, done as *15*2/100)
 *       car[0x14] += weighted
 *       car[0x40] += weighted            (SAME term added to a SECOND,
 *                                          separate field -- not a typo)
 *       D_8012CF70 = -4 * gradient_lon    (OVERWRITTEN each call, not
 *                                          accumulated -- a per-frame
 *                                          delta, not a running total;
 *                                          still the leading candidate
 *                                          for a camera-shake signal)
 *       car[0x20] += -4 * gradient_lon    (the RUNNING total of the same
 *                                          term D_8012CF70 receives raw)
 *       car[0x14] += |cos_lookup(car[0x28]) >> 6|   (rounded toward zero
 *                     before the shift; cos_lookup is the CONFIRMED Q12
 *                     trig table, func_80044D0C, round 12)
 *     CONDITIONALLY (only when gradient_lat >= 5 -- a SIGNED comparison,
 *     so a strongly NEGATIVE gradient_lat never takes this branch at
 *     all, a real one-sided asymmetry, not a magnitude threshold):
 *       car[0xAC] += (gradient_lat * car[0xA4]) >> 6   (rounded toward
 *                     zero before the shift; car+0xAC is the ALREADY-
 *                     CONFIRMED accel accumulator, round 3/14; car+0xA4
 *                     is read here but its own meaning/typical value is
 *                     NOT independently confirmed -- no other write site
 *                     was searched for this round)
 *
 *     Round 17's summary was WRONG in two ways, corrected here: (1) it
 *     described car+0x14's update as "rotated by heading via
 *     func_80044D0C" -- there is no heading/rotation involved at all;
 *     the cos_lookup call's ANGLE ARGUMENT is car[0x28] itself (a
 *     separate accumulator driven by gradient_lat, NOT the car's
 *     heading), making car[0x28] act as an OSCILLATOR PHASE that
 *     advances proportional to lateral wall-scrape intensity -- i.e.
 *     the real mechanic includes a genuine judder/vibration component
 *     (a cosine wave whose frequency scales with how hard the car is
 *     scraping a wall), not a simple linear term. (2) it implied a
 *     single combined update to car+0x14; there are actually TWO
 *     separate additions to car+0x14 in the same call (the sum4*0.3
 *     term and the |cos|/64 term), plus car+0x40 gets the sum4*0.3 term
 *     too and car+0x20 gets its own running total of the D_8012CF70
 *     delta -- four distinct accumulator fields in total (0x14, 0x20,
 *     0x28, 0x40), not the two this file previously implied.
 *
 *     STILL NOT resolved: what any of car+0x14/+0x20/+0x28/+0x40
 *     actually DRIVE downstream (visual shake? a secondary speed/grip
 *     term read by a different function? a UI damage indicator?) --
 *     none of their READ sites were searched for this round, only their
 *     writes inside func_80033584. car+0xA4's real value/meaning is
 *     still unconfirmed. This port's physics_wall_probe_lateral_gradient
 *     (round 17) does NOT reproduce this asymmetry (it applies its decel
 *     via fabs() regardless of sign, and has no threshold or oscillator
 *     term) -- left as-is rather than retrofitting just the sign/
 *     threshold detail onto a formula that doesn't share the rest of
 *     this shape (the excess-past-boundary combination this port uses
 *     isn't the same quantity as the real slotN.f4 fields), which would
 *     be cherry-picking one detail without becoming meaningfully more
 *     faithful. A real tighter port is future work, not this round's.
 *   - [RESOLVED round 17] D_80010128 (car hull, func_8001B374) and
 *     D_8007306E (wall-probe offsets, func_8001BD9C) -- both read out of
 *     rr-decomp's rodata directly. Hull: 6 points (X,Z) = (-19,22),
 *     (-19,-22), (35,24), (35,-24), (79,25), (79,-25) -- a rear-left/
 *     rear-right, mid-left/mid-right, front-left/front-right hexagon,
 *     ~98 units long (X: -19..79) and ~44-50 units wide (Z: |22|..|25|),
 *     matching round 16's independently-derived OBB threshold range
 *     almost exactly. Wall-probe offsets: front-left (-32,32), front-
 *     right (32,32), rear-left (-32,-64), rear-right (32,-64) -- ported
 *     directly into physics_wall_probe_lateral_gradient() below.
 *   - [PORTED round 17] physics_wall_probe_lateral_gradient() +
 *     physics_car_integrate's new wall_lateral_gradient parameter: uses
 *     the CONFIRMED probe offsets and the CONFIRMED (front_left-
 *     front_right)+(rear_left-rear_right) combination from
 *     func_80033584 to apply a continuous "grazing the wall" decel, on
 *     top of (not replacing) round 15's binary off_track penalty -- see
 *     physics.h for the full derivation and what's still approximated
 *     (the response SCALE, car+0xA4's real value, and the >>6 rounding
 *     weren't extracted, so wall_lateral_decel_scale is a tuning knob).
 *     The longitudinal (front-vs-rear) half of func_80033584's effect,
 *     which feeds car+0x14 and the camera-shake global, is deliberately
 *     NOT ported -- car+0x14's real meaning (velocity component? heading
 *     correction?) wasn't confirmed, and guessing at a steering-adjacent
 *     effect risks a wrong-feeling nudge worse than leaving it out.
 *   - [ROUND 20] Re-read func_80033584 directly from raw MIPS bytes
 *     (asm/29E8.s lines 37882-37985) instead of relying on round 19's own
 *     prose summary, to cross-check it byte-by-byte -- round 19's
 *     breakdown above HOLDS UP, with one small refinement: the "sum4*0.3"
 *     term is computed as an actual MIPS `div` of (sum4*30) by 100 (round
 *     toward zero, with the compiler's standard div-by-zero/overflow
 *     guard sequence around it), not a clean multiply -- behaviorally
 *     equivalent to *0.3 for the vast majority of values but technically
 *     integer-truncating division, noted for completeness. Also verified
 *     func_80044D0C directly (asm/29E8.s line 58024): it wraps its input
 *     mod 0x1000 (4096, i.e. one full BAM12 turn) with sign-correct
 *     folding, dispatches to func_80044D70, which does a 4-way octant
 *     split into signed 16-bit tables D_80075FA0/D_80076F9E/D_80074FA0/
 *     (and further tables past what was read) -- this IS a genuine
 *     Q12-angle trig lookup, confirming (not just assuming, as round 19
 *     had) that car[0x28] really is being fed through a cosine/sine-style
 *     table as an oscillator phase.
 *
 *     Bigger find: func_80033584 is never called alone. Its one call site
 *     (8001CBF8/8001CC00 inside FUN_8001c490's drive-update routine) calls
 *     func_80033438 on the SAME car pointer immediately before it, back
 *     to back, no branch between them. func_80033438 (asm/29E8.s
 *     37786-37882) is a state-machine ramp that ALSO writes car+0x20 (the
 *     same field func_80033584 accumulates into) via a single SHARED
 *     (not per-car) global D_8012CF80:
 *       if D_801D9060 < 2: D_8012CF80 = 0, return (armed-delay gate --
 *         likely "wait N ticks after race start/respawn")
 *       elif D_8012CD20 < 30: D_8012CF80 += 1, clamp <= 16  (warmup ramp)
 *       elif car[0xCA] >= 129 AND car[0xA0] >= 81: D_8012CF80 += 1,
 *         clamp <= 8   (both a button-flag field AND the CONFIRMED
 *         car+0xA0 ramp/frame-timer field held "high")
 *       elif car[0xC8] >= 129: D_8012CF80 -= 2, clamp >= -16  (the
 *         OTHER button-flag field held)
 *       else: D_8012CF80 = trunc(D_8012CF80 * 3 / 4)   (decay toward 0)
 *       car[0x20] += D_8012CF80   (always, last step)
 *     car+0xC8/0xCA were already confirmed (round 3/14) as a button-press
 *     flag pair; car+0xA0 as a ramp/frame-timer. This reads as a classic
 *     press-and-hold ease-in/ease-out ramp: hold one input, the ramp
 *     climbs (capped low, 8, once past the 30-tick warmup); hold the
 *     other, it falls fast (-2/call, floor -16); hold neither, it decays
 *     by 25%/call. Since car+0x20 receives BOTH this ramp's contribution
 *     AND func_80033584's own wall-scrape longitudinal-gradient running
 *     total, car+0x20 is a single accumulator driven by two unrelated
 *     causes (sustained directional input, and scraping a wall) -- the
 *     leading hypothesis is a body-lean/weight-transfer value (visual
 *     tilt, or a secondary grip/traction modifier), since both physical
 *     causes are the kind of thing that would make an arcade car lean.
 *     STILL NOT found (as of round 20): car+0x20's own READ site(s).
 *   - [ROUND 21] Found car+0x20's read site. First confirmed the exact
 *     player car global: func_80014C2C (the top-level per-frame drive
 *     dispatcher, callers of func_8001C490 all trace back here) loads
 *     `$s3 = D_8007C258` and passes it as func_8001C490's a0 every call
 *     -- D_8007C258 is a real, single, FIXED global address (not an
 *     array slot), confirming this whole project's "car" struct is the
 *     player's own car, addressed directly, not indexed. That let a
 *     codebase-wide grep for D_8007C258 usage (rather than an ambiguous
 *     "offset 0x20 from some register" grep, which is far too noisy --
 *     offset 0x20 is common in dozens of unrelated structs) find every
 *     function that genuinely touches THIS car, cutting the search from
 *     ~70 false-lead candidates to 16 real call sites.
 *
 *     One of those, func_8002B024, calls func_8002AE14(index, car_ptr,
 *     other_ptr) -- and func_8002AE14 reads car+0x20 (as a 16-bit `lhu`,
 *     not the 32-bit `lw` func_80033584 uses to write it -- same field,
 *     narrower read) as part of packing a compact 0x28-byte (40-byte)
 *     RECORD into an array at global D_8007C4F8[index]. The record
 *     layout: 8 halfwords from the car (offsets +0x10, +0x14, +0x18,
 *     +0x40, +0x20, +0x24, +0x28, +0x38, in that order) at record bytes
 *     0x0-0xE, the SAME 8 offsets read from a second struct pointer
 *     (`other_ptr`, global D_801E9250 -- role not independently
 *     confirmed this round) at record bytes 0x10-0x1E, then one full
 *     word from car+0x8 at record+0x20 and other_ptr+0x8 at record+0x24.
 *
 *     This function is called from a gated state machine (func_8002B024,
 *     checking globals D_801D34E8/D_8007C32C/D_8007C338, with a ~1800-
 *     frame -- 30s @ 60fps -- cadence via D_801D77F0/D_801D7E68), and
 *     the buffer it writes into (D_8007C4F8) is initialized in
 *     func_8002ACD4 as `D_8005F560 + 4`, right after a small header (one
 *     flag byte plus what looks like a packed best-time value read from
 *     the same D_8005F560 region and copied into D_801D77F0). This
 *     overall shape -- fixed persisted-looking storage, a header that
 *     looks like a saved best time, and per-frame snapshots of position-
 *     adjacent fields -- is the signature of a GHOST/RECORD REPLAY
 *     system: it periodically snapshots the car's state (and a second,
 *     not-yet-identified reference struct's matching state) so a later
 *     pass can play it back.
 *
 *     The important part for this project: car+0x20 is snapshotted
 *     ALONGSIDE car+0x8 (very likely a position component, given
 *     func_80017DF4/func_80017B58 read car+0x0/+0x8 as an (x,z) query
 *     point -- round 17/18), car+0x10/+0x14/+0x18 (read together
 *     elsewhere as position/velocity-like inputs to the steering-
 *     reference lookahead -- func_8001C490, round 18/19 area), car+0x38
 *     (independently seen wrapped mod 0x1000 = one BAM12 turn in
 *     func_8001C490, i.e. almost certainly the car's HEADING), and its
 *     own sibling accumulators car+0x24/+0x28/+0x40. Being recorded in
 *     the SAME snapshot as position, velocity, and heading -- rather
 *     than living only inside the two writer functions and going
 *     nowhere -- is real evidence (not proof) that car+0x20 affects the
 *     car's visible pose or motion during ghost playback, supporting
 *     round 20's body-lean/weight-transfer hypothesis: a ghost replay
 *     that only stored position+heading would look subtly wrong (no
 *     body roll) without also storing this. NOT yet found (as of round
 *     21): the PLAYBACK side that reads D_8007C4F8 back out. D_801E9250's
 *     own identity (rival car? recorded checkpoint reference? something
 *     else with the exact same field layout as the player car?) also
 *     still open as of round 21.
 *   - [ROUND 22] Found the playback side, and it's a stronger result than
 *     "evidence" -- it's a DIRECT confirmation. func_8002AF1C is the
 *     exact byte-for-byte inverse of func_8002AE14 (round 21): given an
 *     index and two destination pointers (a1, a2), it reads record
 *     D_8007C4F8[index] and unpacks it back into a1/a2 at the SAME
 *     offsets func_8002AE14 packed from -- +0x10/+0x14/+0x18/+0x40/+0x20/
 *     +0x24/+0x28/+0x38 (halfwords) and +0x8 (word) -- plus two small
 *     extra fields (from globals D_801D7E88/D_801D77F8) written to a1+2/
 *     a2+2 that aren't part of the original snapshot.
 *
 *     All 3 call sites found pass `a1 = D_8007C258` -- meaning
 *     func_8002AF1C writes DIRECTLY INTO THE LIVE PLAYER CAR STRUCT, not
 *     a copy -- and `a2 = D_801E9250`. One call site (func_8002B370)
 *     uses a HARDCODED index of 0 and, in the same function, resets
 *     `D_801D9060` to 3 -- the exact "arm-delay" gate confirmed in round
 *     20 to hold func_80033438's ramp at 0 for the first few ticks after
 *     a reset. That combination (resetting the arm-delay gate + restoring
 *     car state from a fixed slot 0) is the signature of a RACE (RE)START
 *     handler: slot 0 in D_8007C4F8 is a saved/canonical starting state
 *     (consistent with round 21's read of D_8007C4F8's init as sitting
 *     right after what looks like persisted save data), and restarting a
 *     race resets not just position/velocity/heading but ALSO car+0x20/
 *     +0x24/+0x28/+0x40 back to that saved baseline -- so a fresh start
 *     doesn't carry over leftover wall-scrape lean/judder from a previous
 *     attempt. The other 2 call sites use a VARIABLE index (global
 *     D_80173310, a counter also seen gating a long chain of countdown/
 *     sequence-stage comparisons in func_80014C2C, going up to at least
 *     0x12C = 300) instead of 0 -- consistent with a frame-by-frame
 *     ghost/intro-sequence PLAYBACK driving the same live car struct
 *     during the pre-race countdown (e.g. a "your best lap" ghost replay
 *     or a scripted camera-pan sequence), though this wasn't traced far
 *     enough to be certain which. None of these 3 caller functions has a
 *     direct `jal` caller anywhere in the codebase -- they're almost
 *     certainly 3 of the 40 state-handler functions reached through the
 *     function-pointer dispatch table found back in round 2 (D_80070EA4),
 *     which is consistent with them being race-sequence/state handlers.
 *
 *     Bottom line: car+0x20 (and its siblings +0x24/+0x28/+0x40) are
 *     CONFIRMED first-class car state, explicitly saved and restored by
 *     the game's own restart/playback logic on equal footing with
 *     position, velocity, and heading -- not internal scratch values
 *     that just happen to be readable. This doesn't nail down the exact
 *     visual/gameplay meaning (still the leading hypothesis: body-lean/
 *     weight-transfer), but it rules out "unused" or "debug-only"
 *     definitively. STILL open: the precise meaning of car+0x20/+0x28
 *     visually or mechanically; D_801E9250's identity; whether the
 *     variable-index call sites are truly a ghost replay or something
 *     else (would need tracing D_80173310's own producer/consumer chain,
 *     or the specific state-handler dispatch entries for these 3
 *     functions).
 *   - [ROUND 23] Identified car+0xA4 -- the one field in this cluster
 *     that was still completely unknown. Found via a direct grep for
 *     `0xA4(` load/store instructions (not a car-global grep this time,
 *     since func_80033584 itself already gave a known car+0xA4 read to
 *     anchor on) inside `func_80026CA8`, which reads car+0xA0 (the
 *     ALREADY-CONFIRMED ramp/frame-timer field), car+0xA4, car+0xA8, and
 *     car+0x24 together and computes what is structurally a POLAR
 *     (magnitude, phase) OSCILLATOR UPDATE: it runs BOTH trig-lookup
 *     functions (func_80044D0C, the CONFIRMED Q12 table from round 20,
 *     and its sibling func_80044E2C -- also verified this round to be a
 *     genuine Q12 trig lookup, same quadrant-split shape, different
 *     symmetry-folding: it takes abs() of its argument first, which is
 *     the even-symmetry cos(-x)=cos(x) shape, vs. func_80044D0C's signed
 *     mod-wrap with no abs -- meaning the ROUND 19/20/21/22 docs' label
 *     "cos_lookup" for func_80044D0C may actually be backwards, i.e. it
 *     could be the SINE table and func_80044E2C the cosine one; which
 *     is which isn't nailed down, but both are now confirmed to be a
 *     matched sin/cos Q12 pair, not a single reused table) of BOTH
 *     car+0xA8 and car+0x24, multiplies car+0xA0 and car+0xA4 in with
 *     those, sums cross products (cos(A8)*cos(24) + sin(A8)*sin(24), the
 *     textbook angle-difference identity) plus a car+0xA0²/car+0xA4²
 *     term, feeds the sum through `func_80040B54` (shape consistent with
 *     an integer sqrt/magnitude call), and writes the result BACK into
 *     car+0xA0 (`>> 6`) and, via a `0x400 - result` step and a call to
 *     `func_800187A0`, into car+0xA8. So car+0xA0/car+0xA8 are a
 *     self-updating (magnitude, phase) pair every call, and car+0xA4 is
 *     one of its two scale inputs (alongside car+0xA0's own prior
 *     value) -- structurally a per-frame OSCILLATOR, not a one-shot
 *     lookup.
 *
 *     The other phase input, car+0x24, is advanced separately by
 *     `func_80026E7C` (which calls `func_80026CA8` right after): it
 *     computes `func_80019CA8(car+0x24, car+0xAC) / 5` and adds that to
 *     car+0x24 -- meaning this phase advances proportional to the
 *     CONFIRMED accel accumulator (car+0xAC), i.e. faster when the car
 *     is accelerating harder. `func_80026E7C` itself is called from
 *     inside `func_80019CF4` -- the SAME function round 18 traced for
 *     the boost-button dead end -- specifically from one arm of a
 *     4-way dispatch on car+0x5C (values 0-3, each calling a different
 *     helper: func_80026E7C for 0, func_80027734 for 1, two more
 *     unexplored branches for 2/3). car+0x5C wasn't previously
 *     characterized; this reads as a small per-car MODE/SUB-STATE
 *     selector, plausibly a drive-mode switch (accelerating/coasting/
 *     boosting/braking), each with its own oscillator update rule.
 *
 *     Best-supported reading: car+0x24/+0xA0/+0xA4/+0xA8 is a WHEEL-SPIN
 *     or SUSPENSION-BOUNCE visual system -- a phase that spins faster
 *     under acceleration (car+0x24, fed by car+0xAC) combined with a
 *     second phase/amplitude pair (car+0xA0/car+0xA8) that self-updates
 *     via genuine trigonometry, with car+0xA4 acting as a per-car
 *     RESPONSIVENESS/DAMPING CONSTANT. This also explains why
 *     func_80033584 reuses car+0xA4 as ITS OWN scale factor for how
 *     strongly a lateral wall-scrape nudges the accel accumulator
 *     (round 19/20's `car[0xAC] += (gradient_lat * car[0xA4]) >> 6`):
 *     both consumers are using the SAME per-car tuning constant for "how
 *     strongly this car physically reacts," just applied to two
 *     different triggers (wall contact vs. its own acceleration). NOT
 *     yet found: car+0xA4's own WRITE/init site (is it a fixed per-car-
 *     model constant set once at spawn, or itself dynamic?), nor
 *     `func_80019CA8`/`func_80040B54`/`func_800187A0`'s own bodies (only
 *     their call shape was read, not traced byte-by-byte), nor what
 *     car+0x5C's other 2 states (car+0x5C==2, ==3) do.
 *   - [ROUND 25] Searched harder for car+0xA4's write/init site --
 *     genuinely inconclusive, but worth recording precisely to save a
 *     future round from re-walking the same false leads. A whole-
 *     codebase grep for `0xA4(` stores turned up 3 candidates, and all 3
 *     turned out to be DIFFERENT fields, not the confirmed car+0xA4:
 *     (1) `func_80020B88` copies a per-car-model config block (5 fields,
 *     read from an indexed table) into +0xA0/+0xA4/+0xA8/+0xB0/+0xB4/
 *     +0xB8 -- but relative to `car+0x58` (it computes `a2 = t3 + 0x58`
 *     first), i.e. it writes car+0xFC/+0x100/... , NOT car+0xA4 --
 *     confirmed by re-deriving the base pointer, not assumed; this is a
 *     real and useful finding on its own (a genuine per-car-model
 *     tuning-data loader, one more confirmation that car+0x58 holds
 *     spawn-time config), just not the field this round was looking
 *     for. (2) `func_800229F4`/`func_80022A58` read/write a `0xA4`
 *     field too, but as a 16-bit `lh`/`sh` sawtooth counter (increments
 *     to +0x78/120 then wraps to -0x96/-150) gated by nearby fields
 *     +0x88/+0x96/+0xA2 and a global `D_8007C2F8` -- confirmed via
 *     `func_80025268`'s prologue that these operate on `D_801E9250`
 *     directly (`$s2 = D_801E9250`, the round 24 collision-array base),
 *     NOT the player car -- i.e. this is `D_801E9250`'s OWN +0xA4 field,
 *     a completely different value from car+0xA4 (different struct,
 *     different width, different range) that just happens to share an
 *     offset. Given the sawtooth shape and the surrounding function's
 *     use of track-section data and the CONFIRMED arm-delay gate
 *     (D_801D9060), the best guess is a camera-shake/wobble oscillator,
 *     but this wasn't traced further -- flagging mainly so a future
 *     round doesn't conflate the two +0xA4 fields. (3) Grepping for the
 *     literal computed address of the player's own car+0xA4
 *     (D_8007C258+0xA4 = 0x8007C2FC) as a direct global symbol found
 *     nothing, confirming (as expected, given func_80033584/
 *     func_80026CA8 both reach it via a passed register, not a direct
 *     global) that it's never addressed as a fixed global -- any write
 *     site has to be found by tracing register-relative access in a
 *     function confirmed to hold the RAW car pointer, which is a much
 *     bigger search than a flat grep. One new READ site was found in
 *     the process (`func_80022F88`, around the block that also compares
 *     against `D_801E9E2C` and reads the CONFIRMED velocity field
 *     car+0x60): car+0xA4 is used as a fallback/default value in what
 *     looks like an AI speed/difficulty decision, consistent with (not
 *     additional proof of) the per-car tuning-constant hypothesis. This
 *     sub-thread is now well enough mapped to hand to a future round
 *     without repeating this search from scratch.
 *   - [ROUND 26] Two real advances, found while re-reading func_80014C2C
 *     (the top-level per-frame drive dispatcher) with fresh eyes.
 *
 *     (1) FOUND the per-opponent update loop round 24 was looking for.
 *     Right before the ghost-recorder call, func_80014C2C loops over all
 *     12 slots of `D_801E9250` (round 24's collision array): for each
 *     slot whose +0x0 halfword is nonzero (an active-slot flag) AND
 *     whose +0x58 field equals 1 (a type/kind flag -- +0x58 being the
 *     SAME sub-struct offset confirmed since round 14 for the player),
 *     it calls `func_8002128C(slot_ptr, 0)`. func_8002128C is a large
 *     (367-instruction) per-slot update: it reads slot+0x8, calls two
 *     sound-adjacent functions, reads slot+0x2 as a small index into
 *     what looks like a function-pointer table (`func_80059228`-based),
 *     and repeatedly calls `func_8003486C` -- one of the state-handler-
 *     cluster functions from rounds 20-23's investigation of car+0x5C's
 *     dispatch. This is strong (not byte-exact-confirmed) evidence that
 *     func_8002128C is the AI/opponent equivalent of the player's own
 *     per-frame update, reusing at least some of the same underlying
 *     state-handler subroutines. It was NOT traced instruction-by-
 *     instruction (367 instructions calling ~15 different subroutines is
 *     a multi-round job on its own), so it isn't confirmed to reach
 *     func_8001B374's own collision-response fields specifically -- but
 *     it firmly establishes that a per-opponent update DOES exist in
 *     this decompiled code, which round 24 had left as an open question
 *     ("not confirmed to exist"). This is the natural next place to
 *     look for func_8001B374's response consumer.
 *
 *     (2) CORRECTED round 21's characterization of func_8002AE14's own
 *     recording cadence. Round 21 described it as fired by "a gated
 *     ~30-second-cadence state machine" -- re-reading func_8002B024 in
 *     full shows it's actually a CONTINUOUS per-frame recording session,
 *     not a periodic snapshot: gated on a race-mode flag (D_8007C32C)
 *     and a lap-progress-like field (D_8007C338), it detects passing a
 *     CHECKPOINT via a small table at `D_801D7760` (indexed by
 *     `D_80173470`, advancing on each checkpoint reached) -- crossing a
 *     checkpoint ARMS recording (`D_801D34E8 = 1`) with a checkpoint-
 *     specific duration loaded from that same table row
 *     (`D_801D7E68`). While armed, func_8002AE14 is called EVERY FRAME
 *     with an ever-incrementing index (`D_801D77F0`, which is what gets
 *     passed as the record index, not a fixed or rarely-changing value
 *     as round 21 implied), for up to 1800 frames (0x708, ~30s @ 60fps)
 *     or until the checkpoint's own duration counts down to 0, whichever
 *     comes first. So `D_8007C4F8` isn't a handful of periodic
 *     snapshots -- it's a genuine multi-frame (up to 1800 entries)
 *     continuous per-frame recording, checkpoint-triggered, strongly
 *     reinforcing round 22's ghost/replay-system read: this really does
 *     look like recording a checkpoint-to-checkpoint clip, most likely
 *     for a replay or ghost feature, not just a single reference pose.
 *
 *     Also confirmed `D_80173310` (left open since round 22) IS a
 *     genuine incrementing frame counter: `func_80014C2C` does
 *     `D_80173310 += 1` unconditionally as one of the first things it
 *     does every frame, and it gets reset to 0 in a few places and to
 *     -1 specifically in `func_8002B370` (round 22's race-(re)start
 *     handler). This confirms round 22's "frame-by-frame ghost/intro
 *     playback" reading for `func_8002AF1C`'s 2 variable-index call
 *     sites is plausible on the index's OWN semantics -- though
 *     `D_80173310` is also used by many other, unrelated countdown/
 *     sequence checks throughout `func_80014C2C`, so it's a general-
 *     purpose frame counter, not a dedicated ghost-playback index; it
 *     just happens to be reused for that purpose at those 2 call sites.
 *   - [ROUND 40 -- THE BIG ONE. See src/physics_psx.c/h: the authentic
 *     fixed-point player cycle is now PORTED.] func_8001C490 traced
 *     INSTRUCTION-BY-INSTRUCTION end to end (asm 11593-12225), plus
 *     func_8001C0E4 (11323-11589, in full), func_80026E7C (in full),
 *     and func_80019CF4's engine block. Major REINTERPRETATIONS of
 *     earlier rounds, each now grounded in the frame cycle:
 *       - car+0x24 is the VELOCITY DIRECTION (BAM12), not an
 *         oscillator phase: it chases car+0xAC (the facing direction)
 *         with a /5 one-pole filter, and the velocity vector is
 *         re-projected onto it every frame with the lateral component
 *         DISCARDED -- that lag+projection pair IS Ridge Racer's
 *         celebrated drift model. Rounds 19/23's "phase-accumulation
 *         oscillator" and round 34's "wheel-spin oscillator" readings
 *         both described real code but mislabeled its meaning:
 *         func_80026CA8 is the polar decomposition of the velocity
 *         (car+0xA0 = |v| = the car's SPEED, car+0xA8 = atan2(v)).
 *       - car+0x38 is the WHEEL ROTATION angle, not the heading
 *         (corrects round 21): it advances by min(speed*3, 0x249)
 *         per frame and gains flag bit 0x1000 when speed >= 0x321
 *         (wheel-blur). The heading is car+0xAC.
 *       - car+0xA4's WRITE SITE FOUND (closes rounds 25/33's
 *         deprioritized thread): it's `sw v1, 0x4C($s1)` with
 *         $s1 = car+0x58 inside func_80019CF4's engine block -- the
 *         +0x58-substruct offset trap IN REVERSE (searches for
 *         `0xA4(` could never see it). It is recomputed EVERY FRAME
 *         from the live engine force (force>>7 throttle / force>>4
 *         brake), a response/decay term -- not a spawn constant.
 *       - func_8001C0E4 is the STEERING function: digital wheel ramps
 *         +-0x500/frame (snap across zero, /2 release decay), NeGcon
 *         analog path, airborne /4 damp, low-speed turn scale
 *         speed/0x320, commit to car+0x28. Attract mode (D_801D9060
 *         ==4) contains the AUTOPILOT steering law.
 *       - THE ENGINE (func_80019CF4): force normalized by
 *         (0x2710-rpm)*base/0xC350 from per-model tables (values not
 *         extracted), slope feed += car+0x2C>>8, coast drag
 *         speed*996/1000, brake speed*94/100.
 *       - INTEGRATION: predicted pos = pos + (vel>>8); wall probe runs
 *         on the PREDICTION; on hit the commit is REFUSED (wall
 *         blocks you), penalty global -= 0x1388, speed(+0xA0)*=0.70,
 *         aux(+0xB0)*=0.80. Gravity: y += vy>>3, vy += 0xC/frame,
 *         land at ground-8 with airtime effects (hard-landing handler
 *         at >= 0x15 frames).
 *       - spin-out: excessive |lateral|*speed sets car+0xB4, and the
 *         spin rotation runs at ramp*3 capped 0x249/frame in C490.
 *     All of the above is implemented as an integer BAM12 core in
 *     src/physics_psx.c with a per-line CONFIRMED/APPROXIMATED ledger
 *     (the only approximated numbers: the per-gear engine curve and
 *     rpm rise/fall rates -- the per-model tables weren't extracted).
 *     Validated by src/physics_psx_test.c: autopilot laps a synthetic
 *     oval (using the traced attract-mode steering law), gearbox
 *     climbs 1->6 on the confirmed thresholds, drag bounds the speed,
 *     slip is provably higher in curves than straights (the drift
 *     lag), and a wall hit blocks position and repeatedly cuts speed
 *     by the confirmed 0.7. This floating-point file (physics.c)
 *     remains as the earlier approximation layer; new work should
 *     target the authentic core.
 *   - [ROUND 37] Identified `func_800382A0` -- the function round 13
 *     glimpsed as "aux_a_raw's checkpoint-looking consumption path" via
 *     its shared use of `func_80032F50` (the generic sound-effect call).
 *     Its only caller is `func_80014C2C` (the confirmed top-level
 *     per-frame dispatcher), called unconditionally once/frame, right
 *     after the `D_801E9250` 12-slot opponent loop and its
 *     `func_8002128C` audio dispatch (the AI-state-1 gated call). No
 *     parameters -- it operates entirely on fixed globals.
 *
 *     Structure (traced in full, ~480 instructions): computes the
 *     squared-distance-then-sqrt (`func_80040B54`, the confirmed integer
 *     sqrt) from the player's world position (`D_8007C268`/`6C`/`70`)
 *     to a fixed record's position fields (`D_801D80A8+0x14`/`+0x18`/
 *     `+0x1C`) -- i.e. `D_801D80A8` is a SINGLETON record (not an
 *     indexed table), almost certainly the next-checkpoint tracker. On
 *     first entry inside a ~4096-unit range (latched via flag
 *     `D_801D8120`, so it fires once per approach, not every frame),
 *     calls `func_8003A958(0x11)` then `func_80032F50(5, ...)` -- the
 *     SAME sound-call round 13 flagged, confirming this really is the
 *     "approaching checkpoint" cue. Then buckets the distance into a
 *     near/mid/far/default category (`s3` = 1/2/0x10/4) and writes a
 *     screen-space pair (`D_801D3578`/`357A`/`357E`/`3580`) that looks
 *     like an on-screen checkpoint arrow/blip (position + a
 *     size/pulse value written to two slots). After a heading-vs-table
 *     comparison (`D_800747FC`) that conditionally calls
 *     `func_80037F64`/`func_800373BC` (not traced -- shaped like
 *     camera/view-angle helpers), it checks `D_801D80A8+0x8` (a 16-bit
 *     flag, -1 = "no active target") and, if active, calls
 *     `func_80038018(D_801D80A8)` -- THE SAME function round 36 called
 *     "an unrelated tagged-field stream decoder" and used to rule out a
 *     car+0x5C connection. That classification's "unrelated to the
 *     physics system" conclusion still holds (it never touches the car
 *     struct), but "unrelated" was too strong -- it's actually part of
 *     THIS checkpoint record's own per-frame update, decoding whatever
 *     tagged tuning data drives the checkpoint record's fields at
 *     `+0x44`/`+0x6C`. Correcting that note here rather than silently.
 *
 *     The function's back half is a generic LERP: advances an elapsed
 *     counter (`D_801D80A8+0xC`, step size `s2`=4 normally) against a
 *     duration (`+0x10`), then interpolates position fields (`+0x14..
 *     +0x1C` toward `+0x34..+0x3C`) and 3 angles (`+0x44..+0x4C` toward
 *     `+0x54..+0x5C`, mod-0xFFF via `func_80038264`, the same wrap
 *     convention as every other BAM12 angle in this project) over that
 *     duration -- almost certainly the checkpoint arrow/marker's own
 *     smooth position+orientation transition as the player crosses one
 *     checkpoint and the target advances to the next.
 *
 *     Also resolves the long-open "record offset 0x12" question from
 *     round 21's `D_8007C4F8` ghost-record layout: that record packs 8
 *     halfwords from `car` at bytes 0x0-0xE (offsets +0x10/+0x14/+0x18/
 *     +0x40/+0x20/+0x24/+0x28/+0x38, in that order) then the SAME 8
 *     offsets from `other_ptr` (`D_801E9250`) at bytes 0x10-0x1E, in the
 *     same order. Record+0x12 is the 2nd halfword of that second block,
 *     i.e. `other_ptr+0x14` -- the exact same field as `car+0x14`, just
 *     read off `D_801E9250` instead of the player car. This isn't a new
 *     field to identify; it's a structural alias of an already-tracked
 *     one (the "position/velocity-like lookahead input" group from
 *     round 21), so the "unknown offset" framing is retired -- what's
 *     still open is car+0x14's own precise physical meaning within that
 *     group, not the record layout.
 *
 *     CONCLUSION: the "aux_a_raw checkpoint path" thread (open since
 *     round 13) is now fully closed -- `func_800382A0` is the per-frame
 *     checkpoint-approach-cue + on-screen arrow + smooth-marker-
 *     transition updater, called once/frame from the top-level
 *     dispatcher, entirely global-driven (no car-struct parameter).
 *     It does not feed back into car physics itself, so it stays
 *     documentation-only like the last several rounds.
 *   - [ROUND 36] Two threads this round. First, closed the search for
 *     car+0x5C's mid-race state-1/2/3 trigger (round 35's remaining
 *     open half): confirmed file-wide there are only 6 total
 *     `sw ..., 0x5C(` instructions in the entire codebase. Checked the 5
 *     not already resolved: `func_8001AA60` (a per-car spawn/init
 *     function -- writes literal 3 to `a1+0x58+0x5C` = car+0xB4, the
 *     sub-struct offset trap again, not car+0x5C), and `func_80038018`
 *     (an unrelated tagged-field stream decoder writing to a
 *     non-car-shaped struct, +0x44 through +0x6C -- ruled out by
 *     inspection, no relation to any confirmed car field). Combined
 *     with round 35's 3 already-resolved false leads and the 1 genuine
 *     hit (func_800205E4's reset to 0), this exhausts EVERY direct-offset
 *     store to 0x5C in the whole file -- none of them sets car+0x5C to a
 *     nonzero value. CONCLUSION: like the 8-collision-field and car+0xA4
 *     write-site threads, the mid-race trigger (if it exists at all) is
 *     not reachable via a literal-offset search; only a broader/indirect
 *     search would find it. Deprioritizing alongside those 2 threads.
 *
 *     Second, picked up a much older stale item: `D_801733A0`'s ultimate
 *     purpose (open since round 13ish). Only 6 references exist file-
 *     wide (1 write, 5 reads) -- fully tractable. The write:
 *     `func_80015CD4` sets it to a fixed constant `0xF000`, alongside 3
 *     other one-time globals (`D_801D82D0`=a0 param, `D_801D7620`=0,
 *     `D_8007C208`=-0x20, `D_801D82C8`=4). Its caller, `func_80032A54`,
 *     is a ONE-TIME SETUP function: right before calling
 *     `func_80015CD4` it also initializes `D_80173318` (round 27's
 *     CONFIRMED active-SPU-voice-count global) from `func_80012670`'s
 *     return -- i.e. this is race/level-load-time setup, not a per-frame
 *     call. The 5 read sites use `D_801733A0` purely as a constant
 *     upper-bound in `D_801733A0 - (shifted_value) - (other_value)`
 *     Q14-fixed-point expressions, operating on pointer parameters with
 *     fields shaped like track-section/geometry data (offsets +0x0/+0x4/
 *     +0x8/+0xA/+0xE), not on the confirmed car struct at all.
 *     CONCLUSION: `D_801733A0` is a one-time race-setup constant
 *     (0xF000) used as a max-range/clamp bound for track-geometry or
 *     audio-range calculations -- grouped with SPU voice-count init at
 *     its write site, so plausibly an audio attenuation range rather
 *     than rendering, but either way NOT a physics constant. This
 *     resolves the open question by ruling it OUT of the physics scope
 *     entirely, rather than leaving it open indefinitely.
 *   - [ROUND 35] Found car+0x5C's RESET site, answering half of round
 *     34's "what selects the 4 states" question. Grepped the whole file
 *     for `0x5C(` stores on plausible car-pointer registers; most hits
 *     were false leads that resolve to the a0+0x58 sub-struct (i.e.
 *     car+0xB4, NOT car+0x5C) once the base register was checked --
 *     `func_80026E7C` (state 0's own handler, writes `0x5C(s3)` where
 *     s3=a0+0x58), `func_80027734` (state 1, same pattern, writes the
 *     literal 2 to car+0xB4 -- an unrelated small field inside the
 *     sub-struct, not a state-4/5), and `func_80027F60` (state 2, zeros
 *     the same car+0xB4 field at its end). None of these touch the raw
 *     car+0x5C.
 *
 *     The real hit: `func_800205E4(a0, a1)` writes `sw $zero, 0x5C($a0)`
 *     directly on the raw pointer, as the last of a cascade of resets
 *     (+0x38 through +0x58, all zeroed or set to small defaults,
 *     conditionally gated on `a1<5`). This is called from
 *     `func_800206CC(a0=car, a1=slot_index, a2=model_table)` -- a genuine
 *     per-car SPAWN/INIT function: sets +0x4=1 (active flag), +0x2 =
 *     model id read from the `a2` table indexed by `a1`, has a special
 *     case comparing the model id against global `D_8007C25A` (a
 *     sibling of the player-car-pointer global `D_8007C258`, 2 bytes in
 *     -- plausibly "the player's own model id"), then calls
 *     `func_800205E4` and `func_80017DF4` (round 18's reclassified
 *     function). `func_800206CC` itself is called from 3 sites inside
 *     `func_80021048`, a per-slot loop that references `D_801E9240` and
 *     compares against a loop-local car-count -- shaped like RACE/GRID
 *     SETUP code that assigns cars to `D_801E9250` slots (and plausibly
 *     the player too, though this wasn't confirmed with a direct
 *     `D_8007C258` xref this round).
 *
 *     CONCLUSION: car+0x5C's confirmed write site is a SPAWN-TIME RESET
 *     TO 0 (state 0, the baseline oscillator tick), not a per-frame
 *     gameplay trigger. This answers "where does it get zeroed" but NOT
 *     "what advances it to 1/2/3 during a race" -- that transition
 *     trigger is still unfound; this round only traced the reset path.
 *     Documented the 3 false leads precisely so a future round doesn't
 *     re-walk them.
 *   - [ROUND 34] Identified round 23's 3 oscillator helper functions
 *     (previously only call-shape guesses) and traced car+0x5C's other 2
 *     dispatch states (2 and 3) -- both open since round 23.
 *
 *     `func_80019CA8(a0, a1)` -- CONFIRMED (full trace): masks both args
 *     to 0xFFF (BAM12 range), computes the unsigned difference the short
 *     way around the circle, and re-signs it based on which argument was
 *     smaller (with a wrap correction past the half-circle point,
 *     0x801). This is a standard SIGNED SHORTEST BAM12 ANGLE-DIFFERENCE
 *     utility -- exactly what round 23's `car[0x24] += func_80019CA8(
 *     car+0x24, car+0xAC) / 5` needs to advance a phase toward a target
 *     angle.
 *
 *     `func_80040B54(a0)` -- CONFIRMED (via shape): bit-scan/normalize
 *     via `func_80044078` (a leading-zero-count-shaped helper), shift by
 *     the result, call `func_800409D4` (a refine/lookup step), then
 *     shift back by the inverse amount. This normalize-lookup-denormalize
 *     shape is the textbook structure for an INTEGER SQUARE ROOT on
 *     fixed-point hardware without a sqrt instruction -- confirms round
 *     19's "shape consistent with sqrt/magnitude" guess.
 *
 *     `func_800187A0(a0, a1)` -- CONFIRMED (via shape, traced the first
 *     ~half): full argument-reduction control flow (sign checks on both
 *     args, `|a1|>|a0|` swap, 0/+-0x400 boundary shortcuts for the
 *     axis-aligned cases) then `(smaller<<10)/larger` followed by a
 *     lookup into a table at `D_8005E93C`. This is the textbook shape of
 *     an ATAN2-STYLE ARCTANGENT utility returning a BAM12 angle -- the
 *     "0x400 - result" step round 23 saw feeding car+0xA8 is the
 *     standard complementary-angle adjustment used when computing a
 *     phase from vector components.
 *
 *     Together these 3 confirmations complete round 23's picture: the
 *     car+0xA0/car+0xA8 pair is a genuine CARTESIAN-TO-POLAR reconversion
 *     every call -- magnitude via sqrt(x^2+y^2) (func_80040B54) and phase
 *     via atan2(y,x)-equivalent (func_800187A0) -- not just "some trig",
 *     a textbook 2D vector magnitude+phase recompute.
 *
 *     car+0x5C states 2/3 (`func_80027F60`, `func_80028294`): both open
 *     with the EXACT SAME phase-advance-and-oscillator-update sequence as
 *     state 0 (`func_80019CA8(car+0x24, car+0xAC)/5` added into car+0x24,
 *     then `func_80026CA8` -- i.e. states 0/2/3 all share the same
 *     underlying oscillator tick). What's NEW and different: both state
 *     2 and state 3 go on to compute `func_80044D0C(car+0xA8 [+
 *     D_801D7E30 for state 2 only]) * car+0xA0`, shifted, and write the
 *     result into car+0x60 -- THE CONFIRMED VELOCITY FIELD. State 3 goes
 *     further, also computing the matching `func_80044E2C(car+0xA8) *
 *     car+0xA0` (the sin-family counterpart) for a second output. This is
 *     a genuinely new physics link: in states 2/3, the wheel-spin/
 *     suspension oscillator (car+0xA0 magnitude, car+0xA8 phase) DIRECTLY
 *     MODULATES the car's own velocity, strongly consistent with a
 *     wheelspin/traction-loss/skid drive-mode reading for these 2 states
 *     (as opposed to state 0/1's presumably normal-traction behavior).
 *     Not yet determined: what selects which of the 4 car+0x5C states is
 *     active at a given moment (the selector/trigger itself, not just the
 *     4 handlers, is still unmapped), and the exact meaning of the
 *     D_801D7E30 phase offset that only state 2 applies.
 *   - [ROUND 33] Widened the search for car+0xA4's own write/init site
 *     (round 25's other open half of this pair, unrelated to round 32's
 *     `D_801E9250`+0xA4 sawtooth). Round 25 concluded this needs
 *     register-relative tracing since no fixed-global access exists;
 *     this round attacked it 3 ways instead of guessing which function
 *     to trace by hand:
 *
 *     (1) Traced the top of the player-car call chain: `func_8001C490`
 *     (called from 6 sites, ALL of them inside `func_80014C2C` with
 *     `$a0` traced back to `D_8007C258` every time -- confirmed this is
 *     the once-per-frame player-only drive/input dispatcher, never
 *     called for opponent slots) is the parent of `func_80019CF4`
 *     (round 18/23's car+0x5C state dispatcher). Grepped both function
 *     bodies directly for a `0xA4(` offset -- zero hits in either.
 *
 *     (2) Checked every one of the 18 places in the whole file that
 *     reference the `D_8007C258` global directly (`lui/addiu` pair) --
 *     not just the ones already read in earlier rounds. 8 were new
 *     this round: `func_8001B0CC` (touches +0xA0/+0x82/+0xCC, not
 *     +0xA4), `func_8001F23C` (+0x80/+0x2 only), and the 6-function
 *     ghost/restart cluster from round 22/26's territory
 *     (`func_8002B024`, `func_8002B370`, `func_8002B4A8`,
 *     `func_8002B6F4`, `func_8002BBA0`, `func_8002C500`,
 *     `func_8002CE0C`, `func_80039874`) -- zero `0xA4(` hits in any of
 *     them either, so the restart/respawn code that DOES touch
 *     car+0x20/0x24/0x28/0x40 (round 22) does NOT also touch car+0xA4.
 *
 *     (3) Went looking for the round-31-style indirect pattern --
 *     `addiu $reg, $baseReg, 0xA4` (computing a car+0xA4 address into a
 *     fresh register, which wouldn't show up as a literal `0xA4(`
 *     offset). Found exactly 5 hits file-wide, but all 5 turned out to
 *     be a false lead: they're all inside one unrelated cluster
 *     (`func_80034050`, `func_8003446C`, `func_80035638`,
 *     `func_80035EAC`, `func_80036D30`) that reads list/array data with
 *     `blez`-gated loops and calls `func_80043738`/`func_80043794` --
 *     shaped like menu/garage-list rendering, not the live car struct
 *     (no trig calls, no oscillator shape, no relation to any
 *     previously-confirmed car field). Ruled out by inspection, not
 *     assumed.
 *
 *     CONCLUSION: still unfound, but the easy avenues are now
 *     exhausted too -- direct offset (round 25), direct-global-ref
 *     functions (this round, all 18), and the computed-register
 *     pattern (this round, all 5, all false leads). What's left is
 *     exactly what round 25 predicted: a full call-graph trace of every
 *     function that receives the car pointer as a parameter, several
 *     levels deep from `D_8007C258`, which is a much larger undertaking
 *     than a targeted grep. Given the shape of this result mirrors the
 *     "other 8 collision fields" thread (rounds 28-31) -- exhausted the
 *     cheap searches, real answer would need a much bigger investment --
 *     this is a reasonable point to deprioritize car+0xA4's write site
 *     too, unless the user wants to commit a dedicated round to the
 *     full call-graph trace.
 *   - [ROUND 32] Fully closed the `D_801E9250`+0xA4 sawtooth thread left
 *     open since round 25 (the "camera-shake?" guess). Traced both of its
 *     only 2 touchpoints end to end:
 *
 *     `func_800229F4(slot_ptr)` -- the counter itself. Reads slot+0xA4
 *     (halfword). If it's exactly 0 (first-ever call), it calls
 *     `func_8003A958(8)` and DISCARDS the return value (the very next
 *     instruction re-reads slot+0xA4, still 0) -- `func_8003A958` is a
 *     gated random-range helper (returns a fixed override from global
 *     `D_800772FC` if that's nonzero, else dispatches to one of 3 RNG
 *     sub-tables by range). So this call has no effect on the counter;
 *     it's a pure side-effect RNG draw (likely just to advance/desync the
 *     shared RNG stream), not an initializer. Then the actual update: if
 *     the counter is <=0, +1; else if <0x78 (120), +1; else wrap to -0x96
 *     (-150). Net shape: an asymmetric sawtooth counting up from -150 to
 *     120 (~270-tick period), one step per call.
 *
 *     `func_80022A58(slot_ptr)` -- the gate/controller, and the ONLY
 *     caller of func_800229F4 (confirmed: exactly 2 call sites, both
 *     here). If slot+0xA4 < 0, it just calls func_800229F4 unconditionally
 *     (keep counting through the negative "cooldown" phase). If slot+0xA4
 *     >= 0, advancing requires BOTH slot+0x96==1 AND slot+0xA2==1 (the
 *     CONFIRMED AI-decision-state fields from rounds 26/29's `a0+0x58`
 *     sub-struct map) as a precondition, and then compares a threshold
 *     derived from global `D_8007C2F8` (>>3, capped <151) against
 *     slot+0x88 -- i.e. `D_801E92D8`, THE confirmed collision-response
 *     field with a known consumer (round 28's `func_8002252C`, the
 *     overtake/block/avoid decision) -- also shifted >>3; only if the
 *     global-derived value is smaller does it advance the counter again.
 *     There's also a hard force-reset: if slot+0xA4 < -60 AND
 *     slot+0x96==12, snap the counter straight to -30.
 *
 *     Found this round: func_80022A58's ONLY caller (confirmed: exactly 1
 *     call site in the whole file) is INLINE code inside `func_80025268`
 *     itself (the master per-opponent update -- NOT one of the ~11 named
 *     dispatch subroutines catalogued in rounds 28-31, which is why this
 *     call was missed by those rounds' subroutine-by-subroutine sweep),
 *     gated by `D_801D9060 < 4` -- the SAME arm-delay/distance flag
 *     already flagged as a gate in round 25's notes. Also confirmed (grep
 *     across func_80025268's whole body) that this inline call is the
 *     field's ONLY other touchpoint -- no other 0xA4-offset access
 *     anywhere in func_80025268 itself.
 *
 *     CONCLUSION: this is now a fully closed loop -- gate
 *     (func_80025268 inline, `D_801D9060<4`) -> controller
 *     (func_80022A58, keyed on the AI-state fields +0x96/+0xA2 AND the
 *     collision field +0x88/`D_801E92D8`) -> counter (func_800229F4,
 *     -150..120 sawtooth) -- with NOTHING else in the statically
 *     decompiled codebase reading or writing `D_801E9250`+0xA4. This
 *     REPLACES round 25's "camera-shake/wobble" guess: since both the
 *     gate and the controller are keyed on the same AI-decision-state
 *     fields and the same collision-response field round 28 tied to the
 *     overtake/block AI logic, the best-supported reading is now an
 *     AI-STATE PACING/COOLDOWN counter for that same behavior (how long
 *     an opponent stays in, or how soon it re-evaluates, an overtake/
 *     block/avoid decision) -- not a rendering or camera effect. Its
 *     final numeric value has no other reader anywhere in the codebase,
 *     so it's plausibly self-contained (used only to time re-entry into
 *     its own gate condition, no external consumer needed) rather than
 *     driving anything visual. Given the fully closed control loop and no
 *     remaining unexplored touchpoints, this thread is adequately
 *     understood and not worth further rounds unless a broader,
 *     non-AI-cluster search turns up something new.
 *   - [ROUND 31] Finished the sweep of func_80025268's dispatch cluster:
 *     traced the last 4 subroutines (func_800217F4, func_80024C64,
 *     func_8002362C, func_80025050 -- 154-317 instructions each). Checked
 *     for direct offsets (round 29 already covered these 4 and found
 *     none), computed/indirect addressing (`addu $reg, $a0, $other_reg`
 *     patterns -- none found, only plain register-copy `addu` uses), and
 *     even a blunt check for whether the literal values 0x44/0x48/0x4C/
 *     0x50/0x54/0x64/0x6C/0xC0 appear ANYWHERE in these functions as
 *     immediates at all (not just in offset position) -- zero hits
 *     across all 4. Also re-ran round 28's xref search on all 8 still-
 *     unconfirmed field symbols (D_801E9294/98/9C/A0/A4/B4/BC/D_801E9310)
 *     across the WHOLE file one more time to be certain: every single
 *     occurrence is still inside func_8001B374 itself (lines 10593-10842,
 *     all within its 10394-10904 body) -- confirmed zero outside
 *     references, anywhere in the decompiled codebase.
 *
 *     CONCLUSION: this completes a full, clean sweep of func_80025268
 *     and all ~11 of its per-slot subroutines (rounds 28-31). None of
 *     them access the other 8 collision-response fields in any form --
 *     direct, indirect/computed, or as a loadable constant. Combined
 *     with the whole-file symbol search finding zero outside references
 *     to those 8 fields at all, the most honest reading is that
 *     func_8001B374 writes these 8 fields but NOTHING in the statically
 *     decompiled codebase reads them back. Plausible explanations (not
 *     confirmed, no further static evidence available): (a) genuinely
 *     dead/unused code -- a leftover or cut mechanic that still runs but
 *     has no consumer; (b) consumed only through a mechanism invisible
 *     to static symbol/offset tracing (e.g. a function-pointer table
 *     entry never resolved, or a debug/telemetry path compiled out of
 *     the retail path but not the retail binary's dead code); (c) the
 *     consumer lives entirely outside the per-opponent AI/audio cluster
 *     -- e.g. rendering/visual-damage code -- which was never searched
 *     since all 4 rounds assumed an "AI-like" consumer. Given 4 rounds
 *     already invested (28-31) and D_801E92D8's own consumer already
 *     found and understood (round 28), this thread is being deprioritized
 *     rather than pursued further for now -- the value of a full-file
 *     search for a possibly-nonexistent consumer is low compared to the
 *     other open threads (car+0xA4, the D_801E9250+0xA4 sawtooth, etc.).
 *   - [ROUND 30] Fully traced 4 of func_80025268's smaller dispatch
 *     subroutines (func_80020524, func_80022984, func_80021BE0,
 *     func_80021CB4 -- picked for size, 36-79 instructions each) looking
 *     for computed/indirect accesses to the remaining 8 collision-
 *     response fields, since round 29 already ruled out simple
 *     `offset($slot_ptr)` patterns across the whole cluster. None found
 *     -- but a real nuance surfaced: `func_80021BE0` and `func_80021CB4`
 *     are NOT AI/collision logic at all. They compute a distance-like
 *     score from `D_8007C260`/`D_8007C32C`/`D_8007C32E` (globals, not
 *     slot fields) combined with slot+0x8 (position X) and slot+0x9C (a
 *     16-bit field, read directly off the raw slot pointer here -- the
 *     SAME final address round 29's func_80024F54 reached via
 *     `0x44(a0+0x58)`, corroborating slot+0x9C as a real, distinct field
 *     from the collision-response ones), then write the result to
 *     `D_80173348` -- the shape of a priority/distance SCORE feeding a
 *     shared global, most plausibly for the SAME kind of SPU voice-
 *     allocation/culling decision round 27 found in func_8002128C's
 *     audio LOD system (`func_80021BE0` even has an unused-a0 variant
 *     computing a similar score from constants alone, `func_80021CB4`
 *     the per-slot version). `func_80020524` and `func_80022984` are
 *     smaller housekeeping helpers (menu/track-select globals; the
 *     word-sized slot+0x58 flag read/write already known from round 26)
 *     -- neither touches a collision field either.
 *
 *     Net effect: `func_80025268`'s per-slot dispatch cluster is NOT
 *     purely an "AI update" as rounds 26/28 characterized it -- it's a
 *     MIXED per-opponent per-frame dispatcher combining AI/collision-
 *     adjacent decision logic (func_8002252C, confirmed round 28) with
 *     audio-priority-scoring logic (func_80021BE0/func_80021CB4, this
 *     round) under one umbrella call. This narrows the remaining search
 *     for the other 8 response fields' consumer to the 4 STILL-untraced,
 *     larger subroutines: func_800217F4, func_80024C64, func_8002362C,
 *     func_80025050 (154-317 instructions each) -- none of which have
 *     been read in full yet.
 *   - [ROUND 29] Checked func_80025268's own body plus all ~11 of its
 *     per-slot subroutines (func_80021CB4, func_80022984, func_80025050,
 *     func_80024C64, func_8002362C, func_800217F4, func_80024F54,
 *     func_8002252C, func_80023B2C, func_80020524, func_80021BE0) for
 *     direct-offset accesses to the remaining 8 unconfirmed collision-
 *     response fields (+0x44/+0x48/+0x4C/+0x50/+0x54/+0x64/+0x6C/+0xC0,
 *     relative to the raw slot pointer). RESULT: still no consumer found
 *     -- func_80025268's own body only touches those bit patterns as
 *     $sp-relative stack spills (false positives), and 2 apparent hits
 *     inside the subroutines turned out to be FALSE LEADS once the base
 *     register was checked:
 *       - `func_80024F54` reads `0x44($a1)` -- but $a1 = $a0+0x58 there
 *         (the CONFIRMED sub-struct-pointer offset, computed as a raw
 *         address again, same as round 28's func_8002252C), so this is
 *         actually slot+0x9C, not slot+0x44.
 *       - `func_80023B2C` writes `0x6C($s1)` -- same pattern, $s1 =
 *         $a0+0x58, so this is slot+0xC4, not slot+0x6C.
 *     Net effect: this ROUNDS OUT the a0+0x58 sub-struct as a much
 *     bigger AI-decision-state region than previously mapped -- besides
 *     round 28's slot+0x9A/0xA2/0xA6/0xB0 (from func_8002252C), it also
 *     has fields at slot+0x9C, 0xA2 (again, different bit), 0xA6 (again),
 *     0xAE, 0xB0 (again), and now 0xC4 -- a genuine per-opponent
 *     behavior-state block, but still NOT the same fields func_8001B374
 *     writes at the top level. The other 8 collision-response fields'
 *     real consumer(s) remain unfound after 2 rounds of dedicated search;
 *     the most direct remaining avenue is a full byte-by-byte trace of
 *     each of the ~11 subroutines (this round only checked for a raw
 *     immediate-offset pattern, not indirect/computed-offset accesses,
 *     which func_8002252C's own D_801E92D8-as-array-base trick shows is
 *     a real possibility here).
 *   - [ROUND 28] FOUND a real consumer of func_8001B374's collision-
 *     response fields, via a targeted xref search instead of guessing
 *     near the update loop (round 27's own recommendation). Grepped for
 *     every xref to the 9 confirmed response-field symbols (D_801E9294,
 *     98, 9C, A0, A4, B4, BC, D8, and D_801E9310) and found that
 *     `D_801E92D8` (slot+0x88) has xrefs OUTSIDE func_8001B374, inside
 *     `func_8002252C`.
 *
 *     func_8002252C(a0=slot_ptr, a1=opponent_index) treats a0 as a
 *     D_801E9250 slot (its `s1 = a0+0x58` matches the sub-struct-pointer
 *     OFFSET, but here computed as a raw address, not loaded as a
 *     pointer -- consistent with round 26's finding that D_801E9250's own
 *     +0x58 is a plain flag word, a different field shape than the
 *     player car's +0x58 pointer at the same offset). It reads its own
 *     slot+0x88 (`s1+0x30`) AND, via a computed index (a 16-bit field at
 *     slot+0x96, sign-extended, times the confirmed 0x114 slot stride),
 *     looks up a DIFFERENT slot's own +0x88 through `D_801E92D8` used as
 *     an array base -- i.e. this opponent's collision-response value
 *     compared against a SPECIFIC OTHER opponent's collision-response
 *     value. The comparison result drives branches that set behavior-
 *     state-looking fields (slot+0x9A, +0xA2, +0xA6, +0xB0) and finally
 *     calls `func_80021FBC` or `func_80021FA4` (a0=slot, a1=opponent
 *     index, a2=0 or 1) -- the shape of an overtake/block/avoid decision
 *     comparing "did I collide" against "did that specific rival
 *     collide."
 *
 *     func_8002252C's caller is `func_80025268` -- which, tracing ITS own
 *     callers, turns out to be called exactly ONCE per frame from
 *     `func_80014C2C`, immediately BEFORE the round-26 12-slot audio loop
 *     (`jal func_80025268` sits right before the `lui D_801E9250` /
 *     `jal func_8002128C` loop, not inside it). func_80025268 has its OWN
 *     internal loop over all 12 D_801E9250 slots (`s2` walks the array,
 *     `s3` is the loop/opponent index), dispatching each active slot
 *     through a whole cluster of per-slot decision subroutines --
 *     func_80021CB4, func_80022984, func_80025050, func_80024C64,
 *     func_8002362C, func_800217F4, func_80024F54, func_8002252C,
 *     func_80023B2C, func_80020524, func_80021BE0 -- of which
 *     func_8002252C is only one. So `func_80025268` (not func_8002128C)
 *     is the REAL master per-opponent AI/behavior update function round
 *     24/26 were originally looking for; func_8002128C (round 27) is a
 *     separate, purely audio-side per-slot loop that happens to run
 *     right after it in the same frame.
 *
 *     Net result: `func_8001B374`'s collision-response field D_801E92D8
 *     (+0x88) IS consumed, confirmed, by `func_8002252C` inside the real
 *     AI loop `func_80025268` -- resolving the multi-round search. The
 *     other 8 response fields (+0x44/+0x48/+0x4C/+0x50/+0x54/+0x64/+0x6C/
 *     +0xC0) showed no xrefs outside func_8001B374 itself in this pass,
 *     so their consumers (if any exist as simple loads) are still
 *     unfound -- but the pattern (AI decision code inside func_80025268's
 *     per-slot dispatch cluster) is now a well-founded place to keep
 *     looking, rather than an open-ended guess.
 *   - [ROUND 27] Traced func_8002128C (round 26's per-opponent update
 *     function) fully, to answer round 26's open question: does it reach
 *     func_8001B374's collision-response fields (slot+0x44..+0xC0)? ANSWER:
 *     NO. Every field func_8002128C touches on its slot pointer -- +0x0,
 *     +0x2, +0x8, +0x10, +0x14, +0x18, +0x20, +0x24, +0x28, +0x38, +0x40 --
 *     falls inside the "car-shaped prefix" range (+0x8..+0x40) round 24
 *     already identified, and it never reads or writes anything at +0x44
 *     or beyond. So this rules out func_8002128C as the missing collision
 *     consumer; whatever reads func_8001B374's response fields is still
 *     unfound.
 *
 *     What func_8002128C actually looks like, based on this full trace, is
 *     an opponent-car 3D POSITIONAL AUDIO update, not an AI/behavior
 *     update as round 26 guessed:
 *       - It reads slot+0x2 (small integer, likely a car-model/type id)
 *         and uses it as an index (*0x10) into a table based at
 *         `func_80059228` -- read-only lookup of 16-byte per-type audio
 *         parameters (thresholds pulled from it at +0x0/+0x2/+0x4/+0x6/
 *         +0x8 are used later as comparison/index values).
 *       - It reads slot+0x10/+0x14/+0x18 (the CONFIRMED position-like
 *         fields, matching the player car's own +0x10/+0x14/+0x18) and
 *         computes a taxicab-style distance to a reference point held in
 *         `D_801D9068`/`D_801D9070` -- a 2-field global also read
 *         extensively inside `func_800129AC` (a function this same
 *         routine calls repeatedly with a scratchpad address, a distance-
 *         vector pointer, and an output pointer -- the shape of a 3D-pan/
 *         attenuation calculator) and inside the func_80018584/
 *         func_8001865C/func_800185F0 cluster (also called here). That
 *         strongly suggests `D_801D9068`/`D_801D9070` is the audio
 *         LISTENER position (almost certainly the player/camera), not
 *         anything collision-related.
 *       - The computed distance is compared against two thresholds
 *         (0xD00, 0x2500) to pick one of three tiers: near (full
 *         processing -- lots of func_800129AC/func_8003486C calls, plus a
 *         voice-commit call `func_8001315C` at the end if a voice handle
 *         was acquired), mid (a smaller version of the same processing),
 *         and far (skip entirely) -- a textbook distance-based LOD for
 *         per-opponent engine/effect sound, not a physics update.
 *       - slot+0x8 is used right at the top as an argument to
 *         `func_80012EF0`, whose result is treated as a voice/channel
 *         handle: if nonzero, `func_80012FE4` is called on it immediately,
 *         and `func_8001315C` (no args) is called again at the very end if
 *         that handle was nonzero -- an acquire/commit pattern consistent
 *         with per-opponent SPU voice management.
 *     None of this touches car+0xA4, D_801E9250's own +0xA4 sawtooth, or
 *     the collision-response fields -- it's a self-contained finding that
 *     mostly closes out func_8002128C as "solved" (opponent positional
 *     audio) while leaving the actual collision-response consumer as an
 *     open question again, now with one major false lead eliminated.
 *   - [ROUND 24] func_8001B374's "other-slot" globals identified as fields
 *     of a 12-entry, stride-0x114 struct array based at D_801E9250 --
 *     confirmed directly in the asm (`addiu s7,s7,0x114` / `addiu
 *     t1,t1,0x114` advancing two loop pointers each iteration, gated by
 *     `slti v0,v0,0xC`, i.e. loop while index < 12). This is the SAME
 *     `D_801E9250` that rounds 21/22 found being saved/restored
 *     alongside the player car in the ghost/record system, previously
 *     left as "role not independently confirmed" -- it's the base of
 *     the car-vs-car collision response slot array, not a single mystery
 *     struct. The specific fields func_8001B374 writes for slot 0
 *     (D_801E9294, +0x44; D_801E9298, +0x48; D_801E929C, +0x4C;
 *     D_801E92A0, +0x50; D_801E92A4, +0x54; D_801E92B4, +0x64;
 *     D_801E92BC, +0x6C; D_801E92D8, +0x88; D_801E9310, +0xC0, all
 *     relative to D_801E9250) sit well past the +0x8..+0x40 range round
 *     21/22's ghost snapshot reads/writes for this same struct -- i.e.
 *     slot 0 of this array has BOTH a "car-shaped" prefix (matching the
 *     player struct's own layout, used by the ghost system) AND a
 *     separate collision-response region beyond it (+0x44 onward, used
 *     by car-vs-car). This is consistent with D_801E9250 being an array
 *     of FULL per-opponent car structs (0x114 bytes each is a plausible
 *     total car struct size, given confirmed fields already reach
 *     +0xCA/+0xAC/+0xB4), with func_8001B374 writing into whichever
 *     opponent slot(s) the player's own hull overlapped this frame.
 *     STILL NOT found: the actual per-opponent consumer that reads
 *     D_801E9250[slot]+0x44..+0xC0 back out and applies it (would need
 *     to be a per-opponent physics-update routine analogous to
 *     func_8001C490/func_80033584/func_80033438, but for AI cars -- not
 *     confirmed to exist in this decompiled code at all, since the game
 *     may run this logic in a way that isn't statically distinguishable
 *     from the player's own update, or opponents may not be simulated
 *     with this level of fidelity in the version captured here).
 *     Grepping for a `car+0x44`-equivalent read relative to the
 *     player's own D_8007C258 found nothing, so the player itself does
 *     not appear to read this response region the same way. Building a
 *     real multi-car simulation to consume this is still an
 *     architecture decision for this port, not something more tracing
 *     resolves. car+0x14 and car+0x40 (surfaced in func_80033584, round
 *     19) remain unidentified beyond their role in that one formula.
 *     func_80017B58's own tail (the 4-way branch selecting return codes
 *     0-3 based on comparisons against D_80173368/D_801E9248/
 *     D_80173378/D_801E9F40) wasn't fully unpacked -- only enough to
 *     confirm its along/lateral projection shape, which is what mattered
 *     for round 17's port.
 *   - D_801733A0 -- FURTHER NARROWED, still not conclusively explained.
 *     Round 12 found it (compile-time constant 61440, X-channel only, one
 *     write site). Round 13 found 4 more call sites with the identical
 *     asymmetric pattern. Round 17 adds: (a) a SECOND, fully independent
 *     function (func_80017B58) applies the exact same X-only bias in the
 *     exact same algebraic shape, ruling out a one-function coincidence;
 *     (b) hand-expanding func_80017838's cross-product formula with the
 *     bias applied to both edge points but NOT to the query point shows
 *     the bias does NOT cancel out algebraically (it adds a term
 *     proportional to D_801733A0*(edge_b.z - edge_a.z)) -- so this is a
 *     real, deliberate effect on the gate test's result, not a
 *     mathematically-inert recentering. Leading hypothesis: an X-axis-
 *     only world-to-collision-space coordinate offset, set once at
 *     course load (always the same literal, not derived from course
 *     data per round 12). Still unverified against a real captured
 *     numeric example, and still why ONLY X and not Z is unexplained --
 *     needed before physics_find_section_local_walk could adopt
 *     func_8001BAFC's now-confirmed byte-exact walk instead of its
 *     current center-to-center midpoint approximation (see physics.h).
 */
