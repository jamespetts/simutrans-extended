---
status: reviewed
verified: ex-15 @ 1b236a4f1
---
# Project general notes

Short cross-cutting notes that are easy to miss — read at the start of most tasks
(AGENTS.md). Kept deliberately brief per [documentation-architecture](documentation-architecture.md)
rule 3; extended by user statements. Each note ≤3 lines, with tags/anchors.

- **No floating point in sync-critical code.** Windows, Linux and Mac clients compute different
  floating-point results; floats in synced code cause desyncs. Use plain integers wherever possible
  (strongly preferred for performance); `float32e8_t` (utils/float32e8_t.h) only where sync-critical
  code genuinely needs decimals (e.g. physics; much slower than float/int). double/float are safe
  only where results never need to stay in sync between peers [RECOLLECTION:2026-09-07; class and
  physics use CODE ex-15 @ 1b236a4f1: convoy.h, simunits.h]. Full constraint: [project-architecture](project-architecture.md).
  
- This code is old. Raw pointers are common. However, we now target C++14. Do not update the old code to the new standard
  without a stronger justification that the old code is in an old style. Only an actual bug, serious vulnerability or new
  feature can justify a rewrite. Any necessary rewrite should carefully consider performance.
   
- Saved games are fragile and versioned per datum. See [savegame-versioning](savegame-versioning.md) for details. 

- (ex-15 only) No booking of cost or revenue may use base-level .dat prices, or any function of them, that
  bypasses inflation adjustment (`karte_t::get_inflation_adjusted_price`, config/prices.tab; the API does not
  exist on master). [RECOLLECTION:2026-09-07; mechanism CODE ex-15 @ 91d9b252e] Status/audit: [ex-15 economy registry](ex-15/economy-and-vehicles.md).

- Use simrand() for a random number with a seed that can be synchronised among server/clients in a network game (i.e. for
  all RNG that needs to be deterministic among clients/server in a network game to stay in sync). Use sim_async_rand()
  for when this is definitely *not* necessary (e.g. RNG for a UI feature that does not need to be shared).
  Full rules: [sync-and-determinism](sync-and-determinism.md).
  
- The codebase is very large. Simulation changes can have complex and far-reaching effects that can be hard to predict
  without a very thorough understanding of the code. Use the document system to gain that understanding.
  
- Some of the simulation code is performance critical because, although this is an old game, we now commonly play with huge
  maps. Memory bandwidth is the biggest constraint, followed by CPU power and raw memory in that order. Treat all code that
  runs under step() or sync_step() (i.e. almost all live simulation code) as performance critical unless you can prove 
  otherwise. This means:  performance can justify less readable code, or code that needs more work to get right 
  (e.g. no duplicated checks) in these areas. *Always* give detailed consideration to the performance impact of *any* 
  change that runs under step() or sync_step(). 

- Use the Simutrans container and utility classes (tpl/, utils/) instead of std (etc.) equivalents
  for new code: they are profiled to be faster for this game's workloads. Inventory + gotchas:
  [utilities](utilities.md). [RECOLLECTION:2026-09-07]

- Simutrans-Extended is multi-threaded with pthreads. It has unique (i.e. not in Standard) multi-threading for simulation
  code. These are bespoke designs that can be fragile but considerably improve performance. Modify only with great care.
  Network desyncs are a huge risk with even slightly incorrect multi-threading of simulation code.
  Mechanics/rules: [threading](threading.md); sync rules: [sync-and-determinism](sync-and-determinism.md).

- The Squirrel scripting API was never fully ported from Standard and nobody uses it in Extended. Completing it is low
  priority (massive work; Extended prioritises large MP organic games over scripted scenarios).
  Details: [scripting-and-tests](scripting-and-tests.md). [RECOLLECTION:2026-09-06]

- User-facing text: `translator::translate("English literal")` — the key IS the English text; missing or format-mismatched
  translations silently fall back to it. Details: [translations](translations.md). [CODE master @ 84b8345a4]