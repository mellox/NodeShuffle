# ns-finalflag-1: shim so a sweep that globs tools\check_*.ps1 runs the Python final-flag lint. Exit code = the lint's exit code.
# The lint itself (one writer of the entry flag and the epoch, three call lines, the predicate, the tripwire lines) is tools\check_finalflag_lint.py.
$py = Join-Path $PSScriptRoot 'check_finalflag_lint.py'
& python $py @args
exit $LASTEXITCODE
