"""ns-quietlog-1 lint: every NodeShuffle log site is classified in tools/quietlog-sites.tsv, and the code matches its class.

Rules (one id per failure):
  Q1 POPULATION   enumerated site keys == TSV keys; no duplicate TSV key.
  Q2 TOPIC        the statement is preceded on its first line by exactly the topic guard and its closing paren + semicolon by ' }'.
  Q2g TOPICGATE   (orchestrator decision N2) the innermost enclosing if header names the topic guard and not the master gate.
  Q3 ALWAYS/ONDEMAND/MASTER  no topic guard in the statement prefix, an enclosing if header, or an early return before it in its function.
  Q4              no #define whose body names the guard, no #define/#undef of the log macro, no output-device filter in Source.
  Q5              the one automatic INHERIT caller (InsideGate) carries the Gates guard; INHERIT helper bodies carry none.
  Q6              the guard body ORs the master with the topic bit; ReadOnce is the first statement of StartupModule.
  Q7              the startup topics line and the consumer census line exist once each (Q3 keeps them unguarded); TSV rows == sites.
  Q8              the 9 reflected names PrimalFauna reads exist, with UPROPERTY, type, and owner unchanged.
Needles are built by concatenation so this file cannot match itself. Run: python tools/check_quietlog_lint.py [--no-mutants]
"""
import os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SRC = os.path.join(REPO, 'Source')
TSV = os.path.join(HERE, 'quietlog-sites.tsv')
MACRO = 'UE_' + 'LOG'
NEEDLE = MACRO + '(' + 'LogNode' + 'Shuffle'
GUARD = 'Ns' + 'Diag'
GUARDCALL = GUARD + '('
ENUM = 'EN' + 's' + 'Diag::'
MASTER = 'AreDiagnostics' + 'Enabled'
TAGRX = re.compile(r'^\s*(?:\[NodeShuffle\]\s*)?([A-Z][A-Z0-9_\-]{2,})(?=[:\s(])')
TOPICS = ('Void', 'Status', 'Placement', 'Gates', 'Spawn', 'Rocks', 'Visuals', 'Wells', 'Hide', 'Scanner', 'Hologram', 'Veto', 'Roll')


def strip_comments_keep_len(t):
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
            elif c == "'": st = "'"
            out.append(c); i += 1; continue
        if c == '\\':
            out.append(t[i:i + 2]); i += 2; continue
        if c == st or c == '\n': st = None
        out.append(c); i += 1
    return ''.join(out)


def blank_strings(t):
    return re.sub(r'"(?:[^"\\\n]|\\.)*"', lambda m: '"' + ' ' * (len(m.group(0)) - 2) + '"', t)


def headers_at(struct, pos):
    """Enclosing block headers, innermost first: (h0, brace_pos, header_text)."""
    d = 0; k = pos - 1; hs = []
    while k >= 0:
        ch = struct[k]
        if ch == '}': d += 1
        elif ch == '{':
            if d == 0:
                h0 = max(struct.rfind(';', 0, k), struct.rfind('}', 0, k), struct.rfind('{', 0, k)) + 1
                hs.append((h0, k, struct[h0:k].strip()))
            else: d -= 1
        k -= 1
    return hs


def func_start(hs):
    for h0, hk, htxt in reversed(hs):
        flat = ' '.join(' '.join(l for l in htxt.split('\n') if not l.strip().startswith('#')).split())
        if flat.startswith('namespace') or not flat: continue
        return hk
    return -1


_cache = {}
def enumerate_file(rel, raw):
    ck = (rel, hash(raw))
    if ck in _cache: return _cache[ck]
    raw = raw.replace('\r\n', '\n')
    code = strip_comments_keep_len(raw); struct = blank_strings(code)
    out = []
    for m in re.finditer(re.escape(NEEDLE), code):
        pos = m.start(); depth = 0; j = pos + len(MACRO)
        while j < len(struct):
            if struct[j] == '(': depth += 1
            elif struct[j] == ')':
                depth -= 1
                if depth == 0: break
            j += 1
        stmt = raw[pos:j + 1]
        vm = re.match(r'\w+\(LogNodeShuffle\s*,\s*(\w+)', stmt)
        verb = vm.group(1) if vm else '?'
        fmt = ''
        fm = re.search(r'((?:TEXT\(\s*(?:"(?:[^"\\]|\\.)*"\s*)+\)\s*)+)', stmt)
        if fm: fmt = ''.join(re.findall(r'"((?:[^"\\]|\\.)*)"', fm.group(1)))
        else:
            mm2 = re.search(r'\w+\(LogNodeShuffle\s*,\s*\w+\s*,\s*([A-Z_][A-Z0-9_]+)', stmt)
            if mm2: fmt = '<MACRO:' + mm2.group(1) + '>'
        tg = TAGRX.match(fmt)
        fk = tg.group(1) if tg else re.sub(r'\s+', ' ', fmt)[:32].replace('\t', ' ').replace('|', '/')
        out.append(dict(rel=rel, fn=rel.split('/')[-1], pos=pos, end=j + 1, verb=verb, fk=fk, line=raw.count('\n', 0, pos) + 1))
    res = (raw, code, struct, out)
    _cache[ck] = res
    return res


def load_source():
    files = {}
    for dp, _, fns in os.walk(SRC):
        for fn in sorted(fns):
            if fn.endswith(('.cpp', '.h')):
                p = os.path.join(dp, fn)
                files[os.path.relpath(p, SRC).replace('\\', '/')] = open(p, encoding='utf-8', errors='replace').read()
    return files


def all_sites(files):
    sites = []
    for rel in sorted(files):
        raw, code, struct, out = enumerate_file(rel, files[rel])
        sites.extend(out)
    sites.sort(key=lambda s: (s['rel'], s['line']))
    cnt = {}
    for s in sites:
        base = '%s|%s|%s' % (s['fn'], s['verb'], s['fk'])
        cnt[base] = cnt.get(base, 0) + 1
        s['key'] = '%s|%d' % (base, cnt[base])
    return sites


def load_tsv(path=TSV):
    rows = []
    for ln in open(path, encoding='utf-8'):
        if ln.startswith('#') or ln.startswith('key\t') or not ln.strip(): continue
        c = ln.rstrip('\n').split('\t')
        rows.append(dict(key=c[0], cls=c[1], topic=c[2] if len(c) > 2 else ''))
    return rows


def guard_text(topic):
    return 'if (' + GUARDCALL + ENUM + topic + ')) { '


def check(files, rows):
    f = []
    sites = all_sites(files)
    byk = {s['key']: s for s in sites}
    seen = set(); tk = {}
    for r in rows:
        if r['key'] in seen: f.append('(Q1) duplicate TSV key ' + r['key'])
        seen.add(r['key']); tk[r['key']] = r
    for k in sorted(set(byk) - seen): f.append('(Q1) unclassified site ' + k)
    for k in sorted(seen - set(byk)): f.append('(Q1) TSV key matches no site ' + k)
    if len(rows) != len(sites): f.append('(Q7) TSV rows %d != sites %d' % (len(rows), len(sites)))
    for s in sites:
        r = tk.get(s['key'])
        if not r: continue
        raw, code, struct, _ = enumerate_file(s['rel'], files[s['rel']])
        pos = s['pos']; cls = r['cls']
        if cls == 'TOPIC':
            if r['topic'] not in TOPICS: f.append('(Q2) unknown topic %s at %s' % (r['topic'], s['key'])); continue
            g = guard_text(r['topic'])
            if raw[pos - len(g):pos] != g or raw[s['end']:s['end'] + 3] != '; }':
                f.append('(Q2) topic guard missing or wrong at %s (line %d)' % (s['key'], s['line']))
            continue
        hs = headers_at(struct, pos)
        if cls == 'TOPICGATE':
            inner = ' '.join(hs[0][2].split()) if hs else ''
            if not re.match(r'(?:else\s+)?if\s*\(', inner) or (GUARDCALL + ENUM + r['topic'] + ')') not in inner or MASTER in inner:
                f.append('(Q2g) N2 topic gate missing at %s (line %d)' % (s['key'], s['line']))
            continue
        # Q3: everything else carries no topic guard on its own statement, enclosing headers, or an early return before it
        prev = code[max(code.rfind(';', 0, pos), code.rfind('{', 0, pos), code.rfind('}', 0, pos)) + 1:pos]
        bad = GUARDCALL in prev or any(GUARDCALL in h[2] for h in hs)
        fs = func_start(hs)
        if not bad and fs >= 0 and re.search(r'if\s*\([^;{]*' + re.escape(GUARDCALL) + r'[^;{]*\)\s*\{?\s*return\b', code[fs:pos]):
            bad = True
        if bad: f.append('(Q3) %s row carries a topic guard at %s (line %d)' % (cls, s['key'], s['line']))
    allcode = {rel: strip_comments_keep_len(t.replace('\r\n', '\n')) for rel, t in files.items()}
    for rel, c in allcode.items():
        for m in re.finditer(r'^[ \t]*#[ \t]*define[^\n]*(?:\\\n[^\n]*)*', c, re.M):
            if GUARD in m.group(0): f.append('(Q4) #define names the guard in ' + rel)
        if re.search(r'^[ \t]*#[ \t]*(?:define|undef)[ \t]+' + MACRO + r'\b', c, re.M): f.append('(Q4) log macro redefined in ' + rel)
        # The one pre-existing output device (ScanDiag's compass-warning counter, which counts and never filters) is the only one allowed.
        n_add = c.count('AddOutput' + 'Device'); n_sub = len(re.findall(r':\s*public\s+F' + 'OutputDevice', c))
        allowed = 1 if rel.endswith('NodeShuffleScanDiag.cpp') and ('FNodeShuffleCompassWarn' + 'Counter final : public F' + 'OutputDevice') in c else 0
        if n_add > allowed or n_sub > allowed: f.append('(Q4) output-device filter in ' + rel)
    ig = [rel for rel in files if rel.endswith('NodeShuffleInsideGate.cpp')]
    helper = 'LogTotally' + 'InsideReading('
    if not ig: f.append('(Q5) InsideGate.cpp missing')
    else:
        c = allcode[ig[0]]
        calls = [m.start() for m in re.finditer(r'(?<![\w:])' + re.escape(helper), c) if not re.search(r'::\s*$', c[max(0, m.start() - 3):m.start()])]
        if len(calls) != 1 or c[calls[0] - len(guard_text('Gates')):calls[0]] != guard_text('Gates'):
            f.append('(Q5) the InsideGate call of the shared helper must carry the Gates guard (calls found %d)' % len(calls))
    dg = [rel for rel in files if rel.endswith('NodeShuffleDiag.cpp')]
    if not dg: f.append('(Q6) NodeShuffleDiag.cpp missing')
    else:
        c = allcode[dg[0]]
        m = re.search(r'bool\s+' + GUARD + r'\s*\(\s*' + 'EN' + 's' + r'Diag\s+\w+\s*\)\s*\{(.*?)\n\}', c, re.S)
        if not m or not re.search(r'return\s+FNodeShuffleModule::' + MASTER + r'\(\)\s*\|\|', m.group(1)):
            f.append('(Q6) the guard must return the master OR the topic bit')
    mods = [rel for rel in files if rel.endswith('NodeShuffle/Private/NodeShuffle.cpp')]
    if not mods: f.append('(Q6) module source missing')
    else:
        c = allcode[mods[0]]
        m = re.search(r'void\s+FNodeShuffleModule::StartupModule\s*\(\s*\)\s*\{\s*([^;]*);', c)
        if not m or m.group(1).strip() != GUARD + 'Topics::ReadOnce()':
            f.append('(Q6) ReadOnce must be the first statement of StartupModule')
    for needle, name in ((('NSDIAG' + ': topics='), 'startup topics line'), (('NSCONSUMER' + ': pass='), 'consumer census line')):
        n = sum(t.count(needle) for t in files.values())
        if n != 1: f.append('(Q7) the %s must exist exactly once (found %d)' % (name, n))
    f.extend(check_reflected(files))
    return f, sites


REFLECTED = [  # (owner, type regex, name)
    ('FNodeShuffleEntry', r'FGuid', 'EntryGuid'), ('FNodeShuffleEntry', r'FVector', 'Location'), ('FNodeShuffleEntry', r'bool', 'bActive'),
    ('FNodeShuffleEntry', r'bool', 'bPinned'), ('FNodeShuffleEntry', r'bool', 'bRayCasted'), ('FNodeShuffleEntry', r'bool', 'bIsNewNode'),
    ('FNodeShuffleEntry', r'FString', 'VanillaNodePath'),
    ('ANodeShuffleSubsystem', r'TArray<\s*FNodeShuffleEntry\s*>', 'Layout'),
    ('ANodeShuffleSubsystem', r'TMap<\s*FGuid\s*,\s*(?:class\s+)?AFGResourceNode\s*\*\s*>', 'SpawnedNodes')]


def check_reflected(files):
    f = []
    hs = [rel for rel in files if rel.endswith('Public/NodeShuffleSubsystem.h')]
    if not hs: return ['(Q8) NodeShuffleSubsystem.h missing']
    c = strip_comments_keep_len(files[hs[0]].replace('\r\n', '\n'))
    for owner, typ, name in REFLECTED:
        om = re.search(r'(?:struct|class)\s+(?:\w+_API\s+)?' + owner + r'\b[^;{]*\{', c)
        if not om: f.append('(Q8) owner %s not found' % owner); continue
        d = 0; i = om.end() - 1
        while i < len(c):
            if c[i] == '{': d += 1
            elif c[i] == '}':
                d -= 1
                if d == 0: break
            i += 1
        body = c[om.end():i]
        if not re.search(r'UPROPERTY\([^)]*\)\s*' + typ + r'\s+' + name + r'\s*[;={]', body):
            f.append('(Q8) %s::%s must stay a UPROPERTY of its type' % (owner, name))
    return f


MUTANTS = [  # (name, file suffix, find, replace, expected rule)
    ('M1 a TOPIC guard reverted', 'NodeShuffleWellMeshIndex.cpp', guard_text('Wells'), '', 'Q2'),
    ('M2 wrong topic on a guard', 'NodeShuffleWellMeshIndex.cpp', guard_text('Wells'), guard_text('Rocks'), 'Q2'),
    ('M3 ALWAYS ADOPTMISS guarded', 'NodeShuffleAdoptAudit.cpp', NEEDLE + ', Display, TEXT("ADOPTMISS', guard_text('Spawn') + NEEDLE + ', Display, TEXT("ADOPTMISS', 'Q3'),
    ('M4 REROLLAUDIT guarded', 'NodeShuffleRerollWipe.cpp', NEEDLE + ', Display, TEXT("REROLLAUDIT', guard_text('Roll') + NEEDLE + ', Display, TEXT("REROLLAUDIT', 'Q3'),
    ('M5 an unclassified site', 'NodeShuffleDiag.cpp', '\nbool ' + GUARD + '(', '\nstatic void QlMut() { ' + NEEDLE + ', Display, TEXT("QLMUTANT: x")); }\nbool ' + GUARD + '(', 'Q1'),
    ('M6 a classified tag renamed', 'NodeShuffleAdoptAudit.cpp', 'TEXT("ADOPTMISS', 'TEXT("ADOPTMIZZ', 'Q1'),
    ('M7 a #define of the guard', 'NodeShuffleDiag.cpp', '\nbool ' + GUARD + '(', '\n#define NSG if (' + GUARDCALL + '\nbool ' + GUARD + '(', 'Q4'),
    ('M8 #undef of the log macro', 'NodeShuffleDiag.cpp', '\nbool ' + GUARD + '(', '\n#undef ' + MACRO + '\nbool ' + GUARD + '(', 'Q4'),
    ('M18 a new output device', 'NodeShuffleDiag.cpp', '\nbool ' + GUARD + '(', '\nstatic void QlDev() { GLog->AddOutput' + 'Device(nullptr); }\nbool ' + GUARD + '(', 'Q4'),
    ('M9 the InsideGate call unguarded', 'NodeShuffleInsideGate.cpp', guard_text('Gates'), '', 'Q5'),
    ('M10 the guard drops the master', 'NodeShuffleDiag.cpp', 'return FNodeShuffleModule::' + MASTER + '() ||', 'return false ||', 'Q6'),
    ('M11 the startup topics line guarded', 'NodeShuffleDiag.cpp', NEEDLE + ', Display, TEXT("NSDIAG' + ': topics', guard_text('Status') + NEEDLE + ', Display, TEXT("NSDIAG' + ': topics', 'Q3'),
    ('M13 an N2 gate back to the master', 'NodeShuffleMapModeCliffTop.cpp', GUARDCALL + ENUM + 'Placement)', 'FNodeShuffleModule::' + MASTER + '()', 'Q2g'),
    ('M14 a pinned reflected name renamed', 'Public/NodeShuffleSubsystem.h', ' bRayCasted', ' bRayCastedX', 'Q8'),
    ('M15 SpawnedNodes loses UPROPERTY', 'Public/NodeShuffleSubsystem.h', 'UPROPERTY() TMap<FGuid, AFGResourceNode*> Spawned' + 'Nodes;', 'TMap<FGuid, AFGResourceNode*> Spawned' + 'Nodes;', 'Q8'),
    ('M16 ReadOnce no longer first', 'NodeShuffle/Private/NodeShuffle.cpp', GUARD + 'Topics::ReadOnce();', 'int32 QlX = 0; ' + GUARD + 'Topics::ReadOnce();', 'Q6'),
    ('M17 the consumer census line duplicated', 'NodeShuffleConsumerCensus.cpp', 'TEXT("NSCONSUMER' + ': pass=', 'TEXT("NSCONSUMER' + ': pass=NSCONSUMER' + ': pass=', 'Q7'),
]


def run_mutants(files, rows):
    caught = 0; miss = []
    for name, suffix, find, repl, rule in MUTANTS:
        tgt = [rel for rel in files if rel.endswith(suffix)]
        if not tgt or find not in files[tgt[0]]:
            miss.append('%s: find text absent (mutant cannot apply)' % name); continue
        mf = dict(files); mf[tgt[0]] = files[tgt[0]].replace(find, repl, 1)
        fails, _ = check(mf, rows)
        if any(x.startswith('(' + rule + ')') for x in fails): caught += 1
        else: miss.append('%s: rule %s stayed green' % (name, rule))
    # M12 duplicate TSV key
    fails, _ = check(files, rows + [rows[0]])
    if any(x.startswith('(Q1) duplicate') for x in fails): caught += 1
    else: miss.append('M12 duplicate TSV key: Q1 stayed green')
    # M19 the entry's Location field moved out of FNodeShuffleEntry into the subsystem class (type and UPROPERTY kept)
    h = [rel for rel in files if rel.endswith('Public/NodeShuffleSubsystem.h')]
    loc = '    UPROPERTY(SaveGame) FVector Loc' + 'ation = FVector::ZeroVector;'
    anchor = 'UPROPERTY() TMap<FGuid, AFGResourceNode*> Spawned' + 'Nodes;'
    if h and files[h[0]].count(loc) >= 1 and anchor in files[h[0]]:
        mf = dict(files); mf[h[0]] = files[h[0]].replace(loc, '', 1).replace(anchor, anchor + ' ' + loc.strip(), 1)
        fails, _ = check(mf, rows)
        if any(x.startswith('(Q8)') for x in fails): caught += 1
        else: miss.append('M19 Location moved out of the entry struct: Q8 stayed green')
    else: miss.append('M19 Location moved out of the entry struct: find text absent (mutant cannot apply)')
    return caught, len(MUTANTS) + 2, miss


def main():
    files = load_source(); rows = load_tsv()
    fails, sites = check(files, rows)
    counts = {}
    for r in rows: counts[r['cls']] = counts.get(r['cls'], 0) + 1
    print('sites %d; TSV classes %s' % (len(sites), ' '.join('%s=%d' % kv for kv in sorted(counts.items()))))
    for x in fails[:60]: print('FAIL ' + x)
    if len(fails) > 60: print('... %d more' % (len(fails) - 60))
    caught = total = 0; miss = []
    if '--no-mutants' not in sys.argv and not fails:
        caught, total, miss = run_mutants(files, rows)
        for x in miss: print('MUTANT SURVIVED ' + x)
    if fails or miss:
        print('QUIETLOG LINT: FAIL -- %d problem(s); mutants caught %d/%d.' % (len(fails) + len(miss), caught, total)); return 1
    print('PASS: QUIETLOG lint -- Q1..Q8 hold over %d sites in %d files; mutants caught %d/%d.' % (len(sites), len(files), caught, total))
    return 0


if __name__ == '__main__':
    sys.exit(main())
