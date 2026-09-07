---
status: stub
verified: none
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
- CI `run-tests.yml` is red on every push on both branches: ASan/UBSan job hangs after
  `error [suspended] calling start` at test 1/64 (timeout, exit 124); TSan job reports data races in
  `karte_t::load`/`init_threads`, then hangs [CODE master @ 78a4bb3b9 / ex-15 @ 91d9b252e: CI logs].
  The wrapper `scripts/run-automated-tests.sh` does not recognise `[suspended]` as a failure pattern,
  so failures surface as timeouts rather than fast failures [CODE master @ 78a4bb3b9: run-automated-tests.sh].

## Initial facts

- Tests are Squirrel scripts run via an in-game scenario (`tests/scenario.nut` + `all_tests.nut`); they need a built binary and a pakset [CODE].
- CI `run-tests.yml` runs the Squirrel tests on Linux with sanitizers enabled; the workflow file is the authority for current runner/toolchain specifics [CODE].
- `tests/empty-16x16.sve` is a minimal save used as test fixture [CODE].
- Test coverage spans many subsystems (way, halt, factory, player, terraform, and more — enumerate from tests/ filenames) [CODE].

## Planned sections

- Script API surface (script/ inventory; what is exposed to Squirrel).
- Scenario binding: how a scenario loads a script; test_helpers conventions.
- **How to run tests locally on Windows — exact recipe to be derived and verified** (→ [build-and-toolchain](build-and-toolchain.md) open questions); CI invocation mechanics are documented in [build-and-toolchain](build-and-toolchain.md).
- Writing new tests: conventions from existing test_*.nut.
- Coverage gaps relevant to current work (consist order? schedules? — none evident from filenames; verify).

## Open questions

- Can the test suite run headless, or does it need a graphical backend?
- Root cause of the `run-tests.yml` failures (`[suspended]` error + hang; TSan races) — not yet diagnosed.
- Given scripting's low priority, should `run-tests.yml` keep running on every push (options: leave
  red, mark non-blocking/continue-on-error, restrict to manual dispatch)? User decision pending.
