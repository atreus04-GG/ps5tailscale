$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tools = if ([string]::IsNullOrWhiteSpace($env:TSP5_TOOL_ROOT)) { Join-Path $root '.tools' } else { $env:TSP5_TOOL_ROOT }
$sdk = Join-Path $tools 'sdk-v0.43\ps5-payload-sdk'
$llvm = Join-Path $tools 'llvm-mingw\llvm-mingw-20260908-ucrt-x86_64\bin'
$cc = Join-Path $sdk 'win\prospero-clang.cmd'
$repo = Join-Path $root '.build-deps\libtailscale'
$build = Join-Path $root 'build'
$archive = Join-Path $build 'libtailscale_ps5.a'
$core = Join-Path $build 'ps5-libtailscale-auth197-core-v1.0.3.elf'
$coreUnstripped = Join-Path $build 'ps5-libtailscale-auth197-core-v1.0.3-unstripped.elf'
$bootstrap = Join-Path $build 'ps5-libtailscale-auth197-bootstrap-v1.0.3.elf'
$release = Join-Path $build 'ps5tailscale-v1.0.3.elf'
$keepmainBuild = Join-Path $PSScriptRoot 'build-keepmain-resident.ps1'
$keepmainInjector = Join-Path $build 'keepmain\ps5-keepmain-resident-injector.elf'
$keepmainInjectorHash = '4636552D1869BC1932ADC095CDEF4491E6698818F43959ACE0C17A22E77E19CC'
$keepmainInner = Join-Path $build 'keepmain\ps5-keepmain-resident-inner.elf'
$keepmainInnerHash = '6565755668C877FA941B825265252A2030460037DF9A70054BEF5F45B4BE4CAA'
$shellcoreBuild = Join-Path $PSScriptRoot 'build-keepmain-shellcore-companion.ps1'
$shellcoreInjector = Join-Path $build 'keepmain-shellcore\ps5-keepmain-shellcore-injector.elf'
$shellcoreInjectorHash = 'B6FFE41AC32E8A2A627D03253F70327B70D98BF31E40A69ABFC92570E432A3CB'
$shellcoreInner = Join-Path $build 'keepmain-shellcore\ps5-keepmain-shellcore-inner.elf'
$shellcoreInnerHash = '241721F427CBE59F7322AF56567359194CAACFDCE7A6B2DB7B811B9EC9D5B861'
$strip = Join-Path $llvm 'llvm-strip.exe'
$tlsScript = Join-Path $root 'scripts\elf_x86_64_go_tls.x'
$mainScript = Join-Path $sdk 'target\lib\main.script'
$ldScript = Join-Path $sdk 'ldscripts\elf_x86_64.x'
$mainBackup = "$mainScript.v103releasebak"
$ldBackup = "$ldScript.v103releasebak"

foreach ($required in @($cc,$archive,$keepmainBuild,$shellcoreBuild,$strip,$tlsScript,$mainScript,$ldScript,(Join-Path $repo 'tailscale.c'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing v1.0.3 release dependency: $required" }
}

& $keepmainBuild
if ($LASTEXITCODE -ne 0) { throw "SceJSCd KeepMain build failed: $LASTEXITCODE" }
& $shellcoreBuild
if ($LASTEXITCODE -ne 0) { throw "SceShellCore KeepMain build failed: $LASTEXITCODE" }

$hashChecks = @(
    @($keepmainInjector,$keepmainInjectorHash,'SceJSCd injector'),
    @($keepmainInner,$keepmainInnerHash,'SceJSCd inner'),
    @($shellcoreInjector,$shellcoreInjectorHash,'SceShellCore injector'),
    @($shellcoreInner,$shellcoreInnerHash,'SceShellCore inner')
)
foreach ($check in $hashChecks) {
    $actual = (Get-FileHash -LiteralPath $check[0] -Algorithm SHA256).Hash
    if ($actual -ne $check[1]) { throw "$($check[2]) hash mismatch: $actual" }
}

$env:PATH = "$llvm;$env:PATH"
$coreArgs = @(
    '-nostartfiles','-std=c11','-Wall','-Wextra','-Werror','-g',
    '-Wl,--build-id=none',
    '-DTSP5_EXTERNAL_LAUNCHER_BOOTSTRAP=1',
    '-DTSP5_KEEPMAIN_EXTERNAL_TREATMENT=1',
    '-DTSP5_KEEPMAIN_LIFECYCLE_CONTROL=1',
    "-I$(Join-Path $root 'src')",
    "-I$repo",
    '-o',$coreUnstripped,
    (Join-Path $root 'src\libtailscale_auth_probe.c'),
    (Join-Path $root 'src\dashboard.c'),
    (Join-Path $root 'src\homebrew_keepalive.c'),
    (Join-Path $root 'src\ps5_log.c'),
    (Join-Path $repo 'tailscale.c'),
    (Join-Path $sdk 'target\lib\crt1.o'),
    $archive,
    '-lSceNet',
    '-lpthread'
)

Push-Location $root
Copy-Item $mainScript $mainBackup -Force
Copy-Item $ldScript $ldBackup -Force
try {
    Copy-Item $tlsScript $mainScript -Force
    Copy-Item $tlsScript $ldScript -Force
    & $cc @coreArgs
    if ($LASTEXITCODE -ne 0) { throw "v1.0.3 release core link failed: $LASTEXITCODE" }
} finally {
    Copy-Item $mainBackup $mainScript -Force
    Remove-Item $mainBackup -Force
    Copy-Item $ldBackup $ldScript -Force
    Remove-Item $ldBackup -Force
    Pop-Location
}

& $strip --strip-debug -o $core $coreUnstripped
if ($LASTEXITCODE -ne 0) { throw "v1.0.3 release core strip failed: $LASTEXITCODE" }
Remove-Item $coreUnstripped -Force

$bootstrapArgs = @(
    '-nostartfiles','-std=gnu11','-Wall','-Wextra','-Werror','-Os',
    '-Wl,--build-id=none',
    '-DTSP5_V103_RELEASE_CORE_ASSET=1',
    '-DTSP5_SHELLCORE_COMPANION=1',
    "-I$(Join-Path $root 'src')",
    '-o',$bootstrap,
    (Join-Path $root 'src\tailscale_bootstrap.c'),
    (Join-Path $sdk 'target\lib\crt1.o'),
    '-lSceNetCtl',
    '-lSceUserService',
    '-lSceNet',
    '-lSceAppInstUtil'
)

Push-Location $root
try {
    & $cc @bootstrapArgs
    if ($LASTEXITCODE -ne 0) { throw "v1.0.3 release bootstrap link failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

Copy-Item -LiteralPath $bootstrap -Destination $release -Force

$coreText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($core))
if (-not $coreText.Contains('KEEPMAIN_CONTROL active=')) { throw 'v1.0.3 core missing hybrid lifecycle marker.' }
if (-not $coreText.Contains('HOMEBREW_KEEPALIVE_EXPERIMENTAL')) { throw 'v1.0.3 core missing Experimental Homebrew Keepalive marker.' }
if (-not $coreText.Contains('127.0.0.1')) { throw 'Public v1.0.3 core missing loopback-only default diagnostic destination.' }
if (-not $coreText.Contains('udp-packet=9295,9296,9297,9302')) { throw 'v1.0.3 core missing packet-oriented Remote Play UDP relay.' }
if ($coreText.Contains('ready target=%s tcp=9295 udp=9295,9296,9297,9302')) { throw 'v1.0.3 core contains obsolete accepted-flow Remote Play UDP relay marker.' }
$releaseText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($release))
foreach ($marker in @('ps5tailscale-production-jscd','tsp5_keepmain_resident','keepmain-scejscd.log','KEEPMAIN_SCEJSCD_HEARTBEAT','ps5tailscale-shellcore-companion','tsp5_keepmain_shellcore','keepmain-shellcore-resident.log')) {
    if (-not $releaseText.Contains($marker)) { throw "v1.0.3 release missing marker: $marker" }
}

$coreHash = (Get-FileHash -LiteralPath $core -Algorithm SHA256).Hash
$coreFile = Get-Item -LiteralPath $core
$releaseHash = (Get-FileHash -LiteralPath $release -Algorithm SHA256).Hash
$releaseFile = Get-Item -LiteralPath $release

Write-Host 'v1.0.3 hybrid release build: PASS'
Write-Host ("Core bytes={0} SHA256={1}" -f $coreFile.Length,$coreHash)
Write-Host ("Release bytes={0} SHA256={1}" -f $releaseFile.Length,$releaseHash)
Write-Host 'Architecture: SceJSCd KeepMain + lifecycle-controlled SceShellCore companion + Tailscale'
Write-Host 'Persistent logs: tailscale.log, keepmain-scejscd.log, keepmain-shellcore-resident.log'
Write-Host 'Public privacy delta: optional UDP diagnostics default to PS5 loopback (127.0.0.1).'
