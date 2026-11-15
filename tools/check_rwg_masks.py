"""rwg-0-2-0-1 (2026-11-13): OFFLINE check of NodeShuffle's RWG byte masks against an RWG DLL, stdlib only.

Masks and decorated export names are PARSED OUT OF THE SOURCE FILES (never a second hand-typed copy); only the binding
map (which Sym pairs with which Sig in which generation) is written here. The DLL's PE export table is parsed directly,
each mask is applied at the export's file offset, and the TryBind / TryBindCaves verdict is predicted under the rules
the source implements (D1 generation coherence, D4 edge getters required on generation B).

Usage:  python -I tools/check_rwg_masks.py [DLL] [--semver S --build N]   (default DLL: the archived 0.2.0 build 1063)
SemVer/build default to the RandomWorldGenerator.uplugin three levels above the DLL (as TryBind reads it).
Exit 0 = the tool's own assertions and the three controls all hold; 1 = any failed; 2 = a file is missing.
"""
import os, re, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PRIV = os.path.join(ROOT, 'Source', 'NodeShuffle', 'Private')
FILES = {'hapi': 'NodeShuffleMapModeHeightApi.cpp', 'cave': 'NodeShuffleMapModeCave.cpp', 'gate': 'NodeShuffleRwgGateNotice.cpp'}
BASE_COMMIT = 'c47fe27'
DEFAULT_DLL = r'C:/Claude/Projects/_archive/RWG-binaries/0.2.0-build1063/Binaries/Win64/FactoryGameSteam-ProceduralWorld-Win64-Shipping.dll'
SIG_RX = re.compile(r'(?m)^\s*(?:static )?(?:const )?int16 (\w+)\[\] = \{([^}]*)\};')
SYM_RX = re.compile(r'const char\* const ((?:Gate)?Sym\w+) = "([^"]+)";')  # rwg-0-2-0-2 F8: the gate file's copies are Gate-prefixed
FAILS = []


def fail(msg):
    FAILS.append(msg)
    print('ASSERT-FAIL: ' + msg)


def parse_sigs(text):
    out = {}
    for name, body in SIG_RX.findall(text):
        if name in out:
            fail('mask %s declared twice in one file' % name)
        out[name] = [int(x.strip(), 0) for x in body.split(',')]
    return out


def load_sources():
    src = {}
    for k, fn in FILES.items():
        p = os.path.join(PRIV, fn)
        if not os.path.exists(p):
            print('FAIL(load): %s missing' % p)
            sys.exit(2)
        t = open(p, encoding='utf-8').read().replace('\r\n', '\n')
        src[k] = {'text': t, 'sigs': parse_sigs(t), 'syms': dict(SYM_RX.findall(t))}
    return src


# Binding map: (lane, export, group, sym-file, sym-name, mask-file, A-mask, B-mask). B-mask None = shared by both generations;
# A-mask None = generation B only (the edge getters). group: req | opt | cave | edge.
# rwg-0-2-1-1 (2026-11-15): a B-mask may be a TUPLE of fully pinned forms (CarvedCaves 1063/1130, GetState and IsContentSpawned
# 1063/1130). The row matches B when ANY form matches in full; 'form' names the one the source selects (Cave.cpp / GateNotice.cpp: the
# 1130 CarvedCaves array only on a full match, else 1063; the readiness loop: 1063 first, then 1130).
BINDS = [
    ('HeightApi', 'Get', 'req', 'hapi', 'SymGet', 'hapi', 'SigGet', None),
    ('HeightApi', 'GetField', 'req', 'hapi', 'SymGetField', 'hapi', 'SigField', None),
    ('HeightApi', 'GetSpliced', 'req', 'hapi', 'SymGetSpliced', 'hapi', 'SigSpliced', None),
    ('HeightApi', 'SurfaceHeightCm', 'req', 'hapi', 'SymSurface', 'hapi', 'SigSurface', 'SigSurface1063'),
    ('HeightApi', 'IsOutsideWorld', 'req', 'hapi', 'SymOutside', 'hapi', 'SigOutside', None),
    ('HeightApi', 'WaterLevelCmAt', 'req', 'hapi', 'SymWater', 'hapi', 'SigWater', None),
    ('HeightApi', 'HalfWorldCm', 'edge', 'hapi', 'SymHalfWorld', 'hapi', None, 'SigHalfWorld1063'),
    ('HeightApi', 'GridCmValue', 'edge', 'hapi', 'SymGridCm', 'hapi', None, 'SigGridCm1063'),
    ('HeightApi', 'GlobalMinSurfaceCm', 'opt', 'hapi', 'SymGMin', 'hapi', 'SigGMin', None),
    ('HeightApi', 'GlobalMaxSurfaceCm', 'opt', 'hapi', 'SymGMax', 'hapi', 'SigGMax', None),
    ('HeightApi', 'InSeaVolume', 'opt', 'hapi', 'SymInSea', 'hapi', 'SigInSea', 'SigInSea1063'),
    ('HeightApi', 'VoidAt', 'opt', 'hapi', 'SymVoidAt', 'hapi', 'SigVoidAt', None),
    ('HeightApi', 'GetKillboxTop', 'opt', 'hapi', 'SymKillboxTop', 'hapi', 'SigKillboxTop', None),
    ('Cave', 'CarvedCaves', 'cave', 'cave', 'SymCaves', 'cave', 'SigCaves', ('SigCaves1063', 'SigCaves1130')),
    ('Cave', 'Num', 'cave', 'cave', 'SymNum', 'cave', 'SigNum', None),
    ('Cave', 'AnyNear', 'cave', 'cave', 'SymAnyNear', 'cave', 'SigAnyNear', 'SigAnyNear1063'),
    ('Cave', 'FloorCmAt', 'cave', 'cave', 'SymFloor', 'cave', 'SigFloor', 'SigFloor1063'),
    ('Cave', 'IsInsideAt', 'cave', 'cave', 'SymInside', 'cave', 'SigInside', None),
    # rwg-0-2-0-2 item 4: readiness getters, LOG-ONLY group 'ready' -- never part of the verdict (RwgReadinessFields prints -1 when unbound).
    ('Ready', 'GetState', 'ready', 'hapi', 'SymGetState', 'hapi', None, ('SigGetState1063', 'SigGetState1130')),
    ('Ready', 'IsWorldComplete', 'ready', 'hapi', 'SymWorldComplete', 'hapi', None, 'SigWorldComplete1063'),
    ('Ready', 'IsStartReady', 'ready', 'hapi', 'SymStartReady', 'hapi', None, 'SigStartReady1063'),
    ('Ready', 'IsContentSpawned', 'ready', 'hapi', 'SymContentSpawned', 'hapi', None, ('SigContentSpawned1063', 'SigContentSpawned1130')),
]
GATE_COPY = {'SigCaves': 'GateSigCaves', 'SigNum': 'GateSigNum', 'SigAnyNear': 'GateSigAnyNear', 'SigFloor': 'GateSigFloor', 'SigInside': 'GateSigInside',
             'SigCaves1063': 'GateSigCaves1063', 'SigAnyNear1063': 'GateSigAnyNear1063', 'SigFloor1063': 'GateSigFloor1063',
             'SigCaves1130': 'GateSigCaves1130'}  # rwg-0-2-1-1


def pe_exports(data):
    e = struct.unpack_from('<I', data, 0x3C)[0]
    if data[e:e + 4] != b'PE\0\0':
        raise ValueError('not a PE file')
    nsec = struct.unpack_from('<H', data, e + 6)[0]
    optsz = struct.unpack_from('<H', data, e + 20)[0]
    opt = e + 24
    exp_rva, exp_sz = struct.unpack_from('<II', data, opt + 112)
    secs = []
    for i in range(nsec):
        s = opt + optsz + 40 * i
        vsz, va, rsz, raw = struct.unpack_from('<IIII', data, s + 8)
        secs.append((va, max(vsz, rsz), raw))

    def off(rva):
        for va, sz, raw in secs:
            if va <= rva < va + sz:
                return rva - va + raw
        raise ValueError(hex(rva))
    eo = off(exp_rva)
    nfun, nnames, afun, anames, aord = struct.unpack_from('<IIIII', data, eo + 20)
    funcs = [struct.unpack_from('<I', data, off(afun) + 4 * i)[0] for i in range(nfun)]
    exps = {}
    for i in range(nnames):
        o = off(struct.unpack_from('<I', data, off(anames) + 4 * i)[0])
        name = data[o:data.index(b'\0', o)].decode('latin1')
        rva = funcs[struct.unpack_from('<H', data, off(aord) + 2 * i)[0]]
        exps[name] = None if exp_rva <= rva < exp_rva + exp_sz else off(rva)  # forwarded export = unresolvable here
    return exps


def first_diff(data, fo, sig):
    for i, s in enumerate(sig):
        if s >= 0 and data[fo + i] != s:
            return i
    return -1


def wild_readback(data, fo, sig):
    runs, i = [], 0
    while i < len(sig):
        if sig[i] < 0:
            j = i
            while j < len(sig) and sig[j] < 0:
                j += 1
            runs.append('@%d..%d=0x%X' % (i, j - 1, int.from_bytes(data[fo + i:fo + j], 'little')) if j - i > 1 else '@%d=0x%X' % (i, data[fo + i]))
            i = j
        else:
            i += 1
    return ' '.join(runs)


def forms(sb):
    return () if not sb else (sb if isinstance(sb, tuple) else (sb,))


def select_form(data, fo, src, mf, lane, sb):
    """rwg-0-2-1-1: the B form the source applies. Cave lane: the LAST form (1130) only on a full match, else the first (1063) --
    as Cave.cpp / GateNotice.cpp select it. Ready lane: the first form that matches in full, else the first (its diff is reported)."""
    fs = forms(sb)
    if len(fs) == 1:
        return fs[0]
    if lane == 'Cave':
        return fs[-1] if first_diff(data, fo, src[mf]['sigs'][fs[-1]]) == -1 else fs[0]
    return next((f for f in fs if first_diff(data, fo, src[mf]['sigs'][f]) == -1), fs[0])


def evaluate(data, exps, src, listed):
    """Per-export rows + the predicted verdict, mirroring TryBind (HeightApi.cpp) and TryBindCaves (Cave.cpp)."""
    rows = {}
    for lane, nm, grp, sf, sym, mf, sa, sb in BINDS:
        fo = exps.get(src[sf]['syms'][sym])
        r = {'grp': grp, 'resolved': fo is not None, 'dA': None, 'dB': None, 'wild': '', 'form': None}
        if fo is not None:
            if sa:
                r['dA'] = first_diff(data, fo, src[mf]['sigs'][sa])
                r['wild'] = wild_readback(data, fo, src[mf]['sigs'][sa])
            if sb:
                r['form'] = select_form(data, fo, src, mf, lane, sb)
                r['dB'] = first_diff(data, fo, src[mf]['sigs'][r['form']])
                r['wild'] += (' | B ' + wild_readback(data, fo, src[mf]['sigs'][r['form']])) if sa else wild_readback(data, fo, src[mf]['sigs'][r['form']])
        mA, mB = r['dA'] == -1, r['dB'] == -1
        if not r['resolved']:
            r['gen'] = 'missing'
        elif sa and sb:
            r['gen'] = 'both' if (mA and mB) else ('A' if mA else ('B' if mB else 'none'))
        elif sa:
            r['gen'] = 'shared' if mA else 'none'
        else:
            r['gen'] = 'B-only' if mB else 'none'
        rows[nm] = r
    req = ['Get', 'GetField', 'GetSpliced', 'SurfaceHeightCm', 'IsOutsideWorld', 'WaterLevelCmAt']
    if not all(rows[n]['resolved'] for n in req):
        return rows, 'REFUSED exportMissing (required)', None
    gen = rows['SurfaceHeightCm']['gen'] if rows['SurfaceHeightCm']['gen'] in ('A', 'B') else None
    bad = [n for n in req if n != 'SurfaceHeightCm' and rows[n]['gen'] != 'shared'] + ([] if gen else ['SurfaceHeightCm'])
    if gen == 'B':
        if not (rows['HalfWorldCm']['resolved'] and rows['GridCmValue']['resolved']):
            return rows, 'REFUSED exportMissing (generation-B edge getter)', gen
        bad += [n for n in ('HalfWorldCm', 'GridCmValue') if rows[n]['gen'] != 'B-only']
    if bad:
        return rows, 'REFUSED signatureMismatch (required: %s)' % ' '.join(bad), gen
    other = 'A' if gen == 'B' else 'B'
    mixed = [n for n in ('InSeaVolume', 'CarvedCaves', 'AnyNear', 'FloorCmAt') if rows[n]['gen'] == other]
    if mixed:
        return rows, 'REFUSED generationMixed (accepted %s; other-generation match: %s)' % (gen, ' '.join(mixed)), gen
    opt_bad = [n for n in ('GlobalMinSurfaceCm', 'GlobalMaxSurfaceCm', 'VoidAt', 'GetKillboxTop') if rows[n]['resolved'] and rows[n]['gen'] != 'shared']
    opt_bad += ['InSeaVolume'] if rows['InSeaVolume']['resolved'] and rows['InSeaVolume']['gen'] != gen else []
    opt_missing = [n for n in ('GlobalMinSurfaceCm', 'GlobalMaxSurfaceCm', 'InSeaVolume', 'VoidAt', 'GetKillboxTop') if not rows[n]['resolved']]
    cave_ok = lambda n: rows[n]['gen'] == (gen if n in ('CarvedCaves', 'AnyNear', 'FloorCmAt') else 'shared')
    caves = ('CarvedCaves', 'Num', 'AnyNear', 'FloorCmAt', 'IsInsideAt')
    cave_bad = [n for n in caves if rows[n]['resolved'] and not cave_ok(n)]
    cave_missing = [n for n in caves if not rows[n]['resolved']]
    if not listed and (opt_bad or cave_bad or opt_missing or cave_missing):
        return rows, 'REFUSED unlisted strict (optional bad %s missing %s; cave bad %s missing %s)' % (opt_bad, opt_missing, cave_bad, cave_missing), gen
    cave = 'bound' if not cave_bad and not cave_missing else 'off (%s)' % ' '.join(cave_bad + cave_missing)
    dropped = (' optionalDropped=%s' % ' '.join(opt_bad + opt_missing)) if (opt_bad or opt_missing) else ''
    return rows, 'ACCEPTED %s generation=%s caveGroup=%s%s' % ('bound' if listed else 'signatureVerifiedUnlisted', gen, cave, dropped), gen


def readiness(rows):
    rd = [n for lane, n, g, *_ in BINDS if g == 'ready']
    return ' '.join('%s=%s' % (n, ('bound/form%s' % rows[n]['form'][-4:]) if rows[n]['gen'] == 'B-only' else rows[n]['gen']) for n in rd)


def control_d(data, exps, src, listed, rows, verdict, gen):
    """rwg-0-2-1-1 CONTROL (d): for every export with two B forms, patch the bytes where the forms differ (values PARSED from the
    masks) to each form in turn -- the row must match that form -- and then to a value in neither form -- the row must leave B. For
    CarvedCaves the verdict must accept with caves bound on each form and change on the neither value; for the log-only readiness
    getters the READINESS line must change (the verdict never does)."""
    print('CONTROL(d) two-form exports, patched in memory:')
    for lane, nm, grp, sf, sym, mf, sa, sb in BINDS:
        fs = forms(sb)
        fo = exps.get(src[sf]['syms'][sym])
        if len(fs) < 2 or fo is None:
            continue
        masks = [src[mf]['sigs'][f] for f in fs]
        pos = [i for i in range(len(masks[0])) if len(set(m[i] for m in masks)) > 1]
        for f, m in zip(fs, masks):
            mut = bytearray(data)
            for i in pos:
                mut[fo + i] = m[i]
            mr, mv, _ = evaluate(bytes(mut), exps, src, listed)
            print('    %-17s bytes %s -> form %s: generation=%s form=%s verdict: %s' % (nm, pos, f, mr[nm]['gen'], mr[nm]['form'], mv))
            if mr[nm]['gen'] not in ('B', 'B-only') or mr[nm]['form'] != f or mr[nm]['dB'] != -1:
                fail('control (d): %s patched to form %s did not match that form' % (nm, f))
            if grp == 'cave' and 'caveGroup=bound' not in mv:
                fail('control (d): %s on form %s but the cave group is not bound' % (nm, f))
        mut = bytearray(data)
        for i in pos:
            v = masks[0][i] ^ 0xFF
            if any(m[i] == v for m in masks):
                fail('control (d): %s byte %d: the neither value collides with a form' % (nm, i))
            mut[fo + i] = v
        mr, mv, _ = evaluate(bytes(mut), exps, src, listed)
        print('    %-17s bytes %s -> neither form: generation=%s verdict: %s readiness: %s' % (nm, pos, mr[nm]['gen'], mv, readiness(mr)))
        if mr[nm]['gen'] in ('B', 'B-only', 'both'):
            fail('control (d): %s patched to neither form still matches B' % nm)
        if grp == 'cave' and mv == verdict:
            fail('control (d): %s patched to neither form but the verdict did not change' % nm)
        if grp == 'ready' and readiness(mr) == readiness(rows):
            fail('control (d): %s patched to neither form but the READINESS line did not change' % nm)


def static_asserts(src):
    h, c, g = src['hapi'], src['cave'], src['gate']
    for a, b in GATE_COPY.items():
        if c['sigs'].get(a) is None or g['sigs'].get(b) is None or c['sigs'][a] != g['sigs'][b]:
            fail('cave mask copy differs: Cave.cpp %s vs GateNotice.cpp %s' % (a, b))
    for s in ('SymCaves', 'SymNum', 'SymAnyNear', 'SymFloor', 'SymInside'):
        if c['syms'].get(s) is None or c['syms'].get(s) != g['syms'].get('Gate' + s):
            fail('decorated name %s differs between the cave and gate files' % s)
    for lane, nm, grp, sf, sym, mf, sa, sb in BINDS:
        if sym not in src[sf]['syms']:
            fail('%s: %s not declared in %s' % (nm, sym, FILES[sf]))
        for m in ((sa,) if sa else ()) + forms(sb):
            if m not in src[mf]['sigs']:
                fail('%s: mask %s not declared in %s' % (nm, m, FILES[mf]))
        disjoint = lambda A, B: any(i < len(B) and A[i] >= 0 and B[i] >= 0 and A[i] != B[i] for i in range(len(A)))
        if sa and sb and sa in src[mf]['sigs']:  # the two generations must be DISJOINT: some byte pinned in both with different values
            for fb in forms(sb):
                if fb in src[mf]['sigs'] and not disjoint(src[mf]['sigs'][sa], src[mf]['sigs'][fb]):
                    fail('%s: generation A and B (%s) masks are not disjoint (one DLL could match both)' % (nm, fb))
        fs = [f for f in forms(sb) if f in src[mf]['sigs']]
        for i in range(len(fs)):  # rwg-0-2-1-1: two forms of one export must differ at a pinned byte, carry no wildcard, and share a length
            if len(fs) > 1 and any(v < 0 for v in src[mf]['sigs'][fs[i]]):
                fail('%s: form %s carries a wildcard (each form is fully pinned)' % (nm, fs[i]))
            for j in range(i + 1, len(fs)):
                A, B = src[mf]['sigs'][fs[i]], src[mf]['sigs'][fs[j]]
                if len(A) != len(B) or not disjoint(A, B):
                    fail('%s: forms %s and %s are not distinct same-length masks' % (nm, fs[i], fs[j]))
    # (c) generation-A arrays byte-identical to their declaration text at the base commit (no 705/745/802 DLL exists here).
    for k, fn in FILES.items():
        try:
            old = subprocess.run(['git', '-C', ROOT, 'show', '%s:Source/NodeShuffle/Private/%s' % (BASE_COMMIT, fn)], capture_output=True, text=True, check=True).stdout
        except Exception as ex:  # noqa: BLE001 -- report and count, never hide
            fail('control (c): git show %s:%s failed: %s' % (BASE_COMMIT, fn, ex))
            continue
        old_decl = {n: b.strip() for n, b in SIG_RX.findall(old.replace('\r\n', '\n'))}
        new_decl = {n: b.strip() for n, b in SIG_RX.findall(src[k]['text'])}
        cur = lambda n: new_decl[n] if n in new_decl else new_decl.get('Gate' + n)  # rwg-0-2-0-2 F8: gate copies renamed Gate*
        same = [n for n in old_decl if cur(n) == old_decl[n]]
        diff = [n for n in old_decl if cur(n) != old_decl[n]]
        print('CONTROL(c) %s: %d generation-A arrays at %s, %d byte-identical now, changed=%s' % (fn, len(old_decl), BASE_COMMIT, len(same), diff or 'none'))
        if diff:
            fail('control (c): generation-A arrays changed in %s: %s' % (fn, diff))


def main():
    args = sys.argv[1:]
    dll = next((a for a in args if not a.startswith('--') and (args.index(a) == 0 or not args[args.index(a) - 1].startswith('--'))), DEFAULT_DLL)
    opt = {args[i][2:]: args[i + 1] for i in range(len(args) - 1) if args[i].startswith('--')}
    if not os.path.exists(dll):
        print('FAIL(load): DLL %s missing' % dll)
        sys.exit(2)
    src = load_sources()
    static_asserts(src)
    up = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(dll)))), 'RandomWorldGenerator.uplugin')
    semver, build = opt.get('semver'), int(opt['build']) if 'build' in opt else None
    if os.path.exists(up) and (semver is None or build is None):
        u = open(up, encoding='utf-8', errors='replace').read()
        semver = semver or re.search(r'"SemVersion"\s*:\s*"([^"]*)"', u).group(1)
        build = build if build is not None else int(re.search(r'"PWBuildNumber"\s*:\s*(\d+)', u).group(1))
    acc = re.search(r'RwgHeightApiAccepted\[\] = \{(.*)\};', src['hapi']['text']).group(1)
    accepted = [(s, int(b)) for s, b in re.findall(r'\{ TEXT\("([^"]+)"\), (\d+) \}', acc)]
    listed = (semver, build) in accepted
    data = open(dll, 'rb').read()
    exps = pe_exports(data)
    print('DLL %s  exports=%d  semVer=%s build=%s  listed=%d  accepted=%s' % (dll, len(exps), semver, build, int(listed), accepted))
    rows, verdict, gen = evaluate(data, exps, src, listed)
    for lane, nm, grp, *_ in BINDS:
        r = rows[nm]
        print('  %-9s %-19s %-4s resolved=%d generation=%-7s firstDiffA=%-4s firstDiffB=%-4s wild[%s]' % (lane, nm, grp, int(r['resolved']), r['gen'], r['dA'], r['dB'], r['wild']))
    print('PREDICTED (listed=%d): %s' % (int(listed), verdict))
    rd = [n for lane, n, g, *_ in BINDS if g == 'ready']
    print('READINESS getters (log-only): %s' % readiness(rows))
    print('PREDICTED (as unlisted): %s' % evaluate(data, exps, src, False)[1])
    # CONTROL (a): one pinned byte flipped per export, in memory; the export must leave its matched generation.
    print('CONTROL(a) one pinned byte flipped per export (in-memory copy):')
    for lane, nm, grp, sf, sym, mf, sa, sb in BINDS:
        fo = exps.get(src[sf]['syms'][sym])
        mask = src[mf]['sigs'][rows[nm]['form'] if (sb and rows[nm]['gen'] in ('B', 'B-only')) else (sa or forms(sb)[0])]
        if fo is None:
            continue
        pos = next(i for i, s in enumerate(mask) if s >= 0)
        mut = bytearray(data)
        mut[fo + pos] ^= 0xFF
        mrows, mverdict, _ = evaluate(bytes(mut), exps, src, listed)
        moved = mrows[nm]['gen'] != rows[nm]['gen']
        print('    %-19s byte %2d -> generation %s->%s  verdict: %s' % (nm, pos, rows[nm]['gen'], mrows[nm]['gen'], mverdict))
        if not moved:
            fail('control (a): mutating %s byte %d did not change its generation match' % (nm, pos))
        if grp in ('req', 'edge') and not mverdict.startswith('REFUSED'):
            fail('control (a): required %s mutated but the verdict is still accepted' % nm)
        if grp in ('opt', 'cave') and mverdict == verdict:
            fail('control (a): %s mutated but the predicted verdict did not change' % nm)
    # CONTROL (b): mixed generation -- CarvedCaves' disp8 patched to the generation-A value 0x28.
    fo = exps.get(src['cave']['syms']['SymCaves'])
    if fo is not None and gen == 'B':
        mut = bytearray(data)
        mut[fo + 3] = 0x28
        mv = evaluate(bytes(mut), exps, src, listed)[1]
        print('CONTROL(b) CarvedCaves disp8 0x%02X -> 0x28: %s' % (data[fo + 3], mv))
        if 'generationMixed' not in mv:
            fail('control (b): a mixed-generation DLL was not refused as generationMixed')
    else:
        print('CONTROL(b) skipped: needs an accepted generation-B DLL')
    if gen == 'B':
        control_d(data, exps, src, listed, rows, verdict, gen)
    else:
        print('CONTROL(d) skipped: needs an accepted generation-B DLL')
    print('RESULT: %s (%d assertion failure(s))' % ('PASS' if not FAILS else 'FAIL', len(FAILS)))
    sys.exit(1 if FAILS else 0)


if __name__ == '__main__':
    main()
