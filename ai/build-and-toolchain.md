---
status: stub
verified: none
---
# Build & toolchain

**Covers:** Simutrans-Extended.sln, Makeobj-Extended.sln, Nettool-Extended.sln, Makefile, common.mk, uncommon.mk, config.default.in, config.template, configure.ac, CMakeLists.txt, cmake/, .github/workflows/, nsis/, distribute.sh, findversion.sh, revision.jse, OSX/.

## Seed facts

- MSVC solutions at root: `Simutrans-Extended.sln` (game), `Makeobj-Extended.sln` (pak compiler), `Nettool-Extended.sln`; legacy non-Extended `.sln`/`.vcxproj` variants also present [CODE].
- GNU make: `Makefile` + `common.mk`/`uncommon.mk`; autoconf route via `configure.ac` + `config.default.in` [CODE]. CMake: `CMakeLists.txt` + `cmake/` [CODE].
- CI (.github/workflows/): `ci.yml`, `msvc-build.yml`, `linux-build.yml`, `nightly.yml` (deploys GitHub nightly release when master CI passes), `run-tests.yml` (sanitizer + Squirrel tests) [CODE].
- Packaging: `nsis/` (Windows installer), `distribute.sh`, `play.sh`/`restart.sh`; revision stamped via `findversion.sh`/`revision.jse` → `revision.h` (gitignored) [CODE].
- The SDL3 backend (`sys/simsys_s3.cc`, `sys/clipboard_s3.cc`, `sound/sdl3_sound.cc`) exists on master only; ex-15 does not have it [CODE].
- `zlib-1.2.5/` is local-only (gitignored); zstd support appears optional (`0001-zstd.patch`, `zstd_enable.diff` — local artefacts) [UNVERIFIED].

## Planned sections

- Exact local build commands (MSVC IDE + `msbuild` CLI; make on Linux/WSL; CMake), verified by running them.
- How to run tests locally (binary + pakset + scenario) → [scripting-and-tests](scripting-and-tests.md).
- **Bridgewater-Brunel server automatic-build pipeline — interview slot; nothing documented yet; user to supply details** [RECOLLECTION pending].
- Dependency inventory: SDL2/SDL3, PNG, bz2, zlib, ICU (source of ICU headers is an open question → [utilities-and-threading](utilities-and-threading.md)), MIDI/sound.
- makeobj build & usage → [data-and-pak](data-and-pak.md).

## Open questions

- Where do the `unicode/*` (ICU) headers come from on each platform?
- Does the BB server build from GitHub or a local checkout? Which branches?
