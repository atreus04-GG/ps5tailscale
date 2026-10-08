# Security

## Sensitive information

Never post or upload any of the following to a public issue:

- Tailscale auth keys such as `tskey-auth-...`
- `/data/tailscale/ps5-auth.key`
- `tailscaled.state`, `_current-profile`, or a copy of the full `/data/tailscale` state directory
- private keys, reusable access tokens, or account credentials

The diagnostic collectors are intended to gather operational logs, not authentication secrets. Review the collected diagnostic folder or ZIP before sharing it publicly because it can still contain IP addresses, host information, timestamps, and other network metadata.

## PS5 dashboard and FTP

The PS5Tailscale dashboard listens on the PS5 network interfaces on TCP 8085 so another device on the local network can complete registration. The PS5 FTP service used by the diagnostic collector is also a local homebrew service.

Use PS5Tailscale only on a trusted LAN. Do **not** forward TCP 8085, FTP 2121, the payload loader, or other PS5 homebrew ports directly from your router to the public internet.

The dashboard uses plain HTTP and does not require a separate dashboard password. Anyone who can reach it may be able to use its registration, reconnect, disconnect, or reset controls. Enter Tailscale auth keys only from a trusted network, prefer a short-lived or tightly scoped auth key, and remove the saved auth key after successful registration if you do not need it again.

Treat a Tailscale auth key as a password while it is valid. Prefer a short-lived or tightly scoped key for initial registration, and use the dashboard option to remove the saved auth key after registration if you do not need it anymore.

Diagnostic bundles can contain the PS5 LAN address, Tailscale status output, peer/device names, timestamps, and other network metadata. The collectors intentionally avoid authentication keys and Tailscale state files, but every bundle should still be reviewed before it is shared.

## Reporting a vulnerability

For a security issue that could expose credentials, private state, or remote code execution, use GitHub's private security-advisory reporting flow for this repository rather than posting exploit details in a public issue. For ordinary bugs with no sensitive impact, use the bug report template.
