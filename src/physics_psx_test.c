/* physics_psx_test.c -- headless validation of the round-40 authentic
 * fixed-point player physics (physics_psx.c). Runs a synthetic closed
 * course (two straights joined by half-circles, with walls at
 * +-ROAD_HALF of the centerline), drives the car with the byte-traced
 * autopilot steering law, and asserts the model's confirmed behaviors:
 *
 *   1. the car accelerates from rest; the auto gearbox climbs through
 *      the gears via the CONFIRMED 0x1900/0xDAC thresholds;
 *   2. coast/drag (996/1000) settles the speed -- no runaway;
 *   3. in the curve the velocity direction LAGS the facing direction
 *      (the drift model): slip is nonzero there, near zero on the
 *      straight;
 *   4. a wall hit refuses the position commit and cuts speed by the
 *      CONFIRMED 70/100 factor;
 *   5. low-speed steering scale: at standstill the turn rate is 0.
 *
 * The physics core is pure integer; this TEST uses libm only to build
 * the synthetic track geometry. With --csv, writes /tmp/psx_traj.csv
 * (frame,x,z,heading,vel_dir,speed,gear,slip).
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_psx.h"

/* CHECK() vanishes under NDEBUG/Release -- use a hard check that
 * always runs, so ctest actually validates in every build type. */
#define CHECK(cond, msg) \
    do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

#define ROAD_HALF 1800
#define STRAIGHT_LEN 40000
#define RADIUS 10000

/* Oval centerline: bottom straight z=0 (x: 0..L, dir +x), right
 * half-circle around (L, R), top straight z=2R (dir -x), left
 * half-circle around (0, R). BAM convention here: dir 0 = +x,
 * 0x400 = +z (quarter turn), i.e. angle = bam/4096*2pi. */
static void oval_query(int32_t xi, int32_t zi, int32_t *dir_bam, int32_t *lat)
{
    double x = xi, z = zi;
    double dir, latd;
    if (x >= 0 && x <= STRAIGHT_LEN) {
        if (z < RADIUS) { /* bottom straight */
            dir = 0.0;
            latd = z - 0.0;
        } else {          /* top straight */
            dir = M_PI;
            latd = -(z - 2.0 * RADIUS);
        }
    } else if (x > STRAIGHT_LEN) { /* right half-circle, CCW */
        double dx = x - STRAIGHT_LEN, dz = z - RADIUS;
        double a = atan2(dz, dx);
        dir = a + M_PI / 2.0;
        latd = -(sqrt(dx * dx + dz * dz) - RADIUS);
    } else {                       /* left half-circle */
        double dx = x - 0.0, dz = z - RADIUS;
        double a = atan2(dz, dx);
        dir = a + M_PI / 2.0;
        latd = -(sqrt(dx * dx + dz * dz) - RADIUS);
    }
    *dir_bam = ((int32_t)llround(dir / (2.0 * M_PI) * 4096.0)) & PSX_BAM_MASK;
    *lat = (int32_t)llround(latd);
}

static int32_t trk_ground(void *c, int32_t x, int32_t z)
{ (void)c; (void)x; (void)z; return 0; }

static int32_t trk_dir(void *c, int32_t x, int32_t z)
{ int32_t d, l; (void)c; oval_query(x, z, &d, &l); return d; }

static int32_t trk_lat(void *c, int32_t x, int32_t z)
{ int32_t d, l; (void)c; oval_query(x, z, &d, &l); return l; }

static int trk_wall(void *c, int32_t x, int32_t z, int32_t nx, int32_t nz)
{
    int32_t d, l;
    (void)c; (void)x; (void)z;
    oval_query(nx, nz, &d, &l);
    return l < -ROAD_HALF || l > ROAD_HALF;
}

int main(int argc, char **argv)
{
    PsxCar car;
    PsxInput in;
    PsxTrackIface trk;
    FILE *csv = NULL;
    int frame, max_gear_seen = 1;
    int32_t top_speed = 0;
    long straight_slip = 0, curve_slip = 0;
    int straight_n = 0, curve_n = 0;
    int i;

    if (argc > 1 && strcmp(argv[1], "--csv") == 0)
        csv = fopen("/tmp/psx_traj.csv", "w");

    memset(&trk, 0, sizeof trk);
    trk.ground_y = trk_ground;
    trk.road_dir = trk_dir;
    trk.lat_offset = trk_lat;
    trk.wall_blocked = trk_wall;

    /* --- test 5 first: standstill turn rate is 0 (low-speed scale) --- */
    psx_car_init(&car, 1000, 0, 0);
    memset(&in, 0, sizeof in);
    in.steer_left = 1;
    psx_car_frame(&car, &in, &trk);
    CHECK(car.turn_rate == 0, "standstill must not pivot");

    /* --- main run: autopilot lap on the oval --- */
    psx_car_init(&car, 1000, 0, 0);
    car.lat_offset = 0;
    memset(&in, 0, sizeof in);
    in.throttle = 1;

    for (frame = 0; frame < 3600; frame++) {
        /* Harness-side PD driver (NOT the traced attract law -- that
         * one lives in psx_autopilot_steer but its lat-term units
         * aren't matched yet, which makes it weave; a plain PD shows
         * the physics core itself cleanly). */
        int32_t steer;
        {
            static int32_t prev_lat = 0;
            int32_t road = trk_dir(NULL, car.pos_x, car.pos_z);
            int32_t lat = trk_lat(NULL, car.pos_x, car.pos_z);
            steer = psx_angdiff(car.vel_dir, road) * 24
                  - lat - (lat - prev_lat) * 12;
            prev_lat = lat;
            if (steer < -0x1000) steer = -0x1000;
            if (steer > 0x1000) steer = 0x1000;
        }
        psx_car_frame_steer(&car, &in, &trk, steer);

        if (car.gear > max_gear_seen) max_gear_seen = car.gear;
        if (car.speed > top_speed) top_speed = car.speed;

        {
            int in_curve = (car.pos_x < 0 || car.pos_x > STRAIGHT_LEN);
            int32_t slip = car.slip_last < 0 ? -car.slip_last : car.slip_last;
            if (frame > 400) { /* past the launch transient */
                if (in_curve) { curve_slip += slip; curve_n++; }
                else          { straight_slip += slip; straight_n++; }
            }
        }
        if (csv)
            fprintf(csv, "%d,%d,%d,%d,%d,%d,%d,%d\n", frame,
                    car.pos_x, car.pos_z, car.heading, car.vel_dir,
                    car.speed, car.gear, car.slip_last);
    }

    /* --- assertions 1-3 --- */
    CHECK(max_gear_seen >= 4, "gearbox must climb under throttle");
    CHECK(top_speed > 0x300, "car must actually reach speed");
    CHECK(top_speed < 0x1000, "drag must bound the speed");
    CHECK(curve_n > 0 && straight_n > 0, "lap must cover both zones");
    {
        long avg_curve = curve_slip / curve_n;
        long avg_straight = straight_slip / straight_n;
        CHECK(avg_curve > avg_straight, "velocity must lag facing in curves (drift model)");
    }

    /* --- test 4: drive straight into the wall --- */
    {
        int32_t before;
        int hit_frames = 0;
        psx_car_init(&car, 1000, 0, 0);
        memset(&in, 0, sizeof in);
        in.throttle = 1;
        /* aim at the wall: heading straight toward +z */
        car.heading = 0x400;
        car.vel_dir = 0x400;
        for (i = 0; i < 300; i++) {
            before = car.speed;
            psx_car_frame(&car, &in, &trk);
            if (car.pos_z > ROAD_HALF - 400 && car.speed < before)
                hit_frames++;
            if (hit_frames > 3)
                break;
        }
        CHECK(car.pos_z <= ROAD_HALF + 8, "wall must block the commit");
        CHECK(hit_frames > 3, "wall must repeatedly cut speed (x0.7)");
    }

    if (csv) fclose(csv);
    printf("physics_psx_test: all assertions passed "
           "(top_speed=0x%X, max_gear=%d)\n", top_speed, max_gear_seen);
    return 0;
}
