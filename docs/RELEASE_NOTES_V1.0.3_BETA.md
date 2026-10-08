# PS5Tailscale v1.0.3 Beta — release notes

PS5Tailscale v1.0.3 Beta runs a Tailscale node on the PS5 itself for private remote access and Remote Play. The normal setup does not require an always-on PC, Raspberry Pi, external relay, or normal subnet-router device.

## Highlights

- PS5-resident Tailscale identity with a stable tailnet address.
- Remote Play proxy for TCP 9295 and packet-oriented UDP 9295/9296/9297/9302.
- Hybrid Rest Mode keepalive using an SceJSCd resident plus a lifecycle-controlled SceShellCore companion.
- Experimental Homebrew Keepalive dashboard/status reporting for compatible already-running services verified during genuine Rest Mode.
- Persistent Rest Mode diagnostics and read-only collectors for Windows, Android/Termux, Steam Deck, and Linux.
- Reproducible public source built from pinned upstream inputs and public patches.
- Public privacy cleanup: optional UDP diagnostics default to PS5 loopback instead of a developer LAN destination.

## Beta warning

Rest Mode remains experimental. Testing has produced both successful long-duration runs and failures on other runs/setups. Treat Rest Mode support as Beta and preserve diagnostics before relaunching the payload after a failure.

Manual Chiaki-ng connection to a sleeping PS5 is not guaranteed to wake the console in v1.0.3. Later automatic-wake experiments are not part of this release.

The PS5 dashboard uses plain HTTP and is intended for a trusted LAN. Do not port-forward the dashboard or other PS5 homebrew services to the public internet. Prefer a short-lived Tailscale auth key and remove the saved auth key after registration when it is no longer needed.

## Release hashes

- `ps5tailscale-v1.0.3.elf` — SHA256 `ECFBB8FB8BDC0047F196753E3562D249087F8FCDEAA283A778C56EF75CA3A956` — `28,637,440` bytes
- `ps5tailscale-v1.0.3-release-package.zip` — SHA256 `5D33815AC443A4615ECA2CB14D195DAF42C9CEAA70799C6C0631847FB5F2B171` — `9,923,738` bytes

These hashes identify the release ELF and binary/documentation package. Verify downloaded files before use.

## Documentation

Start with `docs/SETUP.md`. Rest Mode limitations are in `docs/REST_MODE.md`; diagnostic collection and privacy guidance are in `docs/DIAGNOSTICS.md` and `SECURITY.md`.
