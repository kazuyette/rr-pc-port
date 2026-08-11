/* physics_psx.h -- the AUTHENTIC fixed-point player-car physics cycle,
 * ported from Ridge Racer 1 (PS1)'s func_8001C490 frame function and
 * its callees, traced instruction-by-instruction in round 40 (see
 * physics_psx.c's header for the full provenance map and the
 * CONFIRMED vs APPROXIMATED ledger).
 *
 * This is the integer BAM12/fixed-point core the original runs at
 * 30fps -- distinct from physics.h's earlier floating-point
 * approximation layer (kept for the existing demo/tests). The two
 * coexist; this one is the real model:
 *
 *   THE RIDGE RACER DRIFT MODEL (round 40's central discovery):
 *   - car->vel_dir (orig car+0x24) is the VELOCITY DIRECTION, a BAM12
 *     angle that chases the car's facing direction with a /5 one-pole
 *     filter each frame: vel_dir += angdiff(vel_dir, heading)/5.
 *   - the velocity vector is then PROJECTED onto vel_dir: the lateral
 *     component is computed (that's the SLIP), discarded from the
 *     velocity, and its magnitude drives drift scoring and the
 *     spin-out trigger. Forward component is kept.
 *   - so "drift" in Ridge Racer is exactly: your velocity direction
 *     lagging your steering by a smoothed 5-frame constant, with grip
 *     re-projecting every frame. Elegant and cheap -- pure 1994 Namco.
 */
#ifndef RR_PHYSICS_PSX_H
#define RR_PHYSICS_PSX_H

#include <stdint.h>

/* One BAM12 turn = 0x1000. Q12 trig (sin/cos scaled 4096). */
#define PSX_BAM_MASK 0xFFF

typedef struct {
    /* Field names map to original car-struct offsets -- see the
     * provenance table in physics_psx.c. Original comments give the
     * offset each field mirrors. */
    int32_t pos_x, pos_y, pos_z;   /* +0x10/+0x14/+0x18 world position */
    int32_t track_pos;             /* +0x8   position along track (mod'd) */
    int32_t lat_offset;            /* +0xC   signed offset from centerline */
    int32_t vel_dir;               /* +0x24  velocity direction (BAM12) */
    int32_t turn_rate;             /* +0x28  committed turn rate (D_8012CD98) */
    int32_t vert_vel_ext;          /* +0x2C  slope/vertical term fed to engine */
    int32_t wheel_rot;             /* +0x38  wheel rotation angle (BAM12)
                                             + bit 0x1000 = blur flag */
    int32_t ground_y;              /* +0x40  terrain height under car */
    int32_t drive_state;           /* +0x5C  0..3 grip/slide mode */
    int32_t vel_x, vel_z;          /* +0x60/+0x68 velocity, position<<8 per frame */
    int32_t steer;                 /* +0x74  wheel position -0x1000..0x1000 */
    int16_t gear;                  /* +0x82  1..6 */
    int32_t rpm;                   /* +0x84  accel accumulator (gearbox input) */
    int32_t heading;               /* +0xAC  facing direction (BAM12) --
                                             vel_dir chases this */
    int32_t speed;                 /* +0xA0  velocity magnitude (polar) */
    int32_t resp_scale;            /* +0xA4  response/decay term -- ITS WRITE
                                             SITE WAS FOUND THIS ROUND (see .c) */
    int32_t vel_ang;               /* +0xA8  atan2 of velocity (polar) */
    int32_t aux_speed;             /* +0xB0  secondary speed scalar (x0.8 on wall) */
    int32_t wall_scrape;           /* ROUND 66 (port-side): 1 while this frame's
                                      predicted position hit a wall -- the
                                      original fires its scrape/impact SFX from
                                      the same branch (see integrate()) */
    int32_t spin_state;            /* +0xB4  0 normal; 1 spin-out; 3 blocks gearbox */
    int32_t airborne;              /* +0xB8  nonzero while in the air */
    int32_t air_timer;             /* +0xBA  frames airborne */
    int32_t vert_vel;              /* +0xBC  vertical velocity (gravity 0xC/frame) */
    int32_t slip_last;             /* last frame's discarded lateral component
                                      (the drift amount; original keeps it in
                                      a temp + scoring globals) */
    int32_t manual;                /* nonzero = manual transmission */
    int32_t model;                 /* car model 0..12 (indexes the real
                                      per-model stats table; 12 = the
                                      secret #13) */
    int32_t spin_threshold;        /* +0x94 = stat2*14 (CONFIRMED init) */
} PsxCar;

/* Per-frame driver input. */
typedef struct {
    int throttle;    /* 0/1 (digital, as the original's pad bit) */
    int brake;       /* 0/1 */
    int steer_left;  /* 0/1 */
    int steer_right; /* 0/1 */
    int shift_up;    /* fresh press this frame */
    int shift_down;  /* fresh press this frame */
} PsxInput;

/* Track query callback: the physics core asks the embedding for ground
 * height, road direction and wall test at a position, so it stays
 * independent of the track-data representation. Return nonzero from
 * wall_blocked if the segment position->new position crosses a wall. */
typedef struct {
    void *ctx;
    int32_t (*ground_y)(void *ctx, int32_t x, int32_t z);
    /* BAM12 direction of the road at this position (for the autopilot
     * steering law and slip-vs-road scoring). */
    int32_t (*road_dir)(void *ctx, int32_t x, int32_t z);
    /* signed lateral offset from the road centerline (autopilot term). */
    int32_t (*lat_offset)(void *ctx, int32_t x, int32_t z);
    int (*wall_blocked)(void *ctx, int32_t x, int32_t z,
                        int32_t nx, int32_t nz);
} PsxTrackIface;

/* Q12 BAM12 trig, table-based like the original's func_80044D0C /
 * func_80044E2C (values regenerated mathematically -- identical
 * convention, no game data). psx_cos(0)=0x1000. */
int32_t psx_sin(int32_t bam);
int32_t psx_cos(int32_t bam);
/* Shortest signed BAM12 angle difference (port of func_80019CA8,
 * byte-traced round 34). */
int32_t psx_angdiff(int32_t from, int32_t to);

void psx_car_init(PsxCar *car, int32_t x, int32_t z, int32_t heading_bam);
/* Select the car model (0..12); reloads the CONFIRMED per-model derived
 * fields (spin threshold = stat2*14). */
void psx_car_set_model(PsxCar *car, int model);

/* One 30fps frame of the authentic player cycle (port of
 * func_8001C490 + callees; see .c for what each stage mirrors). */
void psx_car_frame(PsxCar *car, const PsxInput *in, const PsxTrackIface *trk);

/* Same frame with the steering wheel value forced (autopilot mode). */
void psx_car_frame_steer(PsxCar *car, const PsxInput *in,
                         const PsxTrackIface *trk, int32_t steer_value);

/* The original's ATTRACT-MODE AUTOPILOT steering law, byte-traced this
 * round from func_8001C0E4's D_801D9060==4 branch: steer =
 * clamp(angdiff(vel_dir, road_dir)<<5 + centerline pull, +-0x1000).
 * Fills in->steer_* equivalent by directly returning the wheel value
 * to feed psx_car_frame via car->steer override. */
int32_t psx_autopilot_steer(const PsxCar *car, const PsxTrackIface *trk);

#endif /* RR_PHYSICS_PSX_H */
