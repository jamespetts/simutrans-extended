---
status: stub
verified: none
---
# Scripting & tests

**Covers:** script/ (Squirrel bindings/API), squirrel/ (vendored language source), tests/ (all_tests.nut, test_*.nut, test_helpers.nut, scenario.nut, empty-16x16.sve), gui/scenario_frame/scenario_info (→ [gui](gui.md)).

## Seed facts

- Tests are Squirrel scripts run via an in-game scenario (`tests/scenario.nut` + `all_tests.nut`); they need a built binary and a pakset [CODE].
- CI `run-tests.yml` runs the Squirrel tests on Linux with sanitizers enabled; the workflow file is the authority for current runner/toolchain specifics [CODE].
- `tests/empty-16x16.sve` is a minimal save used as test fixture [CODE].
- Test coverage spans many subsystems (way, halt, factory, player, terraform, and more — enumerate from tests/ filenames) [CODE].

## Planned sections

- Script API surface (script/ inventory; what is exposed to Squirrel).
- Scenario binding: how a scenario loads a script; test_helpers conventions.
- **How to run tests locally on Windows — exact recipe to be derived and verified** (→ [build-and-toolchain](build-and-toolchain.md)); how CI invokes them (to be read from run-tests.yml in full).
- Writing new tests: conventions from existing test_*.nut.
- Coverage gaps relevant to current work (consist order? schedules? — none evident from filenames; verify).

## Open questions

- Can the test suite run headless, or does it need a graphical backend?
- Do the tests pass on ex-15 today? (Baseline run needed once build recipe exists.)
