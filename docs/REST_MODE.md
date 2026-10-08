# Rest Mode

PS5Tailscale v1.0.3 Beta includes PS5-resident Rest Mode keepalive support. It does not require an always-on PC, Raspberry Pi, or subnet-router device to keep the PS5's own Tailscale node alive.

## Beta status

Rest Mode is still experimental. The same architecture has produced different long-duration results on different test runs:

- an external tester reported more than six hours of successful Rest Mode operation;
- another sustained run lost the retained PS5Tailscale path roughly 11–19 minutes after entering Rest Mode;
- short physical-button Rest cycles preserved genuine Sony standby, the PS5 Tailscale path, and Remote Play reachability.

That evidence is useful, but it is not enough to claim universal stability.

## Before entering Rest Mode

Only enter Rest after the PS5Tailscale dashboard is fully **Connected** and displays the PS5's Tailscale IP. Do not enter Rest while the dashboard says **Connecting**, **Starting Tailscale**, **Waiting for address**, or shows no tailnet address.

In the PS5 settings, enable:

- **Stay Connected to the Internet**
- **Enable Turning On PS5 from Network**

## Wake behavior

PXPlay / PSPlay successfully woke the PS5 in testing. Chiaki-ng also worked after the console was awake.

Do **not** assume that manually connecting Chiaki-ng to the sleeping PS5's Tailscale IP will always wake it. Automatic authenticated Chiaki wake is a newer experimental branch and is not part of the v1.0.3 Beta release.

## If Rest Mode fails

If the PS5 Tailscale IP stops responding while the console is in Rest Mode:

1. Do not launch another PS5Tailscale payload.
2. If PS5 FTP is reachable, collect diagnostics immediately.
3. If FTP is not reachable, wake the PS5 normally without auto-launching PS5Tailscale.
4. Collect the logs before launching another PS5Tailscale payload or rebooting again.
5. Record approximately how long the console had been in Rest Mode.

See [DIAGNOSTICS.md](DIAGNOSTICS.md) for the collector commands.

## What the keepalive design does

The v1.0.3 hybrid design uses two PS5-resident components:

- an SceJSCd KeepMain resident intended to remain available across deeper Rest transitions;
- a lifecycle-controlled SceShellCore KeepMain companion used while the Tailscale service is active.

Persistent logs from both components are kept under `/data/tailscale` so a failure can be reconstructed after wake.

### Effect on other running homebrew

Hardware testing shows that the KeepMain request can preserve more than the PS5Tailscale process. In a controlled Rest Mode comparison, Sony discovery continued to report `HTTP/1.1 620 Server Standby` while the following already-running services remained reachable with PS5Tailscale active:

- ELF loader on TCP `9021`;
- FTP server on TCP `2121`;
- PKG Manager web service on TCP `8844`;
- native Remote Play TCP `9295`;
- PS5Tailscale dashboard on TCP `8085`.

The FTP server returned its normal service banner and PKG Manager returned its normal web UI with HTTP `200`, confirming that these services were executing rather than merely leaving sockets visible.

For the hardware validation recorded here, the FTP service identified itself in its live banner as `ftpsrv.elf`, version `1.16-ng-stable`, and is stored in Payload Manager as `ftpsrv-drakmor`. The PKG Manager service on TCP `8844` was provided by `pkgmgr_v1.0.0.elf`. These names identify the exact tested implementations; they are documentation evidence, not live payload detection performed by the dashboard.

The same console was also tested in genuine Rest Mode without PS5Tailscale active. In that control state, TCP `9021`, `2121`, `8844`, and `9295` were not reachable. After PS5Tailscale was started and the console returned to genuine standby, those services became reachable again. A later read-only checkpoint reproduced the same result while Sony discovery still returned `620 Server Standby`.

This supports the conclusion that the active KeepMain path is preserving a broader PS5 main execution/service environment during standby rather than keeping only the Tailscale process alive.

This does not mean every ELF or homebrew application will work in Rest Mode. A payload can still stop itself, close its sockets, depend on a subsystem that Sony suspends, or run in a process that is not retained. Treat this as verified compatibility for the tested resident services, not a universal Rest Mode guarantee.

### Experimental Homebrew Keepalive dashboard

The v1.0.3 Beta release includes an **Experimental Homebrew Keepalive** dashboard panel. User-facing labels use **Rest Mode Keepalive** and **Rest Mode Companion** instead of exposing the internal SceJSCd/SceShellCore process names. The panel reads the existing internal KeepMain initialization/control files and lists the service ports already verified in hardware testing (`9021`, `2121`, `8844`, and `9295`). `/api/status` exposes the KeepMain initialization/control state plus the same verified-compatibility list as machine-readable JSON.

The dashboard does not open live probe connections to those services, does not start, stop, restart, or modify third-party homebrew, and does not alter the KeepMain renewal cadence or injection design. The initialization flags show that the injectors completed initialization; they are not a live thread-health guarantee.

Fresh-cycle PS5 hardware validation of the Homebrew Keepalive dashboard/status behavior passed on 2026-10-08. The tested candidate ELF is `28,637,440` bytes with SHA256 `ECFBB8FB8BDC0047F196753E3562D249087F8FCDEAA283A778C56EF75CA3A956`.
