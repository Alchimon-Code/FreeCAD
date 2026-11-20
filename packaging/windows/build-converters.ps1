# SPDX-License-Identifier: LGPL-2.1-or-later
<#
.SYNOPSIS
Build the pinned LibreDWG exchange programs with MinGW-w64 for the Windows payload.

.DESCRIPTION
The 0.14 encoder compiled by MSVC spins forever on the first conversion, in this project and in a
standalone build of the same tarball, while the MinGW-w64 build of those sources completes like the
Linux one. The window payload therefore ships converters produced here and configured through
PD_LIBREDWG_PREBUILT_DIR; the rest of the application keeps using MSVC.

The URL and SHA-256 are read from cmake/libredwg.cmake, so the pin has exactly one source of truth.
The result is fully static: no MinGW runtime DLL has to travel with the two GPLv3 programs.

.PARAMETER Output
Directory that receives dwg2dxf.exe, dxf2dwg.exe and LibreDWG's COPYING.

.PARAMETER Mingw
MinGW-w64 bin directory, for example C:\mingw64\bin.
#>
param(
    [string]$Output = 'build/converters/dist',
    [string]$Mingw = 'C:\mingw64\bin'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$cmakeFile = Join-Path $root 'cmake/libredwg.cmake'
$url = [regex]::Match((Get-Content -Raw $cmakeFile), 'PD_LIBREDWG_URL\s+"([^"]+)"').Groups[1].Value
$expected = [regex]::Match((Get-Content -Raw $cmakeFile), 'PD_LIBREDWG_SHA256\s+"([0-9a-f]{64})"').Groups[1].Value
$version = [regex]::Match((Get-Content -Raw $cmakeFile), 'PD_LIBREDWG_VERSION\s+"([^"]+)"').Groups[1].Value
if (!$url -or !$expected -or !$version) { throw 'Cannot read the LibreDWG pin from cmake/libredwg.cmake' }
$gcc = Join-Path $Mingw 'gcc.exe'
if (!(Test-Path $gcc)) { throw "MinGW-w64 gcc not found at $gcc" }

$scratch = if ($env:RUNNER_TEMP) { $env:RUNNER_TEMP } else { [IO.Path]::GetTempPath() }
$work = Join-Path $scratch "libredwg-$version"
Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $work | Out-Null
$archive = Join-Path $work 'libredwg.tgz'
Invoke-WebRequest -Uri $url -OutFile $archive
$actual = (Get-FileHash $archive -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $expected) { throw "LibreDWG archive hash $actual does not match the pin $expected" }
$source = Join-Path $work 'src'
New-Item -ItemType Directory -Force $source | Out-Null
& tar -xzf $archive -C $source --strip-components=1
if ($LASTEXITCODE -ne 0) { throw 'Cannot unpack the LibreDWG archive' }

# Static, no zlib/pcre2/iconv search: the same configuration the working Linux build uses.
& cmake -S $source -B (Join-Path $work 'build') -G Ninja `
    "-DCMAKE_C_COMPILER=$gcc" -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF `
    -DBUILD_TESTING=OFF -DLIBREDWG_LIBONLY=OFF -DLIBREDWG_DISABLE_WRITE=OFF `
    -DLIBREDWG_DISABLE_JSON=ON -DENABLE_LTO=OFF -DDISABLE_WERROR=ON `
    -DCMAKE_C_FLAGS=-static -DCMAKE_EXE_LINKER_FLAGS=-static
if ($LASTEXITCODE -ne 0) { throw 'Configuring LibreDWG with MinGW failed' }
& cmake --build (Join-Path $work 'build') --target dwg2dxf dxf2dwg --parallel 2
if ($LASTEXITCODE -ne 0) { throw 'Building the LibreDWG converters with MinGW failed' }

New-Item -ItemType Directory -Force $Output | Out-Null
foreach ($tool in @('dwg2dxf', 'dxf2dwg')) {
    $exe = Get-ChildItem -Recurse -Filter "$tool.exe" (Join-Path $work 'build') | Select-Object -First 1
    if (!$exe) { throw "$tool.exe was not produced" }
    Copy-Item $exe.FullName (Join-Path $Output "$tool.exe") -Force
}
Copy-Item (Join-Path $source 'COPYING') (Join-Path $Output 'COPYING') -Force
Get-ChildItem $Output | ForEach-Object { "$($_.Name) $($_.Length)" }
