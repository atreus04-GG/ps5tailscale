param(
    [string]$Ps5Host = '',
    [string]$OutputRoot = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($Ps5Host)) {
    $Ps5Host = (Read-Host 'Enter your PS5 LAN IP address (example: 192.168.0.123)').Trim()
}
if ([string]::IsNullOrWhiteSpace($Ps5Host)) {
    throw 'PS5 LAN IP address is required.'
}
$parsedIp = $null
if (-not [Net.IPAddress]::TryParse($Ps5Host, [ref]$parsedIp) -or
    $parsedIp.AddressFamily -ne [Net.Sockets.AddressFamily]::InterNetwork) {
    throw "Invalid IPv4 address: $Ps5Host"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $OutputRoot = Join-Path $root "logs\field-diagnostic-$stamp"
}
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Fetch-Optional([string]$RemotePath, [string]$Leaf) {
    $destination = Join-Path $OutputRoot $Leaf
    & curl.exe -sS --fail "ftp://$Ps5Host`:2121$RemotePath" -o $destination
    if ($LASTEXITCODE -ne 0) {
        Remove-Item -LiteralPath $destination -Force -ErrorAction SilentlyContinue
        Write-Warning "Could not fetch $RemotePath"
        return
    }
    Write-Host "Fetched $RemotePath -> $destination"
}

Fetch-Optional '/data/tailscale/bootstrap.log' 'bootstrap.log'
Fetch-Optional '/data/tailscale/tailscale.log' 'tailscale.log'
Fetch-Optional '/data/tailscale/keepmain-scejscd.log' 'keepmain-scejscd.log'
Fetch-Optional '/data/tailscale/keepmain-injector.status' 'keepmain-injector.status'
Fetch-Optional '/data/tailscale/keepmain-shellcore-resident.log' 'keepmain-shellcore-resident.log'
Fetch-Optional '/data/tailscale/keepmain-shellcore-injector.status' 'keepmain-shellcore-injector.status'
Fetch-Optional '/data/tailscale/keepmain.control' 'keepmain.control'

$summary = Join-Path $OutputRoot 'collector-summary.txt'
@(
    "collector_time=$(Get-Date -Format o)"
    "ps5_host=$Ps5Host"
) | Set-Content -LiteralPath $summary -Encoding UTF8

try {
    $status = & tailscale status 2>&1
    $status | Add-Content -LiteralPath $summary
} catch {
    "tailscale_status_unavailable=$($_.Exception.Message)" | Add-Content -LiteralPath $summary
}

Write-Host "Diagnostic bundle collected in: $OutputRoot"
Write-Warning 'Review the collected files before sharing them. They can contain LAN/Tailscale addresses, peer/device names, timestamps, and other network metadata.'
Write-Host 'If the PS5 failed in Rest Mode, collect these files before relaunching the Tailscale payload whenever possible.'
