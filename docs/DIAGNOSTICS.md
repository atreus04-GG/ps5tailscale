# Diagnostics

The v1.0.3 Beta release includes read-only field collectors for Windows and for Android/Termux, Steam Deck, and Linux.

The collectors use the PS5 FTP service to download existing log files. They do not reboot the console, restart PS5Tailscale, or change its Tailscale registration.

## Runtime log cadence

The persistent logs are intentionally independent so the order of a Rest Mode failure can be reconstructed after the console wakes:

- `/data/tailscale/keepmain-scejscd.log` records injection/thread-start events, a heartbeat approximately every 60 seconds, and immediate KeepMain renewal failures. The SceJSCd KeepMain request itself normally runs every 30 seconds, but successful renewals are summarized by counters in the heartbeat instead of producing one log line per renewal.
- `/data/tailscale/keepmain-shellcore-resident.log` records ShellCore companion state changes, KeepMain renewal returns, and a heartbeat approximately every 30 seconds while the resident exists.
- `/data/tailscale/tailscale.log` records Tailscale lifecycle, connection, watchdog, Remote Play proxy, and error information. Its write frequency is event-driven, with a steady-state resident-alive entry approximately every 60 seconds plus additional activity/error records.

These files currently use append mode and do not have a size cap or rotation policy. This is acceptable for current Beta diagnostics but is not intended as the final long-term behavior.

## Important after a Rest Mode failure

Preserve the failed state before relaunching PS5Tailscale whenever possible. A new payload launch can replace useful evidence.

If FTP is unavailable while the PS5 is asleep, wake the console normally first, then collect the logs before launching another PS5Tailscale payload.

## Windows

From PowerShell in the release folder:

```powershell
.\collect-field-diagnostics.ps1
```

The script asks for the PS5's normal LAN IPv4 address. You can also supply it directly:

```powershell
.\collect-field-diagnostics.ps1 -Ps5Host 192.168.0.123
```

## Android / Termux

Install the required download tool once:

```sh
pkg install curl
```

Optional, for automatic ZIP creation:

```sh
pkg install zip
```

Then run:

```sh
bash ./collect-field-diagnostics.sh
```

If Termux storage permission is configured with `termux-setup-storage`, the result is placed under Android Downloads. Otherwise it falls back to the Termux home directory.

## Steam Deck / Linux

With `bash` and `curl` available:

```sh
bash ./collect-field-diagnostics.sh
```

The default output location is under `~/Downloads/PS5Tailscale-Diagnostics/` when that directory exists.

## Files collected

The collector requests these files when available:

- `/data/tailscale/bootstrap.log`
- `/data/tailscale/tailscale.log`
- `/data/tailscale/keepmain-scejscd.log`
- `/data/tailscale/keepmain-injector.status`
- `/data/tailscale/keepmain-shellcore-resident.log`
- `/data/tailscale/keepmain-shellcore-injector.status`
- `/data/tailscale/keepmain.control`

## Privacy

The collector intentionally does **not** download Tailscale auth keys or the Tailscale state database. Even so, the bundle can contain the PS5 LAN address, Tailscale status output, peer/device names, Tailscale IP addresses, timestamps, and other host/network metadata. **Always review the bundle before sharing it publicly.**

Never share `ps5-auth.key`, `tailscaled.state`, `_current-profile`, private keys, or a full copy of `/data/tailscale`.
