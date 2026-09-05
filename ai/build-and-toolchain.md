---
status: draft
verified: ex-15 @ b83291e56
---
# Build & toolchain

**Covers:** Simutrans-Extended.sln/.vcxproj, Makeobj-Extended.sln/.vcxproj, Nettool-Extended.sln +
Nettool.vcxproj, Makefile, common.mk, uncommon.mk, config.template, config.default.in, configure.ac,
configs/, CMakeLists.txt, cmake/, makeobj/ and nettools/ build files, .github/ (workflows,
init_env.py, toolchain_mingw.cmake), nsis/, cleanup_code.sh, distribute.sh, get_pak.sh, play.sh,
restart.sh, findversion.sh, revision.jse, scripts/run-automated-tests.sh, OSX/osx.mk.

## Overview

Four build paths coexist:

1. **MSVC handwritten projects** — the maintainer's daily Windows path; outputs land in `simutrans/`.
2. **CMake** (`CMakeLists.txt` + `cmake/`) — the CI path and the cross-platform route; targets
   `simutrans-extended`, `makeobj-extended`, `nettool-extended`.
3. **GNU make** (`Makefile` + `common.mk`, `config.$(CFG)`) — Linux/MinGW/server builds, incl. the
   Bridgewater-Brunel nightly pipeline and the CI sanitizer test builds.
4. **GitHub Actions** (`.github/workflows/`) — CI builds, sanitizer tests, GitHub nightlies.

## MSVC path (maintainer's Windows recipe)

- `Simutrans-Extended.sln` → `Simutrans-Extended.vcxproj`: ~30 configurations (Debug, Release,
  "Optimised debug", server, no-randomness, single-threaded, Dr. Memory, IP-v4-only, SDL 2, legacy
  OpenGL) × Win32/x64. Some solution configurations map to differently-named project configurations.
- Toolsets are mixed: most configurations `v140_xp` (VS2015+XP toolset); the newer ones (`Debug`,
  `Optimised debug`, `Release|x64`, graphical/non-graphical server x64) are `v142` (VS2019).
  `WindowsTargetPlatformVersion` is `10.0` (latest installed SDK). `Debug|x64` has `EnableASAN` and
  produces static-analysis warnings (C6xxx/C26xxx series) — builds are slow and warning-heavy.
- Third-party dependencies are resolved via per-configuration global `IncludePath`/`LibraryPath`
  pointing at sibling source-built trees one level above the repo: `..\OpenTTD\win32\library` and
  `..\OpenTTD\win64\library` (zlibstat, libpng, pthreadVCE2, ICU), `..\bzip\include|lib|lib\x64`
  (libbz2), `..\zstd-1.4.4` (libzstd_static), `..\freetype-2.10.1` (freetype), `..\miniupnpc`
  (miniupnpc). All present on the maintainer's machine.
- Link inputs per configuration also name these libs explicitly (`AdditionalDependencies`):
  zlibstat, libbz2, libpng, libzstd_static, pthreadVCE2, freetype/libfreetype2, miniupnpc (some
  configurations) + Win32 system libs. `Debug|Win32` preprocessor set includes `USE_ZSTD`,
  `USE_UPNP`, `USE_FREETYPE`, `MULTI_THREAD`, `DEBUG_FREELIST`, `COLOUR_DEPTH=16`,
  `REVISION_FROM_FILE`, `GDI_SOUND`; exceptions disabled; `/LARGEADDRESSAWARE`.
- Output: `OutDir` = `.\simutrans\` (the game working directory); per-configuration `OutputFile`
  overrides names (e.g. `Simutrans-Extended-debug.exe`; `Release` → `Simutrans-Extended.exe`).
- Pre-build event: `cscript.exe //Nologo revision.jse` writes `revision.h` (gitignored) from
  `git rev-parse --short=7 HEAD` (see Revision embedding below).
- `Makeobj-Extended.sln` → `Makeobj-Extended.vcxproj`: v142, Win32-only configurations
  (Debug, Debug-new, Release); output `simutrans\Makeobj-Extended.exe`.
- `Nettool-Extended.sln` → `Nettool.vcxproj`: stale — v140_xp toolset, `WindowsTargetPlatformVersion`
  7, VS2010-format solution; not buildable on the maintainer's machine.
- Legacy Standard-era projects (`Simutrans.sln/.vcxproj`, `Makeobj.sln`, `makeobj/Makeobj*.vcxproj`)
  also sit at the root; do not confuse them with the Extended ones.

**Verified by execution on the maintainer's Windows machine, 2026-09-05** (VS2019 Community MSBuild,
because VS2022 there has only the v143 toolset):

- Game, Debug x64:
  `& "C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe" Simutrans-Extended.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
  → `simutrans\Simutrans-Extended-Debug.exe`.
- Makeobj, Release Win32: same MSBuild with
  `Makeobj-Extended.vcxproj /p:Configuration=Release /p:Platform=Win32 /m`
  → `simutrans\Makeobj-Extended.exe`.
- Nettool: not attempted (toolset/SDK missing).

## CMake path (CI and cross-platform)

- `cmake_minimum_required` 3.8; project `simutrans-extended`; C++14 forced; out-of-source builds only.
- Modules in `cmake/`: `SimutransVcpkgTriplet`, `SimutransCommitInfo` (git short-sha + branch →
  `REVISION` define when `SIMUTRANS_WITH_REVISION`), `SimutransBackend`, `SimutransCompileOptions`,
  `SimutransSourceList`, `SimutransPack`, `FindCCache`, `FindMiniUPNP`.
- Backend selection (`SimutransBackend.cmake`): `sdl2` if SDL2 found, `gdi` on Win32, else `none`
  (headless); `SIMUTRANS_BACKEND` cache variable.
- Dependencies: ZLIB, BZip2, PNG required; ZSTD, SDL2, Freetype, FluidSynth, MiniUPNP optional.
  MSVC uses `find_package` (vcpkg in CI); other platforms use pkg-config.
- Options (`SimutransCompileOptions.cmake` + root): `SIMUTRANS_MULTI_THREAD` (**default OFF** —
  unlike the MSVC configurations and the usual GNU-make configs, which enable MULTI_THREAD),
  `SIMUTRANS_VALGRIND_SUPPORT`, `SIMUTRANS_ENABLE_PROFILING`, `SIMUTRANS_USE_SYSLOG`,
  `SIMUTRANS_USE_IP4_ONLY`, `SIMUTRANS_STEAM_BUILT`, `DEBUG_FLUSH_BUFFER`, `ENABLE_WATERWAY_SIGNS`,
  `AUTOJOIN_PUBLIC`, `SIMUTRANS_MSG_LEVEL` (default 3), plus USE_ZSTD/UPNP/FREETYPE/FLUIDSYNTH paths.
- Outputs: `build/simutrans/`, `build/makeobj/`, `build/nettools/`; MSVC adds per-config subdirs.
- Custom target `test` runs the binary with `-use_workdir -objects pak128.britain-ex-nightly
  -scenario automated-tests -debug 2 -lang en -fps 100` (→ [scripting-and-tests](scripting-and-tests.md)).
- Packaging: `SimutransPack.cmake` → CPack, `ZIP;NSIS` on Windows, `ZIP;TGZ` elsewhere; `make package`.
- Observation: the `SIMUTRANS_HEAVY_MODE` block and the `test` target's `DEPENDS` reference a target
  named `simutrans`, which is not defined (the target is `simutrans-extended`) — latent bug.

## GNU make path

- `Makefile` reads `config.$(CFG)` (`CFG` defaults to `default`), defines the source list and
  per-backend/OS flags, then includes `common.mk` (compile/link rules; `BUILDDIR ?= build/$(CFG)`).
  `makeobj/Makefile` and `nettools/Makefile` share the parent `../config.$(CFG)` and include
  `../uncommon.mk`. Top-level targets: `all`, `clean`, `makeobj`, `nettool`, `test`.
- Variables: `BACKEND` (gdi/sdl2/mixer_sdl2/posix), `COLOUR_DEPTH` (0/16), `OSTYPE` (linux, mingw32,
  mingw64, mac, cygwin, haiku, …), `DEBUG` 1–3, `MSG_LEVEL`, `OPTIMISE`, `PROFILE`, `LTO`,
  `TUNE_NATIVE`, `USE_UPNP`, `USE_FREETYPE`, `USE_ZSTD`, `USE_FLUIDSYNTH_MIDI`, `MULTI_THREAD`,
  `WITH_REVISION`; Windows `PROG` = `Simutrans-Extended.exe`.
- `test` target: runs the built binary with `-set_workdir $(pwd)/simutrans -objects pak -scenario
  automated-tests -debug 2 -lang en -fps 100` (note: differs from the CMake `test` target).
- Configuration sources: copy `config.template` → `config.default`; presets in `configs/`
  (sim-linux-{debug-sdl2,sdl2mixer,posix}, sim-mac-sdl2, sim-mingw-{gdi,sdl2,posix},
  makeobj-{linux,mac,mingw}, nettool-{linux,mac,mingw}); or generate via autoconf:
  `autoconf && ./configure` fills `config.default` from `config.default.in` + `configure.ac`
  (detects libs/OS/backend; `--enable-server` for headless). CI's run-tests.yml uses this route.
- `OSX/osx.mk` adds macOS bundle rules when `OSTYPE=mac`.

## Revision embedding (network-play critical)

The `REVISION` define must match between server and clients for network play
(→ [network](network.md)). Three mechanisms:

- GNU make: `WITH_REVISION = 1` → `-DREVISION="$(git rev-parse --short=7 HEAD)"`.
- CMake: `SimutransCommitInfo.cmake` → `REVISION=<short-sha>` when git found.
- MSVC: pre-build `revision.jse` writes `#define REVISION <sha>` into gitignored `revision.h`,
  combined with the `REVISION_FROM_FILE` define. Observation (2026-09-05): after a successful
  MSBuild run, `revision.h` still held a stale 8-character value — the script silently leaves the
  file untouched when its `git` invocation fails (exact cause unverified); stale revisions can
  break network compatibility, so check `revision.h` after MSVC builds.
- `findversion.sh` is an SVN-era legacy script (OpenTTD compile farm); obsolete.

## CI (.github/workflows, present on both branches)

- `ci.yml` (push/PR): first job runs `cleanup_code.sh` (perl include-guard normalisation + trailing
  whitespace removal) and **auto-commits the result**; then build matrix:
  - `linux-build.yml` (reusable; ubuntu-22.04; CMake + clang; apt zlib1g-dev/libbz2-dev + extras):
    SDL2 client, headless, makeobj, nettool. Quirks: its `name:` field says "msvc-build"
    (copy-paste), and it installs clang++-10 but configures with CC/CXX=clang-14.
  - macOS jobs: brew (cmake freetype libpng pkg-config sdl2 fluidsynth); `make simutrans-extended
    && make package`; also makeobj-extended, nettool-extended.
  - MinGW cross-builds on ubuntu: container `ceeac/simutrans-build-env:mingw-sdl2`, CMake with
    `.github/toolchain_mingw.cmake`, backends sdl2/gdi/none.
  - `msvc-build.yml` (reusable; windows-2022): setup-msbuild + CMake 3.23 + vcpkg (pinned commit,
    triplet `x64-windows-static`; zlib bzip2 libpng [+ sdl2]); generator "Visual Studio 17 2022",
    `-A x64`; builds `cmake --build . --target <target>-extended`. Note: CI's MSVC route is
    CMake+vcpkg, **not** the handwritten .sln.
  - `.github/init_env.py` computes artifact names/paths for uploads.
- `run-tests.yml` (push/PR): two ubuntu-22.04 jobs — ASan+UBSan
  (`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-sanitize=shift,function`) and TSan
  (`-fsanitize=thread`). Build: autoconf/configure + clang-14 + ccache, sanitizer flags appended to
  `config.default`. Test setup: `get_pak.sh` (fed answers `2`, `i`, `y`) installs
  pak128.britain-ex-nightly; `tests/` symlinked to `<pakset>/scenario/automated-tests`;
  `~/simutrans/simuconf.tab` sets fps=100; `scripts/run-automated-tests.sh` runs the binary
  headless-ish and watches the log for "Tests completed successfully." or script-error markers,
  10-minute timeout (→ [scripting-and-tests](scripting-and-tests.md)).
- `nightly.yml` (02:00 UTC + manual): finds the last ci.yml-green commit on **master**; moves the
  `Nightly` tag and GitHub release; deploys linux.zip + windows.zip (SDL2 client, headless server as
  `simutrans-extended-server`, makeobj, nettool); triggers the `simutrans/simutrans-pak128.britain-ex`
  deploy-nightly workflow; bundles everything + pakset into `simutrans-extended.zip`. GitHub
  nightlies therefore track master (14.x), not ex-15.

## Packaging & distribution

- `nsis/`: NSIS installer scripts (`simutrans.nsi`, `simutrans-offline.nsi`, `onlineupgrade.nsi`,
  `paksets.nsh` + assets). CPack also generates ZIP/NSIS packages from CMake.
- `get_pak.sh`: interactive pakset downloader/installer (wget/curl; also used by CI).
- `distribute.sh`: legacy release helper (downloads pthreadGC2.dll, SDL2 runtime; assembles zips;
  options `-no-lang`, `-no-rev`, `-rev=###`).
- `play.sh`: legacy developer launcher (copies sim.exe to a binaries dir and runs it).

## Bridgewater-Brunel VPS & nightly pipeline

All facts in this section come from user statements and user-supplied VPS scripts (untracked; they
live on the VPS, mostly under `/root` and `/usr/share/games/`) [RECOLLECTION:2026-09-05 user
statement + user-supplied VPS scripts]. They cannot be verified against this repository.

- Private rented **Linux VPS** ("legacy tier"; upgrade intended but the build setup is complex to
  reproduce). Hosts: the public Simutrans-Extended game server, the nightly build pipeline, and
  miscellaneous hosting (including an apt repository).
- Download endpoint: `https://bridgewater-brunel.me.uk/downloads/`; nightly artifacts under
  `downloads/nightly/{linux-x64, windows, pakset, themes, packages}`.
- Layout: build checkout `/usr/share/games/nightly/simutrans-experimental` (git **master** pull);
  pakset checkouts `/usr/share/games/nightly/simutrans-pak128.britain` and
  `/usr/share/games/nightly/Pak128.Sweden-Ex`; live server `/usr/share/games/simutrans-extended`
  (binary, nettool, `server13353-network.sve`, `simu-server13353.log`; port 13353); web root
  `/var/www/downloads/`.
- `nightly.sh` (cron; schedule not recorded) drives the whole pipeline with **GNU make**:
  1. `git checkout master && git pull` in the code and pakset checkouts; delete old `nightly.hash`.
  2. Linux client: `CFG=default` (BACKEND=sdl2, OSTYPE=linux, OPTIMISE, USE_FREETYPE, USE_UPNP,
     USE_ZSTD, MULTI_THREAD, WITH_REVISION) → strip → `build/default/simutrans-extended`.
  3. Linux server: `CFG=server` (BACKEND=posix, COLOUR_DEPTH=0, `-DSYSLOG`, gnu++14) →
     `build/server/simutrans-extended`.
  4. Windows cross-compile: `CFG=mingw64` (BACKEND=sdl2, OSTYPE=mingw64,
     `x86_64-w64-mingw32-g++`; static libs from `/root/*` source-built trees — freetype-2.10.4,
     SDL2-2.0.7 build-win64, bzip2-1.0.6, zlib-1.2.11, zstd, miniupnpc — plus MXE under
     `/usr/share/mxe`; USE_UPNP disabled: no cross-compiled static lib). This configuration took
     substantial effort to set up. The 32-bit `CFG=mingw32` GDI build was discontinued in Dec 2020.
  5. makeobj (Linux + mingw64), symlinked into the pakset/theme dirs; nettool (Linux + mingw64 —
     the Windows build is reported broken, see Known problems); themes via
     `themes.src/build_themes.sh`; both paksets via `make`.
  6. Copy artifacts to the web root (incl. `linux-x64/command-line-server-build/`), install the
     server binary + nettool into `/usr/share/games/simutrans-extended`, assemble
     `packages/Simutrans-Extended-Complete.zip`, tar the pakset nightlies, then run
     `server-hasher.jar` to write `nightly.hash` (selective-download manifest).
- `package.sh`: manual `.deb` creation (dpkg-deb; version numbers set by hand; references older
  `simutrans-experimental` binary names — partly stale). `update-repo.sh`: regenerates apt
  `Packages.gz`/`Sources.gz` for `/var/www/repository` (dists/stable/main, amd64+i386).
- Server operations scripts (VPS `/root`): `force-sync.sh` (nettool force-sync);
  `warn-save.sh` (nettool `say` warnings → force-sync → `simctrl brit kill`; a cron job runs
  `simctrl brit restart` every minute, which brings the server back up); `rotate-backup.sh`
  (hourly: if the savegame is stale, force-sync, then `rotate.sh`); `rotate.sh` (rotates six
  generations of savegame and pwdhash backups); `showlog.sh`; `lock-public-player.sh`.
  These scripts invoke nettool with the password of a lower-privilege server user (not the admin
  password — user correction) in plaintext [RECOLLECTION:2026-09-05 user statement]; it is
  deliberately not recorded here. Server security improvements are planned (see Open questions).
- `restart.sh` (tracked in this repo): alternative restart flow — nettool say/force-sync/shutdown,
  savegame backup, `java -jar "Nightly Updater V2.jar" -cl`, download of the nightly
  `linux-x64/command-line-server-build/simutrans-extended`, relaunch in screen session
  `dome_simutrans`.

## Known problems & observations

- Windows nettool cross-build reported broken by the user; `nightly.sh` copies
  `build/mingw64/nettool/nettool` (no `.exe` suffix) to `windows/Nettool-Extended.exe`, which can
  silently keep shipping a stale binary (the script has no `set -e`)
  [RECOLLECTION:2026-09-05 user statement + user-supplied VPS scripts].
- `revision.h` staleness after MSVC builds (see Revision embedding above).
- `Nettool.vcxproj` stale (v140_xp, Windows SDK 7, VS2010-format solution).
- `linux-build.yml` misnamed ("msvc-build") and clang version mismatch (installs 10, uses 14).
- `CMakeLists.txt` references an undefined `simutrans` target (HEAVY_MODE block; test `DEPENDS`).
- The SDL3 backend (`sys/simsys_s3.cc`, `sys/clipboard_s3.cc`, `sound/sdl3_sound.cc`) exists on
  master only; ex-15 does not have it (→ [rendering](rendering.md)).
- ICU: the repo vendors ICU/OpenTTD headers under `utils/openttd/` (tracked), but no game source
  includes them; the game's own `unicode.h`/`unicode.cc` (included via relative paths) handles
  UTF-8. MSVC include paths also carry an ICU copy in `..\OpenTTD\shared\include`.
- `zlib-1.2.5/`, `0001-zstd.patch`, `zstd_enable.diff` at the root are local-only artefacts
  (gitignored), historical zstd-enablement material [UNVERIFIED].

## Open questions

- What cron schedule runs `nightly.sh` (and the other VPS cron jobs) exactly?
- Provenance and maintenance status of `Nightly Updater V2.jar`, `server-hasher.jar`, `simctrl`.
- VPS legacy-tier migration: which parts of the pipeline must be reproduced on a new machine, and
  should the scripts be brought under version control (with the password removed)?
- Server security improvements (the user intends to address these at some point): scope not yet
  defined — password handling and privilege levels are known concerns
  [RECOLLECTION:2026-09-05 user statement].
- Exact cause of the `revision.h` staleness under MSBuild (git visibility in the cscript
  environment vs. the script's update condition).
- Local Windows recipe for running the automated tests (binary + pakset + `tests/` linked as
  `scenario/automated-tests`) → [scripting-and-tests](scripting-and-tests.md).
