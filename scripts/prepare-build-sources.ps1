$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tools = if ([string]::IsNullOrWhiteSpace($env:TSP5_TOOL_ROOT)) { Join-Path $root '.tools' } else { $env:TSP5_TOOL_ROOT }
$go = Join-Path $tools 'go\bin\go.exe'
$goRoot = Join-Path $tools 'go'
$gopath = if ([string]::IsNullOrWhiteSpace($env:TSP5_GOPATH)) { Join-Path $tools 'gopath' } else { $env:TSP5_GOPATH }
$gocache = if ([string]::IsNullOrWhiteSpace($env:TSP5_GOCACHE)) { Join-Path $tools 'gocache' } else { $env:TSP5_GOCACHE }
$libSource = Join-Path $root 'third_party\libtailscale'
$ps5debug = Join-Path $root 'third_party\ps5debug-NG'
$workRoot = Join-Path $root '.build-deps'
$libWork = Join-Path $workRoot 'libtailscale'
$goPatch = Join-Path $root 'patches\go1.25.5-ps5-current.patch'
$tailscalePatch = Join-Path $root 'patches\tailscale-v1.94.1-ps5-current.patch'
$libPatch = Join-Path $root 'patches\libtailscale-59d4bb8-ps5.patch'
$libCommit = '59d4bb82744915815178e0f0776d60026a397ee7'
$ps5debugCommit = 'd32d2d001dbbfd4cd2c0b7d6335b9a49d8a1cb86'

$gitExe = (Get-Command git.exe -ErrorAction Stop).Source
$gitRoot = Split-Path -Parent (Split-Path -Parent $gitExe)
$patchExe = Join-Path $gitRoot 'usr\bin\patch.exe'
if (-not (Test-Path -LiteralPath $patchExe)) {
    throw "Git for Windows patch utility not found at $patchExe. Install a standard Git for Windows distribution."
}

function Assert-Commit([string]$Path, [string]$Expected, [string]$Label) {
    if (-not (Test-Path -LiteralPath (Join-Path $Path '.git'))) {
        throw "$Label is missing. Run: git submodule update --init --recursive"
    }
    $actual = (& git -C $Path rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $actual -ne $Expected) {
        throw "$Label commit mismatch. Expected $Expected, got $actual"
    }
}

function Test-GoPatchReady([string]$Path) {
    $rootSource = Join-Path $Path 'src\crypto\x509\root_bsd.go'
    $runtimeSys = Join-Path $Path 'src\runtime\sys_freebsd_amd64.s'
    $sysTypes = Join-Path $Path 'src\syscall\ztypes_freebsd_amd64.go'
    $syscallAsm = Join-Path $Path 'src\syscall\asm_unix_amd64.s'
    if (-not ((Get-Content -Raw $rootSource) -match '/data/tailscale/ca-certificates\.crt')) { return $false }
    if (-not ((Get-Content -Raw $runtimeSys) -match 'SYS_kevent\s+363')) { return $false }
    if (-not ((Get-Content -Raw $sysTypes) -match 'Lspare\s+int32')) { return $false }
    $stackMaps = [regex]::Matches(
        (Get-Content -Raw $syscallAsm),
        '(?m)^TEXT\s+·(?:Syscall|Syscall6|RawSyscall|RawSyscall6)\(SB\),NOSPLIT,\$32-(?:56|80)\r?\n\s+NO_LOCAL_POINTERS\s*$'
    ).Count
    return $stackMaps -eq 4
}

function Test-TailscalePatchReady([string]$Path) {
    $tsnet = Get-Content -Raw (Join-Path $Path 'tsnet\tsnet.go')
    $ipnlocal = Get-Content -Raw (Join-Path $Path 'ipn\ipnlocal\local.go')
    $magicsock = Get-Content -Raw (Join-Path $Path 'wgengine\magicsock\magicsock.go')
    $derp = Get-Content -Raw (Join-Path $Path 'wgengine\magicsock\derp.go')
    $derpHttp = Get-Content -Raw (Join-Path $Path 'derp\derphttp\derphttp_client.go')
    return ($tsnet -match 'PS5/Prospero is built as freebsd/amd64' -and
            $ipnlocal -match '\[AUTH192 reconfig\] PS5 restoring engine\.Reconfig side effects' -and
            $magicsock -match '\[AUTH193 netcheck\] PS5 restoring ReSTUN' -and
            $derp -match '\[AUTH194 derp\] PS5 restoring receive loop after ServerInfo' -and
            $derpHttp -match 'normalized negative Read n=%d err=%v')
}

function Apply-UnifiedPatch([string]$Path, [string]$Patch, [string]$Label) {
    Push-Location $Path
    try {
        & $patchExe --forward -p1 -i $Patch
        if ($LASTEXITCODE -ne 0) {
            throw "$Label patch apply failed. Start from the pinned clean dependency version."
        }
    } finally {
        Pop-Location
    }
}

Assert-Commit $libSource $libCommit 'libtailscale submodule'
Assert-Commit $ps5debug $ps5debugCommit 'ps5debug-NG submodule'

if (-not (Test-Path -LiteralPath $go)) {
    throw 'Go 1.25.5 is missing from the configured tool root. See docs\BUILDING.md.'
}
foreach ($patch in @($goPatch,$tailscalePatch,$libPatch)) {
    if (-not (Test-Path -LiteralPath $patch)) { throw "Missing patch: $patch" }
}

New-Item -ItemType Directory -Force -Path $workRoot,$gopath,$gocache | Out-Null
if (Test-Path -LiteralPath $libWork) {
    $resolvedRoot = [IO.Path]::GetFullPath($workRoot).TrimEnd('\') + '\'
    $resolvedWork = [IO.Path]::GetFullPath($libWork)
    if (-not $resolvedWork.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace unexpected path: $resolvedWork"
    }
    Remove-Item -LiteralPath $libWork -Recurse -Force
}

& git clone --quiet --no-hardlinks $libSource $libWork
if ($LASTEXITCODE -ne 0) { throw 'Could not create the libtailscale build tree.' }
& git -C $libWork checkout --quiet $libCommit
if ($LASTEXITCODE -ne 0) { throw 'Could not pin the libtailscale build tree.' }
& git -C $libWork apply $libPatch
if ($LASTEXITCODE -ne 0) { throw 'libtailscale patch apply failed.' }
Write-Host 'libtailscale patch applied.'

if (Test-GoPatchReady $goRoot) {
    Write-Host 'Go 1.25.5 PS5 patch already present.'
} else {
    Apply-UnifiedPatch $goRoot $goPatch 'Go 1.25.5'
    if (-not (Test-GoPatchReady $goRoot)) { throw 'Go patch verification failed after application.' }
    Write-Host 'Go 1.25.5 PS5 patch applied.'
}

$env:GOPATH = $gopath
$env:GOCACHE = $gocache
Push-Location $libWork
try {
    & $go mod download
    if ($LASTEXITCODE -ne 0) { throw "go mod download failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

$tailscaleModule = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1'
if (-not (Test-Path -LiteralPath $tailscaleModule)) {
    throw "Pinned tailscale.com v1.94.1 module was not downloaded to $tailscaleModule"
}
Get-ChildItem -LiteralPath $tailscaleModule -Recurse -File | ForEach-Object { $_.IsReadOnly = $false }
if (Test-TailscalePatchReady $tailscaleModule) {
    Write-Host 'tailscale.com v1.94.1 PS5 patch already present.'
} else {
    Apply-UnifiedPatch $tailscaleModule $tailscalePatch 'tailscale.com v1.94.1'
    if (-not (Test-TailscalePatchReady $tailscaleModule)) { throw 'Tailscale patch verification failed after application.' }
    Write-Host 'tailscale.com v1.94.1 PS5 patch applied.'
}

Write-Host 'Public v1.0.3 build sources are prepared.'
Write-Host "libtailscale=$libCommit"
Write-Host "ps5debug-NG=$ps5debugCommit"
