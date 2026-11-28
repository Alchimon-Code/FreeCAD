# SPDX-License-Identifier: LGPL-2.1-or-later
# Run from an x64 VS developer environment after building windows-msvc. Produce both artifacts.
param([string]$BuildDir = 'build/windows-msvc', [string]$Iscc = 'ISCC.exe')
$ErrorActionPreference = 'Stop'
$build = (Resolve-Path $BuildDir).Path
$out = Join-Path $build 'packages'
$versionLine = Select-String -Path "$build/CMakeCache.txt" -Pattern '^CMAKE_PROJECT_VERSION:STATIC=(.+)$'
$version = $versionLine.Matches.Groups[1].Value
if (!$version) { throw 'Missing CMake project version' }
$name = "PlaneDraft-$version-windows-x64"
& cpack --config "$build/CPackConfig.cmake" -C Release -G ZIP -B $out
if ($LASTEXITCODE -ne 0) { throw 'CPack failed' }
$tmp = Join-Path ([IO.Path]::GetTempPath()) ([Guid]::NewGuid().ToString())
try {
    Expand-Archive -LiteralPath "$out/$name.zip" -DestinationPath $tmp
    $payload = Join-Path $tmp $name
    if (!(Test-Path "$payload/bin/PlaneDraft.exe")) { throw 'Unexpected archive layout' }
    if (!(Get-Command $Iscc -ErrorAction SilentlyContinue)) {
        $Iscc = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe"
    }
    & $Iscc "/DPayload=$payload" "/DAppVersion=$version" "/DOutput=$out" "$PSScriptRoot/PlaneDraft.iss"
    if ($LASTEXITCODE -ne 0) { throw 'Inno Setup failed' }
} finally {
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
}
