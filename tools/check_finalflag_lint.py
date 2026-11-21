"""ns-finalflag-1 lint: FNodeShuffleEntry's final-placement flag has ONE writer, three call sites, and the designed predicate.

Rules (one id per failure):
  F1  WRITER     exactly one assignment of the entry flag and one of the epoch in Source, both inside ReconcilePlacementFinal; no
                 reference or address taken of either; the two names appear only in the subsystem header, the reconcile file and the
                 consumer census file.
  F2  CALLS      exactly three call lines: the first statement of ApplyLayout (ApplyStart), the line before EmitConsumerCensus inside
                 ApplyLayout (ApplyEnd), inside PostLoadGame_Implementation (Seed); ApplyLayout has no return.
  F3  FIELDS     the entry flag is UPROPERTY(SaveGame) bool = false right after bRwgProvisional; the epoch is UPROPERTY() int32 = 0 in the
                 subsystem; the stage enum is file scope.
  F4  LOGS       FINALSEED / FINALMOVED / FINALCENSUS format strings exactly as designed, once each, each statement on its own line,
                 and the reconcile file names no diagnostics guard.
  F5  DISTANCE   one 3-D strict test against a 200 cm constant; no 2-D distance anywhere in the file; the FINALMOVED cap is 16.
  F6  PREDICATE  the row half carries the four-way vanilla/RWG clause; other map modes return before the epoch moves.
  F7  DWELL      the dwell test of 3 reconciles guards the set branch.
  F8  SIZE/MCP   the reconcile file is under 500 lines; no MCP file names the reconcile, the flag or the epoch (decision 7).
  F9  READ-ONLY  the reconcile file assigns no member other than the flag, holds one mutable entry reference (the loop), calls no
                 mover/spawner/destroyer, and keeps its contract comment.
  F10 MARKER     finalflag-1 (newest until ns-rwg022-list-1) and quietlog-1 stay listed; finalflag-1 once in NodeShuffle.cpp; no marker literal in
                 the new file.
  F11 NSCONSUMER the census format keeps its old field order and appends the two fields; the change test holds 9 slots.
  F12 DOCS       docs/DIAGNOSTICS.md carries the three lines, the contract names, the not-promised list and the LOADED sentence.
  F13 TOOLS      the quietlog TSV, the quietlog lint (Q8 SaveGame), and the rotated marker pins in mcpcontract/scanfilter/rwg020.
  F14 GUARDS     the decision-5 sticky test is exact; ApplyLayout has one caller (RefreshTick) and RefreshTick calls no mover after it.
Needles are built by concatenation so this file cannot match itself. Run: python tools/check_finalflag_lint.py [--no-mutants]
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = os.path.join(REPO, 'Source')
FLAG = 'bPlacement' + 'Final'
EPOCH = 'PlacementFinal' + 'Epoch'
RECON = 'Reconcile' + 'PlacementFinal'
STAGE = 'ENs' + 'FinalStage::'
MARK = '2026-11-20-' + 'finalflag-1'
PREV = '2026-11-20-' + 'quietlog-1'
GUARD = 'Ns' + 'Diag'
ALLOWED_MENTION = ('Public/NodeShuffleSubsystem.h', 'Private/NodeShuffleFinalFlag.cpp', 'Private/NodeShuffleConsumerCensus.cpp')
TOOLS = {'tsv': 'quietlog-sites.tsv', 'ql': 'check_quietlog_lint.py', 'mcp': 'check_mcpcontract_lint.ps1',
         'sf': 'check_scanfilter_lint.ps1', 'g20': 'check_rwg020_lint.ps1', 'doc': os.path.join('..', 'docs', 'DIAGNOSTICS.md')}

FMT_SEED = 'FINAL' + 'SEED: flaggedInSave=%d'
FMT_MOVED = ('FINAL' + 'MOVED: stage=%s applyPass=%d guid=%s res=%s anchor=%s now=%s actorAt=%s rowDeltaCm=%.0f actorDeltaCm=%.0f '
             'predicateNow=%d pinned=%d rayCasted=%d provisional=%d active=%d')
FMT_CENSUS = ('FINAL' + 'CENSUS: stage=%s epoch=%d applyPass=%d mode=%s rows=%d checked=%d kept=%d revokedRowMoved=%d '
              'revokedActorOff=%d revokedActorNotFound=%d revokedPredicate=%d noAnchor=%d newlyFinal=%d dwellHeld=%d finalNow=%d '
              'armPinned=%d armNewRow=%d armOriginal=%d linesSuppressed=%d stickyUnresolved=%d')
FMT_CONSUMER = ('NS' + 'CONSUMER: pass=%d layoutEntries=%d active=%d newNode=%d newNodeNotRayCasted=%d activeNewNodeNotRayCasted=%d '
                'pinned=%d spawnedNodes=%d placementFinal=%d finalEpoch=%d')


def strip_comments(t):
    out = []; i = 0; n = len(t); st = None
    while i < n:
        c = t[i]
        if st is None:
            if t.startswith('//', i):
                j = t.find('\n', i); j = n if j < 0 else j
                out.append(' ' * (j - i)); i = j; continue
            if t.startswith('/*', i):
                j = t.find('*/', i + 2); j = n if j < 0 else j + 2
                out.append(re.sub(r'[^\n]', ' ', t[i:j])); i = j; continue
            if c == '"': st = '"'
            out.append(c); i += 1; continue
        if c == '\\':
            out.append(t[i:i + 2]); i += 2; continue
        if c == st or c == '\n': st = None
        out.append(c); i += 1
    return ''.join(out)


def blank_strings(t):
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"', t)


def body_of(code, signature):
    """(start, end) of the brace body of the function whose definition text starts with signature; (-1, -1) when absent."""
    k = code.find(signature)
    if k < 0: return -1, -1
    s = blank_strings(code); b = s.find('{', k); d = 0; i = b
    while i < len(s):
        if s[i] == '{': d += 1
        elif s[i] == '}':
            d -= 1
            if d == 0: return b, i + 1
        i += 1
    return -1, -1


def load():
    files = {}
    for dp, _, fns in os.walk(SRC):
        for fn in sorted(fns):
            if fn.endswith(('.cpp', '.h')):
                p = os.path.join(dp, fn)
                files[os.path.relpath(p, SRC).replace('\\', '/')] = open(p, encoding='utf-8', errors='replace').read().replace('\r\n', '\n')
    tools = {k: open(os.path.join(HERE, v), encoding='utf-8', errors='replace').read().replace('\r\n', '\n') for k, v in TOOLS.items()}
    return files, tools


def one(files, suffix):
    m = [r for r in files if r.endswith(suffix)]
    return m[0] if len(m) == 1 else None


WRITE_RX = lambda name: re.compile(r'(?:\b' + name + r'\s*(?:=(?!=)|\|=|&=|\^=|\+=|-=|\+\+|--))|(?:(?:\+\+|--)\s*(?:[\w\]\)]+\s*(?:\.|->)\s*)*' + name + r'\b)')
REF_RX = lambda name: re.compile(r'(?:(?<!&)&(?!&)\s*\w+\s*=\s*[^;]*\b' + name + r'\b)|(?:(?<!&)&(?!&)\s*\(?\s*[\w\]\)]+\s*(?:\.|->)\s*' + name + r'\b)')


def check(files, tools):
    f = []
    hdr = one(files, 'Public/NodeShuffleSubsystem.h'); ff = one(files, 'Private/NodeShuffleFinalFlag.cpp')
    sub = one(files, 'Private/NodeShuffleSubsystem.cpp'); cc = one(files, 'Private/NodeShuffleConsumerCensus.cpp')
    nsm = one(files, 'NodeShuffle/Private/NodeShuffle.cpp'); mk = one(files, 'Private/NodeShuffleBuildMarkers.h')
    for k, v in (('header', hdr), ('reconcile file', ff), ('subsystem cpp', sub), ('consumer census', cc), ('module cpp', nsm), ('markers header', mk)):
        if not v: f.append('(F0) %s missing' % k)
    if f: return f
    code = {r: strip_comments(t) for r, t in files.items()}
    cs = {r: blank_strings(c) for r, c in code.items()}
    # ---- F1 writer
    decl_flag = 'UPROPERTY(Save' + 'Game) bool ' + FLAG + ' = false;'
    decl_epoch = 'UPROPERTY() int32 ' + EPOCH + ' = 0;'
    rb, re_ = body_of(code[ff], 'void ANodeShuffleSubsystem::' + RECON + '(' + 'ENs' + 'FinalStage Stage)')
    if rb < 0: f.append('(F1) the reconcile definition is missing'); return f
    for name, decl, want in ((FLAG, decl_flag, 'E.' + FLAG + ' = bNext;'), (EPOCH, decl_epoch, '++' + EPOCH + ';')):
        hits = []
        for r, c in cs.items():
            scan = c.replace(decl, ' ' * len(decl)) if r == hdr else c
            for m in WRITE_RX(name).finditer(scan): hits.append((r, m.start()))
            if REF_RX(name).search(scan): f.append('(F1) a reference or address of %s is taken in %s' % (name, r))
            if re.search(r'\b' + name + r'\b', scan) and not r.endswith(ALLOWED_MENTION): f.append('(F1) %s named outside the allowed files: %s' % (name, r))
        if len(hits) != 1 or hits[0][0] != ff or not (rb < hits[0][1] < re_):
            f.append('(F1) exactly one write of %s, inside the reconcile (found %d: %s)' % (name, len(hits), ', '.join(sorted(set(h[0] for h in hits)))))
        if code[ff][rb:re_].count(want) != 1: f.append('(F1) the one %s write must read exactly as designed' % name)
    # ---- F2 calls
    calls = [(r, m.start()) for r, c in cs.items() for m in re.finditer(r'\b' + RECON + r'\s*\(\s*' + re.escape(STAGE), c)]
    if len(calls) != 3 or any(r != sub for r, _ in calls): f.append('(F2) exactly three call lines, all in NodeShuffleSubsystem.cpp (found %d)' % len(calls))
    c = code[sub]
    if not re.search(r'void ANodeShuffleSubsystem::ApplyLayout\(\)\s*\{\s*' + RECON + r'\(' + re.escape(STAGE) + r'ApplyStart\);', c):
        f.append('(F2) ApplyStart must be the first statement of ApplyLayout')
    ab, ae = body_of(c, 'void ANodeShuffleSubsystem::ApplyLayout()')
    m = re.search(RECON + r'\(' + re.escape(STAGE) + r'ApplyEnd\);\s*EmitConsumerCensus\(\);', c)
    if ab < 0 or not m or not (ab < m.start() < ae) or not re.match(r'\s*\}\s*$', c[m.end():ae]):
        f.append('(F2) ApplyEnd must be the line before EmitConsumerCensus at the end of ApplyLayout')
    if ab >= 0 and re.search(r'\breturn\b', cs[sub][ab:ae]): f.append('(F2) ApplyLayout must have no return (the ApplyEnd reconcile would be skipped)')
    pb, pe = body_of(c, 'void ANodeShuffleSubsystem::PostLoadGame_Implementation(')
    if pb < 0 or c[pb:pe].count(RECON + '(' + STAGE + 'Seed);') != 1: f.append('(F2) Seed must be called once inside PostLoadGame_Implementation')
    # ---- F3 fields
    h = code[hdr]
    if not re.search(r'UPROPERTY\(SaveGame\) bool bRwgProvisional = false;\s*' + re.escape(decl_flag) + r'\s*\};', h):
        f.append('(F3) the entry flag must be UPROPERTY(SaveGame) bool = false, appended after bRwgProvisional, last in the struct')
    if h.count(decl_epoch) != 1: f.append('(F3) the epoch must be one UPROPERTY() int32 = 0')
    if not re.search(r'\n(?:// [^\n]*\n)*enum class ENs' + r'FinalStage : uint8 \{ Seed, ApplyStart, ApplyEnd \};', h): f.append('(F3) the stage enum must be file scope')
    # ---- F4 logs
    raw = files[ff]
    for fmt in (FMT_SEED, FMT_MOVED, FMT_CENSUS):
        n = sum(t.count('"' + fmt + '"') for t in files.values())
        if n != 1: f.append('(F4) format string %s... must exist exactly once, as designed (found %d)' % (fmt[:12], n))
        tag = fmt.split(':')[0]
        if not re.search(r'\n[ \t]*UE_' + r'LOG\(LogNodeShuffle, Display, TEXT\("' + tag + ':', raw): f.append('(F4) %s must be its own statement on its own line' % tag)
    if GUARD in code[ff] or ('AreDiagnostics' + 'Enabled') in code[ff]: f.append('(F4) the reconcile file must not name a diagnostics guard (its lines are always on)')
    # ---- F5 distance
    w = code[ff]
    if w.count('FVector::DistSquared(A, B) < FMath::Square(ToleranceCm)') != 1 or w.count('static constexpr float ToleranceCm = 200.0f;') != 1:
        f.append('(F5) one strict 3-D test against ToleranceCm = 200.0f')
    if re.search(r'2D\s*\(', w) or 'Size2D' in w: f.append('(F5) no 2-D distance in the reconcile file')
    if w.count('Within(') < 3: f.append('(F5) the row and actor tests must go through Within')
    if w.count('static constexpr int32 MovedLineCap = 16;') != 1: f.append('(F5) the FINALMOVED cap must be 16')
    # ---- F6 predicate
    body = w[rb:re_]
    clause = '(bVanilla || E.bRwgSettledOnTerrain || E.bUnderground || E.bRwgCave)'
    rowbits = ('const bool bRowBits = E.bActive && (E.bPinned || (E.bIsNewNode && E.bRayCasted && !E.bRwgProvisional\n            && '
               + clause + '));')
    if body.count(rowbits) != 1 or w.count(clause) != 1: f.append('(F6) the row half must carry the four-way clause exactly once')
    if body.count('const bool bVanilla = Mode == ENodeShuffleMapMode::Vanilla;') != 1: f.append('(F6) bVanilla must be the map-mode test')
    gate = 'if (Mode != ENodeShuffleMapMode::Vanilla && Mode != ENodeShuffleMapMode::Rwg) { return; }'
    if body.count(gate) != 1 or body.find(gate) > body.find('++' + EPOCH): f.append('(F6) other map modes must return before the epoch moves')
    if body.find(gate) < body.find('Stage == ' + STAGE + 'Seed'): f.append('(F6) Seed must run before the map-mode gate')
    if body.count('const bool bActorBelow = Actor && NsFinal::Within(Actor->GetActorLocation(), E.Location);') != 1: f.append('(F6) ActorBelow2m must be the 3-D Within test')
    # ---- F7 dwell
    dw = 'if (RevokedAt && Recon - *RevokedAt < NsFinal::DwellReconciles) { ++C[9]; }\n            else { bNext = true; ++C[8]; }'
    if body.count(dw) != 1 or w.count('static constexpr int32 DwellReconciles = 3;') != 1 or body.count('bNext = true;') != 1:
        f.append('(F7) the only set must sit behind the dwell test of 3 reconciles')
    # ---- F8 size / MCP
    if files[ff].count('\n') >= 500: f.append('(F8) the reconcile file must stay under 500 lines')
    for r, t in cs.items():
        if '/NodeShuffleMcp' in '/' + r.split('/')[-1] and any(x in t for x in (RECON, FLAG, EPOCH)): f.append('(F8) MCP file %s names the flag (decision 7)' % r)
    # ---- F9 read-only
    for m in re.finditer(r'([\w\]\)]+(?:\s*(?:\.|->)\s*\w+)+)\s*(?:=(?!=)|\+=|-=|\*=|/=|\|=|&=|\^=|\+\+|--)|(?:\+\+|--)\s*([\w\]\)]+(?:\s*(?:\.|->)\s*\w+)+)', cs[ff]):
        lhs = re.sub(r'\s+', '', m.group(1) or m.group(2))
        if lhs != 'E.' + FLAG: f.append('(F9) the reconcile file assigns a member other than the flag: ' + lhs)
    if len(re.findall(r'(?<!const )FNodeShuffleEntry\s*&', cs[ff])) != 1 or cs[ff].count('for (FNodeShuffleEntry& E : Layout)') != 1: f.append('(F9) one mutable entry reference only: the loop')
    for bad in ('SetActor' + 'Location', 'SetWorld' + 'Location', 'Teleport', 'Destroy(', 'Spawn' + 'Actor', 'RollLayout(', 'ApplyLayout(',
                'Layout.Add', 'Layout.Remove', 'Layout.Empty', 'Layout.Reset', 'Layout.Insert', 'SpawnedNodes.Add', 'SpawnedNodes.Remove',
                'SpawnedNodes.Empty', 'SpawnedNodes.Reset', 'MoveTemp(', 'Swap('):
        if bad in cs[ff]: f.append('(F9) the reconcile file calls ' + bad)
    if 'moves, spawns, destroys and re-rolls NOTHING, and writes no entry field other than' not in raw.split('#include')[0]:
        f.append('(F9) the contract comment at the top of the reconcile file is missing')
    # ---- F10 marker
    tags = re.findall(r'TEXT\("([\w-]+)"\)', re.search(r'PacketMarkers\[\]\s*=\s*\{(.*?)\};', code[mk], re.S).group(1)) if re.search(r'PacketMarkers\[\]\s*=\s*\{(.*?)\};', code[mk], re.S) else []
    # ns-rwg022-list-1 bumped the newest marker: finalflag-1 is pinned LISTED, no longer newest (the newest is pinned by mcpcontract C4).
    if not tags or tags.count(MARK) != 1 or tags.count(PREV) != 1: f.append('(F10) finalflag-1 and quietlog-1 must each stay listed once in the header')
    for t in (MARK, PREV):
        if files[nsm].count('packet marker (' + t + ')=====') != 1: f.append('(F10) marker %s once on its own line in NodeShuffle.cpp' % t)
    for k in (ff, cc):
        if MARK in files[k]: f.append('(F10) no marker literal in ' + k)
    # ---- F11 NSCONSUMER
    if files[cc].count('"' + FMT_CONSUMER + '"') != 1 or 'Now[6], Now[7], ' + EPOCH + ');' not in code[cc] or 'static int32 Last[9]' not in code[cc] \
            or 'for (int32 i = 0; i < 9; ++i)' not in code[cc] or EPOCH + ' > 0 ? 1 : 0 };' not in code[cc]:
        f.append('(F11) NSCONSUMER keeps its field order, appends placementFinal/finalEpoch, and compares 9 slots (epoch as above-0 only)')
    # ---- F12 docs
    d = tools['doc']
    for need in ('`FINALSEED`', '`FINALMOVED`', '`FINALCENSUS`', '`' + FLAG + '`', '`' + EPOCH + '`', 'Not promised', 'stickyUnresolved',
                 'LAST `packet marker (` line'):
        if need not in d: f.append('(F12) docs/DIAGNOSTICS.md must carry ' + need)
    # ---- F13 tools
    for tag in ('FINALSEED', 'FINALMOVED', 'FINALCENSUS'):
        if not re.search(r'^NodeShuffleFinalFlag\.cpp\|Display\|' + tag + r'\|1\tALWAYS\t\t', tools['tsv'], re.M): f.append('(F13) TSV row %s must be class ALWAYS' % tag)
    if "SAVEGAME_REQUIRED = ('FNodeShuffleEntry', 'bPlacement' + 'Final')" not in tools['ql'] or "'PlacementFinal' + 'Epoch')" not in tools['ql']:
        f.append('(F13) quietlog Q8 must pin both names and the SaveGame specifier')
    own = "'2026-11-20-' + 'rwg022-list-1'"  # ns-rwg022-list-1 rotation
    if ('$own = ' + own) not in tools['mcp'] or own not in tools['sf'] or own not in tools['g20']: f.append('(F13) mcpcontract C4 / scanfilter S9 / rwg020 G9 must name rwg022-list-1 as newest')
    # ---- F14 (cold review): decision-5 sticky test exact; one ApplyLayout caller; no mover after it in RefreshTick
    if body.count('const bool bSticky = !Actor && !E.bIsNew' + 'Node && E.bPinned;') != 1:
        f.append('(F14) the decision-5 sticky test must read: no actor, an original (not a new row), pinned')
    alc = [(r, mm.start()) for r, t in cs.items() if r.endswith('.cpp') for mm in re.finditer(r'(?<![:\w])' + 'Apply' + r'Layout\s*\(\s*\)\s*;', t)]
    tb, te = body_of(code[sub], 'void ANodeShuffleSubsystem::Refresh' + 'Tick()')
    if len(alc) != 1 or alc[0][0] != sub or not (tb < alc[0][1] < te):
        f.append('(F14) ApplyLayout must have exactly one caller, inside RefreshTick (found %d)' % len(alc))
    else:
        tail = cs[sub][alc[0][1]:te]
        for bad in ('EnsureNew' + 'NodeSpawned(', 'Settle' + 'NewNodesNearPlayers(', 'Roll' + 'Layout(', 'RwgSpawn' + 'Pass(', 'Raycast' + 'Settle(',
                    'Try' + 'Redeal', 'RwgRedeal' + 'LiveEntry(', 'SetActor' + 'Location', 'Destroy' + '(', 'AdoptRestored' + 'SpawnedNodes(',
                    'CaptureLate' + 'OriginalNodes(', 'Restore' + 'OriginalsForReroll('):
            if bad in tail: f.append('(F14) RefreshTick calls a mover after ApplyLayout: ' + bad)
    return f


def mut(name, key, find, repl, rule):
    return (name, key, find, repl, rule)


SUB = 'Private/NodeShuffleSubsystem.cpp'; FF = 'Private/NodeShuffleFinalFlag.cpp'; HDR = 'Public/NodeShuffleSubsystem.h'
MUTANTS = [
    mut('N1 ApplyStart call dropped', SUB, '    ' + RECON + '(' + STAGE + 'ApplyStart);', '   ', 'F2'),
    mut('N2 ApplyEnd call dropped', SUB, '    ' + RECON + '(' + STAGE + 'ApplyEnd);', '   ', 'F2'),
    mut('N3 Seed call dropped', SUB, '    ' + RECON + '(' + STAGE + 'Seed);', '   ', 'F2'),
    mut('N4 second flag write in RollLayout', SUB, 'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)\n{\n',
        'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)\n{\n    for (FNodeShuffleEntry& X : Layout) { X.' + FLAG + ' = false; }\n', 'F1'),
    mut('N5 tolerance 200 -> 300', FF, 'ToleranceCm = 200.0f;', 'ToleranceCm = 300.0f;', 'F5'),
    mut('N6 3-D -> 2-D', FF, 'FVector::DistSquared(A, B)', 'FVector::DistSquared' + '2D(A, B)', 'F5'),
    mut('N7 RWG clause dropped', FF, '\n            && (bVanilla || E.bRwgSettledOnTerrain || E.bUnderground || E.bRwgCave)));', '));', 'F6'),
    mut('N8 settled term dropped', FF, 'bVanilla || E.bRwgSettledOnTerrain || ', 'bVanilla || ', 'F6'),
    mut('N9 dwell dropped', FF, 'if (RevokedAt && Recon - *RevokedAt < NsFinal::DwellReconciles) { ++C[9]; }\n            else { bNext = true; ++C[8]; }',
        '(void)RevokedAt; bNext = true; ++C[8];', 'F7'),
    mut('N10 FINALMOVED behind a topic guard', FF, '                    UE_' + 'LOG(LogNodeShuffle, Display, TEXT("FINAL' + 'MOVED:',
        '                    if (' + GUARD + '(ENs' + 'Diag::Placement)) UE_' + 'LOG(LogNodeShuffle, Display, TEXT("FINAL' + 'MOVED:', 'F4'),
    mut('N11 SaveGame dropped', HDR, 'UPROPERTY(Save' + 'Game) bool ' + FLAG, 'UPROPERTY() bool ' + FLAG, 'F3'),
    mut('N12 reconcile writes another entry field', FF, '            E.' + FLAG + ' = bNext;', '            E.' + FLAG + ' = bNext;\n            E.bActive = true;', 'F9'),
    mut('N13 second epoch write', SUB, 'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)\n{\n',
        'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)\n{\n    ' + EPOCH + ' = 0;\n', 'F1'),
    mut('N14 write through a reference', SUB, 'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)\n{\n',
        'void ANodeShuffleSubsystem::RollLayout(int32 Seed, bool bIsReroll)\n{\n    bool& Fz = Layout[0].' + FLAG + '; Fz = false;\n', 'F1'),
    mut('N15 map-mode gate dropped', FF, '    if (Mode != ENodeShuffleMapMode::Vanilla && Mode != ENodeShuffleMapMode::Rwg) { return; }', '', 'F6'),
    mut('N16 reconcile moves an actor', FF, '            E.' + FLAG + ' = bNext;', '            E.' + FLAG + ' = bNext;\n            if (Actor) { const_cast<AActor*>(Actor)->SetActor' + 'Location(E.Location); }', 'F9'),
    mut('N17 NSCONSUMER field order changed', 'Private/NodeShuffleConsumerCensus.cpp', 'pinned=%d spawnedNodes=%d', 'spawnedNodes=%d pinned=%d', 'F11'),
    mut('N18 MCP snapshot reads the flag', 'Private/NodeShuffleMcpSnapshot.cpp', '#include', 'static bool NsFz(const FNodeShuffleEntry& E) { return E.' + FLAG + '; }\n#include', 'F1'),
    mut('N19 ApplyEnd after EmitConsumerCensus', SUB, '    ' + RECON + '(' + STAGE + 'ApplyEnd);', '   ', 'F2'),
    mut('N20 strict < relaxed to <=', FF, 'FVector::DistSquared(A, B) < FMath', 'FVector::DistSquared(A, B) <= FMath', 'F5'),
    mut('N25 sticky widened to new rows', FF, 'const bool bSticky = !Actor && !E.bIsNew' + 'Node && E.bPinned;', 'const bool bSticky = !Actor && E.bPinned;', 'F14'),
    mut('N26 mover after ApplyLayout', SUB, '        ApplyLayout();\n', '        ApplyLayout();\n        SettleNewNodesNearPlayers();\n', 'F14'),
    mut('N27 second ApplyLayout caller', SUB, '        RollLayout(RerollSeed, true);\n', '        RollLayout(RerollSeed, true);\n        ApplyLayout();\n', 'F14'),
]


def run_mutants(files, tools):
    caught = 0; miss = []
    for name, suffix, find, repl, rule in MUTANTS:
        tgt = [r for r in files if r.endswith(suffix)]
        if len(tgt) != 1 or find not in files[tgt[0]]:
            miss.append('%s: find text absent (mutant cannot apply)' % name); continue
        mf = dict(files); mf[tgt[0]] = files[tgt[0]].replace(find, repl, 1)
        if name.startswith('N19'):  # also re-insert it after the census call, so the count stays 3 and only the position rule can catch it
            mf[tgt[0]] = mf[tgt[0]].replace('    EmitConsumerCensus();', '    EmitConsumerCensus();\n    ' + RECON + '(' + STAGE + 'ApplyEnd);', 1)
        fails = check(mf, tools)
        if any(x.startswith('(' + rule + ')') for x in fails): caught += 1
        else: miss.append('%s: rule %s stayed green' % (name, rule))
    for name, key, find, repl, rule in (
            ('N21 DIAGNOSTICS drops the not-promised list', 'doc', 'Not promised', 'Not listed', 'F12'),
            ('N22 TSV row reclassified', 'tsv', 'FINAL' + 'MOVED|1\tALWAYS', 'FINAL' + 'MOVED|1\tTOPIC', 'F13'),
            ('N23 quietlog Q8 SaveGame pin removed', 'ql', "SAVEGAME_REQUIRED = ('FNodeShuffleEntry', 'bPlacement' + 'Final')", "SAVEGAME_REQUIRED = ('', '')", 'F13'),
            ('N24 mcpcontract C4 not rotated', 'mcp', "$own = '2026-11-20-' + 'rwg022-list-1'", "$own = '2026-11-20-' + 'finalflag-1'", 'F13')):
        if find not in tools[key]: miss.append('%s: find text absent (mutant cannot apply)' % name); continue
        mt = dict(tools); mt[key] = tools[key].replace(find, repl, 1)
        if any(x.startswith('(' + rule + ')') for x in check(files, mt)): caught += 1
        else: miss.append('%s: rule %s stayed green' % (name, rule))
    return caught, len(MUTANTS) + 4, miss


def main():
    files, tools = load()
    fails = check(files, tools)
    for x in fails: print('FAIL ' + x)
    caught = total = 0; miss = []
    if '--no-mutants' not in sys.argv and not fails:
        caught, total, miss = run_mutants(files, tools)
        for x in miss: print('MUTANT SURVIVED ' + x)
    if fails or miss:
        print('FINALFLAG LINT: FAIL -- %d problem(s); mutants caught %d/%d.' % (len(fails) + len(miss), caught, total)); return 1
    print('PASS: FINALFLAG lint -- F1..F13 hold over %d source files + %d tool/doc files; mutants caught %d/%d.' % (len(files), len(tools), caught, total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
