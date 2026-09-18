---
status: stub
verified: mapgen-perf-fixes @ 0954e8c3c
---
# Scripting & tests

**Covers:** script/ (Squirrel bindings/API), squirrel/ (vendored language source), tests/ (all_tests.nut, test_*.nut, test_helpers.nut, scenario.nut, empty-16x16.sve), gui/scenario_frame/scenario_info (→ [gui](gui.md)).

## Status & priority

- The scripting system is currently non-working in Extended in general: the Squirrel interface was
  never fully ported from Standard, Extended-specific features are largely absent from the script
  API, and nobody uses scripting in Extended [RECOLLECTION:2026-09-06]. Scripting-specific bugs are
  therefore NOT tracked in [known-bugs](known-bugs.md); this status note covers the whole system.
- Completing/backporting the scripting interface is **low priority**: it would require massive work to
  expose Extended's feature set, and Extended's priorities are large multi-player organic games rather
  than scripted scenarios [RECOLLECTION:2026-09-06].
- CI: the Squirrel scenario jobs in `run-tests.yml` (ASan/UBSan + TSan, via
  `scripts/run-automated-tests.sh`) are suspended — Squirrel scripting is non-working (above) —
  and run only on demand (workflow_dispatch, `squirrel_tests` input); the infrastructure is kept
   wired for a possible future Squirrel port [user decision 2026-09-07]. Their last full run hung
   until the 10-minute timeout: ASan/UBSan after `error [suspended] calling start` at test 1/64
   [CODE master @ 78a4bb3b9 / ex-15 @ 91d9b252e: CI logs]. The wrapper does not recognise
   `[suspended]` as a failure pattern, so failures surface as timeouts rather than fast failures
   [CODE master @ 78a4bb3b9: run-automated-tests.sh]. (The TSan flavour of that job additionally
   reported the `karte_t::load`/`init_threads` race family; that family is now fixed and
   independent of the `[suspended]` hang, which remains undiagnosed.)
- Push/PR CI gates on the smoke harness instead (smoke + network determinism on `tests/demo.sve`
   via `scripts/run-smoke-tests.sh`, pakset pinned in `tests/pakset-pin.tab`); the TSan smoke
   flavour runs as its own workflow `tsan-smoke.yml` (a threading-race regression detector),
   independent of Squirrel
   [CODE master @ 49fd95a32: .github/workflows]. Mechanics:
   [build-and-toolchain](build-and-toolchain.md).
- World generation is exercised by `scripts/run-mapgen-tests.sh`: a 9-case size/seed/town-count
   matrix driving the headless `-generate_map` mode (overrides `-map_size X,Y`, `-map_seed N`,
   `-map_towns N`, `-map_factories N`, `-map_attractions N`, `-map_water_level N`; pass = exit 0
   + `MAP-GEN: PASS` line + no failure markers; hangs fail via the per-run watchdog, and in DEBUG
   builds the objlist seqlock spin assertion fatals fast instead). It runs as a step of
   `smoke-harness.yml` (ASan gate, TSan detector, pakset-pin verification)
   [CODE master @ 69b4c8c84: simmain.cc, scripts/run-mapgen-tests.sh, .github/workflows/smoke-harness.yml].
- `-generate_map` reproducibility: 0954e8c3c (simmain.cc, branch mapgen-perf-fixes) makes
  `-map_seed` seed all RNG streams, so one seed gives an identical `MAP-GEN:` result line across
  server/GUI and release/debug builds — generated worlds become byte-comparable for A/B tests and
  replay debugging. Harness requirements: delete `settings-extended.xml` from the workdir before
  every run (settings persist between runs), and note factory placement remains config-sensitive
  in DEBUG builds ([performance](performance.md)). Before this fix, `-map_seed` alone did not
  reproduce runs [EXECUTION-VERIFIED:2026-09-18].
- Smoke-harness network determinism oracle [CODE private-car-mt-network, 2026-09-16;
   user-directed]: the two loopback runs are compared on their per-step **semantic
   private-car route hash** sequences ("Private car route hash" log lines, computed every
   step independently of the route-map storage layout and logged at `-debug 2` or higher),
   not on byte-identity of the final saves. Byte-identity is not expected: private-car
   route-map list indices are allocated while worker threads are running, in
   thread-timing-dependent order, so semantically identical runs can produce
   byte-different `final.sve` files (the checklist and the simulation only ever see
   semantic content). The byte comparison runs as informational output ("NOTE:" lines)
   only. The same applies to the Windows local runner `scripts/run-smoke-tests.ps1`.

## Initial facts

- Tests are Squirrel scripts run via an in-game scenario (`tests/scenario.nut` + `all_tests.nut`); they need a built binary and a pakset [CODE].
- CI `run-tests.yml` (push/PR) gates on the smoke harness; the Squirrel suite runs only on workflow_dispatch. The workflow files are the authority for current runner/toolchain specifics [CODE].
- `tests/empty-16x16.sve` is a minimal save used as test fixture [CODE].
- Test coverage spans many subsystems (way, halt, factory, player, terraform, and more — enumerate from tests/ filenames) [CODE].

## Planned sections

- Script API surface (script/ inventory; what is exposed to Squirrel).
- Scenario binding: how a scenario loads a script; test_helpers conventions.
- **How to run tests locally on Windows — exact recipe to be derived and verified** (→ [build-and-toolchain](build-and-toolchain.md) open questions); CI invocation mechanics are documented in [build-and-toolchain](build-and-toolchain.md).
- Writing new tests: conventions from existing test_*.nut.
- Coverage gaps relevant to current work (consist order? schedules? — none evident from filenames; verify).

## Open questions

- Can the Squirrel test suite run headless, or does it need a graphical backend? (World
  generation can: the server build with `-generate_map` creates and reports a world without
  entering the interactive loop [CODE master @ 69b4c8c84: simmain.cc].)
- Root cause of the `run-tests.yml` Squirrel-suite failure (`[suspended]` error + hang) — not yet
  diagnosed. (The TSan races once reported by that job were the separate, since-fixed
  `karte_t::load`/`init_threads` family.)
