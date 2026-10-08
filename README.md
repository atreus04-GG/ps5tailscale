# PS5Tailscale

**Beta PS5 homebrew that gives a jailbroken PlayStation 5 its own Tailscale IP for private remote access and Remote Play.**

PS5Tailscale runs the Tailscale node on the PS5 itself. The normal setup does **not** require an always-on PC, Raspberry Pi, subnet router, or external relay device in your home.

> **Beta:** Awake use and Remote Play have worked in testing. Rest Mode is still experimental and can behave differently between consoles or firmware setups. Do not treat this release as a guaranteed long-duration Rest Mode solution.

## How to use PS5Tailscale

For the complete installation, registration, and Remote Play instructions, see the **[Setup and Usage Guide](docs/SETUP.md)**.

For Rest Mode-specific instructions and limitations, see **[Rest Mode](docs/REST_MODE.md)**.

For troubleshooting and diagnostic collection, see **[Troubleshooting](docs/TROUBLESHOOTING.md)** and **[Diagnostics](docs/DIAGNOSTICS.md)**.

This is the sanitized public release repository for `https://github.com/atreus04-GG/ps5tailscale`. Ongoing engineering remains in a separate development repository.

## What it does

- Gives the PS5 a stable `100.x.x.x` Tailscale address.
- Provides a small PS5 dashboard for first-time Tailscale registration and connection status.
- Proxies the PS5 Remote Play ports through the PS5-resident Tailscale node.
- Includes PS5-resident Rest Mode keepalive support and persistent diagnostics.
- In tested Rest Mode sessions, the KeepMain path also allowed compatible already-running homebrew services such as the ELF loader, FTP server, and PKG Manager web service to remain reachable while the PS5 still reported genuine Sony standby.
- Includes Windows and Android/Termux/Linux/Steam Deck log collectors.

## What it does not do

- It is **not** a system-wide VPN for every PS5 application.
- It does not make a fully powered-off PS5 run Tailscale.
- It does not guarantee that a manual Chiaki-ng connection can wake a sleeping PS5. That automatic wake path remains experimental.
- It does not require or install a normal Tailscale subnet router.

## Current status

| Feature | Status |
| --- | --- |
| Tailscale registration and stable PS5 tailnet IP | Working |
| Remote Play while PS5 is awake | Working |
| Chiaki-ng through the PS5 Tailscale IP | Working while awake / after wake |
| PXPlay / PSPlay wake in tested setups | Working in testing |
| Rest Mode tailnet retention | **Beta / experimental** |
| Compatible resident homebrew retention in Rest Mode | **Verified in testing / experimental** |
| Manual Chiaki-ng connection waking Rest Mode automatically | Experimental, not release-ready |

An external tester reported more than six hours of Rest Mode operation with the v1.0.3 hybrid design, while another sustained test lost the retained homebrew path after roughly 11–19 minutes in Rest. That variation is why this is a Beta release.

## Requirements

- A jailbroken PS5 that can run ELF payloads.
- A working ELF loader / Payload Manager.
- A Tailscale account.
- Tailscale on the client device you will connect from.
- Chiaki-ng or PXPlay / PSPlay for Remote Play.

For the beginner walkthrough, see **[docs/SETUP.md](docs/SETUP.md)**.

For common problems, see **[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)**.

## Download

Download the **v1.0.3 Beta** package from the repository's GitHub Releases page: `https://github.com/atreus04-GG/ps5tailscale/releases`. The release notes include SHA256 values for both the ZIP and the ELF so you can verify the download.

Do not treat experimental development-branch binaries or issue attachments as the public release.

## Quick start

1. Enable Remote Play on the PS5.
2. Run `ps5tailscale-v1.0.3.elf` once after a clean boot.
3. Open **Media → Tailscale**.
4. Create a Tailscale auth key and register the PS5 from the dashboard.
5. Wait until the dashboard says **Connected** and shows a `100.x.x.x` address.
6. Put your Remote Play client on the same tailnet and use the PS5's Tailscale IP as the host.

Do not start a second PS5Tailscale payload on the same boot. Reboot first if the resident service needs to be restarted.

## Rest Mode

Rest Mode support is PS5-resident and does not require an always-on companion device, but it is still Beta. Before entering Rest, make sure the dashboard is fully **Connected** and has a Tailscale address. See **[docs/REST_MODE.md](docs/REST_MODE.md)** for the current limitations and failure procedure.

The KeepMain behavior is broader than the Tailscale process itself on tested hardware. When compatible homebrew services were already running before Rest Mode, the ELF loader, FTP server, PKG Manager web UI, Remote Play service, and PS5Tailscale dashboard all remained reachable while Sony discovery still reported `620 Server Standby`. This is an observed compatibility behavior, not a guarantee that every payload or PS5 subsystem will remain available in Rest Mode.

The dashboard includes **Experimental Homebrew Keepalive** status. It reports the existing KeepMain initialization/control state and lists the services already verified in hardware testing. It does not probe, start, stop, restart, or control those services. The fresh-cycle PS5 hardware test of this dashboard/status feature passed on 2026-10-08.

## Diagnostics

If a Rest Mode failure happens, preserve the state before relaunching PS5Tailscale whenever possible. The collectors in `scripts/` download the persistent logs over PS5 FTP without restarting the console.

See **[docs/DIAGNOSTICS.md](docs/DIAGNOSTICS.md)**.

## Security

Tailscale auth keys and `/data/tailscale` state are sensitive. Never post them in an issue. Do not expose the PS5 dashboard or FTP service directly to the public internet. See **[SECURITY.md](SECURITY.md)**.

Public builds default optional UDP diagnostics to PS5 loopback (`127.0.0.1`). A developer can explicitly override `TSP5_REMOTE_LOG_HOST` at build time for local debugging.

## Building from source

The release build is reproducible from pinned upstream inputs and patches. Start with:

```powershell
git clone --recurse-submodules https://github.com/atreus04-GG/ps5tailscale.git
cd ps5tailscale
.\scripts\prepare-build-sources.ps1
.\scripts\build-libtailscale.ps1
.\scripts\build-v1.0.3-release.ps1
```

Toolchain installation and exact upstream pins are documented in **[docs/BUILDING.md](docs/BUILDING.md)**.

## License and credits

Project-owned code is distributed under **GPL-3.0-only**. Individual third-party components keep their original licenses and notices; see **[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)**.

### Upstream projects and developer references

PS5Tailscale was built with help from, and technical reference to, several open-source projects and developers:

- [@ps5-payload-dev](https://github.com/ps5-payload-dev) — [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk) used for the PS5 build toolchain, plus [elfldr](https://github.com/ps5-payload-dev/elfldr) as a loader/process-lifecycle reference.
- [@OpenSourcereR-dev](https://github.com/OpenSourcereR-dev) — [ps5debug-NG](https://github.com/OpenSourcereR-dev/ps5debug-NG), used by the KeepMain injector path and as a PS5 process/networking reference.
- [@tailscale](https://github.com/tailscale) — [libtailscale](https://github.com/tailscale/libtailscale) and the upstream Tailscale codebase that PS5Tailscale adapts for the console.
- [@itsPLK](https://github.com/itsPLK) — [PS5 Payload Manager](https://github.com/itsPLK/ps5-payload-manager), used as a reference for the PS5 payload/launcher workflow and homebrew integration patterns.
- [@streetpea](https://github.com/streetpea) — [chiaki-ng](https://github.com/streetpea/chiaki-ng), used as a Remote Play protocol/client-behavior reference while validating the PS5-resident proxy.
- [@SalamiTheMan](https://github.com/SalamiTheMan) — [chiaki-ng-android-extended](https://github.com/SalamiTheMan/chiaki-ng-android-extended), used during Android Chiaki compatibility investigation and comparison.
These references do not imply endorsement or affiliation. Some are direct build dependencies; others were consulted as engineering references while diagnosing PS5 networking, lifecycle, loader, and Remote Play behavior. Their code and licenses remain governed by their respective upstream projects.

PS5Tailscale is an independent homebrew project and is not affiliated with or endorsed by Sony Interactive Entertainment or Tailscale Inc. PlayStation and PS5 are trademarks of their respective owner; Tailscale is a trademark of Tailscale Inc.
