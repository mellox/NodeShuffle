#!/usr/bin/env python3
"""arity.py -- count UE_LOG format specifiers against arguments, per log line, per file.

    python tools/arity.py <file.cpp> [<file.cpp> ...]

Exit code 0 = every UE_LOG in every file scanned to completion and every one balanced.
Exit code 1 = at least one MISMATCH (a real arity defect).
Exit code 2 = the scanner could not finish a file (see LOUD FAILURE below). Treat as UNSCANNED.

WHY THIS FILE IS IN THE REPO AND NOT IN A SCRATCHPAD
----------------------------------------------------
ns-t27-perf, 2026-08-09. This script lived only in a session scratchpad and its verdicts were quoted
forward across three cold-review rounds. It also ABORTED -- with an uncaught IndexError, i.e. a stack
trace that was easy to read as "the file has no more log lines" -- on the literal token `UE_LOG(`
sitting inside a `//` comment in NodeShuffleWellRelocateRoll.cpp. The paren walk started at that fake
call and ran off the end of the file looking for a closing paren that was never opened. So every
"all balanced" verdict on that file covered only the lines ABOVE the abort and claimed the whole file.

Two changes fix that class, not just that instance:
  1. COMMENTS AND STRING LITERALS ARE REMOVED BEFORE ANY PAREN WALK (StripCommentsPreservingLines).
     Line numbers are preserved exactly, so reported line numbers still match the original file.
  2. AN ABORT IS A LOUD FAILURE, NOT A SHORT READ. If the walk cannot balance, the script prints
     *** SCAN ABORTED *** naming the file and line, states how much of the file it actually covered,
     and exits 2. A checker that silently covers less than it claims is this project's named defect
     class and it must not be able to happen here quietly again.
"""

import re
import sys

BS = chr(92)
TEXTPAT = r'\s*TEXT\(\s*"([^"]*)"\s*\)'


def StripCommentsPreservingLines(src):
    """Return src with // and /* */ comment bodies replaced by spaces, newlines preserved.

    String and character literals are tracked so that a `//` or `/*` INSIDE a literal (a URL in a
    TEXT(), a path separator) is not mistaken for a comment. Literal CONTENT is preserved -- the
    format-string parser downstream needs it -- only comment content is blanked.
    """
    out = []
    i = 0
    n = len(src)
    while i < n:
        c = src[i]
        nxt = src[i + 1] if i + 1 < n else ''
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                if src[i] == BS and i + 1 < n:
                    out.append(src[i]); out.append(src[i + 1]); i += 2; continue
                out.append(src[i])
                if src[i] == quote:
                    i += 1
                    break
                if src[i] == '\n':       # unterminated literal; do not swallow the rest of the file
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and nxt == '/':
            while i < n and src[i] != '\n':
                out.append(' ')
                i += 1
            continue
        if c == '/' and nxt == '*':
            out.append('  ')
            i += 2
            while i < n and not (src[i] == '*' and i + 1 < n and src[i + 1] == '/'):
                out.append('\n' if src[i] == '\n' else ' ')
                i += 1
            out.append('  ')
            i += 2
            continue
        out.append(c)
        i += 1
    return ''.join(out)


def ScanFile(path):
    """Returns (mismatches, aborted, scanned_lines, total_lines) and prints one row per UE_LOG."""
    raw = open(path, encoding='utf-8').read()
    total_lines = raw.count('\n') + 1
    src = StripCommentsPreservingLines(raw)
    if len(src) != len(raw):
        # The stripper is length-preserving by construction; if that ever stops being true the line
        # numbers below stop matching the file and every verdict becomes unreadable. Fail loudly.
        print('*** SCAN ABORTED *** %s: comment stripper changed the source length '
              '(%d -> %d). Line numbers would be wrong. FILE NOT SCANNED.'
              % (path, len(raw), len(src)))
        return (0, True, 0, total_lines)

    mismatches = 0
    furthest = 0
    for m in re.finditer(r'UE_LOG\(', src):
        i = m.end()
        line = src[:m.start()].count('\n') + 1
        depth = 1
        j = i
        while depth and j < len(src):
            if src[j] == '(':
                depth += 1
            elif src[j] == ')':
                depth -= 1
            j += 1
        if depth:
            print('*** SCAN ABORTED *** %s:%d: unbalanced parentheses after UE_LOG( -- the walk ran '
                  'to end of file. Lines %d-%d of %d were NEVER SCANNED. Do not read any "all '
                  'balanced" verdict for this file.' % (path, line, line, total_lines, total_lines))
            return (mismatches, True, furthest, total_lines)
        furthest = line
        body = src[i:j - 1]
        hdr = re.match(r'\s*(\w+)\s*,\s*(\w+)\s*,(.*)$', body, re.S)
        if not hdr:
            print('line %4d  UNPARSED HEADER -- not counted' % line)
            mismatches += 1
            continue
        rest = hdr.group(3)
        fmt = ''
        while True:
            mm = re.match(TEXTPAT, rest)
            if not mm:
                break
            fmt += mm.group(1)
            rest = rest[mm.end():]
        spec = re.findall(r'%(?:[-+ #0]*)(?:\d+)?(?:\.\d+)?[a-zA-Z]', fmt)
        rest = rest.lstrip()
        if not rest.startswith(','):
            print('line %4d  format-only, specifiers=%d' % (line, len(spec)))
            if len(spec):
                mismatches += 1
            continue
        rest = rest[1:]
        d = 0
        args = 1
        inq = False
        k = 0
        while k < len(rest):
            c = rest[k]
            if c == '"' and rest[k - 1] != BS:
                inq = not inq
            elif not inq:
                if c in '([':
                    d += 1
                elif c in ')]':
                    d -= 1
                elif c == '?':
                    d += 1
                elif c == ':' and d > 0:
                    d -= 1
                elif c == ',' and d == 0:
                    args += 1
            k += 1
        ok = 'OK ' if len(spec) == args else '*** MISMATCH ***'
        if len(spec) != args:
            mismatches += 1
        print('line %4d  %-8s specifiers=%3d args=%3d  %s  %s'
              % (line, hdr.group(2), len(spec), args, ok, ''.join(spec)))
    return (mismatches, False, furthest, total_lines)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    worst = 0
    for path in argv[1:]:
        print('===== %s =====' % path)
        mismatches, aborted, furthest, total = ScanFile(path)
        if aborted:
            print('---- %s: ABORTED. NOT a clean scan. ----' % path)
            worst = max(worst, 2)
            continue
        print('---- %s: %d UE_LOG mismatch(es); whole file scanned (%d lines). ----'
              % (path, mismatches, total))
        if mismatches:
            worst = max(worst, 1)
    return worst


if __name__ == '__main__':
    sys.exit(main(sys.argv))
