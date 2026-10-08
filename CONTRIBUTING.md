# Contributing

Bug reports and focused fixes are welcome. This is PS5 homebrew, so hardware evidence is especially useful.

For a bug report, include the PS5 firmware, jailbreak / loader, wired or Wi-Fi connection, client device and OS, Remote Play client and version, whether the PS5 was awake or in Rest Mode, and how long it had been in Rest before the failure.

If possible, attach the diagnostic output after reviewing it for private information. Some platforms create a ZIP automatically; on Windows you may need to zip the collected folder yourself. Never attach a Tailscale auth key, `tailscaled.state`, private keys, or the full `/data/tailscale` state directory.

Keep pull requests focused. Do not mix later experimental wake/recovery work into fixes for the v1.0.3 Beta release line without clearly separating the changes and validation evidence.

Keep pull requests and issue reports public-safe. Do not include auth keys, Tailscale state, private keys, private IP addresses, developer-local paths, raw private diagnostic bundles, or other secrets/private identifiers.

By contributing code to the main project, you agree that your contribution may be distributed under `GPL-3.0-only`. Third-party files must preserve their original license notices.
