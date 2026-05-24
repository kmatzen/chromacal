<#
.SYNOPSIS
  Build chromacal.aex (the native Premiere/AE effect) on Windows and stage a
  self-contained plugin folder. The Windows analog of build_bundle.sh (macOS).

.DESCRIPTION
  1. Configures + builds the chromacal_effect target with CMake (Ninja, Release).
     The PiPL resource is generated and linked automatically (see plugin/CMakeLists.txt
     + win_pipl.cmake), so the resulting chromacal.aex is directly loadable.
  2. Stages chromacal.aex plus its *transitive* dependency DLLs (OpenCV, Ceres,
     OpenColorIO, ... resolved via dumpbin against the vcpkg bin) into $DistDir, so
     the plugin loads without vcpkg on PATH.

  Run from a "x64 Native Tools Command Prompt for VS" / Developer PowerShell so
  cl.exe, rc.exe and dumpbin.exe are on PATH.

.EXAMPLE
  pwsh plugin/effect/build_windows.ps1 `
    -Vcpkg C:\vcpkg `
    -PrSdk "C:\Premiere Pro 26.0 C++ SDK\Examples\Headers" `
    -AeSdk "C:\After Effects SDK\Examples\Headers"
#>
param(
  [Parameter(Mandatory = $true)][string]$Vcpkg,
  [Parameter(Mandatory = $true)][string]$PrSdk,
  [Parameter(Mandatory = $true)][string]$AeSdk,
  [string]$Triplet  = "x64-windows",
  [string]$BuildDir = "build-win",
  [string]$DistDir  = "dist\chromacal"
)
$ErrorActionPreference = "Stop"

# Repo root = two levels up from plugin/effect.
$root = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $root

$toolchain = Join-Path $Vcpkg "scripts\buildsystems\vcpkg.cmake"
if (!(Test-Path $toolchain)) { throw "vcpkg toolchain not found: $toolchain (is -Vcpkg correct?)" }
foreach ($t in 'cl.exe', 'rc.exe', 'dumpbin.exe') {
  if (!(Get-Command $t -ErrorAction SilentlyContinue)) {
    throw "$t not on PATH — run from a 'x64 Native Tools Command Prompt for VS'."
  }
}

Write-Host "==> Configuring..." -ForegroundColor Cyan
cmake -B $BuildDir -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DCHROMACAL_BUILD_PPRO=ON `
  -DCHROMACAL_PRSDK_DIR="$PrSdk" `
  -DCHROMACAL_AESDK_DIR="$AeSdk" `
  -DCMAKE_TOOLCHAIN_FILE="$toolchain" `
  -DVCPKG_TARGET_TRIPLET="$Triplet"
if ($LASTEXITCODE) { throw "cmake configure failed" }

Write-Host "==> Building chromacal.aex..." -ForegroundColor Cyan
cmake --build $BuildDir --config Release --target chromacal_effect
if ($LASTEXITCODE) { throw "build failed" }

$aex = Get-ChildItem -Path $BuildDir -Recurse -Filter "chromacal.aex" | Select-Object -First 1
if (!$aex) { throw "chromacal.aex not found under $BuildDir" }

Write-Host "==> Staging self-contained plugin -> $DistDir" -ForegroundColor Cyan
if (Test-Path $DistDir) { Remove-Item $DistDir -Recurse -Force }
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
Copy-Item $aex.FullName $DistDir -Force

# Resolve the transitive DLL closure that lives in the vcpkg bin (skips system
# DLLs, which aren't there). Copy only those — minimal + self-contained.
$vcbin = Join-Path $Vcpkg "installed\$Triplet\bin"
if (!(Test-Path $vcbin)) { throw "vcpkg bin not found: $vcbin" }

function Get-DllClosure([string]$binary, [string]$searchDir) {
  $seen  = @{}
  $queue = [System.Collections.Queue]::new()
  $queue.Enqueue($binary)
  while ($queue.Count -gt 0) {
    $b = $queue.Dequeue()
    $deps = & dumpbin /nologo /dependents $b 2>$null |
      Select-String '^\s+(\S+\.dll)\s*$' |
      ForEach-Object { $_.Matches[0].Groups[1].Value }
    foreach ($d in $deps) {
      $cand = Join-Path $searchDir $d
      if ((Test-Path $cand) -and -not $seen.ContainsKey($d.ToLower())) {
        $seen[$d.ToLower()] = $cand
        $queue.Enqueue($cand)
      }
    }
  }
  return $seen.Values
}

$dlls = Get-DllClosure $aex.FullName $vcbin
foreach ($dll in $dlls) { Copy-Item $dll $DistDir -Force }

Write-Host "==> Done. $($dlls.Count) dependency DLL(s) bundled." -ForegroundColor Green
Get-ChildItem $DistDir | Select-Object Name, @{n='KB';e={[int]($_.Length/1KB)}} | Format-Table -AutoSize
Write-Host "Install with plugin/effect/chromacal.iss (Inno Setup), or copy $DistDir to"
Write-Host "  C:\Program Files\Adobe\Common\Plug-ins\7.0\MediaCore\"
