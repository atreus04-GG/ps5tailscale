# Third-party notices

PS5Tailscale combines or builds against several upstream open-source projects. Project-owned code is licensed under `GPL-3.0-only`, but third-party files remain under their original licenses. Nothing in the root `LICENSE` file is intended to replace an upstream copyright or license notice.

The release-source tree also includes `LICENSES/`, containing the Go 1.25.5 license and patent notice, MPL-2.0 text, and the license/notice files discovered for all 45 Go modules linked by the v1.0.3 libtailscale archive. `LICENSES/go-modules/INDEX.txt` records the exact module versions and bundled notice filenames.

## PS5 Payload SDK

- Project: `ps5-payload-dev/sdk`
- Release used: `v0.43`
- Commit for that tag: `d9c9519116944a7f1c22d262012c53b80b3520b7`
- License: the upstream v0.43 README states that files under `include/freebsd` are BSD-licensed and, unless explicitly stated otherwise, the remaining SDK is GNU GPL version 3 or later.
- Relevant release input: `target/lib/crt1.o`, produced from the SDK `crt/` sources. The v0.43 `crt/Makefile`, `_start.S`, and `crt.c` state GPL version 3 or later.

The SDK itself is not vendored in this repository. Builders obtain the exact v0.43 SDK separately; see `docs/BUILDING.md`.

## ps5debug-NG

- Project: `OpenSourcereR-dev/ps5debug-NG`
- Commit: `d32d2d001dbbfd4cd2c0b7d6335b9a49d8a1cb86`
- Repository license: GNU GPL version 3.
- The exact injector-support files used by PS5Tailscale carry `SPDX-License-Identifier: GPL-3.0-only` notices.

The pinned upstream source is included as the `third_party/ps5debug-NG` Git submodule. Those files retain their upstream notices. This `GPL-3.0-only` dependency is why the combined PS5Tailscale project uses `GPL-3.0-only` at the root rather than `GPL-3.0-or-later`.

## Tailscale libtailscale

- Project: `tailscale/libtailscale`
- Commit: `59d4bb82744915815178e0f0776d60026a397ee7`
- License: BSD 3-Clause
- Upstream copyright notice: `Copyright (c) 2023 Tailscale & AUTHORS.`

The pinned upstream source is included as the `third_party/libtailscale` Git submodule. PS5-specific changes are represented by `patches/libtailscale-59d4bb8-ps5.patch` and retain the upstream BSD terms for upstream-derived files.

## Tailscale module

- Module: `tailscale.com`
- Version: `v1.94.1`
- License: BSD 3-Clause for Tailscale-authored code, with additional third-party dependencies under their own compatible licenses.

PS5Tailscale applies `patches/tailscale-v1.94.1-ps5-current.patch` to the pinned module during source preparation. Upstream and transitive dependency notices remain applicable.

## Go

- Project: The Go programming language
- Version: `go1.25.5`
- License: BSD-style 3-Clause license included by the Go project.
- Patent notice: the upstream Go `PATENTS` file is included in `LICENSES/Go-1.25.5/`.

PS5Tailscale applies `patches/go1.25.5-ps5-current.patch` to a local Go 1.25.5 toolchain for PS5/Prospero compatibility. The Go toolchain is not redistributed in this repository.

## Mozilla CA certificate bundle / curl CA Extract

`assets/tailscale-ca/ca-certificates.crt` is a PEM bundle generated from Mozilla's NSS root certificate data using the curl CA Extract process. The bundle header identifies the Mozilla source and extraction date. curl documents that the converted output inherits the Mozilla source license, MPL 2.0.

The certificate bundle is data used to establish ordinary TLS trust; it does not change the license of the surrounding program.

The full MPL-2.0 license text is included at `LICENSES/MPL-2.0.txt`.

## PlayStation system libraries

The build refers to PS5 system-library interfaces through the open-source payload SDK. Sony system software is supplied by the console at runtime and is not redistributed by this repository.

## elfldr / payload loader

PS5Tailscale requires a working ELF loader or Payload Manager on the console, but the v1.0.3 release does not compile or redistribute `ps5-payload-dev/elfldr`. It is therefore not included as a source dependency in this public repository.

## Trademarks

PlayStation and PS5 are trademarks of their respective owner. Tailscale is a trademark of Tailscale Inc. Use of names here is descriptive only. PS5Tailscale is not affiliated with or endorsed by Sony Interactive Entertainment or Tailscale Inc.
