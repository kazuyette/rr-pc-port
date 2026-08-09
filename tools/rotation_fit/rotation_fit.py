#!/usr/bin/env python3
"""rotation_fit.py -- Ridge Racer 1 (PS1) MAP.RRM/IDX.HED empirical
per-section rotation investigation (Phase 5, "round 7b").

STANDALONE research tool (Python, not part of the CMake build -- the
rest of this repo's tools/mapparse/ parsers are C; this one is Python
because it leans on numpy for least-squares/vectorised search and is
explicitly exploratory, not production parsing code). Requires the
user's own legally-owned MAP.RRM/IDX.HED (never shipped with this
repo):

    python3 rotation_fit.py <path/to/MAP.RRM> <path/to/IDX.HED> [--figure out.png]

============================================================================
WHY THIS EXISTS / WHAT IT DOES
============================================================================
Six prior rounds of static binary analysis (Ghidra + rr-decomp asm
reading) confirmed the PS1 game DOES apply a real per-section rotation
to MAP.RRM road geometry at render time (real GTE `rtps`/`rtpt`
instructions with a loaded rotation matrix), but never located the code
that BUILDS that matrix from section data -- it's reached only through a
runtime function-pointer dispatch (`jalr`) that is invisible to static
analysis. See project history / tools/mapparse/idx_hed.h for the full
writeup.

This tool takes a different approach for round 7b: instead of finding
the game's exact mechanism, it tries to derive a WORKING per-section
rotation empirically, by least-squares fitting rotations that make
IDX.HED-grid-adjacent MAP.RRM sections line up geometrically. This is
GEOMETRIC RECONSTRUCTION, not decoding -- if it had worked, the result
would NOT be "what the game's code computes", just "a rotation that
happens to produce a closed, non-tangled track", which would still be
useful for a PC port. This distinction matters and is called out
explicitly in all output.

============================================================================
RESULT: NEGATIVE, BUT INFORMATIVE (see project memory rr_pc_port_round7b.md
for full writeup; summary below is reproduced in this script's own
report output too, so it travels with the tool)
============================================================================
Two independent methodologies were tried:

  1. CHAINED fit: propagate a per-section rotation theta_i around the
     258-section loop in MAP.RRM index order (which is confirmed 80%
     grid-adjacency-consistent with track order), each step solving the
     closed-form optimal-rotation-only alignment between one section's
     "exit" road-edge corners and the next section's "entry" road-edge
     corners (near/far edge correspondence discovered THIS round by
     direct inspection -- opposite of a naive guess, see
     `pick_primary_run()`/`longest_clean_segment()` below: consecutive
     type-B records in a section satisfy record[k].v0,v1 ~=
     record[k+1].v2,v3, not the other way around, and only within
     short "clean segments" a few records long -- group_id boundaries
     and periodic resets inside a single group break the naive
     "whole run is one path" assumption used by earlier rounds).
     Result: the fitted rotation for section 0, recovered by chaining
     all the way around the loop back to section 0, differs from the
     reference (0 deg) by roughly 12-18 deg depending on convention
     details -- suggestively small, EXCEPT the per-transition fit
     quality along the way is bad (median ~4500 world units corner
     mismatch, only ~18% of the 258 transitions land within one grid
     cell (2048u), far worse than the existing translation-only
     baseline's 85.9%). A small final angle from a chain of bad
     individual fits is not trustworthy on its own (see point 2).

  2. INDEPENDENT per-pair fit + CYCLE-CONSISTENCY check (the
     methodologically stronger result this round): for each of the
     ~300 IDX.HED-grid-adjacent section pairs, independently
     brute-force search (2deg coarse + 0.1deg refine, 0-360) the
     rotation of one section (holding its neighbour fixed at 0 deg)
     that minimises nearest-corner "tangle" distance between the two
     sections' full type-B point clouds. This alone looks great
     (median tangle drops from ~185u translation-only to ~6u) -- BUT a
     control test against 304 RANDOM unrelated section pairs shows
     free rotation search also "succeeds" 34-46% of the time purely by
     coincidence on large point clouds, so this metric alone is not
     trustworthy either. The decisive test: the IDX.HED grid graph
     contains 98 real 2x2 "small loops" (four sections a-b-c-d, each
     edge grid-adjacent) where all type-B geometry is present. If a
     single true per-section rotation existed, the four INDEPENDENTLY
     fit relative rotations around each loop should sum to ~0 deg. They
     do not: median closure error 65-66 deg, mean ~70 deg, across a
     roughly flat/uniform distribution from 0-180 deg (see
     round7b_report.png) -- barely better than the ~90 deg you'd expect
     from unrelated/random angles. Only 8% of loops close within 10 deg.

  CONCLUSION: a SINGLE RIGID ROTATION PER SECTION (about the section's
  local coordinate origin, combined with the existing IDX.HED grid-cell
  translation) is NOT a coherent model of this track's true geometry --
  this is a stronger and more direct negative result than round 3's
  "dead reckoning accumulates error" finding, because the cycle-closure
  test uses LOCAL, non-accumulated evidence (four independent one-hop
  fits, not a quarter of the way around a 258-section chain) and still
  fails. This rules out not just "our fitting algorithm has bugs" but
  the basic shape of the model. Also tested (both chained and
  independent-pair methodologies): NO correlation between the fitted
  rotation angle and the candidate `heading`/`unk_1e`/`group_id` record
  fields (all |Pearson r| < 0.1) -- consistent with, though not
  additional proof beyond, round 2's earlier exclusion of `heading` as
  a direct per-section rotation value.

  Given this, no track geometry was wired into the rr-pc-port
  rasterizer this round (gated on a working transform per the round's
  task description) -- see project memory for the honest next-round
  recommendation (a richer model -- per-record/continuous orientation
  rather than one angle per section, and/or a free translation offset
  rather than a literal grid-cell-corner anchor -- would need to be
  tried before another rotation-fitting attempt is likely to pay off;
  a "smarter" global least-squares/pose-graph optimisation over the
  SAME single-rotation-per-section model is not expected to help, since
  the cycle test shows the underlying per-pair constraints are already
  mutually inconsistent, not just noisily chained).
============================================================================
"""
import argparse
import math
import struct
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("rotation_fit.py requires numpy (pip install numpy)")

CELL = 2048.0
GRID_DIM = 32
CLEAN_THRESH = 150.0  # world units; used to find "clean" (well-chained) record runs

# ---------------------------------------------------------------------------
# Minimal standalone MAP.RRM / IDX.HED parsing (re-derives the CONFIRMED
# layout documented in tools/mapparse/map_rrm.h and tools/mapparse/idx_hed.h
# -- kept independent/duplicated here on purpose so this script has zero
# dependency on the C tools and can be run standalone).
# ---------------------------------------------------------------------------

RECORD_FMT = '<3h3h3h3h hHhhhHhH'
RECORD_SIZE = 40


def parse_record(buf, off):
    vals = struct.unpack_from(RECORD_FMT, buf, off)
    v0, v1, v2, v3 = vals[0:3], vals[3:6], vals[6:9], vals[9:12]
    (unk_18, heading, unk_1c, unk_1e, unk_20, group_id, unk_24, flags) = vals[12:20]
    return dict(v0=v0, v1=v1, v2=v2, v3=v3, unk_18=unk_18, heading=heading,
                unk_1c=unk_1c, unk_1e=unk_1e, unk_20=unk_20, group_id=group_id,
                unk_24=unk_24, flags=flags)


def parse_map_rrm(buf):
    n = struct.unpack_from('<H', buf, 0)[0]
    off = 4
    directory = []
    for _ in range(n):
        directory.append(struct.unpack_from('<HHHH', buf, off))
        off += 8
    sections = []
    for i in range(n):
        ca, cb, cc, cd = directory[i]
        sec = {'a': [], 'b': [], 'c': []}
        for _ in range(ca):
            sec['a'].append(parse_record(buf, off)); off += RECORD_SIZE
        for _ in range(cb):
            sec['b'].append(parse_record(buf, off)); off += RECORD_SIZE
        for _ in range(cc):
            sec['c'].append(parse_record(buf, off)); off += RECORD_SIZE
        sections.append(sec)
    return n, directory, sections, off


def parse_idx_hed(buf):
    assert len(buf) == 2048, "IDX.HED must be exactly 2048 bytes (32x32 int16 grid)"
    sec_to_cell = {}
    for row in range(32):
        for col in range(32):
            idx = row * 32 + col
            v = struct.unpack_from('<h', buf, idx * 2)[0]
            if v != -1:
                sec_to_cell[v] = (col, row)
    return sec_to_cell


def grid_origin(col, row):
    mirrored_col = (GRID_DIM - 1) - col
    return np.array([mirrored_col * CELL, row * CELL], dtype=float)


def rot(theta):
    c, s = math.cos(theta), math.sin(theta)
    return np.array([[c, -s], [s, c]])


def xz(v):
    return np.array([v[0], v[2]], dtype=float)


def section_corners_local(sec):
    pts = []
    for r in sec['b']:
        pts.append(xz(r['v0'])); pts.append(xz(r['v1']))
        pts.append(xz(r['v2'])); pts.append(xz(r['v3']))
    return pts


# ---------------------------------------------------------------------------
# Method 1: chained fit around the 258-section index-order loop.
# ---------------------------------------------------------------------------

def longest_clean_segment(run):
    if len(run) == 0:
        return None
    best = (0, 0)
    cur_start = 0
    for k in range(len(run) - 1):
        r0, r1 = run[k], run[k + 1]
        ok = r0['group_id'] == r1['group_id']
        if ok:
            d1 = np.linalg.norm(xz(r0['v0']) - xz(r1['v2']))
            d2 = np.linalg.norm(xz(r0['v1']) - xz(r1['v3']))
            ok = d1 < CLEAN_THRESH and d2 < CLEAN_THRESH
        if not ok:
            if (k - cur_start) > (best[1] - best[0]):
                best = (cur_start, k)
            cur_start = k + 1
    if (len(run) - 1 - cur_start) > (best[1] - best[0]):
        best = (cur_start, len(run) - 1)
    return best


def pick_primary_run(sec):
    if len(sec['b']) > 0:
        seg = longest_clean_segment(sec['b'])
        run = sec['b']
        first, last = run[seg[0]], run[seg[1]]
        return 'b', (xz(first['v2']), xz(first['v3'])), (xz(last['v0']), xz(last['v1'])), first, last
    for key in ('a', 'c'):
        if len(sec[key]) > 0:
            run = sec[key]
            first, last = run[0], run[-1]
            return key, (xz(first['v2']), xz(first['v3'])), (xz(last['v0']), xz(last['v1'])), first, last
    return None, None, None, None, None


def best_rotation(p_pts, q_pts):
    num = den = 0.0
    for p, q in zip(p_pts, q_pts):
        num += p[0] * q[1] - p[1] * q[0]
        den += p[0] * q[0] + p[1] * q[1]
    return math.atan2(num, den)


def residual(theta, p_pts, q_pts):
    R = rot(theta)
    return sum(((R.dot(p) - q) ** 2).sum() for p, q in zip(p_pts, q_pts))


def run_chained_fit(n, sections, origins):
    entry, exit_, entry_rec, run_type = {}, {}, {}, {}
    for i in range(n):
        rt, e, x, frec, lrec = pick_primary_run(sections[i])
        entry[i], exit_[i], entry_rec[i], run_type[i] = e, x, frec, rt

    theta = [0.0] * n
    residuals = [None] * n
    for step in range(n):
        i, j = step, (step + 1) % n
        if exit_[i] is None or entry[j] is None:
            theta[j] = theta[i]
            continue
        Ri = rot(theta[i])
        q_world = [Ri.dot(p) + origins[i] for p in exit_[i]]
        q_rel = [q - origins[j] for q in q_world]
        p_local = list(entry[j])
        th_a = best_rotation(p_local, q_rel)
        res_a = residual(th_a, p_local, q_rel)
        p_sw = [p_local[1], p_local[0]]
        th_b = best_rotation(p_sw, q_rel)
        res_b = residual(th_b, p_sw, q_rel)
        if j == 0:
            wrap_theta = th_b if res_b < res_a else th_a
            print(f'\n[chained fit] wraparound (section 257 -> 0): fitted theta_0 = '
                  f'{math.degrees(wrap_theta):.2f} deg (reference is 0.00 deg) '
                  f'=> angular closure gap {math.degrees(abs(wrap_theta)):.2f} deg')
            break
        if res_b < res_a:
            theta[j], residuals[j] = th_b, res_b
        else:
            theta[j], residuals[j] = th_a, res_a

    res_vals = [r for r in residuals if r is not None]
    res_dist = sorted(math.sqrt(r / 2.0) for r in res_vals)
    print(f'[chained fit] per-transition corner-mismatch (n={len(res_dist)}): '
          f'median={res_dist[len(res_dist)//2]:.0f}u, '
          f'mean={sum(res_dist)/len(res_dist):.0f}u, '
          f'{sum(1 for d in res_dist if d < 2048)/len(res_dist)*100:.1f}% within one grid cell (2048u)')
    return theta, entry_rec


def fit_pair(i, j, origins, local_pts):
    """Independent pairwise fit: theta_i fixed at 0, search theta_j
    minimising nearest-corner ('tangle') distance between the two
    sections' full type-B point clouds. Returns (best_theta_j, best_tangle)."""
    pi = np.array([p + origins[i] for p in local_pts[i]])
    pj_local = np.array(local_pts[j])
    oj = origins[j]
    coarse = np.radians(np.arange(0, 360, 2.0))
    best, bth = 1e18, 0.0
    for th in coarse:
        pj = pj_local.dot(rot(th).T) + oj
        m = np.sqrt(((pi[:, None, :] - pj[None, :, :]) ** 2).sum(axis=2)).min()
        if m < best:
            best, bth = m, th
    for th in np.radians(np.arange(math.degrees(bth) - 2, math.degrees(bth) + 2, 0.1)):
        pj = pj_local.dot(rot(th).T) + oj
        m = np.sqrt(((pi[:, None, :] - pj[None, :, :]) ** 2).sum(axis=2)).min()
        if m < best:
            best, bth = m, th
    return bth, best


def run_cycle_consistency(n, sections, origins, local_pts, sec_to_cell):
    cell_to_sec = {v: k for k, v in sec_to_cell.items()}
    have_b = set(i for i in range(n) if len(sections[i]['b']) > 0)
    cycles = []
    for row in range(31):
        for col in range(31):
            cells = [(col, row), (col + 1, row), (col + 1, row + 1), (col, row + 1)]
            if all(c in cell_to_sec for c in cells):
                secs = [cell_to_sec[c] for c in cells]
                if all(s in have_b for s in secs):
                    cycles.append(secs)

    loop_errs = []
    for (a, b, c, d) in cycles:
        th_ab, _ = fit_pair(a, b, origins, local_pts)
        th_bc, _ = fit_pair(b, c, origins, local_pts)
        th_cd, _ = fit_pair(c, d, origins, local_pts)
        th_da, _ = fit_pair(d, a, origins, local_pts)
        total = math.degrees(th_ab + th_bc + th_cd + th_da) % 360.0
        if total > 180:
            total -= 360
        loop_errs.append(abs(total))

    loop_errs.sort()
    n_e = len(loop_errs)
    print(f'\n[cycle consistency] {n_e} independent 2x2 grid loops tested')
    print(f'[cycle consistency] closure error (deg): median={loop_errs[n_e//2]:.1f} '
          f'mean={sum(loop_errs)/n_e:.1f} min={loop_errs[0]:.1f} max={loop_errs[-1]:.1f}')
    print(f'[cycle consistency] fraction < 10 deg: {sum(1 for e in loop_errs if e<10)/n_e*100:.1f}% '
          f'(random/spurious would be ~5.6%; a real single per-section rotation would be ~100%)')
    return loop_errs


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('map_rrm')
    ap.add_argument('idx_hed')
    ap.add_argument('--figure', help='optional path to write a PNG report figure (requires matplotlib/PIL)')
    args = ap.parse_args()

    with open(args.map_rrm, 'rb') as f:
        map_buf = f.read()
    with open(args.idx_hed, 'rb') as f:
        idx_buf = f.read()

    n, directory, sections, consumed = parse_map_rrm(map_buf)
    sec_to_cell = parse_idx_hed(idx_buf)
    print(f'parsed MAP.RRM: {n} sections, {consumed} bytes consumed (file is {len(map_buf)} bytes)')
    print(f'parsed IDX.HED: {len(sec_to_cell)} sections placed in 32x32 grid')

    origins = {i: grid_origin(*sec_to_cell[i]) for i in range(n)}
    local_pts = {i: section_corners_local(sections[i]) for i in range(n)}

    print('\n=== Method 1: chained per-section rotation fit (index-order loop) ===')
    theta, entry_rec = run_chained_fit(n, sections, origins)

    print('\n=== Method 2: independent per-pair rotation fit + cycle consistency ===')
    loop_errs = run_cycle_consistency(n, sections, origins, local_pts, sec_to_cell)

    print('\n=== Correlation: chained delta-theta vs record fields ===')

    def wrap180(a):
        while a > 180: a -= 360
        while a < -180: a += 360
        return a

    fields = ['heading', 'unk_1e', 'group_id', 'unk_18', 'unk_1c', 'unk_20', 'unk_24', 'flags']
    dtheta, field_vals = [], {f: [] for f in fields}
    for step in range(n - 1):
        i, j = step, step + 1
        if entry_rec.get(j) is None:
            continue
        dtheta.append(math.degrees(wrap180(theta[j] - theta[i])))
        for f in fields:
            v = entry_rec[j][f]
            if f == 'heading' and v >= 32768:
                v -= 65536
            field_vals[f].append(v)
    dtheta_arr = np.array(dtheta)
    for f in fields:
        arr = np.array(field_vals[f], dtype=float)
        if np.std(arr) < 1e-9 or np.std(dtheta_arr) < 1e-9:
            print(f'  {f}: constant, skipped')
            continue
        r = np.corrcoef(dtheta_arr, arr)[0, 1]
        print(f'  {f}: pearson r = {r:.4f}  {"<-- weak/no correlation" if abs(r) < 0.15 else "*** POSSIBLE SIGNAL ***"}')

    print('\n=== SUMMARY (see this file\'s module docstring for full detail) ===')
    print('A single rigid per-section rotation (about local origin, IDX.HED grid-cell')
    print('translation) does NOT produce a globally-consistent track: independent 4-cycle')
    print('rotation sums fail to close (median ~65 deg error vs ~90 deg for pure noise).')
    print('No record field correlates with the fitted rotation (all |r| < 0.15 above).')
    print('This is an empirical/geometric negative result, not a decode of the game\'s')
    print('actual mechanism -- see project memory for next-round recommendations.')

    if args.figure:
        try:
            import matplotlib
            matplotlib.use('Agg')
            import matplotlib.pyplot as plt
            fig, ax = plt.subplots(figsize=(7, 5))
            ax.hist(loop_errs, bins=20, color='#c65b7c')
            ax.axvline(sorted(loop_errs)[len(loop_errs)//2], color='k', linestyle='--')
            ax.set_title('4-cycle rotation-sum closure error (deg)\nsingle-per-section-rotation model: NOT globally consistent')
            ax.set_xlabel('closure error, degrees')
            fig.tight_layout()
            fig.savefig(args.figure, dpi=130)
            print(f'\nwrote figure: {args.figure}')
        except ImportError:
            print('\n(matplotlib/PIL not available, skipped --figure output)')


if __name__ == '__main__':
    main()
