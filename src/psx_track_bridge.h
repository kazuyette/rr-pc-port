/* psx_track_bridge.h -- ROUND 43: shared bridge between the authentic
 * fixed-point physics core (physics_psx.h) and the real course
 * geometry (tools/trackdata's TrackData, decoded from the user's own
 * PSX.EXE). Extracted from round 42's psx_drive_demo.c so the
 * interactive mode in main.c and the headless lap test drive through
 * the SAME code.
 *
 * Design notes (round-42 lessons, see ROADMAP):
 *   - road direction and lateral offset are computed from SECTION
 *     GEOMETRY (center -> next center), not from heading_raw, whose
 *     orientation convention on real data diverges per-section from
 *     the synthetic-test reading (byte-matching it is a future task);
 *   - the wall test blocks only motion that goes DEEPER into a wall
 *     (grinding along it passes) -- plus psx_bridge_resolve() projects
 *     the car back inside the width after each frame, standing in for
 *     the still-untraced func_800181C8 resolver; without it, integer
 *     rounding can wedge the car inside a wall and the authentic
 *     low-speed steering scale turns that into a death spiral. */
#ifndef RR_PSX_TRACK_BRIDGE_H
#define RR_PSX_TRACK_BRIDGE_H

#include "physics_psx.h"
#include "trackdata.h"

typedef struct {
    TrackData td;  /* owned by the caller (or moved in) */
    int cur;       /* current section index (local-walk state) */
} PsxBridge;

/* Points the iface at `b` (which must outlive it). `b->td` must be
 * populated and `b->cur` seeded (psx_bridge_seed does a full scan). */
void psx_bridge_iface(PsxBridge *b, PsxTrackIface *out);

/* Full O(n) nearest-section scan to (re)seed the local walk. */
void psx_bridge_seed(PsxBridge *b, int32_t x, int32_t z);

/* Road direction (core BAM frame) at section `idx`. */
int32_t psx_bridge_road_dir(const PsxBridge *b, int idx);

/* Signed lateral offset of (x,z) from section idx's centerline. */
double psx_bridge_lat(const PsxBridge *b, int idx, double x, double z);

/* Boundary resolver: if the car ended the frame outside the track
 * width, project it back onto the edge (stand-in for func_800181C8).
 * Call once per frame after psx_car_frame*. */
void psx_bridge_resolve(PsxBridge *b, PsxCar *car);

/* Curvature-lookahead brake helper (what round 42's driver uses):
 * returns a target speed for the current position, from the worst
 * upcoming direction change over the next ~10 sections. */
int32_t psx_bridge_corner_target(PsxBridge *b);

#endif /* RR_PSX_TRACK_BRIDGE_H */
