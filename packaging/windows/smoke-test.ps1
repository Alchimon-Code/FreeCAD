# SPDX-License-Identifier: LGPL-2.1-or-later
# CI only: use a fresh per-user install on a disposable Windows runner, then uninstall it.
param([string]$BuildDir = 'build/windows-msvc')
$ErrorActionPreference = 'Stop'
$packages = Join-Path (Resolve-Path $BuildDir) 'packages'
# Keep an absolute path for the independent audit when PATH no longer includes developer tools.
$python = (Get-Command python -CommandType Application | Select-Object -First 1).Source
$zip = @(Get-ChildItem "$packages/PlaneDraft-*-windows-x64.zip")
$setup = @(Get-ChildItem "$packages/PlaneDraft-*-setup.exe")
if ($zip.Count -ne 1 -or $setup.Count -ne 1) { throw 'Expected a portable ZIP and one installer' }
$tmp = Join-Path $env:TEMP ([Guid]::NewGuid().ToString())
$install = "$tmp/installed with spaces"
$oldPath = $env:Path
$oldPluginPath = $env:QT_PLUGIN_PATH
$oldQpaPath = $env:QT_QPA_PLATFORM_PLUGIN_PATH
function RunChecked([string]$exe, [string]$arguments) {
    $p = Start-Process -Wait -PassThru -FilePath $exe -ArgumentList $arguments
    if ($p.ExitCode -ne 0) { throw "$exe exited with $($p.ExitCode)" }
}
function CheckPayload([string]$root) {
    foreach ($path in @('bin/PlaneDraft.exe','bin/pdconvert.exe','bin/platforms/qwindows.dll',
        'bin/Qt6Core.dll','bin/Qt6Widgets.dll','bin/Qt6Svg.dll','bin/translations/qtbase_ru.qm',
        'bin/msvcp140.dll','bin/vcruntime140.dll','plugins/sample_line.dll','plugins/frame_generator.dll',
        'include/pd_plugin_api.h','examples/scripts/pattern.pdl','examples/drawings/cover.dxf',
        'examples/drawings/panel.dxf','THIRD_PARTY_NOTICES.md','RUNTIME_INVENTORY.md',
        'tools/libredwg/dwg2dxf.exe','tools/libredwg/dxf2dwg.exe',
        'LICENSES/LibreDWG-0.14-GPL-3.0.txt',
        'LICENSES/ports/qtbase/copyright','LICENSES/ports/qtsvg/vcpkg.spdx.json')) {
        if (!(Test-Path -LiteralPath (Join-Path $root $path))) { throw "Missing from payload: $path" }
    }
    # --version constructs QApplication (loads QPA) but doesn't require a GPU on the runner.
    RunChecked "$root/bin/PlaneDraft.exe" '--version'
    & "$root/bin/pdconvert.exe" --layout 'Панель' "$root/examples/drawings/panel.dxf" "$tmp/smoke.pdf"
    if ($LASTEXITCODE -ne 0) { throw 'CLI sheet export failed' }
    $pdf = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$tmp/smoke.pdf"))
    if (!$pdf.StartsWith('%PDF-') -or $pdf -notmatch '/MediaBox\s*\[0 0 ([.0-9]+) ([.0-9]+)\]') { throw 'Missing vector PDF page' }
    # Exercise the shipped GPLv3 programs on their own first: a hang or crash here is a converter
    # problem, not a bridge one, and console output names the phase that failed.
    function Invoke-Converter([string]$exe, [string[]]$arguments, [int]$seconds) {
        $out = "$tmp/convert.out"
        $err = "$tmp/convert.err"
        $p = Start-Process -FilePath $exe -NoNewWindow -PassThru -ArgumentList $arguments `
            -RedirectStandardOutput $out -RedirectStandardError $err
        if (!$p.WaitForExit($seconds * 1000)) {
            $p.Kill()
            throw "Converter did not finish in ${seconds}s: $exe $arguments`nSTDOUT: $(Get-Content -Raw $out)`nSTDERR: $(Get-Content -Raw $err)"
        }
        if ($p.ExitCode -ne 0) {
            throw "Converter exit $($p.ExitCode): $exe $arguments`nSTDOUT: $(Get-Content -Raw $out)`nSTDERR: $(Get-Content -Raw $err)"
        }
        return (Get-Content -Raw $out)
    }
    Invoke-Converter "$root/tools/libredwg/dwg2dxf.exe" @('--help') 60 | Out-Null
    Invoke-Converter "$root/tools/libredwg/dxf2dwg.exe" @('--help') 60 | Out-Null
    # A one-line drawing: separates "the encoder cannot start at all" from "this drawing loops".
    $tiny = @'
0
SECTION
2
HEADER
9
$ACADVER
1
AC1015
0
ENDSEC
0
SECTION
2
ENTITIES
0
LINE
8
0
10
0.0
20
0.0
11
10.0
21
0.0
0
ENDSEC
0
EOF
'@
    Set-Content -LiteralPath "$tmp/tiny.dxf" -Value $tiny -Encoding ascii
    Invoke-Converter "$root/tools/libredwg/dxf2dwg.exe" `
        @('-o', "$tmp/tiny.dwg", "$tmp/tiny.dxf") 90 | Out-Null
    Remove-Item "$tmp/tiny.dwg"
    $written = Invoke-Converter "$root/tools/libredwg/dxf2dwg.exe" `
        @('-o', "$tmp/direct.dwg", "$root/examples/drawings/panel.dxf") 180
    if ($written -notmatch 'Writing DWG file') {
        throw "Bundled dxf2dwg reported no write: $written"
    }
    if (!(Test-Path "$tmp/direct.dwg")) { throw 'Bundled dxf2dwg produced no DWG' }
    if ((Get-Item "$tmp/direct.dwg").Length -lt 1000) { throw 'Bundled dxf2dwg produced a truncated DWG' }
    Remove-Item "$tmp/direct.dwg"
    # DWG out of the box: the bundled LibreDWG programs are found beside the executable.
    & "$root/bin/pdconvert.exe" "$root/examples/drawings/panel.dxf" "$tmp/smoke.dwg"
    if ($LASTEXITCODE -ne 0 -or (Get-Item "$tmp/smoke.dwg").Length -lt 1000) { throw 'DWG export failed' }
    & "$root/bin/pdconvert.exe" "$tmp/smoke.dwg" "$tmp/smoke-dwg.dxf"
    if ($LASTEXITCODE -ne 0) { throw 'DWG import failed' }
    $roundtrip = Get-Content -Raw "$tmp/smoke-dwg.dxf"
    if ($roundtrip -notmatch 'ENTITIES' -or $roundtrip -notmatch 'CIRCLE') { throw 'DWG round-trip lost geometry' }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    if ([Math]::Abs([double]::Parse($Matches[1], $culture) * 25.4 / 72 - 297) -gt 0.01 -or
        [Math]::Abs([double]::Parse($Matches[2], $culture) * 25.4 / 72 - 210) -gt 0.01) { throw 'Wrong A4 sheet dimensions' }
    Remove-Item "$tmp/smoke.pdf"
}
try {
    Expand-Archive -LiteralPath ($zip[0].FullName) -DestinationPath "$tmp/portable"
    $portable = "$tmp/portable/$($zip[0].BaseName)"
    & $python "$PSScriptRoot/../../tools/check_docs.py" $portable
    if ($LASTEXITCODE -ne 0) { throw 'Broken links in the shipped manuals' }
    # Never let a build-machine SDK or Qt plugin path mask missing runtime dependencies.
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = ''
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = ''
    CheckPayload $portable
    & $python "$PSScriptRoot/verify-runtime.py" $portable
    if ($LASTEXITCODE -ne 0) { throw 'Portable inventory check failed' }
    Copy-Item -LiteralPath "$portable/RUNTIME_INVENTORY.md" -Destination "$packages/RUNTIME_INVENTORY.md"
    # Test the real default on this fresh runner; forcing /TASKS="" would mask a default-on regression.
    RunChecked ($setup[0].FullName) "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=`"$install`""
    CheckPayload $install
    if (!(Test-Path "$install/unins000.exe")) { throw 'Missing generated uninstaller' }
    if ((Get-FileHash "$install/RUNTIME_INVENTORY.md").Hash -ne (Get-FileHash "$portable/RUNTIME_INVENTORY.md").Hash) {
        throw 'Installer inventory differs from the tested portable inventory'
    }
    & $python "$PSScriptRoot/verify-runtime.py" $install --installed
    if ($LASTEXITCODE -ne 0) { throw 'Installer inventory check failed' }
    if (!(Test-Path "$install/LICENSES/Inno-Setup.txt")) { throw 'Missing installer engine notice' }
    if (Get-ItemProperty -LiteralPath 'HKCU:\Software\Classes\.dxf\OpenWithProgids' -Name 'PlaneDraft.Drawing' -ErrorAction SilentlyContinue) {
        throw 'Installer registered DXF without opt-in'
    }
    RunChecked ($setup[0].FullName) "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=`"$install`" /TASKS=assocdxf"
    $assoc = Get-ItemProperty -LiteralPath 'HKCU:\Software\PlaneDraft\Capabilities\FileAssociations' -Name '.dxf'
    if ($assoc.'.dxf' -ne 'PlaneDraft.Drawing') { throw 'Opt-in DXF association was not registered' }
    $open = (Get-Item -LiteralPath 'HKCU:\Software\Classes\PlaneDraft.Drawing\shell\open\command').GetValue('')
    if ($open -notlike '*PlaneDraft.exe" "%1"') { throw 'Expected a quoted DXF open command' }
} finally {
    $env:Path = $oldPath
    $env:QT_PLUGIN_PATH = $oldPluginPath
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = $oldQpaPath
    if (Test-Path "$install/unins000.exe") { RunChecked "$install/unins000.exe" '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART' }
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
}
if ((Test-Path 'HKCU:\Software\Classes\PlaneDraft.Drawing') -or
    (Test-Path 'HKCU:\Software\PlaneDraft\Capabilities') -or
    (Get-ItemProperty -LiteralPath 'HKCU:\Software\Classes\.dxf\OpenWithProgids' -Name 'PlaneDraft.Drawing' -ErrorAction SilentlyContinue) -or
    (Get-ItemProperty -LiteralPath 'HKCU:\Software\RegisteredApplications' -Name 'PlaneDraft 2D' -ErrorAction SilentlyContinue)) {
    throw 'Uninstall left a registered DXF handler'
}
# Hash the outer release assets too, after both forms have passed the smoke test.
@($zip[0], $setup[0]) | ForEach-Object {
    '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(), $_.Name
} | Set-Content -Encoding Ascii -LiteralPath "$packages/SHA256SUMS"
