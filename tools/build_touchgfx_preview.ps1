[CmdletBinding()]
param(
    [switch]$Run,
    [switch]$RegenerateAssets,
    [ValidateRange(1, 32)]
    [int]$Jobs = 8
)

$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$touchGfxRoot = Join-Path $repoRoot 'stm32f411_fw\TouchGFX'
$touchGfxInstall = 'C:\TouchGFX\4.26.1'
$mingwRoot = Join-Path $touchGfxInstall 'env\MinGW'
$msysBin = Join-Path $mingwRoot 'msys\1.0\bin'
$compilerBin = Join-Path $mingwRoot 'bin'
$rubyBin = Join-Path $mingwRoot 'msys\1.0\Ruby30-x64\bin'
$makeExe = Join-Path $msysBin 'make.exe'
$touchGfxSdk = Join-Path $touchGfxInstall 'touchgfx'
$simulatorExe = Join-Path $touchGfxRoot 'build\bin\simulator.exe'
$simulatorBin = Split-Path $simulatorExe

foreach ($requiredPath in @($touchGfxRoot, $makeExe, $compilerBin, $msysBin)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required TouchGFX path not found: $requiredPath"
    }
}

$pathParts = @($compilerBin, $msysBin)
if (Test-Path -LiteralPath $rubyBin) {
    $pathParts += $rubyBin
}
$env:PATH = (($pathParts + $env:PATH) -join ';')

# The project keeps the target framework sources in-tree but not the Windows
# simulator DLLs. Seed the ignored output directory from the matching Designer
# installation so the generated Makefile does not look for DLLs in the target
# SDK directory.
New-Item -ItemType Directory -Force -Path $simulatorBin | Out-Null
$runtimeFiles = @(
    (Join-Path $touchGfxSdk 'lib\sdl2\win32\SDL2.dll'),
    (Join-Path $touchGfxSdk 'lib\sdl2\win32\SDL2_image.dll'),
    (Join-Path $touchGfxSdk 'lib\sdl2\win32\libpng16-16.dll'),
    (Join-Path $touchGfxSdk 'lib\sdl2\win32\zlib1.dll'),
    (Join-Path $touchGfxSdk '3rdparty\libjpeg\lib\win32\libjpeg-8.dll')
)
foreach ($runtimeFile in $runtimeFiles) {
    if (-not (Test-Path -LiteralPath $runtimeFile)) {
        throw "Required simulator runtime not found: $runtimeFile"
    }
    Copy-Item -LiteralPath $runtimeFile -Destination $simulatorBin -Force
}

Write-Host 'Building TouchGFX simulator preview...'
if ($RegenerateAssets) {
    & $makeExe -C $touchGfxRoot -f simulator/gcc/Makefile "-j$Jobs" all
}
else {
    # Generated TouchGFX source is tracked in Git. The default build consumes
    # it without rewriting tracked files, keeping a preview build side-effect
    # free outside the ignored build directory.
    & $makeExe -C $touchGfxRoot -f simulator/gcc/Makefile `
        "-j$Jobs" preview
}
if ($LASTEXITCODE -ne 0) {
    throw "TouchGFX simulator build failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath $simulatorExe)) {
    throw "Simulator build completed but output is missing: $simulatorExe"
}

Write-Host "Preview generated: $simulatorExe"

if ($Run) {
    Write-Host 'Starting TouchGFX simulator...'
    Start-Process -FilePath $simulatorExe -WorkingDirectory (Split-Path $simulatorExe)
}
