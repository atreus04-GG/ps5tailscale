#!/usr/bin/env bash

set -u

ps5_host="${1:-}"
output_root="${2:-}"

if ! command -v curl >/dev/null 2>&1; then
    printf '%s\n' 'curl is required. In Termux run: pkg install curl'
    exit 1
fi

if [ -z "$ps5_host" ]; then
    printf '%s' 'Enter your PS5 LAN IP address (example: 192.168.0.123): '
    IFS= read -r ps5_host
fi

is_ipv4() {
    local ip="$1"
    local a b c d extra octet

    IFS=. read -r a b c d extra <<EOF
$ip
EOF
    [ -z "${extra:-}" ] || return 1
    for octet in "$a" "$b" "$c" "$d"; do
        case "$octet" in
            ''|*[!0-9]*) return 1 ;;
        esac
        [ "$octet" -ge 0 ] 2>/dev/null || return 1
        [ "$octet" -le 255 ] 2>/dev/null || return 1
    done
    return 0
}

if ! is_ipv4 "$ps5_host"; then
    printf 'Invalid IPv4 address: %s\n' "$ps5_host" >&2
    exit 1
fi

timestamp="$(date '+%Y%m%d-%H%M%S')"
if [ -z "$output_root" ]; then
    if [ -d "$HOME/storage/downloads" ]; then
        output_root="$HOME/storage/downloads/PS5Tailscale-Diagnostics/field-diagnostic-$timestamp"
        platform_name="android-termux"
    elif [ -d "$HOME/Downloads" ]; then
        output_root="$HOME/Downloads/PS5Tailscale-Diagnostics/field-diagnostic-$timestamp"
        platform_name="linux"
    else
        output_root="$HOME/PS5Tailscale-Diagnostics/field-diagnostic-$timestamp"
        platform_name="linux"
    fi
else
    if [ -d "$HOME/storage/downloads" ]; then
        platform_name="android-termux"
    else
        platform_name="linux"
    fi
fi

mkdir -p "$output_root" || {
    printf 'Could not create output folder: %s\n' "$output_root" >&2
    exit 1
}

fetch_optional() {
    local remote_path="$1"
    local leaf="$2"
    local destination="$output_root/$leaf"

    if curl -sS --fail "ftp://$ps5_host:2121$remote_path" -o "$destination"; then
        printf 'Fetched %s -> %s\n' "$remote_path" "$destination"
    else
        rm -f "$destination"
        printf 'Warning: could not fetch %s\n' "$remote_path" >&2
    fi
}

fetch_optional '/data/tailscale/bootstrap.log' 'bootstrap.log'
fetch_optional '/data/tailscale/tailscale.log' 'tailscale.log'
fetch_optional '/data/tailscale/keepmain-scejscd.log' 'keepmain-scejscd.log'
fetch_optional '/data/tailscale/keepmain-injector.status' 'keepmain-injector.status'
fetch_optional '/data/tailscale/keepmain-shellcore-resident.log' 'keepmain-shellcore-resident.log'
fetch_optional '/data/tailscale/keepmain-shellcore-injector.status' 'keepmain-shellcore-injector.status'
fetch_optional '/data/tailscale/keepmain.control' 'keepmain.control'

summary="$output_root/collector-summary.txt"
{
    printf 'collector_time=%s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')"
    printf 'ps5_host=%s\n' "$ps5_host"
    printf 'platform=%s\n' "$platform_name"
    if command -v tailscale >/dev/null 2>&1; then
        printf '\n--- tailscale status ---\n'
        tailscale status 2>&1 || true
    else
        printf 'tailscale_status_unavailable=tailscale CLI not installed; CLI status is optional for PS5 log collection.\n'
    fi
} > "$summary"

printf '\nDiagnostic bundle collected in:\n%s\n' "$output_root"

if command -v zip >/dev/null 2>&1; then
    zip_path="${output_root}.zip"
    parent_dir="$(dirname "$output_root")"
    folder_name="$(basename "$output_root")"
    (
        cd "$parent_dir" || exit 1
        zip -qr "$zip_path" "$folder_name"
    )
    if [ -f "$zip_path" ]; then
        printf 'Diagnostic ZIP created (REVIEW BEFORE SHARING):\n%s\n' "$zip_path"
    fi
else
    printf '%s\n' 'Optional: install zip with `pkg install zip` to create a diagnostic ZIP automatically.'
fi

printf '%s\n' 'Review the collected files before sharing them. They can contain LAN/Tailscale addresses, peer/device names, timestamps, and other network metadata.'
printf '%s\n' 'If the PS5 failed in Rest Mode, collect these files before relaunching the Tailscale payload whenever possible.'
