# Building v1.0.3 Beta

These instructions are for developers. Normal users should download the release ZIP instead.

The public repository intentionally keeps generated binaries and toolchains out of Git. The release build uses pinned upstream source plus the patches in `patches/`.

## Host environment

The maintained build path uses Windows PowerShell 5.1.

By default the scripts read toolchains from the repository's `.tools` directory. Developers may set `TSP5_TOOL_ROOT` to an alternate toolchain directory without changing tracked files.

For isolated or CI builds, `TSP5_GOPATH` and `TSP5_GOCACHE` can separately select clean module/cache directories. This is useful for proving that a release build does not depend on a previously modified Go module cache.

Use a standard **Git for Windows** installation. In addition to Git itself, source preparation uses Git for Windows' bundled `usr/bin/patch.exe` so the Go and Tailscale patches are applied relative to their exact extracted tool directories rather than the parent project worktree.

Expected tool layout:

```text
.tools/go/                                      Go 1.25.5 Windows/amd64 distribution
.tools/sdk-v0.43/ps5-payload-sdk/              PS5 Payload SDK v0.43 prepared build tree
.tools/llvm-mingw/llvm-mingw-20260908-ucrt-x86_64/
.tools/gopath/
.tools/gocache/
```

The SDK tree must provide at least `win/prospero-clang.cmd`, `target/lib/crt1.o`, `target/lib/main.script`, and `ldscripts/elf_x86_64.x`. Use the upstream PS5 Payload SDK v0.43 release/build instructions; the v0.43 Git tag resolves to commit `d9c9519116944a7f1c22d262012c53b80b3520b7`.

## Pinned inputs

- Go `1.25.5`
- `tailscale.com@v1.94.1`
- `tailscale/libtailscale` commit `59d4bb82744915815178e0f0776d60026a397ee7`
- `OpenSourcereR-dev/ps5debug-NG` commit `d32d2d001dbbfd4cd2c0b7d6335b9a49d8a1cb86`
- PS5 Payload SDK `v0.43` / commit `d9c9519116944a7f1c22d262012c53b80b3520b7`
- LLVM-MinGW `20260908-ucrt-x86_64`

Clone the project with its source submodules:

```powershell
git clone --recurse-submodules https://github.com/atreus04-GG/ps5tailscale.git
cd ps5tailscale
git submodule update --init --recursive
```

## Prepare patched source

After placing the toolchains under `.tools`, run:

```powershell
.\scripts\prepare-build-sources.ps1
```

The script:

1. verifies the exact libtailscale and ps5debug-NG commits;
2. creates an ignored `.build-deps/libtailscale` working copy and applies the PS5 libtailscale patch there;
3. applies the PS5 Go 1.25.5 patch to the local `.tools/go` copy if it is not already applied;
4. downloads the pinned Go modules into the workspace GOPATH;
5. makes the Tailscale module cache writable and applies the `tailscale.com v1.94.1` PS5 patch.

The tracked submodules themselves remain clean.

## Build the Go/libtailscale archive

```powershell
.\scripts\build-libtailscale.ps1
```

This writes:

```text
build/libtailscale_ps5.a
```

The public archive is built with Go `-trimpath` so local workspace and toolchain paths are not embedded in the shipped binary.

## Build the PS5Tailscale ELF

```powershell
.\scripts\build-v1.0.3-release.ps1
```

The build first reproduces both KeepMain injector pairs, links the Tailscale core with the TLS-aware linker script, embeds that core and the KeepMain payloads into the native bootstrap, and writes:

```text
build/ps5tailscale-v1.0.3.elf
```

The public build intentionally defaults optional UDP diagnostic output to `127.0.0.1`. Developers who need remote UDP logging can override `TSP5_REMOTE_LOG_HOST` at compile time in their own local build. Persistent `/data/tailscale/*.log` diagnostics remain enabled.

## Package the release

After the ELF has been hardware-validated:

```powershell
.\scripts\package-v1.0.3-release.ps1
```

The package script computes the actual ELF SHA256 and writes it into `SHA256SUMS.txt`; it does not trust an old development-candidate hash.

## Source provenance

The public v1.0.3 source was cleaned for public layout, build portability, and privacy. It carries the packet-oriented Remote Play UDP relay used by the hardware-validated v1.0.3 build so the tested networking behavior is represented directly in the reproducible source path.

For a release source archive that includes the exact tracked contents of the pinned libtailscale and ps5debug-NG submodules, run `scripts\package-v1.0.3-source.ps1` from a clean tagged public checkout. The generated source ZIP includes a `SOURCE_MANIFEST.txt` with the public commit and dependency commits. The PS5 Payload SDK itself is not vendored; its exact v0.43 source requirement is recorded here and in `THIRD_PARTY_NOTICES.md`.

See `THIRD_PARTY_NOTICES.md` at the repository root for the license provenance of the SDK, ps5debug-NG, Go, Tailscale, libtailscale, and CA bundle.
