#!/usr/bin/env python3
"""layout_diff.py -- diff two LAYOUTDUMP blocks of one FactoryGame.log by entry key (rwg-layout-diff-1, 2026-10-30).

  python -I tools/layout_diff.py --at X,Y[,label] --out diff.md      # the last two complete blocks of the live log
  python -I tools/layout_diff.py --log <FactoryGame.log> --blocks 3 4 --at 171988,-403996,camp --out diff.md
  python -I tools/layout_diff.py ... --radius-m 1000 --moved-m 1.0     # spot disk radius and move threshold (the defaults)
  python -I tools/layout_diff.py ... --allow-seed-change               # diff two blocks whose seeds differ
  python -I tools/layout_diff.py --selftest                            # tools/fixtures/layoutdiff_fixture.log; exit 0 or 1
  Exit codes: 0 report written, 1 selftest failure, 2 refused (one line on stderr). Stdlib only.

Keys: entry rows by the first 8 hex digits of their printed guid (dumps up to rwg-layout-dump-1 print 8, rwg-clifftop-1 and later print all 32); well rows by their printed claim-guid
prefix; original rows carry no guid, so they pair by resource class plus exact printed XYZ. A key printed twice in one block
is AMBIGUOUS: counted and listed, never paired. Distances are XY only. Spot counts use every entry row (all classes) and
add one column for active new entries only.
"""
# TODO(2026-10-30, rwg-layout-diff-1): diagnostic tool for the walk test; re-check against the live LAYOUTDUMP format if
# NodeShuffleLayoutDump.cpp changes. Line formats come from tools/layout_spacing.py (FIELDS, LINE_RE, parse_line), which
# tools/check_layoutdump_lint.ps1 pins against the C++ format strings; this file never restates them.
from __future__ import annotations

import argparse
import importlib.util
import math
import os
import re
import sys
from datetime import datetime

def _load_spacing():
    """Load tools/layout_spacing.py by explicit path: `python -I` keeps the script directory off sys.path."""
    sys.dont_write_bytecode = True
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'layout_spacing.py')
    spec = importlib.util.spec_from_file_location('layout_spacing', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


LS = _load_spacing()
DumpError = LS.DumpError
TS_RE = re.compile(r'^\[([^\]]*)\]\[\s*\d+\]')  # the UE log prefix; BEGIN itself carries no time field
TS_FMT = '%Y.%m.%d-%H.%M.%S:%f'
RAW_KEYS = ('guid', 'res', 'src')  # kept as the printed token: parse_line's int/float coercion turns 00012345 into 12345, 1E234567 into inf
E_FLAGS = ('cave', 'rayCasted', 'live', 'active', 'newNode', 'pinned', 'res', 'purity')
W_FLAGS = ('src', 'satellites', 'placed', 'managed', 'pinned', 'res')
O_FLAGS = ('purity', 'trueSet', 'modded')
O_REANCHOR_CM = 500.0  # second-pass pairing radius for originals; REHIDE rebinds within 300 cm of the recorded anchor (Subsystem.cpp ~L6360)
DEFAULT_LOG = os.path.join(os.environ.get('LOCALAPPDATA', ''), 'FactoryGame', 'Saved', 'Logs', 'FactoryGame.log')


# ---------------------------------------------------------------- parsing and refusals
def parse_all(lines):
    """Every COMPLETE block in file order, plus (dumpId, line) of every BEGIN that never reached its END."""
    complete, incomplete, cur = [], [], None
    for n, raw in enumerate(lines, 1):
        s = raw.rstrip('\r\n')
        m = LS.LINE_RE.match(s)
        if not m:
            continue
        cls, body = m.group(1), m.group(2)
        if cls == 'LEGEND':
            continue
        if cls == 'BEGIN':
            if cur is not None:
                incomplete.append((cur['begin']['dumpId'], cur['line']))
            t = TS_RE.match(s)
            cur = {'begin': LS.parse_line('BEGIN', body, n), 'E': [], 'W': [], 'O': [], 'G': [], 'end': None,
                   'line': n, 'ts': t.group(1) if t else None}
            continue
        if cur is None:
            continue
        rec = LS.parse_line(cls, body, n)
        raw_kv = dict(LS.KV_RE.findall(body))
        for k in RAW_KEYS:
            if k in raw_kv:
                rec[k] = raw_kv[k]
        if cls == 'END':
            if rec['dumpId'] != cur['begin']['dumpId']:
                raise DumpError(f'line {n}: END dumpId {rec["dumpId"]} does not match BEGIN dumpId {cur["begin"]["dumpId"]}')
            cur['end'] = rec
            complete.append(cur)
            cur = None
        else:
            cur[cls].append(rec)
    if cur is not None:
        incomplete.append((cur['begin']['dumpId'], cur['line']))
    return complete, incomplete


def check_block(b):
    B, X = b['begin'], b['end']
    checks = [('entries', B['layoutEntries'], X['entriesExpected'], X['entriesPrinted'], len(b['E'])),
              ('wells', B['wellCores'], X['wellsExpected'], X['wellsPrinted'], len(b['W'])),
              ('originals', B['originals'], X['originalsExpected'], X['originalsPrinted'], len(b['O'])),
              ('gridCells', B['gridCells'], X['gridCellsExpected'], X['gridCellsPrinted'], len(b['G']))]
    bad = [c for c in checks if len(set(c[1:])) != 1]
    if bad:
        raise DumpError(f'block dumpId {B["dumpId"]}: count mismatch (begin, endExpected, endPrinted, parsed): '
                        + '; '.join(f'{c[0]} {c[1:]}' for c in bad))
    if X['countsMatch'] != 1:
        raise DumpError(f'block dumpId {B["dumpId"]}: END countsMatch is {X["countsMatch"]}, not 1')
    if [e['i'] for e in b['E']] != list(range(len(b['E']))):
        raise DumpError(f'block dumpId {B["dumpId"]}: entry indices are not 0..n-1 in order')
    E = b['E']
    got = (sum(1 for e in E if e['newNode'] and e['active'] and not e['cave']), sum(1 for e in E if e['newNode'] and e['active'] and e['cave']),
           sum(1 for e in E if e['newNode'] and not e['active']), sum(1 for e in E if not e['newNode']))
    if got != (B['surface'], B['cave'], B['inactive'], B['notNew']):
        raise DumpError(f'block dumpId {B["dumpId"]}: entry flags give surface/cave/inactive/notNew {got}, BEGIN prints '
                        f'{(B["surface"], B["cave"], B["inactive"], B["notNew"])}')


def select(complete, incomplete, ids, allow_seed_change):
    if ids is None:
        last = complete[-1]['line'] if complete else 0
        late = [d for d in incomplete if d[1] > last]
        if late:
            raise DumpError(f'the last LAYOUTDUMP-BEGIN (dumpId {late[-1][0]}, line {late[-1][1]}) has no END: incomplete block; '
                            f'pass --blocks to name two complete blocks')
        if len(complete) < 2:
            raise DumpError(f'{len(complete)} complete LAYOUTDUMP block(s) in the log; a diff needs two')
        A, B = complete[-2], complete[-1]
    else:
        if ids[0] == ids[1]:
            raise DumpError(f'--blocks names dumpId {ids[0]} twice')
        picked = []
        for want in ids:
            hits = [b for b in complete if b['begin']['dumpId'] == want]
            if len(hits) > 1:
                raise DumpError(f'dumpId {want} appears in {len(hits)} complete blocks of this log; it does not name one block')
            if not hits:
                if any(d[0] == want for d in incomplete):
                    raise DumpError(f'block dumpId {want} is incomplete (BEGIN without END)')
                raise DumpError(f'no block with dumpId {want} in the log (complete: {[b["begin"]["dumpId"] for b in complete]})')
            picked.append(hits[0])
        A, B = picked
    for b in (A, B):
        check_block(b)
    if A['begin']['seed'] != B['begin']['seed'] and not allow_seed_change:
        raise DumpError(f'seeds differ (A {A["begin"]["seed"]}, B {B["begin"]["seed"]}): a diff across a roll compares two '
                        f'different deals, not one deal over time; pass --allow-seed-change to diff anyway')
    return A, B


# ---------------------------------------------------------------- diff
def _xy(a, b):
    return math.hypot(b['x'] - a['x'], b['y'] - a['y'])


def _flags(a, b, names):
    return [(f, a[f], b[f]) for f in names if a.get(f) != b.get(f)]


def _new_result(nA, nB, key):
    return {'nA': nA, 'nB': nB, 'key': key, 'ambiguous': [], 'pairs': [], 'moved': [], 'flagOnly': [], 'zOnly': [],
            'unchanged': 0, 'onlyA': [], 'onlyB': []}


def _classify(r, a, b, flags, moved_cm, sentinel=False):
    r['pairs'].append((a, b))
    fl = _flags(a, b, flags)
    d = _xy(a, b)
    if sentinel:  # a well printed with src none carries 0,0,0, not a position: a state change when a flag differs, never a move
        if fl:
            r['flagOnly'].append((a, b, None, fl))  # None: no distance exists between a sentinel and a position
        else:
            r['unchanged'] += 1
    elif d > moved_cm:
        r['moved'].append((a, b, d, fl))
    elif fl:
        r['flagOnly'].append((a, b, d, fl))
    elif abs(b['z'] - a['z']) > moved_cm:
        r['zOnly'].append((a, b, d, fl))
    else:
        r['unchanged'] += 1


def diff_keyed(A, B, flags, moved_cm, key, is_well=False):
    r = _new_result(len(A), len(B), key)
    ka, kb = {}, {}
    for rows, d in ((A, ka), (B, kb)):
        for row in rows:
            d.setdefault(row['guid'][:8], []).append(row)  # rwg-clifftop-1 E3: 8-hex prefix, so an 8-hex dump diffs against a 32-hex one
    dup = {k for d in (ka, kb) for k, v in d.items() if len(v) > 1}
    for side, d in (('A', ka), ('B', kb)):
        for k in sorted(dup):
            r['ambiguous'] += [(side, row) for row in d.get(k, [])]
    for k, rows in ka.items():
        if k in dup:
            continue
        if k not in kb:
            r['onlyA'].append(rows[0])
            continue
        a, b = rows[0], kb[k][0]
        _classify(r, a, b, flags, moved_cm, sentinel=is_well and 'none' in (a['src'], b['src']))
    r['onlyB'] = [rows[0] for k, rows in kb.items() if k not in dup and k not in ka]
    return r


def diff_originals(A, B, moved_cm):
    r = _new_result(len(A), len(B), 'resource class + exact printed XYZ (originals carry no guid)')
    ka, kb = {}, {}
    for rows, d in ((A, ka), (B, kb)):
        for row in rows:
            d.setdefault((row['res'], row['x'], row['y'], row['z']), []).append(row)
    for k, ra in ka.items():
        rb = kb.get(k, [])
        m = min(len(ra), len(rb))
        for a, b in zip(ra[:m], rb[:m]):
            _classify(r, a, b, O_FLAGS, moved_cm)
        r['onlyA'] += ra[m:]
    for k, rb in kb.items():
        r['onlyB'] += rb[len(ka.get(k, [])):]
    left = list(r['onlyB'])
    still = []
    for a in r['onlyA']:
        near = [(_xy(a, b), j) for j, b in enumerate(left) if b['res'] == a['res'] and _xy(a, b) <= O_REANCHOR_CM]
        if not near:
            still.append(a)
            continue
        d, j = min(near)
        b = left.pop(j)
        r['pairs'].append((a, b))
        r['flagOnly'].append((a, b, d, _flags(a, b, O_FLAGS) + [('xyz', _p(a), _p(b))]))
    r['onlyA'], r['onlyB'] = still, left
    return r


def spot_stats(A, B, ed, sx, sy, r_cm):
    r2 = r_cm * r_cm
    inside = lambda row: (row['x'] - sx) ** 2 + (row['y'] - sy) ** 2 < r2
    act = lambda row: row['newNode'] == 1 and row['active'] == 1
    s = {'nA': sum(map(inside, A)), 'nB': sum(map(inside, B)),
         'activeA': sum(1 for e in A if inside(e) and act(e)), 'activeB': sum(1 for e in B if inside(e) and act(e)),
         'out': sum(1 for a, b, _, _ in ed['moved'] if inside(a) and not inside(b)),
         'into': sum(1 for a, b, _, _ in ed['moved'] if not inside(a) and inside(b)),
         'within': sum(1 for a, b, _, _ in ed['moved'] if inside(a) and inside(b)),
         'removedIn': sum(map(inside, ed['onlyA'])), 'addedIn': sum(map(inside, ed['onlyB'])),
         'ambA': sum(1 for side, e in ed['ambiguous'] if side == 'A' and inside(e)),
         'ambB': sum(1 for side, e in ed['ambiguous'] if side == 'B' and inside(e)),
         'zOnlyIn': sum(1 for a, b, _, _ in ed['zOnly'] if inside(a))}
    unmoved = [(a, b) for a, b in ed['pairs'] if not any(a is m[0] for m in ed['moved'])]
    s['subOut'] = sum(1 for a, b in unmoved if inside(a) and not inside(b))
    s['subIn'] = sum(1 for a, b in unmoved if not inside(a) and inside(b))
    s['expect'] = (s['nA'] - s['out'] + s['into'] - s['subOut'] + s['subIn'] - s['removedIn'] + s['addedIn']
                   - s['ambA'] + s['ambB'])
    s['reconciles'] = 1 if s['expect'] == s['nB'] else 0
    return s


# ---------------------------------------------------------------- report
def _p(row):
    return f'{row["x"]:.0f},{row["y"]:.0f},{row["z"]:.0f}'


def _fl(fl):
    return ', '.join(f'{f} {a}->{b}' for f, a, b in fl) or '-'


def _ts(b):
    try:
        return datetime.strptime(b['ts'], TS_FMT) if b['ts'] else None
    except ValueError:
        return None


def _quants(dists_cm):
    if not dists_cm:
        return 'no moved rows'
    q = lambda v: f'{LS.quant(dists_cm, v) / 100:.1f}'
    return f'distance m p10 {q(.1)}, p50 {q(.5)}, p90 {q(.9)}, max {max(dists_cm) / 100:.1f}'


def _by_field(r):
    n = {}
    for rows in (r['moved'], r['flagOnly']):
        for _, _, _, fl in rows:
            for f, _, _ in fl:
                n[f] = n.get(f, 0) + 1
    return n


def pop_section(title, r, show_res, show_guid=True):
    md = [f'## {title}', '', f'Key: {r["key"]}. Rows in A {r["nA"]}, in B {r["nB"]}; paired {len(r["pairs"])}; ambiguous rows '
          f'{len(r["ambiguous"])}; only in A (removed) {len(r["onlyA"])}; only in B (added) {len(r["onlyB"])}.', '',
          f'Moved (XY distance above the threshold): {len(r["moved"])}; {_quants([m[2] for m in r["moved"]])}. '
          f'Flag changes (XY within the threshold, a printed flag or field differs; Z may differ too): {len(r["flagOnly"])}. '
          f'Z-only changes (XY within the threshold, |dz| above it, no flag change): {len(r["zOnly"])}. Unchanged: {r["unchanged"]}.', '',
          'Flag changes by field (moved and flag-change rows): ' + (', '.join(f'{k} {v}' for k, v in sorted(_by_field(r).items())) or 'none') + '.', '']
    head = ('| guid ' if show_guid else '| ') + ('| res ' if show_res else '') + '| from XYZ (cm) | to XYZ (cm) | dist m | flag changes (before->after) |'
    sep = '|' + '---|' * (head.count('|') - 1)
    for name, rows in (('Moved', r['moved']), ('Flag changes', r['flagOnly']), ('Z-only changes', r['zOnly'])):
        if rows:
            md += [f'**{name}**', '', head, sep]
            for a, b, d, fl in sorted(rows, key=lambda t: -(t[2] if t[2] is not None else -1.0)):
                dist = '-' if d is None else f'{d / 100:.1f}'
                md.append(('| ' + a['guid'] + ' ' if show_guid else '| ') + (f'| {a["res"]} ' if show_res else '')
                          + f'| {_p(a)} | {_p(b)} | {dist} | {_fl(fl)} |')
            md.append('')
    for name, rows in (('Only in A (removed)', r['onlyA']), ('Only in B (added)', r['onlyB']),
                       ('Ambiguous (key printed more than once in a block; never paired)', [row for _, row in r['ambiguous']])):
        if rows:
            md += [f'**{name}**: ' + '; '.join(f'{row.get("guid", "-")} {row["res"]} at {_p(row)}' for row in rows), '']
    return md


def build_report(log, A, B, ed, wd, od, spots, r_cm, moved_cm, allow_seed_change):
    BA, BB = A['begin'], B['begin']
    ta, tb = _ts(A), _ts(B)
    elapsed = f'{(tb - ta).total_seconds():.1f} s' if ta and tb else 'unknown (a BEGIN line has no parsable log timestamp)'
    seed_ok = BA['seed'] == BB['seed']
    ctrl = 'PASS: both blocks complete, BEGIN counts equal END expected, END printed and parsed rows, countsMatch 1 in both; ' + (
        'seeds equal.' if seed_ok else 'seeds DIFFER (accepted by --allow-seed-change; this is a cross-roll comparison).')
    order = '' if B['line'] > A['line'] else ' Block B precedes block A in the file.'
    md = [f'# LAYOUTDUMP diff: dumpId {BA["dumpId"]} -> {BB["dumpId"]}', '',
          f'Log: {log}', '',
          f'A: dumpId {BA["dumpId"]} (line {A["line"]}, logged {A["ts"] or "unknown"}), seed {BA["seed"]}, entries {BA["layoutEntries"]} '
          f'(surface {BA["surface"]}, cave {BA["cave"]}, inactive {BA["inactive"]}, notNew {BA["notNew"]}), well cores {BA["wellCores"]}, originals {BA["originals"]}.',
          f'B: dumpId {BB["dumpId"]} (line {B["line"]}, logged {B["ts"] or "unknown"}), seed {BB["seed"]}, entries {BB["layoutEntries"]} '
          f'(surface {BB["surface"]}, cave {BB["cave"]}, inactive {BB["inactive"]}, notNew {BB["notNew"]}), well cores {BB["wellCores"]}, originals {BB["originals"]}.', '',
          f'Elapsed between the two BEGIN lines: {elapsed}.{order}', '',
          f'Controls: {ctrl}', '',
          f'Move threshold {moved_cm / 100:g} m (XY); spot radius {r_cm / 100:g} m. Keys: entries by guid prefix, wells by claim-guid prefix '
          f'(a pair with a well printed as src none is a state change, never a move), originals by resource class + exact XYZ '
          f'(a pair whose printed XYZ differs by at most 5 m XY is listed as a flag change carrying xyz; a larger difference, as after a trueSet flip, shows as one removed + one added; originals never count as moved).', '',
          '## Spots (entry rows within the disk; every entry class, plus active new entries in their own column)', '']
    agg = {'out': 0, 'into': 0, 'within': 0, 'removedIn': 0, 'addedIn': 0}
    if not spots:
        md += ['No --at spot given: per-spot counts not computed.', '']
    else:
        md += ['| spot | X,Y (cm) | in A | in B | active new A | active new B | moved out | moved in | moved within | removed inside '
               '| added inside | sub-threshold crossers out/in | ambiguous A/B | partition check |', '|' + '---|' * 14]
        cens = []
        for label, sx, sy in spots:
            s = spot_stats(A['E'], B['E'], ed, sx, sy, r_cm)
            for k in agg:
                agg[k] += s[k]
            md.append(f'| {label} | {sx:.0f},{sy:.0f} | {s["nA"]} | {s["nB"]} | {s["activeA"]} | {s["activeB"]} | {s["out"]} | {s["into"]} '
                      f'| {s["within"]} | {s["removedIn"]} | {s["addedIn"]} | {s["subOut"]}/{s["subIn"]} | {s["ambA"]}/{s["ambB"]} '
                      f'| {"PASS" if s["reconciles"] else "FAIL"} (in A - out + in - sub-out + sub-in - removed + added - amb A + amb B = {s["expect"]}) |')
            cens.append(f'LAYOUTDIFF-AT: label={label} x={sx:.0f} y={sy:.0f} ambA={s["ambA"]} ambB={s["ambB"]} subOut={s["subOut"]} '
                        f'subIn={s["subIn"]} zOnlyInside={s["zOnlyIn"]} nA={s["nA"]} nB={s["nB"]} activeA={s["activeA"]} activeB={s["activeB"]} '
                        f'movedOut={s["out"]} movedIn={s["into"]} movedWithin={s["within"]} removedInside={s["removedIn"]} '
                        f'addedInside={s["addedIn"]} reconciles={s["reconciles"]}')
        md += ['', 'The partition check is an identity over how this tool split the rows (paired, removed, added, ambiguous): it fails only '
               'on a defect in this tool, never on what the dumps contain, and it passes when nothing moved.', '', '```'] + cens + ['```', '']
    md += pop_section('Entries (E)', ed, True)
    md += pop_section('Well cores (W)', wd, True)
    md += pop_section('Originals (O)', od, True, show_guid=False)
    sp = lambda k: str(agg[k]) if spots else 'na'
    census = (f'LAYOUTDIFF: blocks={BA["dumpId"]},{BB["dumpId"]} seedA={BA["seed"]} seedB={BB["seed"]} spots={len(spots)} '
              f'radiusM={r_cm / 100:g} movedM={moved_cm / 100:g} '
              f'eA={ed["nA"]} eB={ed["nB"]} ePaired={len(ed["pairs"])} eMoved={len(ed["moved"])} eFlagOnly={len(ed["flagOnly"])} '
              f'eZOnly={len(ed["zOnly"])} eRemoved={len(ed["onlyA"])} eAdded={len(ed["onlyB"])} eAmbiguous={len(ed["ambiguous"])} '
              f'wA={wd["nA"]} wB={wd["nB"]} wMoved={len(wd["moved"])} wMovedSrcChanged={sum(1 for m in wd["moved"] if any(f == "src" for f, _, _ in m[3]))} wFlagOnly={len(wd["flagOnly"])} wRemoved={len(wd["onlyA"])} '
              f'wAdded={len(wd["onlyB"])} wAmbiguous={len(wd["ambiguous"])} oA={od["nA"]} oB={od["nB"]} oFlagOnly={len(od["flagOnly"])} '
              f'oRemoved={len(od["onlyA"])} oAdded={len(od["onlyB"])} eMovedOut={sp("out")} eMovedIn={sp("into")} '
              f'eMovedWithin={sp("within")} eRemovedInside={sp("removedIn")} eAddedInside={sp("addedIn")}')
    md += ['---', '', 'Census (spot fields are summed over all spots; overlapping spots count a row once per spot):', '', census, '']
    return '\n'.join(md), census


def run(lines, log, ids, spots, r_cm, moved_cm, allow_seed_change):
    complete, incomplete = parse_all(lines)
    A, B = select(complete, incomplete, ids, allow_seed_change)
    ed = diff_keyed(A['E'], B['E'], E_FLAGS, moved_cm, 'entry guid prefix (first 8 hex digits)')
    wd = diff_keyed(A['W'], B['W'], W_FLAGS, moved_cm, 'well claim-guid prefix (first 8 hex digits)', is_well=True)
    od = diff_originals(A['O'], B['O'], moved_cm)
    md, census = build_report(log, A, B, ed, wd, od, spots, r_cm, moved_cm, allow_seed_change)
    got = ', '.join('%s@%s' % (b['begin']['dumpId'], b['line']) for b in complete)
    bad = ', '.join('%s@%s' % d for d in incomplete) or 'none'
    inv = f'Complete blocks in this log (dumpId@line): {got}. Incomplete BEGINs (dumpId@line): {bad}.'
    md = md.replace('\n\nElapsed between the two BEGIN lines', f'\n\n{inv}\n\nElapsed between the two BEGIN lines', 1)
    return md, census


def parse_spot(s):
    parts = s.split(',', 2)
    if len(parts) < 2:
        raise DumpError(f'--at {s!r}: expected X,Y[,label]')
    try:
        x, y = float(parts[0]), float(parts[1])
    except ValueError:
        raise DumpError(f'--at {s!r}: X and Y must be numbers (cm)')
    if not (math.isfinite(x) and math.isfinite(y)):
        raise DumpError(f'--at {s!r}: X and Y must be finite')
    label = re.sub(r'[\s=|]+', '_', parts[2].strip()) if len(parts) == 3 and parts[2].strip() else f'{x:.0f},{y:.0f}'
    return label, x, y


# ---------------------------------------------------------------- selftest
def selftest() -> int:
    fx = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures', 'layoutdiff_fixture.log')
    with open(fx, encoding='utf-8', errors='replace') as fh:
        text = fh.read().splitlines(True)
    spots = [('camp', 0.0, 0.0), ('far', 400000.0, -400000.0)]
    fails = []
    forbidden = ('re' + 'deal', 'ex' + 'port', 'be' + 'cause')
    want = {'blocks': '1,2', 'seedA': '123456', 'seedB': '123456', 'spots': '2', 'eA': '8', 'eB': '9', 'ePaired': '7',
            'eMoved': '4', 'eFlagOnly': '1', 'eZOnly': '1', 'eRemoved': '1', 'eAdded': '2', 'eAmbiguous': '0', 'wA': '4',
            'wB': '4', 'wMoved': '1', 'wFlagOnly': '1', 'wRemoved': '0', 'wAdded': '0', 'oA': '3', 'oB': '4', 'oFlagOnly': '0',
            'oRemoved': '0', 'oAdded': '1', 'eMovedOut': '1', 'eMovedIn': '1', 'eMovedWithin': '2', 'eRemovedInside': '1',
            'eAddedInside': '1'}
    want_at = {'camp': 'nA=5 nB=5 activeA=3 activeB=4 movedOut=1 movedIn=1 movedWithin=1 removedInside=1 addedInside=1 reconciles=1',
               'far': 'nA=1 nB=1 activeA=1 activeB=1 movedOut=0 movedIn=0 movedWithin=1 removedInside=0 addedInside=0 reconciles=1'}
    try:
        md, census = run(text, fx, None, spots, 100000.0, 100.0, False)
        got = dict(kv.split('=', 1) for kv in census.split(' ')[1:])
        for k, v in want.items():
            if got.get(k) != v:
                fails.append(f'census {k}={got.get(k)} expected {v}')
        if not census.startswith('LAYOUTDIFF: '):
            fails.append('census does not start with the LAYOUTDIFF tag')
        if sum(1 for ln in md.splitlines() if ln.startswith('LAYOUTDIFF: ')) != 1:
            fails.append('report does not hold exactly one line-initial census line')
        for label, tail in want_at.items():
            hit = [ln for ln in md.splitlines() if ln.startswith(f'LAYOUTDIFF-AT: label={label} ')]
            if len(hit) != 1 or not hit[0].endswith(tail):
                fails.append(f'spot {label}: {hit} does not end with {tail!r}')
        if ('| 00012345000000000000000000000000 | Desc_OreIron_C | -30000,-30000,1250 | -30000,-30000,1600 | 0.0 | - |' not in md
                or '| 9ABCDEF0 | Desc_Water_C | 0,0,0 | 300000,-200000,600 | - |' not in md):
            fails.append('Z-only row 00012345 not listed with its from/to, or the src-none well pair printed a distance')
        if 'max 4060.8' not in md:
            fails.append('entry moved-distance max 4060.8 m not in the report')
        if 'elapsed between the two begin lines: 750.5 s' not in md.lower():
            fails.append('elapsed 750.5 s not in the report header')
        for w in forbidden:
            if w in md.lower():
                fails.append(f'forbidden word {w!r} in the generated report')
        md2, c2 = run(text, fx, [2, 1], spots, 100000.0, 100.0, False)
        if 'eRemoved=2 eAdded=1' not in c2 or 'precedes' not in md2:
            fails.append(f'reversed --blocks 2 1 did not swap removed/added or note the order: {c2}')
    except DumpError as e:
        fails.append(f'fixture refused: {e}')
    end2 = [i for i, ln in enumerate(text) if 'LAYOUTDUMP-END: dumpId=2 ' in ln][0]
    beg2 = [i for i, ln in enumerate(text) if 'LAYOUTDUMP-BEGIN: dumpId=2 ' in ln][0]
    e_row = [i for i, ln in enumerate(text) if i > beg2 and 'LAYOUTDUMP-E: i=8 ' in ln][0]
    reseed = [ln.replace('seed=123456', 'seed=999') if i == beg2 else ln for i, ln in enumerate(text)]
    cases = [('one block only', text[:beg2], None, False, 'complete LAYOUTDUMP block'),
             ('block 2 END dropped', text[:end2] + text[end2 + 1:], None, False, 'incomplete'),
             ('block 2 END dropped, --blocks 1 2', text[:end2] + text[end2 + 1:], [1, 2], False, 'incomplete'),
             ('seed changed', reseed, None, False, 'seeds differ'),
             ('countsMatch 0', [ln.replace('countsMatch=1', 'countsMatch=0') if i == end2 else ln for i, ln in enumerate(text)], None, False, 'countsMatch'),
             ('one E row removed', text[:e_row] + text[e_row + 1:], None, False, 'count mismatch'),
             ('--blocks 1 7', text, [1, 7], False, 'no block with dumpId 7')]
    for name, lines, ids, allow, needle in cases:
        try:
            run(lines, fx, ids, spots, 100000.0, 100.0, allow)
            fails.append(f'{name}: accepted, expected a refusal')
        except DumpError as e:
            if needle not in str(e):
                fails.append(f'{name}: refused for another reason: {e}')
            if any(w in str(e).lower() for w in forbidden):
                fails.append(f'{name}: refusal text holds a forbidden word: {e}')
    try:
        md3, _ = run(reseed, fx, None, spots, 100000.0, 100.0, True)
        if 'seeds DIFFER' not in md3:
            fails.append('--allow-seed-change report does not state the seed difference')
    except DumpError as e:
        fails.append(f'--allow-seed-change still refused: {e}')
    for f in fails:
        print('SELFTEST FAIL:', f)
    print(f'SELFTEST {"FAIL" if fails else "PASS"}: census fields {len(want)}, spot lines {len(want_at)}, refusal cases {len(cases)}, '
          f'seed override, reversed order')
    return 1 if fails else 0


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--log', default=DEFAULT_LOG)
    ap.add_argument('--blocks', nargs=2, type=int, metavar=('A', 'B'), help='dumpIds; default: the last two complete blocks')
    ap.add_argument('--at', action='append', default=[], help='X,Y[,label] in cm (repeatable)')
    ap.add_argument('--radius-m', type=float, default=1000.0)
    ap.add_argument('--moved-m', type=float, default=1.0)
    ap.add_argument('--out')
    ap.add_argument('--allow-seed-change', action='store_true')
    ap.add_argument('--selftest', action='store_true')
    a = ap.parse_args(argv)
    if a.selftest:
        return selftest()
    try:
        if not (math.isfinite(a.radius_m) and math.isfinite(a.moved_m)) or a.radius_m <= 0 or a.moved_m < 0:
            raise DumpError('--radius-m must be a finite number above 0 and --moved-m a finite number at least 0')
        spots = [parse_spot(s) for s in a.at]
        with open(a.log, encoding='utf-8', errors='replace') as fh:
            md, census = run(fh, a.log, a.blocks, spots, a.radius_m * 100.0, a.moved_m * 100.0, a.allow_seed_change)
    except (DumpError, OSError) as e:
        print(f'LAYOUT-DIFF REFUSED: {e}', file=sys.stderr)
        return 2
    if a.out:
        with open(a.out, 'w', encoding='utf-8') as fh:
            fh.write(md)
        print(f'wrote {a.out}')
    else:
        print(md)
    print(census)
    return 0


if __name__ == '__main__':
    sys.exit(main())
