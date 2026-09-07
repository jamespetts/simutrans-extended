# Simutrans-Extended — agent instructions

C++ transport simulation game. Forked from Standard Simutrans in 2008 (as "Experimental",
later "Extended"); fully independent since. Large, legacy codebase. Handle with care.

Two active branches with materially different code:
- `ex-15` — the next major version (15.x, planned since 2018); the primary work target.
- `master` — the stable/release line (14.x).
Always identify the checked-out branch first (`git branch --show-current`).

## Knowledge base

All AI-facing documentation is kept in `ai/` — a linked hierarchy designed for lazy retrieval.

- Entry point: `ai/index.md`. Read it first; then load ONLY the docs whose read-when key
  there matches the task. Never bulk-load the folder.
- Read `ai/project-notes.md` at the start of most code tasks: short cross-cutting notes
  that are easy to miss.
- Docs might lag behind the code erroneously. The checked-out code is the sole authority; distrust
  any doc claim whose provenance record (frontmatter `verified:`) predates the code it
  describes.
- Doc conventions (claim tags, provenance records, statuses): `ai/conventions.md` — read it
  before writing or updating any doc.

## Hard rules

1. Use only precise, literal language, including internal reasoning. No metaphors
   or figurative expressions.
2. AI-facing documentation goes ONLY in `ai/` (plus this file). NEVER create documentation
   colocated with code files, and never create new .md files anywhere else in the repo.
3. Inside `ai/`, use relative markdown links (`[text](file.md)`) — they work in both
   Obsidian and GitHub. This file uses plain paths.
4. Priors are not evidence. Anything recalled from training data about Standard Simutrans
   OR Simutrans-Extended may be wrong or outdated. Use priors only as search hints; verify
   every claim against the checked-out code and tag it per `ai/conventions.md`.
5. Serialization/sync change restriction: do not change load/save behaviour, savegame or
   network version constants (`simversion.h`), or checklist/desync-relevant code without
   first reading `ai/sync-and-determinism.md`, `ai/savegame-versioning.md` and
   `ai/network.md`, and presenting the intended change to the user before proceeding.
6. After significant code changes, PROPOSE updates to the affected `ai/` docs; the user
   reviews and approves all doc changes before commit. Update provenance records when
   updating docs.
7. When unsure, write an open question into the relevant doc instead of a claim.
8. Do *not* write outside the project directory unless absolutely unavoidable. This 
   requires the user's explicit permission, which wastes the user's time.


## Commit messages

`<PREFIX>: <short message>`, where PREFIX is one of:
- FIX — a bug fix
- CHANGE — changes existing behaviour
- ADD — adds a new feature
- CODE — changes the code without changing behaviour
- VERSION — increments the version
- DOC — documentation (including this file and ai/)
- BUILD — build system / CI

If something is outstanding from the commit, follow with a line `NOTE: <short message>`
or `TODO: <short message>`.

## Quick facts

- Build (Windows): MSVC solutions `Simutrans-Extended.sln` (game), `Makeobj-Extended.sln`
  (pak compiler), `Nettool-Extended.sln`. Also GNU make (`Makefile`, `common.mk`) and
  CMake (`CMakeLists.txt`). Details: `ai/build-and-toolchain.md`.
- Version constants live in `simversion.h`: base `SIM_*` series + Extended `EX_*` series
  (branch-dependent!) — see `ai/savegame-versioning.md`.
- Tests: Squirrel scripts `tests/*.nut`, run via an in-game scenario (needs built binary +
  pakset). CI: `.github/workflows/` (Linux ASan/UBSan test runs, MSVC/Linux builds,
  nightlies). Details: `ai/scripting-and-tests.md`.
- The repo root contains much untracked/ignored clutter and some dead legacy files; see
  `ai/repo-map.md` before trusting anything found at the top level.

## Current priorities (user-stated, September 2026)

1. Completing the `ex-15` branch (major work) — see `ai/ex-15.md`.
2. Bug fixes, minor features, testing and optimisation on both branches.
