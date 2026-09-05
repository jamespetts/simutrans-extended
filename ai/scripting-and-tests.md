---
status: stub
verified: none
---
# Scripting & tests

**Covers:** script/ (56 files: Squirrel bindings/API), squirrel/ (vendored language source), tests/ (all_tests.nut, 25 test_*.nut, test_helpers.nut, scenario.nut, empty-16x16.sve), gui/scenario_frame/scenario_info (→ [gui](gui.md)).

## Seed facts

- Tests are Squirrel scripts run via an in-game scenario (`tests/scenario.nut` + `all_tests.nut`); they need a built binary and a pakset [CODE ex-15 @ b06e8fa14].
- CI `run-tests.yml`: ubuntu-22.04, autoconf + clang-14 with ASan/UBSan (`-fno-sanitize-recover=all`), runs on push/PR [CODE master @ 3b70dd4b3].
- `tests/empty-16x16.sve` is a minimal save used as test fixture [CODE ex-15 @ b06e8fa14].
- Test coverage areas (from filenames): building, climate, depot, factory, good, halt, headquarters, label, player, powerline, reservation, scenario, sign, slope, terraform, trees, wayobj, way (bridge/road/runway/tram/tunnel) [CODE ex-15 @ b06e8fa14].

## Planned sections

- Script API surface (script/ inventory; what is exposed to Squirrel).
- Scenario binding: how a scenario loads a script; test_helpers conventions.
- **How to run tests locally on Windows — exact recipe to be derived and verified** (→ [build-and-toolchain](build-and-toolchain.md)); how CI invokes them (to be read from run-tests.yml in full).
- Writing new tests: conventions from existing test_*.nut.
- Coverage gaps relevant to current work (consist order? schedules? — none evident from filenames; verify).

## Open questions

- Can the test suite run headless, or does it need a graphical backend?
- Do the tests pass on ex-15 today? (Baseline run needed once build recipe exists.)
