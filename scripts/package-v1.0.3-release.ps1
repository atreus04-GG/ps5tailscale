$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$elf = Join-Path $build 'ps5tailscale-v1.0.3.elf'
$setup = Join-Path $root 'docs\SETUP.md'
$restMode = Join-Path $root 'docs\REST_MODE.md'
$diagnostics = Join-Path $root 'docs\DIAGNOSTICS.md'
$projectLicense = Join-Path $root 'LICENSE'
$thirdPartyNotices = Join-Path $root 'THIRD_PARTY_NOTICES.md'
$licenseBundle = Join-Path $root 'LICENSES'
$collector = Join-Path $root 'scripts\collect-field-diagnostics.ps1'
$androidCollector = Join-Path $root 'scripts\collect-field-diagnostics.sh'
$zip = Join-Path $build 'ps5tailscale-v1.0.3-release-package.zip'
$stage = Join-Path $build 'v1.0.3-release-package'
$fixedZipTime = [DateTimeOffset]::Parse('2026-10-06T00:00:00Z')

$resolvedBuild = [IO.Path]::GetFullPath($build).TrimEnd('\') + '\'
$resolvedStage = [IO.Path]::GetFullPath($stage)
$resolvedZip = [IO.Path]::GetFullPath($zip)
if (-not $resolvedStage.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase) -or
    -not $resolvedZip.StartsWith($resolvedBuild, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to package outside the repository build directory.'
}

foreach ($required in @($elf,$setup,$restMode,$diagnostics,$projectLicense,$thirdPartyNotices,$licenseBundle,$collector,$androidCollector)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing package input: $required" }
}
$elfBytes = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($elf))
if (-not $elfBytes.Contains('127.0.0.1')) {
    throw 'Refusing to package an ELF without the public loopback-only diagnostic default.'
}
$elfHash = (Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash

Remove-Item -LiteralPath $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Copy-Item -LiteralPath $elf -Destination (Join-Path $stage 'ps5tailscale-v1.0.3.elf')
Copy-Item -LiteralPath $setup -Destination (Join-Path $stage 'SETUP.md')
Copy-Item -LiteralPath $restMode -Destination (Join-Path $stage 'REST_MODE.md')
Copy-Item -LiteralPath $diagnostics -Destination (Join-Path $stage 'DIAGNOSTICS.md')
Copy-Item -LiteralPath $projectLicense -Destination (Join-Path $stage 'LICENSE')
Copy-Item -LiteralPath $thirdPartyNotices -Destination (Join-Path $stage 'THIRD_PARTY_NOTICES.md')
Copy-Item -LiteralPath $licenseBundle -Destination (Join-Path $stage 'LICENSES') -Recurse
Copy-Item -LiteralPath $collector -Destination (Join-Path $stage 'collect-field-diagnostics.ps1')
Copy-Item -LiteralPath $androidCollector -Destination (Join-Path $stage 'collect-field-diagnostics.sh')

@(
    "ps5tailscale-v1.0.3.elf  SHA256=$elfHash"
) | Set-Content -LiteralPath (Join-Path $stage 'SHA256SUMS.txt') -Encoding Ascii

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

$zipHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
$zipFile = Get-Item -LiteralPath $zip
Write-Host ("Package bytes={0} SHA256={1}" -f $zipFile.Length,$zipHash)
Write-Host "Package path=$zip"
