$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$stage = Join-Path $build 'v1.0.3-source-package'
$zip = Join-Path $build 'ps5tailscale-v1.0.3-source.zip'
$fixedZipTime = [DateTimeOffset]::Parse('2026-10-07T00:00:00Z')

$libtailscale = Join-Path $root 'third_party\libtailscale'
$ps5debug = Join-Path $root 'third_party\ps5debug-NG'
$libtailscaleCommit = '59d4bb82744915815178e0f0776d60026a397ee7'
$ps5debugCommit = 'd32d2d001dbbfd4cd2c0b7d6335b9a49d8a1cb86'
$sdkCommit = 'd9c9519116944a7f1c22d262012c53b80b3520b7'

function Invoke-GitLines([string]$WorkingDirectory, [string[]]$Arguments) {
    $lines = & git -C $WorkingDirectory @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git failed in ${WorkingDirectory}: git $($Arguments -join ' ')"
    }
    return @($lines)
}

function Assert-CleanRepository([string]$Path, [string]$Label) {
    $dirty = Invoke-GitLines $Path @('status','--porcelain=v1','--untracked-files=all')
    if ($dirty.Count -ne 0) {
        throw "$Label must be clean before creating a source archive."
    }
}

function Assert-Commit([string]$Path, [string]$Expected, [string]$Label) {
    $actualLines = @(Invoke-GitLines $Path @('rev-parse','HEAD'))
    if ($actualLines.Count -lt 1) {
        throw "$Label commit could not be read."
    }
    $actual = ([string]$actualLines[0]).Trim()
    if ($actual -ne $Expected) {
        throw "$Label commit mismatch. Expected $Expected, got $actual"
    }
}

function Copy-TrackedFiles([string]$Repo, [string]$Destination, [string[]]$Skip = @()) {
    $tracked = Invoke-GitLines $Repo @('ls-files')
    foreach ($relative in $tracked) {
        if ($Skip -contains $relative) {
            continue
        }
        $source = Join-Path $Repo ($relative.Replace('/','\'))
        if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
            continue
        }
        $target = Join-Path $Destination ($relative.Replace('/','\'))
        $parent = Split-Path -Parent $target
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
        Copy-Item -LiteralPath $source -Destination $target
    }
}

Assert-CleanRepository $root 'Public repository'
Assert-CleanRepository $libtailscale 'libtailscale submodule'
Assert-CleanRepository $ps5debug 'ps5debug-NG submodule'
Assert-Commit $libtailscale $libtailscaleCommit 'libtailscale submodule'
Assert-Commit $ps5debug $ps5debugCommit 'ps5debug-NG submodule'

New-Item -ItemType Directory -Force -Path $build | Out-Null
$resolvedBuild = [IO.Path]::GetFullPath($build).TrimEnd('\') + '\'
$resolvedStage = [IO.Path]::GetFullPath($stage)
$resolvedZip = [IO.Path]::GetFullPath($zip)
if (-not $resolvedStage.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedZip.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to package source outside the repository build directory.'
}

Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null

Copy-TrackedFiles $root $stage @('third_party/libtailscale','third_party/ps5debug-NG')
Copy-TrackedFiles $libtailscale (Join-Path $stage 'third_party\libtailscale')
Copy-TrackedFiles $ps5debug (Join-Path $stage 'third_party\ps5debug-NG')

$publicCommitLines = @(Invoke-GitLines $root @('rev-parse','HEAD'))
if ($publicCommitLines.Count -lt 1) {
    throw 'Public repository commit could not be read.'
}
$publicCommit = ([string]$publicCommitLines[0]).Trim()
@(
    'PS5Tailscale v1.0.3 Beta source manifest'
    "public_commit=$publicCommit"
    "libtailscale_commit=$libtailscaleCommit"
    "ps5debug_ng_commit=$ps5debugCommit"
    "ps5_payload_sdk_v0_43_commit=$sdkCommit"
    'ps5_payload_sdk_note=The SDK is not vendored in this source archive; see THIRD_PARTY_NOTICES.md and docs/BUILDING.md for the exact upstream version/source requirement.'
) | Set-Content -LiteralPath (Join-Path $stage 'SOURCE_MANIFEST.txt') -Encoding Ascii

Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zipStream = [IO.File]::Open($zip, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
try {
    $archive = New-Object IO.Compression.ZipArchive($zipStream, [IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        $files = Get-ChildItem -LiteralPath $stage -File -Recurse | Sort-Object FullName
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($stage.Length).TrimStart('\').Replace('\','/')
            $entry = $archive.CreateEntry($relative, [IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $fixedZipTime
            $entryStream = $entry.Open()
            $sourceStream = [IO.File]::OpenRead($file.FullName)
            try {
                $sourceStream.CopyTo($entryStream)
            } finally {
                $sourceStream.Dispose()
                $entryStream.Dispose()
            }
        }
    } finally {
        $archive.Dispose()
    }
} finally {
    $zipStream.Dispose()
}

Remove-Item -LiteralPath $stage -Recurse -Force
$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
$item = Get-Item -LiteralPath $zip
Write-Host ("Source package bytes={0} SHA256={1}" -f $item.Length,$hash)
Write-Host "Source package path=$zip"
