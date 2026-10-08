$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$tools = if ([string]::IsNullOrWhiteSpace($env:TSP5_TOOL_ROOT)) { Join-Path $root '.tools' } else { $env:TSP5_TOOL_ROOT }
$go = Join-Path $tools 'go\bin\go.exe'
$gopath = if ([string]::IsNullOrWhiteSpace($env:TSP5_GOPATH)) { Join-Path $tools 'gopath' } else { $env:TSP5_GOPATH }
$gocache = if ([string]::IsNullOrWhiteSpace($env:TSP5_GOCACHE)) { Join-Path $tools 'gocache' } else { $env:TSP5_GOCACHE }
$libWork = Join-Path $root '.build-deps\libtailscale'
$outRoot = Join-Path $root 'LICENSES'
$moduleOut = Join-Path $outRoot 'go-modules'
$goOut = Join-Path $outRoot 'Go-1.25.5'

foreach ($required in @($go,$libWork,(Join-Path $tools 'go\LICENSE'),(Join-Path $tools 'go\PATENTS'))) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Missing license-inventory input: $required" }
}

if (Test-Path -LiteralPath $moduleOut) {
    $resolvedRoot = [IO.Path]::GetFullPath($outRoot).TrimEnd('\') + '\'
    $resolvedTarget = [IO.Path]::GetFullPath($moduleOut)
    if (-not $resolvedTarget.StartsWith($resolvedRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace unexpected path: $resolvedTarget"
    }
    Remove-Item -LiteralPath $moduleOut -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $moduleOut,$goOut | Out-Null
Copy-Item -LiteralPath (Join-Path $tools 'go\LICENSE') -Destination (Join-Path $goOut 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $tools 'go\PATENTS') -Destination (Join-Path $goOut 'PATENTS.txt') -Force

$env:GOOS = 'freebsd'
$env:GOARCH = 'amd64'
$env:CGO_ENABLED = '1'
$env:CC = Join-Path $tools 'sdk-v0.43\ps5-payload-sdk\win\prospero-clang.cmd'
$env:GOPATH = $gopath
$env:GOCACHE = $gocache

Push-Location $libWork
try {
    $lines = @(& $go list -deps -tags osusergo -f '{{if .Module}}{{.Module.Path}}|{{.Module.Version}}|{{.Module.Dir}}{{end}}' .)
    if ($LASTEXITCODE -ne 0) { throw "go list -deps failed: $LASTEXITCODE" }
} finally {
    Pop-Location
}

$modules = $lines | Where-Object { $_ -and $_ -match '\|' } | Sort-Object -Unique
$index = New-Object System.Collections.Generic.List[string]
$missing = New-Object System.Collections.Generic.List[string]

foreach ($line in $modules) {
    $parts = $line -split '\|',3
    $module = $parts[0]
    $version = $parts[1]
    $dir = $parts[2]
    $displayVersion = if ([string]::IsNullOrWhiteSpace($version)) { 'pinned-source' } else { $version }
    $leaf = (($module + '@' + $displayVersion) -replace '[^A-Za-z0-9._@-]','__')
    $destination = Join-Path $moduleOut $leaf
    $licenseFiles = @(Get-ChildItem -LiteralPath $dir -File | Where-Object { $_.Name -match '^(LICENSE|COPYING|COPYRIGHT|NOTICE|PATENTS)(\..*)?$' } | Sort-Object Name)
    if ($licenseFiles.Count -eq 0) {
        $missing.Add("$module|$displayVersion|$dir")
        continue
    }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    foreach ($licenseFile in $licenseFiles) {
        Copy-Item -LiteralPath $licenseFile.FullName -Destination (Join-Path $destination $licenseFile.Name) -Force
    }
    $names = ($licenseFiles | ForEach-Object { $_.Name }) -join ','
    $index.Add("$module|$displayVersion|$names")
}

$index | Set-Content -LiteralPath (Join-Path $moduleOut 'INDEX.txt') -Encoding UTF8
if ($missing.Count -gt 0) {
    $missing | Set-Content -LiteralPath (Join-Path $moduleOut 'MISSING_LICENSE_FILES.txt') -Encoding UTF8
    throw "License files were not found at the root of $($missing.Count) linked modules. See LICENSES\go-modules\MISSING_LICENSE_FILES.txt."
}

Write-Host "License bundle generated for $($modules.Count) linked Go modules."
