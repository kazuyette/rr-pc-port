/* rr-pc-port -- headless sanity check for src/physics.c and
 * tools/trackdata's project-point math. No SDL/display, no real
 * PSX.EXE needed: builds a small synthetic track section by hand and
 * asserts the confirmed formulas behave as documented (gear thresholds,
 * off-track detection, heading wraparound). Built as its own small
 * executable (rr_pc_port_physics_test) by CMake. Exit code 0 = all
 * checks passed.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "physics.h"

static int failures = 0;

static void expect_true(int cond, const char *what) {
    if (cond) {
        printf("ok:   %s\n", what);
    } else {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

static void expect_int_eq(int got, int expected, const char *what) {
    if (got == expected) {
        printf("ok:   %s (== %d)\n", what, expected);
    } else {
        printf("FAIL: %s: got %d, expected %d\n", what, got, expected);
        failures++;
    }
}

static void expect_near(double got, double expected, double tol, const char *what) {
    if (fabs(got - expected) <= tol) {
        printf("ok:   %s (%.4f ~= %.4f)\n", what, got, expected);
    } else {
        printf("FAIL: %s: got %.4f, expected ~%.4f (tol %.4f)\n", what, got, expected, tol);
        failures++;
    }
}

int main(void) {
    PhysicsCar car;
    TrackSection sec;
    double along, lateral;
    int offtrack;
    double h;

    printf("-- physics headless sanity check --\n");

    /* 1. Gearbox: CONFIRMED thresholds from FUN_8001c490 (see
     * physics.h). Drive RPM straight past the upshift line and check it
     * climbs gears; then straight down past the downshift line for the
     * new gear and check it drops back. */
    physics_car_init(&car, 0.0, 0.0, 0.0);
    expect_int_eq(car.gear, PHYSICS_GEAR_MIN, "gearbox starts in gear 1");

    car.rpm = PHYSICS_UPSHIFT_RPM + 1.0;
    physics_gearbox_update(&car, 1.0, 0, 0, 0.016);
    expect_int_eq(car.gear, 2, "auto-upshift fires once RPM clears 0x1900");

    car.rpm = PHYSICS_DOWNSHIFT_RPM(car.gear) - 1.0;
    physics_gearbox_update(&car, 0.0, 0, 0, 0.016);
    expect_int_eq(car.gear, 1, "auto-downshift fires once RPM drops below its gear's threshold");

    /* Manual mode: shift button edges move gear directly, ignoring RPM. */
    physics_car_init(&car, 0.0, 0.0, 0.0);
    car.manual_transmission = 1;
    physics_gearbox_update(&car, 0.0, 1, 0, 0.016);
    expect_int_eq(car.gear, 2, "manual shift-up moves exactly one gear");
    physics_gearbox_update(&car, 0.0, 0, 1, 0.016);
    physics_gearbox_update(&car, 0.0, 0, 1, 0.016);
    expect_int_eq(car.gear, 1, "manual shift-down clamps at gear 1, not below");

    /* 2. Track projection: a section centered at the origin, heading 0
     * (pointing along +Z per physics_car_integrate's convention: x +=
     * sin(heading)*speed, z += cos(heading)*speed -- heading 0 is
     * "straight along +Z"), with an intentionally asymmetric width
     * (right=10, left=30) so left/right selection is unambiguous. */
    sec.x = 0.0;
    sec.z = 0.0;
    sec.heading_raw = 3072; /* +0xC00 cancels the function's internal -0xC00 offset, i.e. "heading 0" */
    sec.width_right = 10.0;
    sec.width_left = 30.0;

    physics_car_init(&car, 5.0, 0.0, 0.0); /* 5 units to the "right" */
    offtrack = physics_track_project(&car, &sec, &along, &lateral);
    expect_true(lateral > 0.0, "point to the right of centerline reports positive lateral");
    expect_true(!offtrack, "5 units right is within the 10-unit right width -> on track");

    physics_car_init(&car, 20.0, 0.0, 0.0); /* 20 units right, right width is only 10 */
    offtrack = physics_track_project(&car, &sec, &along, &lateral);
    expect_true(offtrack, "20 units right exceeds the 10-unit right width -> off track");

    physics_car_init(&car, -20.0, 0.0, 0.0); /* 20 units left, left width is 30 -> still on track */
    offtrack = physics_track_project(&car, &sec, &along, &lateral);
    expect_true(lateral < 0.0, "point to the left of centerline reports negative lateral");
    expect_true(!offtrack, "20 units left is within the wider 30-unit left width -> on track "
                            "(confirms asymmetric width is actually applied per side)");

    /* 3. Heading blend: CONFIRMED wraparound behavior from
     * func_800177B8 (round 5/8) -- blending from near the top of the
     * BAM12 circle (4090) to near the bottom (10) should go THROUGH the
     * wrap point (the "short way", ~16 units forward), not the long way
     * backward across nearly the whole circle. */
    h = physics_blend_heading_bam(4090, 10, 0.5);
    /* Halfway through a short ~16-unit forward gap from 4090 lands near
     * BAM 4098 mod 4096 = 2, i.e. a small positive angle just past zero,
     * NOT anywhere near pi (which the naive long way would produce). */
    expect_near(fmod(h + 6.283185307179586, 6.283185307179586), 0.0031, 0.02,
                "heading blend takes the short way across the BAM12 wrap point");

    /* 4. Section local-walk: an 8-section closed loop (a circle, radius
     * 1000) built by hand so physics_find_section_local_walk's
     * forward/backward stepping and wraparound-at-index-0 can be
     * exercised without needing a real PSX.EXE. Only x/z matter here --
     * heading/width aren't consulted by this function. */
    {
        const int N = 8;
        TrackSection loop[8];
        TrackData td;
        int i, idx;
        const double R = 1000.0;
        const double two_pi = 6.283185307179586;

        for (i = 0; i < N; i++) {
            double angle = (double)i * (two_pi / (double)N);
            loop[i].x = R * sin(angle);
            loop[i].z = R * cos(angle);
            loop[i].heading_raw = 0;
            loop[i].aux_a_raw = 0;
            loop[i].aux_heading_raw = 0;
            loop[i].width_right = 50.0;
            loop[i].width_left = 50.0;
        }
        td.sections = loop;
        td.count = (size_t)N;

        /* cold start (prev_index=-1): falls back to nearest-center, car
         * sitting exactly on section 0. */
        physics_car_init(&car, loop[0].x, loop[0].z, 0.0);
        idx = physics_find_section_local_walk(&car, &td, -1);
        expect_int_eq(idx, 0, "local-walk cold start (prev_index=-1) finds section 0 via nearest-center");

        /* forward: car moved from section 0's neighborhood to sitting
         * exactly on section 1 -> walk should advance by one. */
        physics_car_init(&car, loop[1].x, loop[1].z, 0.0);
        idx = physics_find_section_local_walk(&car, &td, 0);
        expect_int_eq(idx, 1, "local-walk steps forward one section when the car crosses into it");

        /* backward: car actually behind section 0, sitting on section 7
         * -> walk should step back to 7 even though prev_index says 0. */
        physics_car_init(&car, loop[7].x, loop[7].z, 0.0);
        idx = physics_find_section_local_walk(&car, &td, 0);
        expect_int_eq(idx, 7, "local-walk steps backward when the car is actually behind prev_index");

        /* wraparound: prev_index is the LAST section (7), car has moved
         * forward across the loop-closure boundary onto section 0. */
        physics_car_init(&car, loop[0].x, loop[0].z, 0.0);
        idx = physics_find_section_local_walk(&car, &td, 7);
        expect_int_eq(idx, 0, "local-walk wraps from the last section back to index 0 at the loop closure");
    }

    /* 5. Steering reference (round 13): physics_steering_reference_raw
     * ports func_80017DF4's traced output formula --
     *   magnitude = wrap_to_signed_half_turn(circular_blend(aux_a*8, aux_b*8, t))
     *   output    = magnitude * sin(2048 - current_heading_bam - magnitude)
     * (BAM12 units, 4096 = one turn). These checks re-derive the same
     * formula independently in the test to catch implementation bugs
     * (wrong wrap sign, wrong angle sign, blend not applied), not to
     * assert real PS1-captured values -- how FUN_8001c490 actually
     * consumes this output wasn't traced (see physics.h), so there's no
     * known-good real-world number to check against yet. */
    {
        const double bam_to_rad = 6.283185307179586 / 4096.0;
        double out, expected, magnitude, angle_bam;

        /* Zero aux headings on both sides -> zero magnitude -> zero
         * output regardless of blend weight or current heading. */
        out = physics_steering_reference_raw(0, 0, 0.3, 1.2345);
        expect_near(out, 0.0, 1e-9, "steering reference is exactly zero when both aux headings are zero");

        /* No wraparound: aux_a=aux_b=100 (*8=800, well under the 2048
         * half-turn threshold) -> magnitude=800 untouched by the wrap
         * correction, independent of t since both sides are equal. */
        magnitude = 800.0;
        angle_bam = 2048.0 - 0.0 - magnitude;
        expected = magnitude * sin(angle_bam * bam_to_rad);
        out = physics_steering_reference_raw(100, 100, 0.5, 0.0);
        expect_near(out, expected, 1e-6, "steering reference matches the formula when no wrap is needed");

        /* Wraparound: aux_a=aux_b=300 (*8=2400 >= 2048) -> the blended
         * value must be wrapped back by a full turn (magnitude = 2400 -
         * 4096 = -1696) before feeding the sin term. */
        magnitude = 2400.0 - 4096.0;
        angle_bam = 2048.0 - 0.0 - magnitude;
        expected = magnitude * sin(angle_bam * bam_to_rad);
        out = physics_steering_reference_raw(300, 300, 0.5, 0.0);
        expect_near(out, expected, 1e-6, "steering reference wraps the blended aux heading past the half-turn threshold");

        /* Blend actually applies: aux_a=0, aux_b=100, t=0.5 should land
         * halfway (magnitude=400), not at either endpoint. */
        magnitude = 400.0;
        angle_bam = 2048.0 - 0.0 - magnitude;
        expected = magnitude * sin(angle_bam * bam_to_rad);
        out = physics_steering_reference_raw(0, 100, 0.5, 0.0);
        expect_near(out, expected, 1e-6, "steering reference blends between the two bracketing sections' aux headings");
    }

    /* 6. Off-track penalty (round 15): physics_car_integrate's own
     * arcade-feel stand-in (see physics.c/physics.h -- NOT an RE'd
     * formula, just this port's convention that off-track slows you
     * down), checked structurally: full throttle held off-track should
     * top out measurably below the same car's on-track top speed, and a
     * car already going faster than the off-track cap should visibly
     * bleed speed down when it goes off-track rather than just having
     * acceleration capped. */
    {
        int i;
        double on_track_top, off_track_top, speed_before;

        physics_car_init(&car, 0.0, 0.0, 0.0);
        car.gear = PHYSICS_GEAR_MAX;
        for (i = 0; i < 600; i++) physics_car_integrate(&car, 1.0, 0.0, 0.0, 0, 0.0, 0.016);
        on_track_top = car.speed;

        physics_car_init(&car, 0.0, 0.0, 0.0);
        car.gear = PHYSICS_GEAR_MAX;
        for (i = 0; i < 600; i++) physics_car_integrate(&car, 1.0, 0.0, 0.0, 1, 0.0, 0.016);
        off_track_top = car.speed;

        expect_true(off_track_top < on_track_top * 0.75,
                    "sustained full throttle off-track tops out well below the on-track top speed");

        physics_car_init(&car, 0.0, 0.0, 0.0);
        car.gear = PHYSICS_GEAR_MAX;
        car.speed = on_track_top; /* already at full on-track pace */
        speed_before = car.speed;
        physics_car_integrate(&car, 0.0, 0.0, 0.0, 1, 0.0, 0.1);
        expect_true(car.speed < speed_before,
                    "driving onto the track edge at speed bleeds speed down toward the off-track cap, not just caps future accel");
    }

    /* 7. Wall-probe lateral gradient (round 17): physics_wall_probe_lateral_gradient
     * ports func_80033584's CONFIRMED (front_left-front_right)+(rear_left-
     * rear_right) combination of the 4 CONFIRMED probe offsets
     * (D_8007306E). Checked structurally: a car centered on a wide,
     * symmetric section reports ~zero gradient; a car pushed hard toward
     * one edge (asymmetric width forces the near-side probes off-track,
     * clamping their lateral reading) reports a nonzero gradient; and
     * physics_car_integrate visibly bleeds extra speed when fed a
     * nonzero gradient versus an identical run fed zero. */
    {
        double grad;
        double speed_with_grad, speed_without_grad;

        /* Wide, symmetric section (matches this file's section-2 fixture
         * convention: heading_raw=3072 cancels the internal -0xC00
         * offset, i.e. "heading 0" / centerline along +Z). */
        sec.x = 0.0;
        sec.z = 0.0;
        sec.heading_raw = 3072;
        sec.width_right = 200.0;
        sec.width_left = 200.0;

        physics_car_init(&car, 0.0, 0.0, 0.0); /* dead center, facing along the section */
        grad = physics_wall_probe_lateral_gradient(&car, &sec);
        expect_near(grad, 0.0, 1e-6, "wall-probe gradient is ~zero for a car centered on a wide symmetric section");

        /* Narrow section, car pinned against the right edge -- the
         * front-right/rear-right probes (local +X) get clamped by the
         * tight width_right while the left-side probes still read freely,
         * so the combination should come out clearly nonzero. */
        sec.width_right = 20.0;
        sec.width_left = 200.0;
        physics_car_init(&car, 15.0, 0.0, 0.0); /* pressed toward the right edge */
        grad = physics_wall_probe_lateral_gradient(&car, &sec);
        expect_true(fabs(grad) > 1.0, "wall-probe gradient is clearly nonzero when the car is pressed against one edge");

        /* physics_car_integrate: a nonzero gradient should visibly cost
         * more speed than an otherwise-identical zero-gradient run. */
        physics_car_init(&car, 0.0, 0.0, 0.0);
        car.gear = PHYSICS_GEAR_MAX;
        car.speed = 50.0;
        physics_car_integrate(&car, 0.0, 0.0, 0.0, 0, 0.0, 0.1);
        speed_without_grad = car.speed;

        physics_car_init(&car, 0.0, 0.0, 0.0);
        car.gear = PHYSICS_GEAR_MAX;
        car.speed = 50.0;
        physics_car_integrate(&car, 0.0, 0.0, 0.0, 0, 30.0, 0.1);
        speed_with_grad = car.speed;

        expect_true(speed_with_grad < speed_without_grad,
                    "a nonzero wall-probe gradient bleeds extra speed versus an identical zero-gradient frame");
    }

    printf("%s\n", failures == 0 ? "ALL PHYSICS CHECKS PASSED" : "PHYSICS CHECKS FAILED");
    return failures == 0 ? 0 : 1;
}
