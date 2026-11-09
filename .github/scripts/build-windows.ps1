param(
    [Parameter(Mandatory)][string]$QtDir,
    [string]$BuildDir = 'build/release',
    [string]$VisualStudioVersion,
    [switch]$Benchmark
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsArgs = @('-latest', '-products', '*', '-requires',
    'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath')
if ($VisualStudioVersion) {
    $vsArgs += @('-version', $VisualStudioVersion)
}
$vsPath = & $vswhere @vsArgs
if ($LASTEXITCODE -ne 0 -or !$vsPath) {
    throw 'No matching Visual Studio installation with the MSVC x64 toolset was found.'
}

& (Join-Path $vsPath 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
if ($env:VSCMD_ARG_TGT_ARCH -notin 'x64', 'amd64' -or !(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw 'Visual Studio did not initialize the MSVC x64 environment.'
}

$qt = (Resolve-Path -LiteralPath $QtDir).Path
$qtVersion = & (Join-Path $qt 'bin/qtpaths.exe') --query QT_VERSION
if ($LASTEXITCODE -ne 0 -or $qtVersion -ne '6.8.3') {
    throw "Expected Qt 6.8.3 at $qt, got '$qtVersion'."
}
Write-Output "Visual Studio: $vsPath. Qt: $qt ($qtVersion)"

$cmakeArgs = @('--preset', 'release', '-B', $BuildDir, "-DCMAKE_PREFIX_PATH=$qt",
    "-DQt6_DIR=$qt/lib/cmake/Qt6", '-DCMAKE_CXX_COMPILER=cl')
if ($Benchmark) {
    $cmakeArgs += '-DMARKDOWN_READER_BUILD_BENCHMARK=ON'
}
& cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $BuildDir --config Release
exit $LASTEXITCODE
