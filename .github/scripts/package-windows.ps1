param(
    [Parameter(Mandatory)][string]$BuildDir,
    [Parameter(Mandatory)][string]$QtDir,
    [Parameter(Mandatory)][string]$Package
)

$ErrorActionPreference = 'Stop'
$stage = Join-Path (Resolve-Path -LiteralPath $BuildDir).Path ("deploy-windows-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
$exe = Join-Path $stage 'MarkdownReader.exe'
Copy-Item -LiteralPath (Join-Path $BuildDir 'MarkdownReader.exe') -Destination $exe

& (Join-Path $QtDir 'bin/windeployqt.exe') --release --no-compiler-runtime --no-translations --skip-plugin-types generic --dir $stage $exe
if ($LASTEXITCODE -ne 0) { throw "windeployqt failed with exit code $LASTEXITCODE" }

foreach ($file in 'MarkdownReader.exe', 'Qt6Core.dll', 'Qt6Gui.dll', 'Qt6Widgets.dll', 'platforms/qwindows.dll', 'imageformats/qjpeg.dll') {
    if (!(Test-Path -LiteralPath (Join-Path $stage $file) -PathType Leaf)) {
        throw "Missing deployed file: $file"
    }
}
if (Get-ChildItem -LiteralPath $stage -Recurse -File -Filter 'Qt6*d.dll') {
    throw 'Unexpected debug Qt DLL in the package'
}

$example = (Resolve-Path -LiteralPath 'examples/example.md').Path
$sdkPath = $env:Path
$qtEnvironment = @{}
foreach ($name in 'QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QT_QPA_PLATFORM') {
    $qtEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
    [Environment]::SetEnvironmentVariable($name, $null, 'Process')
}
$app = $null
try {
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $app = Start-Process -FilePath $exe -WorkingDirectory $stage -ArgumentList "`"$example`"" -PassThru
    $deadline = [DateTime]::UtcNow.AddSeconds(20)
    do {
        Start-Sleep -Milliseconds 200
        $app.Refresh()
        if ($app.HasExited) { throw "Packaged application exited with code $($app.ExitCode)" }
    } until ($app.MainWindowTitle -like 'example.md*' -or [DateTime]::UtcNow -ge $deadline)
    if ($app.MainWindowTitle -notlike 'example.md*') {
        throw 'The packaged application did not display example.md'
    }
    Write-Output "Packaged GUI opened: $($app.MainWindowTitle)"
} finally {
    if ($app -and !$app.HasExited) {
        if (!$app.CloseMainWindow() -or !$app.WaitForExit(5000)) {
            Stop-Process -Id $app.Id -ErrorAction SilentlyContinue
        }
    }
    $env:Path = $sdkPath
    foreach ($name in $qtEnvironment.Keys) {
        [Environment]::SetEnvironmentVariable($name, $qtEnvironment[$name], 'Process')
    }
}

New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Package) | Out-Null
Compress-Archive -LiteralPath (Get-ChildItem -LiteralPath $stage).FullName -DestinationPath $Package -Force
Get-Item -LiteralPath $Package | Select-Object Name, Length
