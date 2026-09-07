---
status: reviewed
verified: master @ 78a4bb3b9
---
# Project architectural constraints

Mandatory, stable constraints that apply across subsystems. Violating one is a design error,
not a style choice. Short-form reminders: [project-notes](project-notes.md).

## Constraints (verified or user-sourced)

1. **Determinism — no floating point in sync-critical code.** Network games must produce
   identical results on Windows, Linux and Mac clients; floating-point results differ across
   platforms and cause desyncs. Sync-critical code uses plain integers wherever possible
   (strongly preferred: `float32e8_t` performance is much worse than float or int); the
   fixed-point class `float32e8_t` (utils/float32e8_t.h) only where sync-critical code
   genuinely needs decimals — e.g. convoy physics (forces, resistances, braking, `calc_move`)
   in convoy.h, speed conversions in simunits.h, vehicle resistances in
   descriptor/vehicle_desc.h. `double`/`float` may be used anywhere that never needs to be
   kept in sync between network servers/clients; integers may be used anywhere
   [CODE ex-15 @ 1b236a4f1, anchors confirmed master @ 78a4bb3b9; RECOLLECTION:2026-09-07].
2. **Serialization change restriction.** The savegame/network versioning *system* (mechanism
   and version constants) is stable and rarely changed; the save data themselves change more
   frequently, but only within that system's rules (constraint 3) and following AGENTS.md
   rule 5 → [savegame-versioning](savegame-versioning.md) [RECOLLECTION:2026-09-07].
3. **Ordered persistence.** Save data is read and written in a fixed order within each
   persisted class's rdwr(); a misplaced or reordered entry corrupts the stream. Every new
   persisted entry must be wrapped in a version check so older saves still load; its position
   in the sequence is free — the end is usually best for readability, but an entry belonging
   to a set of data read/written together often goes (and is often put) inside that set
   [CODE; RECOLLECTION:2026-09-07] → [savegame-versioning](savegame-versioning.md).
4. **Branch duality.** ex-15 and master differ materially (versions, features, files).
   Placement workflow: major new features go on ex-15; bugfixes and minor enhancements go
   into master and are then immediately ported/merged into ex-15; ex-15 will eventually be
   merged into master when it is ready for release [RECOLLECTION:2026-09-07]. Canonical
   text → [ex-15](ex-15.md). Docs record the branch+commit they were verified against [CODE].
5. **Checklist policing.** All per-step simulation state must fall within the checklist
   system's coverage: the synced RNG state is sampled at fixed checkpoints bracketing every
   simulation phase, plus per-step aggregate sums and quickstone allocation counters; new
   per-step simulation work must fit this coverage or extend it [CODE master @ 78a4bb3b9].
   Contract → [sync-and-determinism](sync-and-determinism.md); mechanism → [network](network.md).
6. **Translator.** User-facing strings go through the translator (dataobj/translator.*); the
   pattern is pervasive across the tree [CODE master @ 78a4bb3b9; RECOLLECTION:2026-09-07].
7. **Container/handle idioms.** The tpl/ container classes and the quickstone handle idiom
   (`halthandle_t`, `convoihandle_t`, `linehandle_t`, etc., tpl/quickstone_tpl.h — whose
   allocation counters also feed the checklist) are the mandatory pattern for new code in the
   subsystems that use them; violating the idiom is a design error
   [CODE master @ 78a4bb3b9; RECOLLECTION:2026-09-07].

8. **Threading discipline.** Non-main threads mutate shared game state only inside explicitly
   bounded start→await (or lifecycle) windows, protected by a named mutex or by barrier
   discipline guaranteeing no main-thread consumer runs concurrently; code that modifies data a
   live thread may read or write must await that thread first; no new thread lifecycles outside
   the established pattern. Per-thread-class mutation inventory → [threading](threading.md)
   [CODE master @ 78a4bb3b9; RECOLLECTION:2026-09-07].

## Candidate constraints (verify before promoting)

(None.)

## Provenance

Constraint 1 examples verified on ex-15 @ 1b236a4f1, anchors confirmed present on master;
constraints 5–7 verified on master @ 78a4bb3b9 — the underlying files (utils/checklist.*,
dataobj/translator.*, tpl/quickstone_tpl.h, utils/float32e8_t.h) are identical on both
branches [CODE]. Constraint 8 rests on the 2026-09-07 code inventory of non-main-thread
mutations recorded in [threading](threading.md) (threading model identical on both branches —
provenance there). Remaining content is user-sourced.

## Open questions

(None.)
