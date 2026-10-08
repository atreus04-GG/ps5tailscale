$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tools = if ([string]::IsNullOrWhiteSpace($env:TSP5_TOOL_ROOT)) { Join-Path $root '.tools' } else { $env:TSP5_TOOL_ROOT }
$sdk = Join-Path $tools 'sdk-v0.43\ps5-payload-sdk'
$llvm = Join-Path $tools 'llvm-mingw\llvm-mingw-20260908-ucrt-x86_64\bin'
$go = Join-Path $tools 'go\bin\go.exe'
$gopath = if ([string]::IsNullOrWhiteSpace($env:TSP5_GOPATH)) { Join-Path $tools 'gopath' } else { $env:TSP5_GOPATH }
$gocache = if ([string]::IsNullOrWhiteSpace($env:TSP5_GOCACHE)) { Join-Path $tools 'gocache' } else { $env:TSP5_GOCACHE }
$cc = Join-Path $sdk 'win\prospero-clang.cmd'
$repo = Join-Path $root '.build-deps\libtailscale'
$build = Join-Path $root 'build'
$archive = Join-Path $build 'libtailscale_ps5.a'

$env:PATH = "$llvm;$env:PATH"
$env:TSP5_CLANG = Join-Path $llvm 'clang.exe'
$env:GOOS = 'freebsd'
$env:GOARCH = 'amd64'
$env:CGO_ENABLED = '1'
$env:CC = $cc
$env:GOCACHE = $gocache
$env:GOPATH = $gopath

$runtimeSys = Join-Path $tools 'go\src\runtime\sys_freebsd_amd64.s'
$runtimeDefs = Join-Path $tools 'go\src\runtime\defs_freebsd_amd64.go'
$randSource = Join-Path $tools 'go\src\crypto\internal\sysrand\rand_getrandom.go'
$rootSource = Join-Path $tools 'go\src\crypto\x509\root_bsd.go'
$sysNums = Join-Path $tools 'go\src\syscall\zsysnum_freebsd_amd64.go'
$sysTypes = Join-Path $tools 'go\src\syscall\ztypes_freebsd_amd64.go'
$syscallAsm = Join-Path $tools 'go\src\syscall\asm_unix_amd64.s'
$tsnetSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\tsnet\tsnet.go'
$libtailscaleSource = Join-Path $repo 'tailscale.go'
$ipnLocalSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\ipn\ipnlocal\local.go'
$magicsockSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\wgengine\magicsock\magicsock.go'
$netcheckSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\net\netcheck\netcheck.go'
$captivePortalSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\net\netcheck\captiveportal.go'
$portmapperSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\net\portmapper\portmapper.go'
$derpSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\wgengine\magicsock\derp.go'
$derpHTTPSource = Join-Path $gopath 'pkg\mod\tailscale.com@v1.94.1\derp\derphttp\derphttp_client.go'

if ((Get-Content -Raw $runtimeSys) -notmatch 'SYS_kevent\s+363') { throw 'PS5 Go kevent syscall patch is missing.' }
if ((Get-Content -Raw $runtimeDefs) -match 'ext\s+\[4\]uint64') { throw 'PS5 Go kevent layout patch is missing.' }
if ((Get-Content -Raw $randSource) -notmatch 'PS5/Prospero is built as freebsd/amd64') { throw 'PS5 Go crypto/rand patch is missing.' }
if ((Get-Content -Raw $rootSource) -notmatch '/data/tailscale/ca-certificates.crt') { throw 'PS5 Go x509 CA path patch is missing.' }
if ((Get-Content -Raw $sysNums) -notmatch 'SYS_FSTAT\s+=\s+189') { throw 'PS5 Go fstat syscall patch is missing.' }
if ((Get-Content -Raw $sysNums) -notmatch 'SYS_FSTATAT\s+=\s+493') { throw 'PS5 Go fstatat syscall patch is missing.' }
if ((Get-Content -Raw $sysTypes) -notmatch 'Lspare\s+int32') { throw 'PS5 Go Stat_t layout patch is missing.' }
$syscallAsmText = Get-Content -Raw $syscallAsm
$auth190StackMaps = [regex]::Matches(
    $syscallAsmText,
    '(?m)^TEXT\s+·(?:Syscall|Syscall6|RawSyscall|RawSyscall6)\(SB\),NOSPLIT,\$32-(?:56|80)\r?\n\s+NO_LOCAL_POINTERS\s*$'
).Count
if ($auth190StackMaps -ne 4) { throw "AUTH190 Go syscall stack-map patch is incomplete: found $auth190StackMaps of 4 wrappers." }
if ((Get-Content -Raw $tsnetSource) -notmatch 'PS5/Prospero is built as freebsd/amd64') { throw 'PS5 tsnet os.Executable fallback patch is missing.' }
$libtailscaleText = Get-Content -Raw $libtailscaleSource
if ($libtailscaleText -notmatch 'ps5SignalPolicyOnce\.Do\(func\(\)\s*\{\s*signal\.Ignore\(syscall\.SIGPIPE\)\s*C\.tsp5_go_lib_stage\(1910\)') {
    throw 'AUTH191 libtailscale SIGPIPE-ignore startup policy is missing.'
}
if ($libtailscaleText -notmatch 'ListenPacket\("udp4"' -or
    $libtailscaleText -notmatch 'type remotePlayUDPFlow struct' -or
    $libtailscaleText -notmatch 'udp-packet=9295,9296,9297,9302') {
    throw 'Remote Play packet-oriented UDP relay is missing.'
}
if ($libtailscaleText -match 's\.s\.Listen\(spec\.network, addr\)') {
    throw 'Obsolete accepted-flow UDP Remote Play relay is still present.'
}
$ipnLocalText = Get-Content -Raw $ipnLocalSource
if ($ipnLocalText -notmatch '\[AUTH192 reconfig\] PS5 restoring engine\.Reconfig side effects') {
    throw 'AUTH192 engine.Reconfig restoration marker is missing.'
}
if ($ipnLocalText -match '\[AUTH150 reconfig\] PS5 diagnostic skipping engine\.Reconfig side effects') {
    throw 'AUTH192 engine.Reconfig restoration is incomplete: obsolete skip remains.'
}
$magicsockText = Get-Content -Raw $magicsockSource
if ($magicsockText -notmatch '\[AUTH193 netcheck\] PS5 restoring ReSTUN') {
    throw 'AUTH193 ReSTUN restoration marker is missing.'
}
if ($magicsockText -match '\[AUTH145 netcheck\] PS5 diagnostic skipping ReSTUN') {
    throw 'AUTH193 ReSTUN restoration is incomplete: obsolete skip remains.'
}
$netcheckText = Get-Content -Raw $netcheckSource
$captivePortalText = Get-Content -Raw $captivePortalSource
$portmapperText = Get-Content -Raw $portmapperSource
if ($netcheckText -notmatch 'portmap probe exceeded report deadline; continuing' -or
    $netcheckText -notmatch 'captive portal check exceeded report deadline; continuing' -or
    $netcheckText -match 'waitPortMap\.Wait\(\)' -or
    $captivePortalText -notmatch 'rs\.mu\.Lock\(\)\s+rs\.report\.CaptivePortal\.Set\(found\)\s+rs\.mu\.Unlock\(\)' -or
    $portmapperText -notmatch 'c\.listenPacket\(ctx, "udp4", ":0"\)') {
    throw 'PS5 netcheck deadline patch is missing or incomplete.'
}
$derpText = Get-Content -Raw $derpSource
if ((Get-Content -Raw $derpHTTPSource) -notmatch 'normalized negative Read n=%d err=%v') {
    throw 'PS5 DERP negative-read guard is missing.'
}
if ($derpText -notmatch '\[AUTH194 derp\] PS5 restoring receive loop after ServerInfo') {
    throw 'AUTH194 DERP receive-loop restoration marker is missing.'
}
if ($derpText -match '\[AUTH144 derp\] PS5 diagnostic parking reader after ServerInfo') {
    throw 'AUTH194 DERP receive-loop restoration is incomplete: obsolete reader park remains.'
}

New-Item -ItemType Directory -Force -Path $build | Out-Null
Push-Location $repo
try {
    & $go build -trimpath -buildvcs=false -tags osusergo -buildmode=c-archive -o $archive .
    if ($LASTEXITCODE -ne 0) { throw "libtailscale C archive build failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}
$archiveFile = Get-Item -LiteralPath $archive
$archiveHash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
Write-Host ("Built {0} bytes={1} SHA256={2}" -f $archive,$archiveFile.Length,$archiveHash)
