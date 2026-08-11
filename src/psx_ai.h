/* psx_ai.h -- ROUND 53: the opponent-car AI, ported from the game's
 * AI master loop func_80025268 (12 car slots, stride 0x114, base
 * D_801E9250) and its traced callees. Provenance ledger:
 *
 * CONFIRMED (instruction-level trace, round 53):
 *   - func_80025268: 12 AI slots iterated per frame; slot state at
 *     +0xA2 (1 = active); a car with state -2 is published to
 *     D_801D35E0 (the "rival" pointer). Wheel rotation (+0x38) for AI
 *     cars is wheel += speed (&0xFFF), blur bit 0x1000 set when
 *     speed >= 0x321 -- same threshold as the player, simpler ramp.
 *     External impulse velocity (+0x48/+0x4C/+0x50) decays x3/4 per
 *     frame and integrates >>8 (y >>11), plus a constant -12/frame
 *     pull toward the ground. Lap counting: track progress is kept as
 *     (section<<8 | frac); wrapping past the section count increments
 *     the lap counter (+0x9C).
 *   - func_80020524: selects the section table (D_8005A44C course A,
 *     D_8005CC4C course B, D_8005B84C attract) and publishes the
 *     SECTION COUNT to D_801E90E0 (0x100 or 0x170) -- which finally
 *     explains the ((k<<16)+p)/(k<<8) division pattern seen all over:
 *     it wraps a (section<<8|frac) progress scalar around the course.
 *   - func_800177B8: circular BAM12 lerp -- blend(a, b, w) =
 *     (a*(256-w) + b*w) >> 8 with +-0x800 wraparound fixup. The AI's
 *     target heading is 0x800 - blend(road_dir[sec], road_dir[sec+1],
 *     frac) (func_80023B2C): it aims along the road, interpolated
 *     smoothly across section boundaries by its own progress fraction.
 *   - func_80023FF8: the lean/roll controller. d = angle difference
 *     between the stored near/far road directions; inside the +-0x50
 *     deadband the roll target (+0x7C) steps +-2 back toward 0; on a
 *     curve it steps +-2 toward the bounds +0x1E / -0xE. (+0x28, the
 *     value it steps from, is the same field the AI renderer
 *     func_8002128C feeds to its Z-rotation matrix -- so this is the
 *     visible corner LEAN of opponent cars.)
 *   - func_80025268 cruise branch: target speed = per-section limit
 *     (+0xF8) * 8/10, approached by adding the per-car accel (+0x108)
 *     each frame while below it.
 *   - func_80023C58 (traced ROUND 54): the rubber band. A progress
 *     window [D_8012CD80, D_8007C510] is kept around the player;
 *     crossing a bound enters catch-up state (+0xA2 = 4) with target
 *     progress = 9/10 of that bound (stored +0xB0), and a LEVEL
 *     (+0x60) slews +-1/frame toward +-3 across +-5-section distance
 *     bands (0 exactly on target). The rival car (+0xAC == 1) forces
 *     +-5 beyond fixed sections (0x3D normal / 0x47 mirror).
 *   - func_800181C8 (traced ROUND 54): NOT a wall resolver -- it is
 *     the canonical track->world converter: (progress, lateral) ->
 *     x = D_801733A0 - lerp(sec.x>>14), y = -lerp(sec.aux_a)/2,
 *     z = lerp(sec.z>>14), road angle = 0x800 - circular lerp of
 *     heading_raw, banking = lerp(aux_heading_raw << 3); then
 *     x += cos(angle)*lat >> 11, z -= sin(angle)*lat >> 11. This
 *     RESOLVES two long-open trackdata fields: aux_a_raw is the
 *     SECTION HEIGHT (y = -aux_a/2, physics frame) and
 *     aux_heading_raw is the BANKING angle (x8).
 *
 * APPROXIMATED (marked in psx_ai.c where used):
 *   - the per-section speed limit source (we derive it from curvature
 *     via psx_bridge_corner_target_at; the game reads a stored +0xF8);
 *   - the rubber-band level->speed mapping (the per-state handlers
 *     that consume +0x60 are untraced; ~5%/level in psx_ai.c);
 *   - per-slot accel values and lane offsets (the game seeds these
 *     from race setup data not yet located).
 *
 * ALSO ROUND 53 -- the per-model PIECE KIT table D_80059228 (13
 * entries x 8 int16, extracted from the user's own PSX.EXE at load,
 * never committed): {axle_obj, body_obj, lod_obj, aux_obj, axle_dz,
 * axle_dz_model, f6, -32767}. Confirmed against the AI renderer
 * func_8002128C: body = OBJ.RRO object [+0x2]; below LOD distance the
 * axle object [+0x0] is drawn twice, once at the car origin and once
 * offset {0,0,[+0x8]} (world units, x4 into model units by the GTE
 * translation shift -- see func_800129AC note in main.c); beyond
 * Manhattan camera distance 0xD00 only the LOD object [+0x4] is
 * drawn, and past 0x2500 the car is culled entirely. */
#ifndef RR_PSX_AI_H
#define RR_PSX_AI_H

#include <stdint.h>
#include <stddef.h>

#include "psx_track_bridge.h"

#define PSX_AI_SLOTS 11          /* opponents (game loops 12 incl. rival) */
#define PSX_AI_KIT_MODELS 13

typedef struct {
    int16_t axle_obj;      /* +0x0: OBJ.RRO axle object (36/37/38) */
    int16_t body_obj;      /* +0x2: body object */
    int16_t lod_obj;       /* +0x4: far-LOD object (24..35) */
    int16_t aux_obj;       /* +0x6: aux family base (spoiler/shadow) */
    int16_t axle_dz;       /* +0x8: rear-axle z offset, world units */
    int16_t axle_dz_model; /* +0xA: same in model units (~4x +0x8) */
    int16_t f6;            /* +0xC: open (monotone per model) */
    int16_t sentinel;      /* +0xE: -32767 */
} PsxCarKit;

typedef struct {
    int active;
    int model;            /* 0..12 */
    double progress;      /* section + fraction (game: (sec<<8|frac)) */
    int32_t speed;        /* same scale as PsxCar.speed */
    int32_t accel;        /* per-frame accel (+0x108) */
    int32_t limit;        /* real cruise limit (player units), 0 = none */
    int32_t heading;      /* BAM12 (renderer convention) */
    int32_t roll;         /* +0x28 lean, BAM12 */
    int32_t rubber;    /* +0x60 catch-up level -3..+3 (func_80023C58) */
    int32_t wheel;        /* +0x38 (bit 0x1000 = blur) */
    double lane;          /* lateral lane offset, world units (approx) */
    double x, y, z;       /* world pose (y = caller-resolved ground) */
    int lap;
} PsxAiCar;

extern PsxCarKit psx_ai_kit[PSX_AI_KIT_MODELS];
extern int psx_ai_kit_loaded;

/* ROUND 55: the REAL race setup, extracted from the EXE at load
 * (traced in func_80021048 -> func_800206CC / func_80020E4C /
 * func_80020B88):
 *   - D_80073130: grid roster, 12 x int32 model ids
 *     {2,3,1,11,10,6,7,4,8,5,9,-1} (-1 = slot off; the mode-4 variant
 *     D_80073160 fields only model 2 plus the SECRET car 12 as rival);
 *   - D_800731C0: 12 x 16-byte grid entries -- x, z, y-adjust, and
 *     START PROGRESS (section<<8): slots 0-4 grid with the player at
 *     section 68, slots 5-10 are strung out at sections 144..255 (the
 *     rolling field you chase down -- authentic RR1 race design!);
 *   - D_80073560: 12 x 24-byte pace entries; +0x4 * 8 = per-car
 *     cruise limit in AI units (func_80020B88 stores it to +0xF8 with
 *     +-4..5%% per-slot variation). AI speed units are 1/8 of the
 *     player's (the AI integrator shifts >>11 where the player's
 *     shifts >>8), so player-units limit = field * 64.
 * This CLOSES the "per-section limit" approximation: the game gives
 * each opponent ONE cruise pace; corners are handled by the rail. */
typedef struct {
    int model;        /* roster model id, -1 = slot unused */
    double start_rel; /* start progress relative to the player, sections */
    int32_t limit;    /* cruise limit, player speed units (field*64) */
    int32_t accel;    /* pace entry +0xC (approx mapping to units) */
    double grid_x, grid_z; /* REAL grid pose, physics frame (round 56:
        x_phys = 0xF000 - x_mesh, D_801733A0 traced to func_80015CD4) */
} PsxAiSlotSetup;
extern PsxAiSlotSetup psx_ai_setup[12];
extern int psx_ai_setup_loaded;
int psx_ai_race_from_exe(const uint8_t *exe, size_t size);

/* Extracts D_80059228 from a raw PSX.EXE buffer (RAM 0x80059228 ->
 * file 0x49A28 for the retail EXE mapped at 0x80010000 with a 0x800
 * header). Returns 1 and sets psx_ai_kit_loaded on success. */
int psx_ai_kit_from_exe(const uint8_t *exe, size_t size);

/* Circular BAM12 blend, exact port of func_800177B8 (w in 0..256). */
int32_t psx_ai_blend_angle(int32_t a, int32_t b, int32_t w);

void psx_ai_init(PsxAiCar *c, int slot, const PsxBridge *b);

/* One 30Hz step. player_progress in section units (for the
 * rubber-band window). Updates pose from the bridge's centerline. */
void psx_ai_frame(PsxAiCar *c, PsxBridge *b, double player_progress);

#endif /* RR_PSX_AI_H */
