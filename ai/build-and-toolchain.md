---
status: reviewed
verified: master @ cc1c5858f
---
# Build & toolchain

**Covers:** Simutrans-Extended.sln/.vcxproj, Makeobj-Extended.sln/.vcxproj, Nettool-Extended.sln +
Nettool.vcxproj, Makefile, common.mk, uncommon.mk, config.template, config.default.in, configure.ac,
configs/, CMakeLists.txt, cmake/, makeobj/ and nettools/ build files, .github/ (workflows,
init_env.py, toolchain_mingw.cmake), nsis/, cleanup_code.sh, distribute.sh, get_pak.sh, play.sh,
restart.sh, findversion.sh, revision.jse, scripts/run-automated-tests.sh,
scripts/run-smoke-tests.sh/.ps1, scripts/run-perf-suite.ps1, OSX/osx.mk.

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
  "Optimised debug", Profile, server, no-randomness, single-threaded, Dr. Memory, IP-v4-only,
  SDL 2, legacy OpenGL) × Win32/x64 (Profile is x64-only). Some solution configurations map to
  differently-named project configurations.
- Toolsets are mixed: most configurations `v140_xp` (VS2015+XP toolset); the newer ones (`Debug`,
  `Optimised debug`, `Profile`, `Release|x64`, graphical/non-graphical server x64) are `v143`
  (VS2022; updated from v142 on master, merged into ex-15 2026-09-05). VS2022 is the MSVC version
  to use; VS2019 is deprecated in this project [RECOLLECTION:2026-09-05 user statement].
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
- `Makeobj-Extended.sln` → `Makeobj-Extended.vcxproj`: v142 (not covered by the toolset update),
  Win32-only configurations (Debug, Debug-new, Release); output `simutrans\Makeobj-Extended.exe`.
  VS2022 MSBuild builds it by resolving v142 from the co-installed VS2019; retargeting to v143
  would be needed if VS2019 is removed [execution-verified 2026-09-05].
- `Nettool-Extended.sln` → `Nettool.vcxproj`: stale — v140_xp toolset, `WindowsTargetPlatformVersion`
  7, VS2010-format solution; not buildable on the maintainer's machine.
- Legacy Standard-era projects (`Simutrans.sln/.vcxproj`, `Makeobj.sln`, `makeobj/Makeobj*.vcxproj`)
  also sit at the root; do not confuse them with the Extended ones.
- Maintainer usage [RECOLLECTION:2026-09-05 user statement]: since the Bridgewater-Brunel pipeline
  provides automated builds, MSVC is used only for debugging and profiling, and only x64
  configurations: `Debug|x64` (day-to-day), `Debug (graphical server)|x64` (same build as Debug,
  distinguished only by debugger starting commands held in the untracked `.vcxproj.user`), and
  `Debug (non-graphical server)|x64` when the code under work runs only in non-graphical mode.
- `Profile|x64` and `Profile (server)|x64` (added 2026-09-09): the profiling configurations —
  release-like (MaxSpeed, `NDEBUG`, `MultiThreadedDLL`, no `DEBUG`/`MSG_LEVEL` so `DBG_*` macros
  compile out) plus `PROFILE` (enables the `-until`/`-times` benchmark options) and link PDBs;
  outputs `simutrans\Simutrans-Extended-Profile.exe` / `Simutrans-Extended-Profile-server.exe`;
  both fully wired into the .sln. Built and verified by execution 2026-09-09. The headless
  (server) build currently crashes on the performance fixture in server-mode simulation
  ([known-bugs](known-bugs.md)). "Optimised debug" (fully wired into the .sln since commit
  5dc127a85) remains the optimised-*debugging* configuration — its `DEBUG=3` define biases
  hot-path profiling (DBG-macro calls, asserts, `DEBUG_FREELIST`). The profiling workflow:
  [performance](performance.md).

**Verified by execution on the maintainer's Windows machine, 2026-09-05** (VS2022 Community MSBuild,
after the master→ex-15 merge):

- Game, Debug x64:
  `& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" Simutrans-Extended.vcxproj /p:Configuration=Debug /p:Platform=x64 /m`
  → `simutrans\Simutrans-Extended-Debug.exe`.
- Game, Profile x64 (2026-09-09): same MSBuild with
  `/p:Configuration=Profile /p:Platform=x64 /m` → `simutrans\Simutrans-Extended-Profile.exe`
  (+ .pdb for profiler symbols).
- Makeobj, Release Win32: same MSBuild with
  `Makeobj-Extended.vcxproj /p:Configuration=Release /p:Platform=Win32 /m`
  → `simutrans\Makeobj-Extended.exe` (v142 resolved from the VS2019 installation).
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
  (detects libs/OS/backend; `--enable-server` for headless). CI's test workflows use this route.
- `OSX/osx.mk` adds macOS bundle rules when `OSTYPE=mac`.

## Revision embedding (network-play critical)

The `REVISION` define must match between server and clients for network play
(→ [network](network.md)). Three mechanisms:

- GNU make: `WITH_REVISION = 1` → `-DREVISION="$(git rev-parse --short=7 HEAD)"`.
- CMake: `SimutransCommitInfo.cmake` → `REVISION=<short-sha>` when git found.
- MSVC: pre-build `revision.jse` writes `#define REVISION <sha>` into gitignored `revision.h`,
  combined with the `REVISION_FROM_FILE` define. Historical observation (2026-09-05): `revision.h`
  could hold a stale value after MSBuild runs — root cause found and fixed 2026-09-09 (the script's
  strict output-length check rejected modern git output; see Known problems). Stale revisions can
  break network compatibility, so check `revision.h` against `git rev-parse --short HEAD` after
  MSVC builds.
- `findversion.sh` is an SVN-era legacy script (OpenTTD compile farm); obsolete.

## CI (.github/workflows, present on both branches)

**Status caveat:** the Bridgewater-Brunel VPS pipeline remains the actual release build source
[RECOLLECTION:2026-09-05 user statement]. The GitHub workflows are actively maintained: the
`ci.yml` build matrix runs green on push, and the test workflows are under active repair
[CODE master @ 49fd95a32: GitHub Actions run history].

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
- `run-tests.yml` (push/PR; "Automated Tests"): blocking gate — calls `smoke-harness.yml`
  (ASan+UBSan) with the pakset pin resolved per branch from `tests/pakset-pin.tab`, fixture
  `tests/demo.sve`, horizon 1945.6. The legacy Squirrel scenario jobs (ubuntu-22.04; ASan+UBSan
  (`-fsanitize=address,undefined -fno-sanitize-recover=all -fno-sanitize=shift,function`) or TSan
  (`-fsanitize=thread`); autoconf/configure + clang-14 + ccache, sanitizer flags appended to
  `config.default`; `get_pak.sh` fed answers `2`, `i`, `y` installs pak128.britain-ex-nightly;
  `tests/` symlinked to `<pakset>/scenario/automated-tests`; `~/simutrans/simuconf.tab` sets
  fps=100; `scripts/run-automated-tests.sh` runs the binary headless-ish and watches the log for
  "Tests completed successfully." or script-error markers, 10-minute timeout) are deferred to
  workflow_dispatch only (`squirrel_tests` input; they hang —
  [scripting-and-tests](scripting-and-tests.md)).
- `tsan-smoke.yml` (push/PR; "TSan smoke"): same harness call with `sanitizer: tsan`; regression
  detector for threading races (the `karte_t::load`/`init_threads` race family that made it
  knowingly red is fixed — workers are now created only at the end of `karte_t::load`,
  [threading](threading.md)). Separate workflow because GitHub
  rejects `continue-on-error` on jobs calling a reusable workflow — this keeps its red run from
  failing the "Automated Tests" gate.
- `smoke-harness.yml` (reusable; "Smoke harness"): ubuntu-22.04; autoconf/configure
  `--enable-server` + clang-14 + ccache; builds plain makeobj first, then engine with `DEBUG = 2`
  + sanitizer flags appended to `config.default` (input: asan default, or tsan); compiles
  pak128.Britain-Ex from a pinned `jamespetts/simutrans-pak128.britain` commit with that makeobj
  (cached), symlinks it into `simutrans/`, runs `scripts/run-smoke-tests.sh --mode network`
  (fixture/until/timeouts/failure-marker regex via inputs); uploads `ai/temp/simutest/` logs and
  results as an artifact on failure.
- `pakset-pin.yml` (daily 03:30 UTC + manual; "Pakset pin updater"): last-known-good ratchet for
  the pin in `tests/pakset-pin.tab`. Per branch: resolve the pakset branch head; if it differs
  from the pin, verify via `smoke-harness.yml`; promote the pin by direct bot commit only when
  `needs.verify-*.result == 'success'` (the `needs` context has no `conclusion` property).
  Scheduled runs fire only on the default branch's copy of the file, hence explicit per-branch
  job triplets.
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
- `package.sh`: `.deb` creation (dpkg-deb) with version numbers hardcoded in the script
  (`simutrans-ex_14.9000.12`); references older `simutrans-experimental` binary names — partly
  stale. `update-repo.sh`: regenerates apt `Packages.gz`/`Sources.gz` for `/var/www/repository`
  (dists/stable/main, amd64+i386).
- Operation verified against the public endpoint on 2026-09-05: the pipeline is live. Fresh that
  morning (server-local timestamps 05:08–05:09): Linux client, headless server (incl.
  `command-line-server-build/`), Linux makeobj + nettool, Windows `Simutrans-Extended.exe` +
  `-64.exe` + `Makeobj-Extended.exe`, both pakset tarballs, themes, `nightly.hash`,
  `Simutrans-Extended-Complete.zip`; `simutrans-ex_14.9000.12.deb` at 05:32 — so `package.sh` (or
  a successor) runs on a schedule after `nightly.sh`, publishing a fixed version number.
  Confirmed broken/stale: Windows nettool — no `Nettool-Extended.exe` is published at all; the
  listing holds only a `nettool.exe` dated 2017-02-08. Discontinued leftovers: `mac/` (2018),
  stray `linux-X64` file (2022).
- Server operations scripts (VPS `/root`): `force-sync.sh` (nettool force-sync);
  `warn-save.sh` (nettool `say` warnings → force-sync → `simctrl brit kill`; a cron job runs
  `simctrl brit restart` every minute, which brings the server back up); `rotate-backup.sh`
  (hourly: if the savegame is stale, force-sync, then `rotate.sh`); `rotate.sh` (rotates six
  generations of savegame and pwdhash backups); `showlog.sh`; `lock-public-player.sh`.
  `simctrl` is an older third-party script (not written by the user), not part of the build
  process: it starts/stops/interacts with running headless servers by game tag
  (`simctrl brit stop`, `simctrl brit restart`); multiple tagged instances are supported in
  theory, but the VPS has never had enough memory to run more than one
  [RECOLLECTION:2026-09-05 user statement].
  These scripts invoke nettool with the password of a lower-privilege server user (not the admin
  password — user correction) in plaintext [RECOLLECTION:2026-09-05 user statement]; it is
  deliberately not recorded here. Server security improvements are planned (see Open questions).
- `restart.sh` (tracked in this repo): alternative restart flow — nettool say/force-sync/shutdown,
  savegame backup, `java -jar "Nightly Updater V2.jar" -cl`, download of the nightly
  `linux-x64/command-line-server-build/simutrans-extended`, relaunch in screen session
  `dome_simutrans`.

## Known problems & observations

- The MSVC game configurations all link the old debug-built zstd static lib
  (`..\zstd-1.4.4\build\VS2010\bin\x64_Debug\libzstd_static.lib`). A freshly built release zstd lib
  (v142 toolset, from `..\zstd-1.4.4\build\VS2010\zstd.sln`) cannot be linked into the v143 game
  builds: its objects force link-time code generation, which then fails against ancient bytecode in
  `..\bzip\lib\x64\libbz2.lib` (LNK C1047/LNK1257) — reproduced with the `Profile|x64` build
  2026-09-09. Consequence: load-phase profiling includes debug-build (unoptimised) zstd
  decompression; simulation profiling is unaffected. Fix options (same-toolset lib rebuild, or
  compiling zstd sources into the project): open [execution-verified 2026-09-09].

- Windows nettool cross-build broken: confirmed against the public endpoint 2026-09-05 — no
  `Nettool-Extended.exe` published; only a 2017 `nettool.exe`. `nightly.sh` copies
  `build/mingw64/nettool/nettool` (no `.exe` suffix) to `windows/Nettool-Extended.exe`, which
  fails silently (the script has no `set -e`). User: fair record, fix during VPS modernisation.
- CMake `SIMUTRANS_MULTI_THREAD` defaults to OFF and no CI workflow enables it — CMake-built
  binaries (CI artifacts, GitHub nightlies) are single-threaded, unlike the BB GNU-make builds
  (`MULTI_THREAD = 1` in every VPS config). Latent problem for any future CMake/CI adoption;
  previously unnoticed [RECOLLECTION:2026-09-05 user statement].
- The published `.deb` version is hardcoded in `package.sh` (14.9000.12) and does not track the
  actual release, despite the package being rebuilt on a schedule.
- `revision.h` staleness under MSBuild (see Revision embedding above) — ROOT CAUSE FOUND AND FIXED
  2026-09-09: `revision.jse` rejected git's `--short=7` output on a strict `length !== 9` check
  (7 hex + LF = 8 chars), silently leaving `revision.h` untouched; the file had held a 2020
  revision since. Fix: trim whitespace and accept 7–12 hex digits. Keep the routine caution of
  checking `revision.h` against `git rev-parse --short HEAD` after MSVC builds
  [execution-verified 2026-09-09: stale value reproduced; fix verified].
- `Nettool.vcxproj` stale (v140_xp, Windows SDK 7, VS2010-format solution).
- `linux-build.yml` misnamed ("msvc-build") and clang version mismatch (installs 10, uses 14).
- `CMakeLists.txt` references an undefined `simutrans` target (HEAVY_MODE block; test `DEPENDS`).
- The "Optimised debug" configurations are defined in `Simutrans-Extended.vcxproj` (Win32 and x64,
  v143) **and fully mapped in `Simutrans-Extended.sln`** (solution configurations + project
  mappings, since commit 5dc127a85, 2019) [CODE master @ cc1c5858f]. An earlier claim here that they were
  absent from the solution was wrong; if the configuration does not appear in a given Visual
  Studio installation's dropdown, the cause lies outside the repository (e.g. VS state), not in
  these files. Note the sln also carries dead legacy entries (solution-only configs with no
  project backing, e.g. "Release (open GL)"; project-only configs unreachable from the sln, e.g.
  "Debug (command-line server)") — cosmetic, no fix pending.
- Revision-embedding observation (2026-09-05, predating the revision.jse fix): after a successful
  MSBuild run, `revision.h` held a stale value — explained and fixed as above.
- The SDL3 backend (`sys/simsys_s3.cc`, `sys/clipboard_s3.cc`, `sound/sdl3_sound.cc`) is on both
  branches: added on master, merged into ex-15 2026-09-05 (→ [rendering](rendering.md)). It was supplied by a
  contributor and integrated by the user in 2026-09, with the aim of testing it and, if it works,
  making SDL3 the standard build backend [RECOLLECTION:2026-09-05 user statement]. Build-system
  wiring covers CMake, GNU make and autoconf: `CMakeLists.txt` supports `SIMUTRANS_BACKEND=sdl3`
  (`find_package(SDL3 CONFIG)` on MSVC, pkg-config elsewhere); the Makefile's `BACKENDS` list
  includes sdl3, found via `SDL3_CONFIG ?= pkg-config sdl3`; `configure.ac` has sdl3 plumbing.
  Only the MSVC `Simutrans-Extended.vcxproj` has no SDL3 configuration
  [CODE master @ cef3550ea].
- Local GNU-make-route build on the maintainer's machine (established 2026-09-05): MSYS2/MinGW64 +
  untracked root `config.msys2-sdl3` (OSTYPE=mingw64, BACKEND=sdl3, native CC/CXX,
  WINDRES=windres); `make CFG=msys2-sdl3` from the mingw64 shell builds the SDL3 backend
  (verified on master and, after the merge, on ex-15); binary runs. Test builds are copied into
  `simutrans/` — the in-repo game working directory (tracked text assets; ignored binaries, `pak*`
  paksets, `config/`) and the standard location for running test builds — under uniquely named
  files (`Simutrans-Extended-<branch>-sdl3.exe`), keeping the principal name
  `Simutrans-Extended.exe` free for release/public-download builds; the gitignored root helper
  `build-test.ps1` runs the make build and the copy (`-Clean` for a full rebuild)
  [RECOLLECTION:2026-09-05 user statement]. The recipe requires two overrides: the Makefile's mingw64 branch assumes a
  cross toolchain (`WINDRES ?= x86_64-w64-mingw32-windres`, `?=` so config-overridable), and repo
  preset `configs/config.sim-mingw-sdl2` still passes `-std=c++11` in FLAGS while the code needs
  C++14 (`std::index_sequence` in `script/api_function.h`; BB's live config uses `-std=c++14`).
  Objects do not track flag changes — `make CFG=... clean` when switching branch or flags
  [execution-verified 2026-09-05].
- ICU: the repo vendors ICU/OpenTTD headers under `utils/openttd/` (tracked), but no game source
  includes them; the game's own `unicode.h`/`unicode.cc` (included via relative paths) handles
  UTF-8. MSVC include paths also carry an ICU copy in `..\OpenTTD\shared\include`.
- `zlib-1.2.5/`, `0001-zstd.patch`, `zstd_enable.diff` at the root are local-only artefacts
  (gitignored), historical zstd-enablement material [UNVERIFIED].

## Open questions

- What cron schedule runs `nightly.sh` (and the other VPS cron jobs) exactly? Partially answered
  by endpoint observation: artifacts published ~05:08–05:09 and the .deb ~05:32 server-local
  (2026-09-05); exact crontab entries unknown.
- Should the build pipeline eventually move from the BB VPS to GitHub CI? The user is considering
  it [RECOLLECTION:2026-09-05 user statement]; the `ci.yml` build matrix now runs green on push.
- Helper-tool provenance [RECOLLECTION:2026-09-05 user statement]: both `.jar` tools were written
  by a third party (name not recalled) to enable differential downloading — clients fetch binary
  diffs of updated files instead of whole artifacts. The user cannot locate Nightly Updater V2's
  source; server-hasher is to be supplied by the user. Maintainability therefore uncertain.
- VPS legacy-tier migration: which parts of the pipeline must be reproduced on a new machine, and
  should the scripts be brought under version control (with the password removed)?
- Server security improvements (the user intends to address these at some point): scope not yet
  defined — password handling and privilege levels are known concerns
  [RECOLLECTION:2026-09-05 user statement].
- Local Windows recipe for running the automated tests (binary + pakset + `tests/` linked as
  `scenario/automated-tests`) → [scripting-and-tests](scripting-and-tests.md).
- Where do the MSVC "Debug (SDL 2)" configurations get their SDL2 libraries? Not found in the
  sibling dependency trees; user unsure. Related: SDL3 library provisioning for the planned
  SDL3-as-standard switch.
- **To do (user-directed):** the local makefile-route build works (MSYS2/MinGW64 — see Known
  problems) and SDL3 binaries build and run on both branches. Remaining: user graphical testing of
  the SDL3 backend; BB nightly config updates when SDL3 becomes the standard backend; decide
  whether to add a tracked native-MSYS2 config preset and fix the stale `-std=c++11` preset.
