/* psx_track_bridge.c -- see psx_track_bridge.h. Extracted from round
 * 42's psx_drive_demo.c (same logic, now shared with main.c's
 * interactive mode). */
#include "psx_track_bridge.h"

#include <math.h>

static void sec_forward(const PsxBridge *b, int idx, double *fx, double *fz)
{
    int n = (int)b->td.count;
    const TrackSection *s0 = &b->td.sections[idx];
    const TrackSection *s1 = &b->td.sections[(idx + 1) % n];
    double dx = s1->x - s0->x, dz = s1->z - s0->z;
    double len = sqrt(dx * dx + dz * dz);
    if (len < 1e-9) { *fx = 1; *fz = 0; return; }
    *fx = dx / len; *fz = dz / len;
}

int32_t psx_bridge_road_dir(const PsxBridge *b, int idx)
{
    double fx, fz;
    long bam;
    sec_forward(b, idx, &fx, &fz);
    bam = lround(atan2(fz, fx) / (2.0 * M_PI) * 4096.0);
    return (int32_t)(bam & PSX_BAM_MASK);
}

double psx_bridge_lat(const PsxBridge *b, int idx, double x, double z)
{
    const TrackSection *s = &b->td.sections[idx];
    double fx, fz;
    sec_forward(b, idx, &fx, &fz);
    return (x - s->x) * fz - (z - s->z) * fx;
}

void psx_bridge_seed(PsxBridge *b, int32_t x, int32_t z)
{
    int i, best = 0;
    double bd = 1e30;
    for (i = 0; i < (int)b->td.count; i++) {
        double dx = b->td.sections[i].x - x, dz = b->td.sections[i].z - z;
        double d = dx * dx + dz * dz;
        if (d < bd) { bd = d; best = i; }
    }
    b->cur = best;
}

static void walk_update(PsxBridge *b, double x, double z)
{
    int n = (int)b->td.count;
    int tries = 8;
    while (tries--) {
        int prev = (b->cur + n - 1) % n, next = (b->cur + 1) % n;
        const TrackSection *sc = &b->td.sections[b->cur];
        const TrackSection *sp = &b->td.sections[prev];
        const TrackSection *sn = &b->td.sections[next];
        double dc = (sc->x - x) * (sc->x - x) + (sc->z - z) * (sc->z - z);
        double dp = (sp->x - x) * (sp->x - x) + (sp->z - z) * (sp->z - z);
        double dn = (sn->x - x) * (sn->x - x) + (sn->z - z) * (sn->z - z);
        if (dn < dc) b->cur = next;
        else if (dp < dc) b->cur = prev;
        else break;
    }
}

static int32_t iface_ground(void *c, int32_t x, int32_t z)
{ (void)c; (void)x; (void)z; return 0; }

static int32_t iface_dir(void *c, int32_t x, int32_t z)
{
    PsxBridge *b = c;
    walk_update(b, (double)x, (double)z);
    return psx_bridge_road_dir(b, b->cur);
}

static int32_t iface_lat(void *c, int32_t x, int32_t z)
{
    PsxBridge *b = c;
    walk_update(b, (double)x, (double)z);
    return (int32_t)lround(psx_bridge_lat(b, b->cur, (double)x, (double)z));
}

static int iface_wall(void *c, int32_t x, int32_t z, int32_t nx, int32_t nz)
{
    PsxBridge *b = c;
    const TrackSection *s;
    double lat_new, lat_old, w;
    walk_update(b, (double)nx, (double)nz);
    s = &b->td.sections[b->cur];
    lat_new = psx_bridge_lat(b, b->cur, (double)nx, (double)nz);
    lat_old = psx_bridge_lat(b, b->cur, (double)x, (double)z);
    w = lat_new > 0.0 ? s->width_right : s->width_left;
    if (fabs(lat_new) <= w)
        return 0;
    /* outside: only block motion that digs DEEPER (grinding passes) */
    return fabs(lat_new) > fabs(lat_old) + 0.25;
}

void psx_bridge_iface(PsxBridge *b, PsxTrackIface *out)
{
    out->ctx = b;
    out->ground_y = iface_ground;
    out->road_dir = iface_dir;
    out->lat_offset = iface_lat;
    out->wall_blocked = iface_wall;
}

void psx_bridge_resolve(PsxBridge *b, PsxCar *car)
{
    const TrackSection *sc;
    double fx, fz, lat, w, along, cx, cz;
    walk_update(b, (double)car->pos_x, (double)car->pos_z);
    sc = &b->td.sections[b->cur];
    sec_forward(b, b->cur, &fx, &fz);
    lat = psx_bridge_lat(b, b->cur, (double)car->pos_x, (double)car->pos_z);
    w = (lat > 0.0 ? sc->width_right : sc->width_left) - 4.0;
    if (w > 4.0 && fabs(lat) > w) {
        double want = lat > 0.0 ? w : -w;
        along = ((double)car->pos_x - sc->x) * fx
              + ((double)car->pos_z - sc->z) * fz;
        cx = sc->x + fx * along + fz * want;
        cz = sc->z + fz * along - fx * want;
        car->pos_x = (int32_t)lround(cx);
        car->pos_z = (int32_t)lround(cz);
    }
}

int32_t psx_bridge_corner_target(PsxBridge *b)
{
    int n = (int)b->td.count;
    int32_t curdir = psx_bridge_road_dir(b, b->cur);
    int32_t worst = 0, k;
    for (k = 2; k <= 10; k += 2) {
        int32_t d = psx_angdiff(curdir,
                                psx_bridge_road_dir(b, (b->cur + k) % n));
        if (d < 0) d = -d;
        d = d * 12 / k;
        if (d > worst) worst = d;
    }
    {
        int32_t target = 0x800 - worst * 4;
        if (target < 0x200) target = 0x200;
        return target;
    }
}
