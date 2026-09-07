---
status: stub
verified: none
---
# Project overview & fork origin

**Covers:** project-level facts; README.md; LICENSE.txt; documentation/ (legacy human docs).

## Initial facts

- Forked from Standard Simutrans in 2008 as "Simutrans-Experimental", later renamed
  "Simutrans-Extended"; independent ever since; backports from Standard are partial and
  irregular [RECOLLECTION:2026-09-05]. The policy now is to backport where useful. 
  Historically (pre-AI), backporting has been subject to severe human capacity constraints.
  In the early years of the fork, most changes from Standard were routinely backported, but
  that became less with time.
- AI was first involved in coding for Simutrans-Extended in September 2026. Prior to that,
  it was human coded (with a lapse in active development from circa 2023-2026, but with some
  minor patches being merged). 
- The repo contains history inherited from Standard from before the fork; the
  shared early history is usable for studying that period [CODE].
- Many git remotes are configured (Extended contributors plus individual Standard devs) [CODE].
- There should be a canonical upstream Standard remote - the user cannot immediately recall the Git URL.
- Standard reference points for task-time comparison: shared in-repo history up to the
  fork point; Standard snapshot branches (`std-r10415`, `Ranran/std-r107xx-ex15`);
  cherry-pick log `documentation/cherry-picked-commits.txt` [CODE].
- License: Artistic License (file headers, LICENSE.txt) [CODE].
- Versioning → As at September 2026 when AI was first involved, the current release branch had a major
  version number of 14 and the next development branch had a major version number of 15. see
  [savegame-versioning](savegame-versioning.md) for details. Typically, the current released branch
  will have one major version number and the active development branch for major changes will have
  the next number up. Minor changes can be made on the current version. Major changes go into the 
  next highest version. Historically, this has not always been followed rigorously.
  
## Purpose and design philosophy

- To maximise player immersion in a deeply simulated world by maximal economic realism within the constraints
  inherent to a game of this sort. 
- All balancing decisions should be based on real life figures not fudged synthetic mechanics. Deal with the inherent
  limitations by abstraction of the actual real underlying mechanism rather than by synthesising a mechanism that differs
  in material respects from the real world.
- We are succeeding if the player can succeed by making the same choices in game as would cause success in an analogous situation
  in real life, and fail by making the same choices in the game as would cause failure in an analogous situation in real life.
  Players should be able to predict what will succeed based on historic transport and economic knowledge and a working knowledge of 
  what the game simulates without having to have a detailed knowledge of game-specific economics.
- Targeting especially (but not exclusively) long form game play on a large map with a semi-persistent online server. Typically,
  if played heavily, a full run from 1750 to the present day will last about one year and the server will then be reset manually.
- For a detailed breakdown of high level design goakls, see [high-level-design-goals](high-level-design-goals.md). 

## Planned sections

- Origin & Standard relationship (coarse only — detailed deltas are deliberately NOT
  documented: too vast and volatile; see AGENTS.md rules on priors).
- Release model: nightlies, Bridgewater-Brunel server and Github CI (set up by the AI) → [build-and-toolchain](build-and-toolchain.md).
- Community: forum, wiki (pointers in README.md).

## Open questions

- Exact fork-point commit/date in 2008 (to be located in history).
