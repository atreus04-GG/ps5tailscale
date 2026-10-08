# Changelog

## v1.0.3 Beta

- PS5-resident Tailscale node with persistent identity and stable tailnet address.
- Remote Play proxy for TCP 9295 and packet-oriented UDP 9295/9296/9297/9302.
- Hybrid Rest Mode keepalive using an SceJSCd resident plus lifecycle-controlled SceShellCore companion.
- Experimental Homebrew Keepalive dashboard/status reporting for compatible already-running services verified during genuine Rest Mode.
- Persistent Rest Mode diagnostics, including SceJSCd heartbeats/failure records and ShellCore companion telemetry.
- Portable read-only field collectors for Windows, Android/Termux, Steam Deck, and Linux.
- Restored the hardware-proven packet-oriented Remote Play UDP relay that existed in the original tester-validated v1.0.3 test artifact but was missing from the first clean public source export.
- Added build-time regression guards that require the packet relay markers and reject the obsolete accepted-flow UDP relay.
- Reproducible public build from pinned upstream inputs and canonical project patches.
- Beginner-focused setup documentation.
- Public-build privacy change: optional UDP diagnostics now default to PS5 loopback (`127.0.0.1`) instead of a development LAN address.

The first clean public candidate used the older accepted-flow UDP relay and reproduced first-session Chiaki latency after a Rest Mode wake. That artifact is superseded. The v1.0.3 Beta release restores the packet-oriented relay, passed the reproduced Rest Mode -> Chiaki wake test without the previous latency issue, and includes the subsequently hardware-validated Homebrew Keepalive status feature.

See the setup, Rest Mode, diagnostics, and release-notes documentation for the public v1.0.3 Beta behavior and limitations.
