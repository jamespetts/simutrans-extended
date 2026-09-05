---
status: stub
verified: none
---
# Project overview & fork lineage

**Covers:** project-level facts; README.md; LICENSE.txt; documentation/ (legacy human docs).

## Seed facts

- Forked from Standard Simutrans in 2008 as "Simutrans-Experimental", later renamed
  "Simutrans-Extended"; independent ever since; backports from Standard are partial and
  irregular [RECOLLECTION:2026-09-05].
- The repo contains Standard-ancestry history from 2006-08-20 (first commit `f8c312383`
  "Import version 84.20"); ~2,070 pre-2009 commits are shared and usable for archaeology
  [CODE master @ 3b70dd4b3].
- ~45 git remotes are configured (Extended contributors plus individual Standard devs);
  there is NO canonical upstream Standard remote [CODE master @ 3b70dd4b3].
- Standard reference points for task-time comparison: shared in-repo history up to the
  fork point; Standard snapshot branches (`std-r10415`, `Ranran/std-r107xx-ex15`);
  cherry-pick log `documentation/cherry-picked-commits.txt` [CODE ex-15 @ b06e8fa14].
- License: Artistic License (file headers, LICENSE.txt) [CODE ex-15 @ b06e8fa14].
- Version lines: master 14.x, ex-15 = 15.0 with its own savegame series →
  [savegame-versioning](savegame-versioning.md) [CODE].

## Planned sections

- Extended's purpose & design philosophy (simulation depth; interview-driven).
- Lineage & Standard relationship (coarse only — detailed deltas are deliberately NOT
  documented: too vast and volatile; see AGENTS.md rule 3 on priors).
- Release model: nightlies, Bridgewater-Brunel server → [build-and-toolchain](build-and-toolchain.md).
- Community: forum, wiki (pointers in README.md).

## Open questions

- Exact fork-point commit/date in 2008 (to be located in history).
