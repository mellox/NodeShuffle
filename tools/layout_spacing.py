#!/usr/bin/env python3
"""layout_spacing.py -- offline spacing statistics for a `NodeShuffle.DumpLayout` block (rwg-layout-dump-1, 2026-10-29).

Question: is the RWG deal's node layout CLUMPY (voids where the player stands) or only sparse by chance?
Input: the LAYOUTDUMP-BEGIN ... LAYOUTDUMP-END block in a FactoryGame.log (stdlib only; safe under `python -I`).

It parses ONLY the chosen complete block (default: the last one), and exits nonzero when:
  * the last LAYOUTDUMP-BEGIN has no matching END (a truncated or still-running dump),
  * any line's field ORDER differs from FIELDS below (the C++ format and this parser drifted),
  * END counts != BEGIN counts != parsed line counts, or countsMatch != 1.
It never prints a statistic on an empty population.

Populations (separately): dealt-surface, dealt-cave, dealt-all (active new layout entries), originals (hidden original
records, the vanilla reference) and well cores. Distances are 2-D (XY) only.

  python -I tools/layout_spacing.py --log <FactoryGame.log> --out <report.md> [--block 1] [--at X,Y ...]
  python -I tools/layout_spacing.py --selftest
"""
from __future__ import annotations

import argparse
import math
import os
import random
import re
import sys

# Field order per line class. tools/check_layoutdump_lint.ps1 pins this tuple against the C++ format strings AND the fixture.
FIELDS = {
    'BEGIN': ('dumpId', 'map', 'seed', 'layoutEntries', 'surface', 'cave', 'inactive', 'notNew', 'wellCores', 'satellites',
              'originals', 'gridCells', 'gridCols', 'gridRows', 'cellCm', 'samplesPerCell', 'halfExtentCm', 'apiUsable'),
    'E': ('i', 'guid', 'res', 'purity', 'x', 'y', 'z', 'cave', 'rayCasted', 'live', 'active', 'newNode'),
    'W': ('i', 'guid', 'res', 'x', 'y', 'z', 'src', 'satellites', 'placed', 'managed', 'pinned'),
    'O': ('i', 'res', 'purity', 'x', 'y', 'z', 'trueSet', 'modded'),
    'G': ('cx', 'cy', 'xMinCm', 'yMinCm', 'samples', 'land', 'dealable', 'landFrac', 'dealableFrac'),
    'END': ('dumpId', 'entriesPrinted', 'entriesExpected', 'wellsPrinted', 'wellsExpected', 'originalsPrinted',
            'originalsExpected', 'originalsPurityKnown', 'gridCellsPrinted', 'gridCellsExpected', 'countsMatch',
            'dealableNotLand', 'latchMaskBefore', 'latchMaskAfter', 'gridMs', 'elapsedMs'),
}
# Anchored on POSITION: optional UE timestamp/frame prefix, then the category and verbosity, then the tag and its colon.
LINE_RE = re.compile(r'^(?:\[[^\]]*\]\[\s*\d+\])?LogNodeShuffle: Display: LAYOUTDUMP-(BEGIN|LEGEND|E|W|O|G|END): (.*)$')
KV_RE = re.compile(r'(?<![A-Za-z0-9])([A-Za-z][A-Za-z0-9]*)=(\S+)')
DEFAULT_AT = [(171988.0, -403996.0), (171888.0, -404024.0), (151973.0, -238107.0)]
DISK_R_CM = 100000.0
CENTRE_STEP_CM = 25000.0
MIN_CELL_FRAC = 0.5  # cells with at least this dealable fraction form the "dealable cells" population for the 50% test


class DumpError(Exception):
    pass


def _num(v: str):
    try:
        return int(v)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def parse_line(cls: str, body: str, lineno: int) -> dict:
    pairs = KV_RE.findall(body)
    keys = tuple(k for k, _ in pairs)
    if keys != FIELDS[cls]:
        raise DumpError(f'line {lineno}: LAYOUTDUMP-{cls} field order {keys} != expected {FIELDS[cls]} (C++ format and parser drifted)')
    return {k: _num(v) for k, v in pairs}


def parse_blocks(lines) -> list:
    """Return every COMPLETE block in order; raise if the last BEGIN is unterminated."""
    blocks, cur = [], None
    for n, raw in enumerate(lines, 1):
        m = LINE_RE.match(raw.rstrip('\r\n'))
        if not m:
            continue
        cls, body = m.group(1), m.group(2)
        if cls == 'LEGEND':
            continue
        if cls == 'BEGIN':
            cur = {'begin': parse_line('BEGIN', body, n), 'E': [], 'W': [], 'O': [], 'G': [], 'end': None, 'line': n}
            continue
        if cur is None:
            continue  # a stray line outside any block
        rec = parse_line(cls, body, n)
        if cls == 'END':
            if rec['dumpId'] != cur['begin']['dumpId']:
                raise DumpError(f'line {n}: END dumpId {rec["dumpId"]} != BEGIN dumpId {cur["begin"]["dumpId"]}')
            cur['end'] = rec
            blocks.append(cur)
            cur = None
        else:
            cur[cls].append(rec)
    if cur is not None:
        raise DumpError(f'the last LAYOUTDUMP-BEGIN (line {cur["line"]}) has no LAYOUTDUMP-END: incomplete block, refusing')
    if not blocks:
        raise DumpError('no complete LAYOUTDUMP block found')
    return blocks


def validate(b: dict) -> None:
    B, X = b['begin'], b['end']
    checks = [
        ('entries', B['layoutEntries'], X['entriesExpected'], X['entriesPrinted'], len(b['E'])),
        ('wells', B['wellCores'], X['wellsExpected'], X['wellsPrinted'], len(b['W'])),
        ('originals', B['originals'], X['originalsExpected'], X['originalsPrinted'], len(b['O'])),
        ('gridCells', B['gridCells'], X['gridCellsExpected'], X['gridCellsPrinted'], len(b['G'])),
    ]
    bad = [c for c in checks if len(set(c[1:])) != 1]
    if bad:
        raise DumpError('count mismatch (begin, endExpected, endPrinted, parsed): ' + '; '.join(f'{c[0]}={c[1:]}' for c in bad))
    if X['countsMatch'] != 1:
        raise DumpError('END countsMatch is not 1')
    if B['surface'] + B['cave'] + B['inactive'] + B['notNew'] != B['layoutEntries']:
        raise DumpError('BEGIN class counts do not sum to layoutEntries')
    E = b['E']
    if [e['i'] for e in E] != list(range(len(E))):
        raise DumpError('entry indices are not 0..n-1 in order')
    got = (sum(1 for e in E if e['newNode'] and e['active'] and not e['cave']), sum(1 for e in E if e['newNode'] and e['active'] and e['cave']),
           sum(1 for e in E if e['newNode'] and not e['active']), sum(1 for e in E if not e['newNode']))
    if got != (B['surface'], B['cave'], B['inactive'], B['notNew']):
        raise DumpError(f'entry classes from E lines {got} != BEGIN {(B["surface"], B["cave"], B["inactive"], B["notNew"])}')
    if B['gridCells'] != B['gridCols'] * B['gridRows']:
        raise DumpError('gridCells != gridCols * gridRows')


# ---------------------------------------------------------------- statistics
class Grid:
    def __init__(self, cells, cell_cm):
        self.cell = float(cell_cm)
        self.cells = {(c['cx'], c['cy']): (float(c['xMinCm']), float(c['yMinCm']), float(c['dealableFrac'])) for c in cells}
        if not self.cells:
            raise DumpError('no grid cells: dealable area unknown')
        self.x0 = min(v[0] for v in self.cells.values())
        self.y0 = min(v[1] for v in self.cells.values())
        self.keys = [k for k, v in self.cells.items() if v[2] > 0.0]
        self.weights = [self.cells[k][2] for k in self.keys]
        self.wsum = sum(self.weights)
        if self.wsum <= 0.0:
            raise DumpError('every grid cell has dealableFrac 0: no dealable area')
        self.area_cm2 = self.wsum * self.cell * self.cell
        self.big = [k for k in self.keys if self.cells[k][2] >= MIN_CELL_FRAC]

    def key_of(self, x, y):
        k = (int(math.floor((x - self.x0) / self.cell)), int(math.floor((y - self.y0) / self.cell)))
        return k if k in self.cells else None


def nn_dists(pts, bucket=50000.0):
    if len(pts) < 2:
        return []
    h = {}
    for i, (x, y) in enumerate(pts):
        h.setdefault((int(x // bucket), int(y // bucket)), []).append(i)
    out = []
    for i, (x, y) in enumerate(pts):
        bx, by = int(x // bucket), int(y // bucket)
        best, r = math.inf, 0
        while True:
            for gx in range(bx - r, bx + r + 1):
                for gy in range(by - r, by + r + 1):
                    if max(abs(gx - bx), abs(gy - by)) != r:
                        continue
                    for j in h.get((gx, gy), ()):
                        if j != i:
                            d = (pts[j][0] - x) ** 2 + (pts[j][1] - y) ** 2
                            if d < best:
                                best = d
            if best < math.inf and r * bucket >= math.sqrt(best):
                break
            r += 1
        out.append(math.sqrt(best))
    return out


def quant(v, q):
    s = sorted(v)
    pos = q * (len(s) - 1)
    lo = int(math.floor(pos))
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (pos - lo)


def pct_rank(null, obs):
    lt = sum(1 for v in null if v < obs)
    eq = sum(1 for v in null if v == obs)
    return 100.0 * (lt + 0.5 * eq) / len(null)


def cell_stats(grid: Grid, counts: dict, n_in: int):
    chi2, k = 0.0, 0
    for key, w in zip(grid.keys, grid.weights):
        e = n_in * w / grid.wsum
        o = counts.get(key, 0)
        chi2 += (o - e) ** 2 / e
        k += 1
    disp = chi2 / (k - 1) if k > 1 else float('nan')
    below = sum(1 for key in grid.big if counts.get(key, 0) < 0.5 * n_in * grid.cells[key][2] / grid.wsum)
    return disp, (below / len(grid.big) if grid.big else float('nan'))


def draw_null(grid: Grid, n, rng):
    keys = rng.choices(grid.keys, weights=grid.weights, k=n)
    pts, counts = [], {}
    for key in keys:
        counts[key] = counts.get(key, 0) + 1
        x0, y0, _ = grid.cells[key]
        pts.append((x0 + rng.random() * grid.cell, y0 + rng.random() * grid.cell))
    return pts, counts


def disk_count(h, bucket, x, y, r=DISK_R_CM):
    n, r2 = 0, r * r
    bx, by, rb = int(x // bucket), int(y // bucket), int(math.ceil(r / bucket))
    for gx in range(bx - rb, bx + rb + 1):
        for gy in range(by - rb, by + rb + 1):
            for (px, py) in h.get((gx, gy), ()):
                if (px - x) ** 2 + (py - y) ** 2 < r2:
                    n += 1
    return n


def analyse(name, pts, grid: Grid, at, draws=1000, nn_draws=200, seed=20261029):
    """Return (markdown lines, verdict or None). None = population too small for any statistic."""
    md = [f'### {name}', '']
    n = len(pts)
    if n < 2:
        md += [f'Population has {n} point(s): no statistic computed.', '']
        return md, None
    inside = [p for p in pts if grid.key_of(*p) is not None]
    counts = {}
    for p in inside:
        k = grid.key_of(*p)
        counts[k] = counts.get(k, 0) + 1
    n_in = len(inside)
    d = nn_dists(pts)
    mean_nn = sum(d) / len(d)
    lam = n / grid.area_cm2
    r_ce = mean_nn / (0.5 / math.sqrt(lam))
    md += [f'N={n} (inside grid {n_in}, outside {n - n_in}); dealable area {grid.area_cm2 / 1e10:.1f} km2 '
           f'(sum of cell dealableFrac x cell area); density {lam * 1e10:.2f} per km2.', '',
           '| nearest-neighbour (m) | p10 | p50 | p90 | mean |', '|---|---|---|---|---|',
           f'| observed | {quant(d, .1) / 100:.0f} | {quant(d, .5) / 100:.0f} | {quant(d, .9) / 100:.0f} | {mean_nn / 100:.0f} |', '',
           f'Clark-Evans R (dealable-area corrected, no edge correction) = {r_ce:.3f}  (1 = random, <1 = clustered, >1 = regular).', '']
    if n_in < 2:
        md += ['Fewer than 2 points inside the grid: cell tests skipped.', '']
        return md, None
    disp, below = cell_stats(grid, counts, n_in)
    rng = random.Random(seed)
    nd, nb = [], []
    for _ in range(draws):
        _, c = draw_null(grid, n_in, rng)
        a, b = cell_stats(grid, c, n_in)
        nd.append(a)
        nb.append(b)
    nn_null = []
    for _ in range(nn_draws):
        p, _ = draw_null(grid, n_in, rng)
        dd = nn_dists(p)
        nn_null.append(sum(dd) / len(dd))
    pd, pb, pn = pct_rank(nd, disp), pct_rank(nb, below), pct_rank(nn_null, mean_nn)
    verdict = 'CLUMPED (over-dispersed vs null)' if pd >= 95.0 else ('MORE REGULAR than null' if pd <= 5.0 else 'CONSISTENT WITH CHANCE')
    md += [f'Null: {draws} draws (cell tests) and {nn_draws} draws (NN mean) of {n_in} points; cells chosen with probability '
           f'proportional to dealableFrac, uniform inside the cell; seed {seed}.', '',
           '| statistic | observed | null p5 | null p50 | null p95 | observed percentile |', '|---|---|---|---|---|---|',
           f'| index of dispersion (chi2/(k-1), {len(grid.keys)} cells with dealableFrac>0) | {disp:.3f} | {quant(nd, .05):.3f} | {quant(nd, .5):.3f} | {quant(nd, .95):.3f} | {pd:.1f} |',
           f'| fraction of {len(grid.big)} cells (dealableFrac>={MIN_CELL_FRAC}) below 50% of expected | {below:.3f} | {quant(nb, .05):.3f} | {quant(nb, .5):.3f} | {quant(nb, .95):.3f} | {pb:.1f} |',
           f'| mean NN distance (m) | {mean_nn / 100:.0f} | {quant(nn_null, .05) / 100:.0f} | {quant(nn_null, .5) / 100:.0f} | {quant(nn_null, .95) / 100:.0f} | {pn:.1f} |', '',
           f'**Verdict (index-of-dispersion percentile): {verdict}.**', '']
    if at and grid.big:
        bucket = 50000.0
        h = {}
        for (x, y) in pts:
            h.setdefault((int(x // bucket), int(y // bucket)), []).append((x, y))
        cc = []
        for key in grid.big:
            x0, y0, _ = grid.cells[key]
            s = int(round(grid.cell / CENTRE_STEP_CM))
            for i in range(s):
                for j in range(s):
                    cc.append(disk_count(h, bucket, x0 + (i + .5) * CENTRE_STEP_CM, y0 + (j + .5) * CENTRE_STEP_CM))
        md += [f'1 km disks: {len(cc)} centres on a {CENTRE_STEP_CM / 100:.0f} m lattice over dealable cells; centre-count p10/p50/p90 = '
               f'{quant(cc, .1):.0f}/{quant(cc, .5):.0f}/{quant(cc, .9):.0f}.', '', '| position (cm) | 1 km count | percentile among centres |', '|---|---|---|']
        for (x, y) in at:
            c = disk_count(h, bucket, x, y)
            md.append(f'| {x:.0f},{y:.0f} | {c} | {pct_rank(cc, c):.1f} |')
        md.append('')
    return md, verdict


def report(b: dict, at, draws, nn_draws) -> str:
    B, X = b['begin'], b['end']
    grid = Grid(b['G'], B['cellCm'])
    E = b['E']
    pops = [
        ('dealt-surface (active new entries, not cave)', [(e['x'], e['y']) for e in E if e['newNode'] and e['active'] and not e['cave']]),
        ('dealt-cave (active new cave entries)', [(e['x'], e['y']) for e in E if e['newNode'] and e['active'] and e['cave']]),
        ('dealt-all (active new entries)', [(e['x'], e['y']) for e in E if e['newNode'] and e['active']]),
        ('originals (hidden original records, vanilla reference)', [(o['x'], o['y']) for o in b['O']]),
        ('well cores (with a printed position)', [(w['x'], w['y']) for w in b['W'] if w['src'] != 'none']),
    ]
    md = [f'# LAYOUTDUMP spacing report (dumpId {B["dumpId"]}, seed {B["seed"]})', '',
          f'Layout {B["layoutEntries"]} = surface {B["surface"]} + cave {B["cave"]} + inactive {B["inactive"]} + notNew {B["notNew"]}; '
          f'well cores {B["wellCores"]} (satellites {B["satellites"]}); originals {B["originals"]}; grid {B["gridCols"]}x{B["gridRows"]} '
          f'cells of {B["cellCm"] / 100:.0f} m, {B["samplesPerCell"]} samples each; dealableNotLand {X["dealableNotLand"]}; '
          f'latch mask {X["latchMaskBefore"]} -> {X["latchMaskAfter"]}; gridMs {X["gridMs"]}, elapsedMs {X["elapsedMs"]}.', '',
          'Read the verdict row per population: an index-of-dispersion percentile at or above 95 against the dealable-weighted null says '
          'the layout is clumpier than chance; between 5 and 95 says the sparse spots are what chance gives. Positions are each entry\'s '
          'CURRENT saved location (after redeals), not the roll-time spot.', '']
    for name, pts in pops:
        lines, _ = analyse(name, pts, grid, at, draws, nn_draws)
        md += lines
    return '\n'.join(md) + '\n'


# ---------------------------------------------------------------- selftest
def _synthetic_grid(n=20, cell=100000.0):
    rng = random.Random(7)
    cells = []
    for cy in range(n):
        for cx in range(n):
            f = 0.0 if (cx + cy) % 7 == 0 else (0.5 if rng.random() < 0.3 else 1.0)
            cells.append({'cx': cx, 'cy': cy, 'xMinCm': cx * cell, 'yMinCm': cy * cell, 'dealableFrac': f})
    return Grid(cells, cell)


def selftest() -> int:
    fails = []
    here = os.path.dirname(os.path.abspath(__file__))
    fx = os.path.join(here, 'fixtures', 'layoutdump_fixture.log')
    with open(fx, encoding='utf-8', errors='replace') as fh:
        text = fh.read().splitlines(True)
    try:
        bl = parse_blocks(text)
        validate(bl[-1])
        b = bl[-1]
        if (len(b['E']), len(b['W']), len(b['O']), len(b['G'])) != (6, 1, 3, 4):
            fails.append(f'fixture parsed {(len(b["E"]), len(b["W"]), len(b["O"]), len(b["G"]))} lines, expected (6, 1, 3, 4)')
    except DumpError as e:
        fails.append(f'fixture did not parse: {e}')
    try:
        parse_blocks([ln for ln in text if 'LAYOUTDUMP-END:' not in ln])
        fails.append('a block with no END was accepted')
    except DumpError:
        pass
    g = _synthetic_grid()
    rng = random.Random(11)
    pois, _ = draw_null(g, 800, rng)
    clus = []
    parents, _ = draw_null(g, 40, rng)
    for (px, py) in parents:
        for _ in range(20):
            clus.append((px + rng.gauss(0, 15000.0), py + rng.gauss(0, 15000.0)))
    _, vp = analyse('poisson', pois, g, [], draws=300, nn_draws=50)
    _, vc = analyse('clustered', clus, g, [], draws=300, nn_draws=50)
    if vp is None or vp.startswith('CLUMPED'):
        fails.append(f'synthetic Poisson set verdict {vp!r} (expected not CLUMPED)')
    if vc is None or not vc.startswith('CLUMPED'):
        fails.append(f'synthetic clustered set verdict {vc!r} (expected CLUMPED)')
    _, v0 = analyse('empty', [], g, [])
    if v0 is not None:
        fails.append('an empty population produced a verdict')
    for f in fails:
        print('SELFTEST FAIL:', f)
    print(f'SELFTEST {"FAIL" if fails else "PASS"}: fixture parse, incomplete-block refusal, Poisson={vp!r}, clustered={vc!r}, empty population refused')
    return 1 if fails else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--log', default=os.path.join(os.environ.get('LOCALAPPDATA', ''), 'FactoryGame', 'Saved', 'Logs', 'FactoryGame.log'))
    ap.add_argument('--out')
    ap.add_argument('--block', type=int, default=0, help='1-based complete block to analyse; 0 = the last (default)')
    ap.add_argument('--at', action='append', default=[], help='X,Y in cm (repeatable); default: the three measured HERE spots')
    ap.add_argument('--draws', type=int, default=1000)
    ap.add_argument('--nn-draws', type=int, default=200)
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    if not a.out:
        ap.error('--out is required')
    if a.draws < 1000:
        ap.error('--draws must be at least 1000')
    at = [tuple(float(v) for v in s.split(',')) for s in a.at] if a.at else DEFAULT_AT
    try:
        with open(a.log, encoding='utf-8', errors='replace') as fh:
            blocks = parse_blocks(fh)
        if a.block < 0 or a.block > len(blocks):
            raise DumpError(f'--block {a.block} out of range (1..{len(blocks)})')
        b = blocks[a.block - 1] if a.block else blocks[-1]
        validate(b)
        out = report(b, at, a.draws, a.nn_draws)
    except (DumpError, OSError) as e:
        print(f'LAYOUT-SPACING ERROR: {e}', file=sys.stderr)
        return 2
    with open(a.out, 'w', encoding='utf-8') as fh:
        fh.write(out)
    print(f'wrote {a.out} from block dumpId {b["begin"]["dumpId"]} ({len(blocks)} complete block(s) in the log)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
