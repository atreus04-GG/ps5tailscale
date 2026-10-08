$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tools = if ([string]::IsNullOrWhiteSpace($env:TSP5_TOOL_ROOT)) { Join-Path $root '.tools' } else { $env:TSP5_TOOL_ROOT }
$sdk = Join-Path $tools 'sdk-v0.43\ps5-payload-sdk'
$llvm = Join-Path $tools 'llvm-mingw\llvm-mingw-20260908-ucrt-x86_64\bin'
$cc = Join-Path $sdk 'win\prospero-clang.cmd'
$crt = Join-Path $sdk 'target\lib\crt1.o'
$build = Join-Path $root 'build\\keepmain-shellcore'
$refs = Join-Path $root 'third_party\ps5debug-NG'
$expectedRefsCommit = 'd32d2d001dbbfd4cd2c0b7d6335b9a49d8a1cb86'
$commonInclude = Join-Path $refs 'common\include'
$commonSource = Join-Path $refs 'common\source'
$installerSource = Join-Path $refs 'installer\source'
$inner = Join-Path $build 'ps5-keepmain-shellcore-inner.elf'
$injector = Join-Path $build 'ps5-keepmain-shellcore-injector.elf'
$embeddedAsm = Join-Path $build 'embedded_inner.S'
$embeddedObj = Join-Path $build 'embedded_inner.o'
$innerCheck = Join-Path $build 'ps5-keepmain-shellcore-inner-check.elf'
$injectorCheck = Join-Path $build 'ps5-keepmain-shellcore-injector-check.elf'
$embeddedAsmCheck = Join-Path $build 'embedded_inner_check.S'
$embeddedObjCheck = Join-Path $build 'embedded_inner_check.o'

function Invoke-CC([string[]]$CompilerArgs, [string]$Label) {
    & $cc @CompilerArgs
    if ($LASTEXITCODE -ne 0) { throw "$Label failed: $LASTEXITCODE" }
}

function Build-Resident([string]$InnerPath, [string]$InjectorPath,
                        [string]$AsmPath, [string]$ObjPath,
                        [string]$Label) {
    $innerArgs = @(
        '-nostartfiles','-std=c11','-Wall','-Wextra','-Werror','-O2',
        '-Wl,--build-id=none','-Wl,--wrap=__patch_init',
        '-o',$InnerPath,
        (Join-Path $root 'src\\keepmain_shellcore_inner.c'),
        (Join-Path $root 'src\hijack_patch_init.c'),
        $crt,
        '-lSceSystemService','-lSceLibcInternal','-lkernel_sys','-lpthread'
    )
    Invoke-CC $innerArgs "$Label inner"

    $innerLeaf = [IO.Path]::GetFileName($InnerPath)
    $asmText = @"
    .section .rodata.embedded_inner,"a",@progbits
    .balign 16
    .global embedded_inner_start
embedded_inner_start:
    .incbin "$innerLeaf"
    .global embedded_inner_end
embedded_inner_end:
    .balign 8
    .global embedded_inner_size
embedded_inner_size:
    .quad embedded_inner_end - embedded_inner_start
"@
    Set-Content -LiteralPath $AsmPath -Value $asmText -Encoding Ascii
    Push-Location $build
    try { Invoke-CC @('-c','-o',$ObjPath,$AsmPath) "$Label embedded inner" }
    finally { Pop-Location }

    $outerArgs = @(
        '-nostartfiles','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-O2',
        '-Wl,--build-id=none',
        "-I$commonInclude",
        '-o',$InjectorPath,
        (Join-Path $root 'src\\keepmain_shellcore_injector.c'),
        (Join-Path $commonSource 'proc_field_offsets.c'),
        (Join-Path $commonSource 'sys_proc_rw.c'),
        (Join-Path $installerSource 'find_pid.c'),
        (Join-Path $installerSource 'kern_rw_fast.c'),
        (Join-Path $installerSource 'proc_elf.c'),
        (Join-Path $installerSource 'proc_remote.c'),
        (Join-Path $installerSource 'stubs.c'),
        $ObjPath,$crt,
        '-lSceLibcInternal','-lkernel_sys','-lSceNet','-lSceNetCtl','-lSceSysCore'
    )
    Invoke-CC $outerArgs "$Label injector"
}

foreach ($required in @(
    $cc,$crt,
    (Join-Path $root 'src\\keepmain_shellcore_inner.c'),
    (Join-Path $root 'src\\keepmain_shellcore_injector.c'),
    (Join-Path $root 'src\hijack_patch_init.c'),
    (Join-Path $commonInclude 'sdk_shim.h'),
    (Join-Path $installerSource 'proc_elf.c'),
    (Join-Path $installerSource 'proc_remote.c')
)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing KeepMain dependency: $required" }
}

$refsCommit = (& git -C $refs rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $refsCommit -ne $expectedRefsCommit) {
    throw "KeepMain reference mismatch: expected $expectedRefsCommit, got $refsCommit"
}
$refsDirty = @(& git -C $refs status --porcelain)
if ($LASTEXITCODE -ne 0 -or $refsDirty.Count -ne 0) {
    throw 'KeepMain reference tree must be clean.'
}

New-Item -ItemType Directory -Force -Path $build | Out-Null
$env:PATH = "$llvm;$env:PATH"
Build-Resident $inner $injector $embeddedAsm $embeddedObj 'KeepMain SceShellCore companion primary'
$innerHash = (Get-FileHash -LiteralPath $inner -Algorithm SHA256).Hash
$injectorHash = (Get-FileHash -LiteralPath $injector -Algorithm SHA256).Hash
Build-Resident $innerCheck $injectorCheck $embeddedAsmCheck $embeddedObjCheck 'KeepMain SceShellCore companion check'
if ((Get-FileHash -LiteralPath $innerCheck -Algorithm SHA256).Hash -ne $innerHash -or
    (Get-FileHash -LiteralPath $injectorCheck -Algorithm SHA256).Hash -ne $injectorHash) {
    throw 'KeepMain SceShellCore companion reproducibility check failed.'
}
Remove-Item -LiteralPath $innerCheck,$injectorCheck,$embeddedAsmCheck,$embeddedObjCheck -Force

foreach ($item in @($inner,$injector)) {
    $file = Get-Item -LiteralPath $item
    $hash = (Get-FileHash -LiteralPath $item -Algorithm SHA256).Hash
    Write-Host ("{0}  {1} bytes  SHA256={2}" -f $file.FullName,$file.Length,$hash)
}
Write-Host "Pinned ps5debug-NG commit: $refsCommit"
Write-Host 'KeepMain SceShellCore companion reproducibility check: PASS'
