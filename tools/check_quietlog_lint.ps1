# ns-quietlog-1: shim so a sweep that globs tools\check_*.ps1 runs the Python quietlog lint. Exit code = the lint's exit code.
# The lint itself (site population, topic guards, the 9 reflected names PrimalFauna reads) is tools\check_quietlog_lint.py.
$py = Join-Path $PSScriptRoot 'check_quietlog_lint.py'
& python $py @args
exit $LASTEXITCODE
