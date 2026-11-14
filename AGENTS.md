# Simutrans-Extended — agent instructions

C++ transport simulation game. Forked from Standard Simutrans in 2008 (as "Experimental",
later "Extended"); fully independent since. Large, legacy codebase. Handle with care.

Two active branches with materially different code:
- `ex-15` — the next major version (15.x, planned since 2018); the primary work target.
- `master` — the stable/release line (14.x).
Always identify the checked-out branch first (`git branch --show-current`).

Branch selection rule (user instruction, 2026-09-08): work that is not specific to a
development branch — CI/workflows, build system, `ai/` docs, tooling, and fixes that
apply identically to all branches — is done on `master` first and then merged into the
active development branch (currently `ex-15`; this rule generalises to whichever
development branch is active after `ex-15` merges into `master`). Work specific to a
development branch is done on that branch directly.

Feature-placement clarification (user instruction, 2026-11-11): all new features are
developed on the active development branch, not on `master`. The master-first rule above
applies to shared bug fixes and infrastructure work, not to new features.

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
   or figurative expressions. Do not invent project-specific jargon, shorthand or labels.
   This is a first-order rule: name the actual code entity (function, file, phase, setting)
   or describe the mechanism directly (what loops over what, what a boundary lies between),
   and define any non-standard term at first use.
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
7. When working on an area of the code covered by a stub in the docs, first expand the
   doc to a full article by researching the code before doing the substantive work. In
   any event, always ensure understanding of the relevant part(s) of the codebase before
   making changes.
8. When unsure, write an open question into the relevant doc instead of a claim.
9. Do *not* write outside the project directory unless absolutely unavoidable. This 
   requires the user's explicit permission, which wastes the user's time. All temporary
   and scratch files (test working dirs, run logs, drafts, handoff notes) go in `ai/temp/`
   (gitignored). Clear `ai/temp/` at the end of a work session, EXCEPT the persistent
   performance-suite state under `ai/temp/perf/` (see `ai/performance.md`). At session start, if
   `ai/temp/` is non-empty, read any handoff notes there first.
10. Any change made for testing only (temporary config values, diagnostic logging,
   scaffolding, scratch data inside the repo) must carry an explicit comment containing
   the word TEST or TESTING in capital letters, so it is never mistaken for an intended
   fix. Revert all such changes before the completion of the task. Inform the user at
   every turn that TESTing code remains in case the user terminates the session earlier
   than expected.
11. All user facing text strings need to go through the translator::translate function exactly once.
    Be careful not to pass translated text to a method that later translates it again before
    display.
12. When writing new code: 
   (a) do not make provision for future but unplanned functionality;
   (b) do not use std:: or similar collections or utilities where equivalent Simutrans collections or utilities exist;
   (c) avoid unnecessary dependencies;
   (d) avoid boilerplate code;
   (e) remove any vestigial code before presenting code to the user;
   (f) re-use existing Simutrans code where possible unless there should be a good reason (e.g. performance) to do otherwise; 
   (g) prioritise performance over "clean" code or similar (but not over stability); 
   (h) do not use non-ASCII characters in comments; and
   (i) do not over-engineer: do not write code that does not actually need to be there.
13. All autonomously started game/binary test runs must pass the `-nomidi` flag,
    unless the task specifically concerns music or sound. Background music from
    unattended runs disturbs the user's work.
14. Do not add a mechanic that affects game state or economics in a way that does not
    correspond to a real quantity or process. Do not introduce an arbitrary tuning value
    that is presented as, or used like, a real quantity. If a mechanic is needed but cannot
    be grounded in a real quantity or process, do not implement it silently: present it to
    the user as a proposed exception, state explicitly that it does not model a real
    quantity and that implementing it would breach the realism principle in
    `ai/high-level-design-goals.md`, and obtain a user decision before writing it.
15. Every change to any documentation - knowledge-base documents in `ai/`, help files,
    READMEs, code comments and any other prose - requires a full theory of mind audit
    before the change is presented. This means reading the changed text, together with the
    surrounding document or comment block, as a maintainer who does not have the session's
    context, both a human contributor and a future AI agent, and checking that:
    (a) every sentence can be understood from the text itself and the documents or code it
        refers to;
    (b) every abbreviation, specialised term and piece of project shorthand is defined at
        first use, or replaced with plain language;
    (c) every pronoun and definite reference has an explicit antecedent;
    (d) code entities are named exactly as they are in the checked-out code, and code
        claims are verifiable from those entities;
    (e) links resolve and the text contains no display or encoding artefacts.
    The audit covers the whole changed passage, not only the newly added lines, because an
    edit can make existing text misleading. If any ambiguity remains, write an open
    question rather than a claim. State in the change summary that the audit was performed
    and what it found.
16. Describe how the code works directly. Do not refer to design decisions by number or
    letter in documentation, and do not describe past code: documentation covers current
    behaviour and planned future behaviour, while Git records past code. If planned behaviour
    differs from current behaviour, describe the planned behaviour separately.


## Commit messages

`<PREFIX>: <short message>`, where PREFIX is one of:
- FIX — a bug fix
- CHANGE — changes existing behaviour, including a change that materially alters the
  performance characteristics of a hot path
- ADD — adds a new feature
- CODE — changes the code without changing behaviour or materially altering the
  performance characteristics of a hot path
- VERSION — increments the version
- DOC — documentation (including this file and ai/)
- BUILD — build system / CI

If something is outstanding from the commit, follow with a line `NOTE: <short message>`
or `TODO: <short message>`.

Push to the `Simutrans--Experimental` remote (SSH), or the HTTPS
`github.com/jamespetts/simutrans-extended` URL with the gh credential helper; `origin`
uses the unauthenticated `git://` protocol and cannot accept pushes.

## Quick facts

- Build (Windows): MSVC solutions `Simutrans-Extended.sln` (game), `Makeobj-Extended.sln`
  (pak compiler), `Nettool-Extended.sln`. Always use x64 configurations — the Win32 ones
  lack the dependency include paths and do not build. Also GNU make (`Makefile`, `common.mk`) and
  CMake (`CMakeLists.txt`). Details: `ai/build-and-toolchain.md`.
- Version constants live in `simversion.h`: base `SIM_*` series + Extended `EX_*` series
  (branch-dependent!) — see `ai/savegame-versioning.md`.
- Tests: Squirrel scripts `tests/*.nut`, run via an in-game scenario (needs built binary +
  pakset). CI: `.github/workflows/` (Linux ASan/UBSan smoke + headless world-generation
  (`-generate_map`, `scripts/run-mapgen-tests.sh`) test runs, MSVC/Linux builds, nightlies).
  Details: `ai/scripting-and-tests.md`.
- The repo root contains much untracked/ignored clutter and some dead legacy files; see
  `ai/repo-map.md` before trusting anything found at the top level.

## Current priorities (user-stated, September 2026)

1. Completing the `ex-15` branch (major work) — see `ai/ex-15.md`.
2. Bug fixes, minor features, testing and optimisation on both branches.
