# SPDX-License-Identifier: LGPL-2.1-or-later
# CI only: use a fresh per-user install on a disposable Windows runner, then uninstall it.
param([string]$BuildDir = 'build/windows-msvc')
$ErrorActionPreference = 'Stop'
$packages = Join-Path (Resolve-Path $BuildDir) 'packages'
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
# Compare the actual portable archive against every row, not just a sample of known Qt DLLs.
function CheckInventory([string]$root, [switch]$Installed) {
    $base = (Resolve-Path -LiteralPath $root).Path
    $files = @{}
    foreach ($file in Get-ChildItem -File -Recurse -LiteralPath $base) {
        if ($file.Extension -in '.exe','.dll','.qm') {
            $relative = $file.FullName.Substring($base.Length + 1).Replace('\','/')
            # Inno creates its own uninstaller on the target; it is not an application binary.
            if ($Installed -and $relative -eq 'unins000.exe') { continue }
            $files.Add($relative, $file.FullName)
        }
    }
    $count = 0
    $records = @{}
    foreach ($line in Get-Content -LiteralPath "$base/RUNTIME_INVENTORY.md" -Encoding UTF8) {
        if (!$line.StartsWith('| `')) { continue }
        if ($line -notmatch '^\| `([^`]+)` \| ([^|]+) \| `([0-9a-f]{64})` \|$') { throw "Malformed inventory row: $line" }
        $path = $Matches[1]; $owner = $Matches[2]; $hash = $Matches[3]
        if (!$files.ContainsKey($path)) { throw "Unexpected inventory file: $path" }
        if ((Get-FileHash -LiteralPath $files[$path] -Algorithm SHA256).Hash -ne $hash) { throw "Wrong SHA-256: $path" }
        $files.Remove($path)
        $count++
        if ($owner -notlike 'PlaneDraft (*' -and $owner -notlike 'Microsoft Visual C++ Redistributable (*') {
            $port = $owner.Split(' ')[0]
            if (!$records.ContainsKey($port)) {
                $dir = "$base/LICENSES/ports/$port"
                if ((Get-Item -LiteralPath "$dir/copyright").Length -le 0) { throw "Empty notice for $port" }
                $sbom = Get-Content -Raw -Encoding UTF8 -LiteralPath "$dir/vcpkg.spdx.json" | ConvertFrom-Json
                $recipe = @($sbom.packages | Where-Object { $_.name -ceq $port })
                if ($recipe.Count -ne 1 -or !$recipe[0].versionInfo) { throw "Wrong SPDX record for $port" }
                $notice = (Get-FileHash -LiteralPath "$dir/copyright" -Algorithm SHA256).Hash.ToLowerInvariant()
                $records[$port] = "$port $($recipe[0].versionInfo): vcpkg $($recipe[0].licenseConcluded); copyright SHA-256 $notice"
            }
        }
    }
    if ($count -eq 0 -or $files.Count -ne 0) { throw "Incomplete runtime inventory: $($files.Keys -join ', ')" }
    Write-Host "Verified SHA-256 and ownership records for all $count runtime files"
    if (!$Installed -and $env:GITHUB_ACTIONS -eq 'true') {
        Write-Host ('::notice title=Notices in this Windows payload::' + (($records.Values | Sort-Object) -join '%0A'))
    }
}
function CheckPayload([string]$root) {
    foreach ($path in @('bin/PlaneDraft.exe','bin/pdconvert.exe','bin/platforms/qwindows.dll',
        'bin/Qt6Core.dll','bin/Qt6Widgets.dll','bin/Qt6Svg.dll','bin/translations/qtbase_ru.qm',
        'bin/msvcp140.dll','bin/vcruntime140.dll','plugins/sample_line.dll','plugins/frame_generator.dll',
        'include/pd_plugin_api.h','examples/scripts/pattern.pdl','examples/drawings/cover.dxf',
        'examples/drawings/panel.dxf','THIRD_PARTY_NOTICES.md','RUNTIME_INVENTORY.md',
        'LICENSES/ports/qtbase/copyright','LICENSES/ports/qtsvg/vcpkg.spdx.json')) {
        if (!(Test-Path -LiteralPath (Join-Path $root $path))) { throw "Missing from payload: $path" }
    }
    # --version constructs QApplication (loads QPA) but doesn't require a GPU on the runner.
    RunChecked "$root/bin/PlaneDraft.exe" '--version'
    & "$root/bin/pdconvert.exe" --layout 'Панель' "$root/examples/drawings/panel.dxf" "$tmp/smoke.pdf"
    if ($LASTEXITCODE -ne 0) { throw 'CLI sheet export failed' }
    $pdf = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes("$tmp/smoke.pdf"))
    if (!$pdf.StartsWith('%PDF-') -or $pdf -notmatch '/MediaBox\s*\[0 0 ([.0-9]+) ([.0-9]+)\]') { throw 'Missing vector PDF page' }
    $culture = [Globalization.CultureInfo]::InvariantCulture
    if ([Math]::Abs([double]::Parse($Matches[1], $culture) * 25.4 / 72 - 297) -gt 0.01 -or
        [Math]::Abs([double]::Parse($Matches[2], $culture) * 25.4 / 72 - 210) -gt 0.01) { throw 'Wrong A4 sheet dimensions' }
    Remove-Item "$tmp/smoke.pdf"
}
try {
    Expand-Archive -LiteralPath ($zip[0].FullName) -DestinationPath "$tmp/portable"
    $portable = "$tmp/portable/$($zip[0].BaseName)"
    & python "$PSScriptRoot/../../tools/check_docs.py" $portable
    if ($LASTEXITCODE -ne 0) { throw 'Broken links in the shipped manuals' }
    # Never let a build-machine SDK or Qt plugin path mask missing runtime dependencies.
    $env:Path = "$env:SystemRoot\System32;$env:SystemRoot"
    $env:QT_PLUGIN_PATH = ''
    $env:QT_QPA_PLATFORM_PLUGIN_PATH = ''
    CheckPayload $portable
    CheckInventory $portable
    Copy-Item -LiteralPath "$portable/RUNTIME_INVENTORY.md" -Destination "$packages/RUNTIME_INVENTORY.md"
    # Test the real default on this fresh runner; forcing /TASKS="" would mask a default-on regression.
    RunChecked ($setup[0].FullName) "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR=`"$install`""
    CheckPayload $install
    if (!(Test-Path "$install/unins000.exe")) { throw 'Missing generated uninstaller' }
    if ((Get-FileHash "$install/RUNTIME_INVENTORY.md").Hash -ne (Get-FileHash "$portable/RUNTIME_INVENTORY.md").Hash) {
        throw 'Installer inventory differs from the tested portable inventory'
    }
    CheckInventory $install -Installed
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
