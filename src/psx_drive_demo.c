/* psx_drive_demo.c -- ROUND 42/43: the authentic fixed-point physics
 * core (physics_psx.c, rounds 40-41) driving on the REAL Ridge Racer
 * course, decoded from the user's own PSX.EXE (course-A section table
 * at 0x8005A44C, 256 sections). Round 43 moved the track bridge into
 * src/psx_track_bridge.{c,h}, shared with main.c's interactive mode --
 * this headless lap test and the playable mode drive through the SAME
 * code.
 *
 * Run:  rr_psx_drive <path/to/PSX.EXE> [--csv out.csv] [--frames N]
 * As a ctest it SKIPS (exit 0) when RR_EXE_FILE is unset, like the
 * texdemo test -- no game data is committed to this repo, ever.
 *
 * Assertions when it does run:
 *   - the car completes at least one full lap of the real course;
 *   - it stays inside the real track widths (with a small margin) for
 *     the overwhelming majority of frames after the launch transient;
 *   - speed reaches race pace (> 0x300) and the gearbox climbs.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "physics_psx.h"
#include "psx_track_bridge.h"

#define CHECK(cond, msg) \
    do { if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); return 1; } } while (0)

int main(int argc, char **argv)
{
    const char *exe_path = NULL, *csv_path = NULL;
    int frames = 16000, i;
    FILE *f, *csv = NULL;
    long sz;
    uint8_t *buf;
    PsxBridge br;
    PsxTrackIface trk;
    PsxCar car;
    PsxInput in;
    int laps = 0, prev_sec, off_frames = 0, on_frames = 0;
    int max_gear = 1;
    int32_t top_speed = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) csv_path = argv[++i];
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) frames = atoi(argv[++i]);
        else exe_path = argv[i];
    }
    if (!exe_path) exe_path = getenv("RR_EXE_FILE");
    if (!exe_path) {
        printf("rr_psx_drive: RR_EXE_FILE not set and no path given -- SKIP\n");
        return 0;
    }
    f = fopen(exe_path, "rb");
    if (!f) {
        printf("rr_psx_drive: cannot open %s -- SKIP\n", exe_path);
        return 0;
    }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)sz);
    if (!buf || fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f); free(buf);
        fprintf(stderr, "read failed\n");
        return 1;
    }
    fclose(f);

    memset(&br, 0, sizeof br);
    CHECK(trackdata_parse(buf, (size_t)sz, TRACKDATA_COURSE_A_RAM_ADDR,
                          TRACKDATA_COURSE_A_COUNT, &br.td) == TRACKDATA_OK,
          "trackdata_parse must succeed on a real PSX.EXE");
    free(buf);

    psx_bridge_iface(&br, &trk);

    /* spawn at section 0's center, facing down the road */
    {
        const TrackSection *s0 = &br.td.sections[0];
        br.cur = 0;
        psx_car_init(&car, (int32_t)lround(s0->x), (int32_t)lround(s0->z),
                     psx_bridge_road_dir(&br, 0));
    }
    psx_bridge_seed(&br, car.pos_x, car.pos_z);

    if (csv_path) csv = fopen(csv_path, "w");
    memset(&in, 0, sizeof in);
    in.throttle = 1;
    prev_sec = br.cur;

    for (i = 0; i < frames; i++) {
        int32_t steer;
        {
            static int32_t prev_lat = 0;
            int32_t road = psx_bridge_road_dir(&br, br.cur);
            int32_t lat = (int32_t)lround(
                psx_bridge_lat(&br, br.cur, (double)car.pos_x,
                               (double)car.pos_z));
            steer = psx_angdiff(car.vel_dir, road) * 24
                  - lat * 8 - (lat - prev_lat) * 40;
            prev_lat = lat;
            if (steer < -0x1000) steer = -0x1000;
            if (steer > 0x1000) steer = 0x1000;
        }
        /* corner speed management (round 42): brake toward a
         * curvature-scaled target, like a player does. */
        {
            int32_t target = psx_bridge_corner_target(&br);
            in.throttle = car.speed < target;
            in.brake = car.speed > target + 0x80;
        }
        psx_car_frame_steer(&car, &in, &trk, steer);
        psx_bridge_resolve(&br, &car);

        if (car.gear > max_gear) max_gear = car.gear;
        if (car.speed > top_speed) top_speed = car.speed;
        if (prev_sec > (int)br.td.count - 8 && br.cur < 8)
            laps++;
        prev_sec = br.cur;
        if (i > 600) {
            const TrackSection *s = &br.td.sections[br.cur];
            double lat = psx_bridge_lat(&br, br.cur, (double)car.pos_x,
                                        (double)car.pos_z);
            double w = lat > 0.0 ? s->width_right : s->width_left;
            if (fabs(lat) > w * 1.1) off_frames++; else on_frames++;
        }
        if (csv)
            fprintf(csv, "%d,%d,%d,%d,%d,%d,%d,%d,%d\n", i,
                    car.pos_x, car.pos_z, car.heading, car.vel_dir,
                    car.speed, car.gear, car.slip_last, br.cur);
    }
    if (csv) fclose(csv);

    printf("rr_psx_drive: %d frames, laps=%d, top_speed=0x%X, max_gear=%d, "
           "off-track frames=%d/%d\n",
           frames, laps, top_speed, max_gear, off_frames, off_frames + on_frames);

    CHECK(laps >= 1, "must complete at least one lap of the real course");
    CHECK(top_speed > 0x300, "must reach race pace");
    CHECK(max_gear >= 4, "gearbox must climb");
    CHECK(off_frames * 20 < on_frames, "must stay on the real track ( >95% )");

    trackdata_free(&br.td);
    printf("rr_psx_drive: all assertions passed on the REAL course\n");
    return 0;
}
