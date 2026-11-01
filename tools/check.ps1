# Repeatable check for the code that builds on the PC.
# Runs the host unit tests, then cross-compiles the QNX programs when SDP 8.0 is installed.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path (Join-Path $PSScriptRoot '..')

& (Join-Path $root 'tests\host\run_tests.ps1')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$qnxEnv = Join-Path $env:USERPROFILE 'qnx800\qnxsdp-env.bat'
$qnxDir = Join-Path $root 'supervisor\qnx'
if (Test-Path $qnxEnv) {
    cmd /c "call `"$qnxEnv`" && cd /d `"$qnxDir`" && make"
    exit $LASTEXITCODE
}

Write-Output 'QNX SDP environment script not found; skipped the supervisor build.'
exit 0
