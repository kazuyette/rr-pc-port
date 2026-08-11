/* physics.h -- host-native (portable C, no PSX dependencies) car
 * physics core for rr-pc-port, reimplementing the CONFIRMED structure
 * of Ridge Racer 1's live race-gameplay code (PS1 function
 * `FUN_8001c490`, the per-car per-frame update, and its track-following
 * callees) derived entirely by tracing rr-decomp's 100%-matched
 * disassembly -- see the project session notes
 * (`rr_pc_port_physics_round1.md`, rounds 3-8) for the full RE writeup.
 *
 * This is a REIMPLEMENTATION of understood BEHAVIOR in original code,
 * not a transcription of PS1 assembly or a copy of any game data table
 * -- consistent with this repo's whole premise (a from-scratch native
 * port informed by, but not derived byte-for-byte from, the original
 * binary). Track SHAPE data (positions/headings/widths) is never
 * committed here -- see tools/trackdata for the tool that extracts it
 * from the user's own PSX.EXE at runtime/build time.
 *
 * What's a faithful port of a CONFIRMED formula vs. an honest
 * approximation, spelled out per-function below:
 *
 *   - Gear-shift thresholds (physics_gearbox_update): CONFIRMED exact --
 *     these are the literal comparison constants read out of
 *     FUN_8001c490's decompiled code (downshift when the RPM-like value
 *     drops below `(6-gear)*-200 + 0xdac`, upshift above `0x1900`).
 *   - Track-relative projection (physics_track_project, wrapping
 *     tools/trackdata's trackdata_project_point): CONFIRMED formula
 *     shape (rotate into section-local forward/right frame, compare the
 *     right-of-center component to an asymmetric width), reimplemented
 *     in real-world doubles rather than the original's fixed-point/BAM
 *     encoding. Round 12: the original's BAM12 trig scale is now
 *     CONFIRMED to be plain Q12 fixed point (raw table value / 4096.0 =
 *     the real sin/cos value, verified against the actual lookup table
 *     bytes in rr-decomp) -- since this port already used real
 *     double-precision sin()/cos() (the infinite-precision version of
 *     the same value), no code changed here, this just confirms the
 *     formula shape and the trig scale were both already right.
 *   - Forward acceleration/braking and steering response curves
 *     (physics_car_integrate): an ORIGINAL, NOT bit-exact model. The
 *     RE work confirmed *what* FUN_8001c490 does structurally
 *     (surface/pitch-influenced accel accumulator, position integration
 *     from velocity components, a dedicated turn-rate subroutine,
 *     wall/car collision with bounce-and-slow response) but the exact
 *     per-gear accel curve and turn-rate numbers were not extracted --
 *     see rr_pc_port_physics_round1.md round 7's note that
 *     `DAT_80077140` (originally guessed as a friction constant) turned
 *     out to be a smoothed suspension/pitch-like value, not a simple
 *     grip multiplier, so a direct 1:1 port of that specific term was
 *     not attempted here. This gives the port real gearbox behavior and
 *     real track-relative off-track detection today, with a reasonable
 *     placeholder accel/steering feel pending a future round that pins
 *     down the remaining constants (see file-level TODO at the bottom
 *     of physics.c).
 */
#ifndef RR_PHYSICS_H
#define RR_PHYSICS_H

#include "../tools/trackdata/trackdata.h"

/* Gear-shift thresholds, CONFIRMED exact from FUN_8001c490 (see file
 * header comment). RPM is an internal unitless accumulator, not a
 * real-world RPM display value -- the original never exposes a
 * dashboard tachometer scale, only these two threshold formulas. */
#define PHYSICS_GEAR_MIN 1
#define PHYSICS_GEAR_MAX 6
#define PHYSICS_UPSHIFT_RPM 0x1900 /* 6400 */
/* Downshift threshold for a given current gear (1..6). */
#define PHYSICS_DOWNSHIFT_RPM(gear) ((6 - (gear)) * -200 + 0xdac)

typedef struct {
    double x, z;       /* world position */
    double heading;    /* radians */
    double speed;      /* world units / second, always >= 0 */
    int gear;           /* 1..6 */
    double rpm;         /* internal unitless accel accumulator, see PHYSICS_*_RPM above */
    int manual_transmission; /* nonzero: gear changes driven by shift_up/shift_down input;
                                zero: auto-shifts off PHYSICS_*_RPM thresholds */
} PhysicsCar;

void physics_car_init(PhysicsCar *car, double x, double z, double heading);

/* Updates car->gear/car->rpm for one frame. `throttle` is 0..1 (how hard
 * the accelerator is pressed -- feeds the auto-shift RPM accumulator);
 * `shift_up`/`shift_down` are nonzero on a fresh button press this frame
 * (only consulted when car->manual_transmission is set). Mirrors
 * FUN_8001c490's confirmed gear/RPM logic (see file header). */
void physics_gearbox_update(PhysicsCar *car, double throttle, int shift_up, int shift_down, double dt);

/* Integrates the car's position/heading/speed for one frame. `throttle`
 * is 0..1, `brake` is 0..1, `steer` is -1..1 (left..right), `off_track`
 * is nonzero if the caller has already determined (via
 * physics_track_project) that the car is off the track surface this
 * frame. See file header: this is an original approximation, not
 * bit-exact, gated by the current gear (higher gears = higher top
 * speed / lower acceleration, in the spirit of a 6-speed gearbox, exact
 * curve not extracted from the original). Round 15: `off_track` scales
 * down top speed and acceleration -- the SCALE FACTORS are this port's
 * own arcade-feel choice (grass slows you down), not an RE'd formula;
 * see physics.c's comment on off_track_speed_scale for what's confirmed
 * (that the original tracks off-track state the same way, every frame)
 * vs. approximated (what it actually does about it). Round 17:
 * `wall_lateral_gradient` (see physics_wall_probe_lateral_gradient below)
 * is a continuous decel term, structurally mirroring func_80033584's
 * CONFIRMED (front_left-front_right)+(rear_left-rear_right) -> accel-
 * accumulator effect -- pass 0.0 if not computed this frame. The scale
 * constant applying it is NOT RE'd (car+0xA4's real value and the
 * original's >>6 rounding weren't extracted), only the SHAPE is. */
void physics_car_integrate(PhysicsCar *car, double throttle, double brake, double steer,
                            int off_track, double wall_lateral_gradient, double dt);

/* Round 17 finding: func_8001BD9C (the wall probe, structurally traced
 * round 16) samples 4 fixed points around the car using CONFIRMED local
 * offsets read out of the original's own lookup table (D_8007306E,
 * real byte values extracted this round) -- front-left (-32,32),
 * front-right (32,32), rear-left (-32,-64), rear-right (32,-64), in
 * car-local units where +X is right and +Z is forward (this port's
 * existing rotation convention, matching physics_car_integrate's own
 * sin(heading)/cos(heading) usage). func_80033584 (the response
 * consumer, traced round 17) combines the 4 probes' recorded per-probe
 * values as (front_left - front_right) + (rear_left - rear_right) and
 * feeds the result into the car's CONFIRMED accel accumulator
 * (car+0xAC) -- i.e. a lateral wall-proximity ASYMMETRY (closer to a
 * wall on one side than the other) drains acceleration, not just being
 * fully off-track. This function is the host-native port of that
 * projection+combine step (not the response, which lives in
 * physics_car_integrate's new wall_lateral_gradient parameter above):
 * rotates the 4 CONFIRMED offsets by car->heading, projects each via
 * physics_track_project against `sec`, and returns
 * (excess_FL - excess_FR) + (excess_RL - excess_RR), where each probe's
 * `excess` is 0 while that probe is still within the section's width and
 * its signed distance past the boundary once it isn't. IMPORTANT: this
 * deliberately does NOT difference the raw lateral values -- a plain
 * (lateral_FL - lateral_FR) cancels to a position-independent constant
 * (just the fixed hull spread) regardless of where the car actually is,
 * since raw lateral grows linearly with car position and that term
 * subtracts away identically for every probe pair -- verified this round
 * by hand-expanding the formula, not a hypothetical. The real per-probe
 * value func_8001BD9C/func_80017B58 actually record (traced this round)
 * defaults to 0 while a probe isn't past the boundary and only becomes a
 * clamped nonzero value once it is, which the excess-based formula here
 * matches in SHAPE (0 while clear, nonzero once a probe is genuinely
 * near/past a wall) even though the exact clamping constant isn't
 * reproduced. ~0 when the car is centered or all probes are clear; grows
 * as one side's probes go off-track while the other's don't. NOT
 * byte-exact: `sec` is assumed to be the car's own current section for
 * all 4 probes (the original's func_8001BAFC independently re-walks the
 * section for each probe point via the byte-exact edge-crossing test --
 * round 16/17 finding -- which this port does not replicate, since a
 * probe a few dozen units from the car is essentially always in the same
 * section in practice). The original's own front/rear longitudinal
 * combination (front_left-rear_left)+(front_right-rear_right) feeds a
 * camera-shake-looking global and a car field (+0x20), and the lateral
 * combination ALSO drives a separate oscillator (car+0x28, whose value
 * is fed through a cos() lookup into car+0x14 -- a genuine judder/
 * vibration term, not a plain linear one) plus a threshold that this
 * port does NOT reproduce: the real accel-drain only triggers when the
 * lateral gradient is >= +5 (a SIGNED check -- a strongly negative
 * gradient never triggers it at all, a one-sided asymmetry). See
 * physics.c's TODO ([RESOLVED round 17, CORRECTED round 19]) for the
 * complete, precise constant-by-constant breakdown -- none of that
 * extra structure (oscillator, threshold, car+0x14/0x20/0x28/0x40) is
 * ported here; this function only reproduces the lateral gradient's
 * SHAPE, applied to this port's own excess-based quantity rather than
 * the original's raw per-probe fields.
 *
 * [round 20 addendum] car+0x20 is NOT exclusive to func_80033584 -- a
 * sibling function, func_80033438, is called on the SAME car pointer
 * immediately before func_80033584 at every call site found (both back
 * to back in the drive-update routine) and ALSO writes car+0x20, via a
 * single shared (non-per-car) global ramp D_8012CF80: while an arm-delay
 * gate (D_801D9060) is under 2 it forces the ramp to 0 and skips; during
 * an initial ~30-tick warmup window (D_8012CD20 < 30) it ramps +1/call up
 * to a cap of 16; afterwards it ramps +1/call up to a cap of 8 while two
 * button/timer fields are both "held" (car+0xCA >= 129 AND car+0xA0 >=
 * 81), ramps -2/call down to a floor of -16 while a third field is held
 * (car+0xC8 >= 129) and neither of the above holds, and otherwise decays
 * toward 0 by a factor of 0.75/call (round toward zero). car+0x20 +=
 * D_8012CF80 unconditionally. This is a classic press-and-hold ease-in/
 * ease-out ramp keyed off the same button-flag region (car+0xC8/0xCA)
 * already confirmed elsewhere, plus the confirmed car+0xA0 ramp/frame-
 * timer field. Combined with func_80033584's own car+0x20 contribution
 * (the running total of the wall-scrape longitudinal gradient), this
 * makes car+0x20 a shared accumulator fed by BOTH sustained directional
 * input AND wall-scraping -- the leading hypothesis is a body-lean/
 * weight-transfer value (visual tilt, or a secondary grip modifier),
 * since both causes are physically the kind of thing that would make an
 * arcade car lean. Not ported: this port's PhysicsCar has no equivalent
 * field. [round 21] The read site IS now found: func_8002AE14, called
 * from a gated ~30s-cadence state machine, snapshots car+0x20 (as a
 * 16-bit value) into a persisted-looking record buffer (D_8007C4F8,
 * next to what looks like a saved best-time header) ALONGSIDE car+0x8
 * (position-like), car+0x10/+0x14/+0x18 (position/velocity-like),
 * car+0x38 (near-certainly heading, seen elsewhere wrapped mod one
 * BAM12 turn), and its own sibling accumulators car+0x24/+0x28/+0x40 --
 * the signature of a ghost/record-replay snapshot. Being recorded
 * alongside position/velocity/heading rather than staying purely
 * internal to its two writer functions is real supporting evidence for
 * the body-lean hypothesis above (a replay without it would visibly
 * lack body roll). [round 22] The playback consumer IS now found too:
 * func_8002AF1C is the exact inverse of func_8002AE14 and writes the
 * unpacked record DIRECTLY BACK into the live player car struct
 * (D_8007C258) at all 3 call sites found. One call site uses a
 * hardcoded slot 0 and, in the same breath, resets the CONFIRMED
 * arm-delay gate (D_801D9060) -- the signature of a race-(re)start
 * handler restoring car+0x20/+0x24/+0x28/+0x40 to a saved baseline
 * alongside position/velocity/heading, so a restart doesn't carry over
 * leftover wall-scrape lean. The other 2 call sites use a variable,
 * sequence-counter-driven slot, consistent with (not yet certain to be)
 * a frame-by-frame ghost/intro-sequence playback. Net result: car+0x20/
 * +0x28/+0x40 are CONFIRMED first-class car state -- explicitly saved
 * and restored on equal footing with position/velocity/heading, not
 * internal scratch -- even though their precise visual/mechanical
 * meaning is still the leading hypothesis (body lean), not proven. [round
 * 23] car+0xA4 -- the wall-scrape response's per-car scale factor that
 * was still unidentified -- is now understood too, though it's a
 * DIFFERENT cluster (+0x24/+0xA0/+0xA4/+0xA8, not +0x14/0x20/0x28/0x40).
 * func_80026CA8 uses car+0xA0/+0xA4 alongside two independent Q12 trig
 * lookups of car+0xA8 and car+0x24 to run a self-updating (magnitude,
 * phase) oscillator, writing new values back into car+0xA0/+0xA8 every
 * call; car+0x24 is itself advanced elsewhere proportional to the
 * CONFIRMED accel accumulator (car+0xAC), so it spins faster under
 * acceleration -- reads as a wheel-spin/suspension-bounce visual system.
 * car+0xA4 is the best-supported reading of a per-car RESPONSIVENESS
 * constant, reused by func_80033584's wall-scrape accel-nudge for the
 * same purpose applied to a different trigger. [round 24] Unrelated to
 * this function, but resolves an open thread from rounds 21/22: the
 * "second struct" (D_801E9250) saved/restored alongside the player car
 * in the ghost/record system is the base of func_8001B374's (car-vs-car
 * collision) 12-slot, stride-0x114 response array -- not a standalone
 * mystery struct. [round 25] car+0xA4's write/init site is still not
 * found; 2 false leads (a car+0x58-relative config loader, and a
 * distinct 16-bit +0xA4 field belonging to D_801E9250 itself, likely
 * camera shake) were ruled out and shouldn't be re-checked. [round 26]
 * Two more threads resolved: found the per-opponent update loop (12
 * slots of D_801E9250, gated on an active flag and a type flag, calling
 * func_8002128C) that round 24 couldn't confirm existed -- this is the
 * likely (not byte-exact-confirmed) home of func_8001B374's collision-
 * response consumer. Also corrected round 21: the ghost/record system's
 * recorder (func_8002AE14) runs as a CONTINUOUS per-frame recording
 * session while armed (up to 1800 frames), triggered by passing track
 * checkpoints, not a periodic ~30s snapshot as previously described --
 * D_8007C4F8 is a genuine multi-frame clip buffer. D_80173310 confirmed
 * as a real general-purpose frame counter. [round 27] Fully traced
 * func_8002128C: it does NOT reach func_8001B374's collision-response
 * fields (+0x44..+0xC0) -- it only ever touches the +0x8..+0x40
 * car-shaped-prefix range. It's actually an opponent-car 3D POSITIONAL
 * AUDIO update (distance-LOD'd via D_801D9068/D_801D9070, the likely
 * listener position, feeding func_800129AC/func_8003486C and an SPU
 * voice acquire/commit pair), not the AI/collision consumer round 26
 * guessed. [round 28] FOUND a real consumer: xref search on the 9
 * confirmed response-field symbols found D_801E92D8 (+0x88) read outside
 * func_8001B374, in func_8002252C -- which compares one opponent's own
 * +0x88 against a SPECIFIC OTHER opponent's +0x88 (via a computed
 * slot-array index) to drive overtake/block/avoid-looking decisions.
 * func_8002252C's caller, func_80025268, is the REAL per-opponent AI
 * update function (called once/frame from func_80014C2C, loops all 12
 * D_801E9250 slots internally) -- distinct from round 27's func_8002128C,
 * which is a separate, purely audio-side per-slot loop. The other 8
 * response fields still have no confirmed consumer. [round 29] Checked
 * func_80025268's own body and all ~11 subroutines for direct-offset
 * accesses to the other 8 fields -- none found (2 apparent hits were
 * false leads, both relative to the a0+0x58 sub-struct, not the raw slot
 * pointer). Rounds out that sub-struct as a bigger AI-state block
 * (slot+0x9C/0xA2/0xA6/0xAE/0xB0/0xC4) but doesn't solve the main
 * question -- still open after 2 dedicated rounds. [round 30] Fully
 * traced 4 of func_80025268's smaller subroutines -- none touch the
 * target fields, but 2 of them (func_80021BE0/func_80021CB4) turned out
 * to be AUDIO priority-scoring helpers, not AI/collision logic. So
 * func_80025268's dispatch cluster is a MIXED per-opponent update (AI +
 * audio), not purely AI as rounds 26/28 assumed. 4 larger subroutines
 * (func_800217F4, func_80024C64, func_8002362C, func_80025050) remain
 * untraced and are the last place to look. [round 31] Finished the sweep
 * -- the last 4 subroutines also touch nothing (direct, indirect, or
 * even as a bare constant), and a whole-file symbol re-check confirms
 * zero references to the other 8 response fields anywhere outside
 * func_8001B374. CONCLUSION: func_8001B374 writes these 8 fields but
 * nothing in the decompiled codebase reads them back -- likely dead
 * code, or consumed through a path invisible to static tracing (or one
 * outside the AI/audio cluster entirely, e.g. rendering). Deprioritized
 * after 4 rounds (28-31); D_801E92D8's own consumer (round 28) is
 * enough to understand the collision-response mechanism's shape.
 * [round 32] Closed a separate thread: D_801E9250+0xA4 (the "camera-
 * shake?" sawtooth flagged round 25) is now fully traced -- a -150..120
 * counter (func_800229F4) gated by func_80022A58, which is itself only
 * ever called inline inside func_80025268, gated by D_801D9060<4. The
 * gate also requires the AI-state fields +0x96/+0xA2 AND compares
 * +0x88 (D_801E92D8) against a global threshold, so this is now read as
 * an AI-state pacing/cooldown counter for the overtake/block behavior,
 * not camera shake -- and it has no other reader anywhere, so it's
 * likely self-contained. [round 33] Widened the search for the OTHER
 * open +0xA4 (car's own, round 25) 3 ways -- top-of-call-chain functions,
 * all 18 direct D_8007C258-referencing functions, and the computed
 * base+0xA4 register pattern (5 hits, all a false lead in an unrelated
 * menu/list-rendering cluster). Still unfound; the cheap searches are now
 * exhausted the same way the 8-collision-field thread was (rounds 28-31)
 * -- only a full call-graph trace would settle it. [round 34] Confirmed
 * round 23's 3 oscillator helpers: func_80019CA8 = signed BAM12
 * angle-difference, func_80040B54 = integer sqrt, func_800187A0 =
 * atan2-style arctangent (LUT-based) -- so car+0xA0/+0xA8 is a genuine
 * Cartesian-to-polar (magnitude/phase) recompute each call. Also traced
 * car+0x5C's states 2/3: same oscillator tick as state 0, but BOTH also
 * feed the oscillator's magnitude*trig(phase) into car+0x60 (velocity)
 * -- a new physics link consistent with a wheelspin/traction-loss
 * drive-mode reading for those 2 states. [round 35] Found car+0x5C's
 * RESET site: func_800205E4 zeroes it as part of a spawn-time cascade,
 * called from func_800206CC (a per-car spawn/init function) inside a
 * race/grid-setup slot loop. Answers "where does it reset to 0" but NOT
 * "what advances it to 1/2/3 during a race" -- that trigger is still
 * unfound. [round 36] Exhausted the direct-offset search for that
 * trigger (all 6 `0x5C(` stores file-wide checked; only the reset is
 * real) -- deprioritized alongside the 8-field/car+0xA4 threads.
 * Separately, resolved `D_801733A0` (stale since ~round 13): a one-time
 * race-setup constant (0xF000), grouped with SPU voice-count init, used
 * as a max-range bound in track-geometry/audio calculations -- NOT a
 * physics constant, ruled out of scope. [round 37] Closed the
 * long-open "aux_a_raw checkpoint path" (round 13): func_800382A0,
 * called once/frame from the top-level dispatcher, is the
 * checkpoint-approach sound cue + on-screen arrow + smooth marker
 * transition updater (a singleton record at D_801D80A8, globals only,
 * no car-struct parameter) -- confirmed not a physics function.
 * Also resolved the "record offset 0x12" question: it's a structural
 * alias of D_801E9250+0x14 (same field as car+0x14), not a distinct
 * unknown field. See physics.c's TODO for the full trace and what's
 * still open. */
double physics_wall_probe_lateral_gradient(const PhysicsCar *car, const TrackSection *sec);

/* Host-native wrapper around trackdata_project_point (see trackdata.h):
 * finds how far car is from `sec`'s centerline and whether it's
 * off-track. Returns nonzero if off-track. */
int physics_track_project(const PhysicsCar *car, const TrackSection *sec,
                           double *out_along, double *out_lateral);

/* Circular (wraparound-aware) interpolation between two BAM12 headings
 * (0..4095 = one turn), by weight t in 0..1 -- host-native port of the
 * CONFIRMED formula from PS1 function func_800177B8 (round 5/8): takes
 * the short way around the circle rather than naively lerping raw
 * angle values, so a blend across the 0/4095 wrap point (e.g. a
 * hairpin near due-north) doesn't snap the wrong way. Returns radians,
 * for direct use as a target heading. */
double physics_blend_heading_bam(int16_t heading_a, int16_t heading_b, double t);

/* Given the car's current position and a TrackData course (see
 * trackdata.h), finds the closest section by brute-force nearest-center
 * search (a simple, robust O(n) fallback -- see
 * physics_find_section_local_walk below for the cheaper, stateful
 * alternative used every frame once a starting guess is known). Returns
 * the section index, or -1 if td->count == 0. */
int physics_find_nearest_section(const PhysicsCar *car, const TrackData *td);

/* Round 10 finding: PS1 function func_80017DF4 does NOT globally search
 * for the car's section -- it assumes the car moved at most a few
 * sections since last frame and walks outward from the previous
 * section index, backward then forward, using func_80017838's
 * edge-crossing test to detect when it has stepped too far. This is
 * this port's OWN reimplementation of that same LOCAL-WALK spirit, not
 * a byte-for-byte port of the original's edge-crossing test.
 *
 * Round 12 finding: func_80017838 itself is now CONFIRMED byte-exact --
 * it's a plain 2D cross-product "which side of the directed line A->B
 * is point P on" test (see tools/trackdata/trackdata.h's file header
 * for the derived formula). The caller builds A/B as a candidate
 * section's own right-edge/left-edge "gate" points via func_800178A0,
 * in car-relative coordinates. func_800178A0 is only PARTIALLY
 * confirmed, though: its Z-channel formula is clean and confirmed, but
 * its X-channel carries an extra additive constant (D_801733A0, a
 * confirmed compile-time value of 61440, but with no confirmed reason
 * to apply only to X and not Z) -- see trackdata.h for the full writeup.
 * Because of that unresolved asymmetry, this function still does NOT
 * port the original's edge-point gate-line test. Instead it walks
 * section-to-section using the midpoint between adjacent section
 * CENTERS as the boundary (car is considered to have crossed into the
 * next/previous section once it's past that midpoint, projected onto
 * the direction between the two centers). This isn't just a stand-in
 * for the unported edge-point math, either: for this function's actual
 * job -- finding *some* correct nearby section index every frame, not
 * reproducing the original's exact per-section gate geometry -- a
 * perpendicular bisector between two consecutive centers is arguably a
 * more robust boundary than one anchored to a single section's own
 * width-line, since it stays sane across the whole local neighborhood
 * the walk explores rather than just near that one section's gate.
 * `prev_index` should be the section index found last frame (or -1 to cold-start via
 * physics_find_nearest_section, e.g. on the very first call). O(1)
 * amortized per call (a handful of steps at most, since the car can't
 * realistically cross many section boundaries in one frame), unlike
 * physics_find_nearest_section's O(n) full scan every call -- use this
 * one for per-frame tracking once you have a previous index to seed it
 * with. Returns the new section index, or -1 if td->count == 0. */
int physics_find_section_local_walk(const PhysicsCar *car, const TrackData *td, int prev_index);

/* Round 13 finding: func_80017DF4's FINAL output computation (the part
 * that writes to its output struct's field 0 -- what FUN_8001c490 goes
 * on to use for steering), traced instruction-by-instruction and
 * CONFIRMED byte-exact in formula SHAPE:
 *
 *   1. Blend aux_heading_a_raw*8 and aux_heading_b_raw*8 across the
 *      bracketing sections by weight t, via the same short-way circular
 *      interpolator as func_800177B8 / physics_blend_heading_bam (that
 *      routine reduces its inputs mod 4096 BAM units before blending,
 *      so multiplying by 8 first and then wrapping is well-defined for
 *      any input).
 *   2. If the blended value is >= 2048 (half a turn), subtract a full
 *      turn (4096) -- wrapping it into roughly [-2048, 2047]. Call this
 *      `magnitude`.
 *   3. angle = 2048 - current_heading_bam - magnitude (BAM units).
 *   4. return magnitude * sin(angle).
 *
 * (The original does step 4 in Q12 fixed point via func_80044E2C then
 * divides by 4096 -- see round 12's trig-scale confirmation in
 * trackdata.h -- collapsed here into a single real-valued multiply.)
 *
 * NOT CONFIRMED: two things upstream of this function's own math.
 * First, func_80017DF4 ALSO blends the two sections' plain heading_raw
 * fields (+0x0A) via func_800177B8 immediately before this computation,
 * but the traced instruction stream never stores or reads that result
 * before it's overwritten -- either genuinely unused (dead in the
 * original source too) or consumed via a path this round didn't
 * account for; this port does not reproduce that call since nothing
 * observable depends on it. Second, `current_heading_bam` corresponds
 * to a value the original reads from its OWN caller-supplied output
 * struct (field +0x4, populated by FUN_8001c490 before the call, not
 * yet traced) -- almost certainly the car's current heading, but not
 * independently confirmed.
 *
 * [RESOLVED round 18] How FUN_8001c490 consumes this function's return
 * value -- traced the ONE call site inside FUN_8001c490 itself (of 7
 * total call sites to func_80017DF4 in the whole codebase, the other 6
 * are in unrelated functions). The surprising answer: it is NOT added to
 * the car's heading at all. FUN_8001c490 calls func_80017DF4, its
 * output-struct's field 0 (this function's return value) is read
 * straight back off the stack, and fed DIRECTLY as the probe-radius
 * argument (a3) to the very next call in the same block -- func_8001BD9C,
 * the CONFIRMED (round 16/17) wall-boundary probe. In other words: this
 * isn't a steering command at all in the original engine -- it's an
 * ANTICIPATED-CURVATURE signal that scales how far ahead/wide the wall
 * probe looks (a sharper upcoming bend -> a different probe reach),
 * which then feeds func_80033584's soft accel-based wall response
 * (round 17). The function name/parameter names here are kept as-is for
 * continuity with rounds 10-17's documentation, but readers should treat
 * "steering reference" as a historical label, not a literal one -- this
 * is a curvature-anticipation term for wall-probing, not a direct
 * heading/turn-rate input. NOT ported this round: wiring a dynamic,
 * curvature-scaled probe radius into physics_wall_probe_lateral_gradient
 * (which currently uses the CONFIRMED but FIXED D_8007306E offsets) is
 * real future work, not attempted here since it changes that function's
 * whole shape, not just a constant.
 *
 * `aux_heading_a_raw`/`aux_heading_b_raw` are the two bracketing
 * sections' TrackSection::aux_heading_raw fields (see trackdata.h); `t`
 * is the same 0..1 blend weight used elsewhere; `current_heading_rad` is
 * the car's current heading in radians (this port's convention --
 * converted internally to BAM units to match the original's domain).
 * Returns the raw, not-yet-scale-confirmed output value (roughly
 * -2048..2048 in magnitude). */
double physics_steering_reference_raw(int16_t aux_heading_a_raw, int16_t aux_heading_b_raw,
                                       double t, double current_heading_rad);

#endif /* RR_PHYSICS_H */
