---
status: draft
verified: ex-15 @ 1b236a4f1
---
# Project architectural constraints

Binding, stable constraints that apply across subsystems. Violating one is a design error,
not a style choice. Short-form reminders: [project-notes](project-notes.md).

## Constraints (verified or user-sourced)

1. **Determinism — no floating point in sync-critical code.** Network games must produce
   identical results on Windows and Linux clients; floating-point results differ across
   platforms and cause desyncs. Simulation code that must stay in sync uses the fixed-point
   class `float32e8_t` (utils/float32e8_t.h) — e.g. convoy physics (forces, resistances,
   braking, `calc_move`) in convoy.h, speed conversions in simunits.h, vehicle resistances
   in descriptor/vehicle_desc.h [CODE ex-15 @ 1b236a4f1; rationale RECOLLECTION:2026-09-05].
2. **Serialization change restriction.** The savegame/network versioning system is stable
   and rarely changed; load/save behaviour and version constants must not be changed
   without following AGENTS.md rule 4 → [savegame-versioning](savegame-versioning.md).
3. **Ordered persistence.** Save data is read and written in order; persisted classes
   evolve by appending entries conditional on version checks →
   [savegame-versioning](savegame-versioning.md) [CODE].
4. **Branch duality.** ex-15 and master differ materially (versions, features, files);
   changes must target one branch deliberately, and docs stamp the branch+commit they were
   verified against → [ex-15](ex-15.md) [CODE].

## Candidate constraints (verify before promoting)

- Sync-critical state must be covered by network checksums (ex-15 history: "FIX: Missing
  checksum datum for vehicles") — mechanism to document in [network](network.md) [UNVERIFIED].
- User-facing strings go through the translator (dataobj/translator.*) [UNVERIFIED].
- Threading: what runs off the main thread (path explorer, save/load threads) and the lock
  discipline → [utilities-and-threading](utilities-and-threading.md) [UNVERIFIED].
- Container/handle idioms (tpl/, *handle_t) — stable enough to document as constraints? [UNVERIFIED].

## Open questions

- Is `float32e8_t` mandated for ALL sync-critical math, or only physics? Where may `double`
  still appear safely (GUI-only? load-time-only)? (Ask user; then record here.)
