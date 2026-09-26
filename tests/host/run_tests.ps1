# Builds and runs the host unit tests with the MSYS2 GCC on PATH.
$ErrorActionPreference = 'Stop'
$root = Resolve-Path "$PSScriptRoot\..\.."
$out = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$sources = @(
    "$PSScriptRoot\test_main.c",
    "$root\shared\protocol\edge_protocol.c",
    "$root\shared\common\sensor_sim.c",
    "$root\shared\common\rt_stats.c"
)

& gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -g `
    -fsanitize=undefined -fsanitize-trap=undefined `
    -I "$root\shared\protocol" -I "$root\shared\common" `
    @sources -o "$out\host_tests.exe"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& "$out\host_tests.exe"
exit $LASTEXITCODE
