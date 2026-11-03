---
status: reviewed
verified: master @ 4974e5b03
---
# External forks

**Covers:** the `Ranran-the-JuicyPork/simutrans-extended-plus` fork of `jamespetts/simutrans-extended`
— what it is, how it relates to our branches, and what comparing it against ours yields.

## Summary

The fork is a long-lived working mirror of our upstream rather than a divergent product: it
carries our own commits, and the great majority of its branch content is already merged into
Extended in a later form. The fork moves continuously: re-derive current branch positions with
git rather than trusting listed branch names. What a comparison against our branches yields:

- Branches named `jamespetts-YYMMDD` hold dated snapshots of our tree, sometimes with small
  additional fix commits on top; the default branch is the newest snapshot. They carry no
  features of their own and lag as our branches advance.
- Work absent from our tree lives on named branches (`fix/…` prefixes, feature names). The
  long-lived feature branches are: a factory/industry information layer, multi-tile city
  buildings, a chart-marker button, an aspect-based signal speed cap, vehicle-descriptor
  coupling groups, and a Standard climate port. Each needs its own review before adoption —
  several change savegame layout, window save ids or pak format without version gating, and
  several are work-in-progress with known defects. Other named branches carry systematic GUI
  robustness fixes (halt list, player ranking, minimap, signal box, replace frame, vehicle
  list) and squashed, PR-prepared fixes against our tree.
- The branch carrying the fork's own "Extended Plus" branding and changelog has no code
  divergence from Extended master; its declared feature list describes work that is already
  upstream.

## Assessed backport candidates

Fixes on the fork for code that exists identically on both our branches (so the master-first
rule applies); each needs a small manual merge against current code:

- `refactor/depot-window-review` — depot GUI correctness: convoy colour bars read the wrong
  vehicle list; removal preview accounting counted the wrong vehicle's length and disagreed
  between preview and execute; `uint8`/`sint8` length accumulators overflow on long convoys;
  line-comparator subtraction overflow; livery selector upper-bound guard.
- `fix/schedule-create-line-assignment-pr` — the line created from the schedule window was not
  selected afterwards, because `init_line_selector()` revalidated the selection against the
  convoy's schedule.

## The private car crash guards

The fork's private-car crash guards in `vehicle/simroadtraffic.cc` were assessed against the
code before adoption. Only one guards a reachable state: `private_car_t::can_enter_tile`
dereferenced `welt->lookup(get_pos())` unguarded at three sites (the third passing the result
to `vehicle_base_t::get_blocking_vehicle`, which dereferences its argument), while neighbouring
code in the same function treats the same lookup as possibly NULL. The state — a live private
car whose ground has been deleted — is corroborated by the "not found on any map square"
recovery scan in `vehicle_base_t::leave_tile()` and the "way returned here was corrupt
(possibly deleted)" comment in `private_car_t::hop()`. It is fixed in our tree: one guarded
lookup is computed at the top of `can_enter_tile` and reused at all three sites, and the car
keeps driving, because the consumers of the value already tolerate a missing way.

The remaining guards defend states the code cannot produce and were not adopted: `gr` is
null-checked at every call site of `can_enter_tile`; `enter_tile` is only called with a non-null
tile that `hop_check()` has verified has a road; the neighbour loop obtains its tiles from
`grund_t::get_neighbour`, which only returns tiles having the requested way; and the
`weg->has_sign()` flag is kept accurate by `weg_t::count_sign()` and `roadsign_t::~roadsign_t`.

## The fork

- A GitHub **fork of our own upstream** `jamespetts/simutrans-extended` (whose own ancestor is
  Standard Simutrans), renamed from `simutrans-extended`. The old URL redirects, and the `Ranran`
  remote in `.git/config` points at the old name.
- It **mirrors upstream**: its branches contain our commits, and work flows in both directions, so
  much of its content is already in `master`/`ex-15` — see the contributor-branch lineage in
  [ex-15](ex-15.md).
- It is a **work area, not a distribution channel**: no CI, no wiki content, no release artefacts.
- Branch names follow the author's own conventions (dated snapshots, feature names, `fix/…`
  prefixes) and are not a version scheme.
- It also carries tooling of its own that touches no game code: a staged identifier-rename script
  under `tools/refactor/` (rules deliberately empty) and a Japanese identifier-readability note,
  reference material for a possible German-derived identifier rename programme.

## Method

Compare with git against our branches: list the fork's branches, take the merge-base with
`master`/`ex-15` per branch, and diff that base against the branch tip. Because the fork's
content has usually been upstreamed in a later form, a plain diff overstates what is new —
patch-id comparison (`git cherry`) is needed to separate genuinely new work from work already
merged.

## A consequence for our tree

`simutrans/themes/modern.tab` defines `gui_color_chart_button_background`, a colour that no code
consumes; in the fork's chart-marker work the same key is the consumer. It is dead data in our
shipped theme, and it is absent from `themes.src/` and from the other theme files.

## Open questions

- Whether any of the fork's unmerged feature work is wanted upstream, and in what order.
- Whether the orphan theme colour is retired or the feature that would use it is completed.
- How a private car comes to outlive its ground; the fix logs a warning when the state occurs.

## Cross-references

- [ex-15](ex-15.md) — contributor branches merged into ex-15.
- [savegame-versioning](savegame-versioning.md) — versioning mechanism.
- [sync-and-determinism](sync-and-determinism.md) — sync-sensitivity rules.
