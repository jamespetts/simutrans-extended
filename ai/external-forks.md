---
status: reviewed
verified: ex-15 @ 1eb670d28
---
# External forks

**Covers:** the `Ranran-the-JuicyPork/simutrans-extended-plus` fork of `jamespetts/simutrans-extended`
— what it is, how it relates to our branches, and what comparing it against ours yields.

## Summary

*Snapshot of 2026-10-21, comparing the fork's branches against our `master` and `ex-15` at that
date. The fork moves continuously; re-derive the current position with git rather than trusting
this section.*

The fork is a long-lived working mirror of our upstream rather than a divergent product: it
carries our own commits, and the great majority of its branch content is already merged into
Extended in a later form. Three things came out of the comparison:

- The fork's default branch is an upstream-master snapshot plus a small body of crash-guard
  fixes. It lags our branches and carries no features of its own.
- The branches that do carry work absent from our tree are long-lived feature branches: a
  factory/industry information layer, multi-tile city buildings, a chart-marker button, an
  aspect-based signal speed cap, vehicle-descriptor coupling groups, a Standard climate port,
  and a body of GUI robustness fixes. Each needs its own review before adoption — several change
  savegame layout, window save ids or pak format without version gating, and several are
  work-in-progress with known defects.
- The branch carrying the fork's own "Extended Plus" branding and changelog has no code
  divergence from Extended master; its declared feature list describes work that is already
  upstream.

## The fork

- A GitHub **fork of our own upstream** `jamespetts/simutrans-extended` (whose own ancestor is
  Standard Simutrans), renamed from `simutrans-extended`. The old URL redirects, and the `Ranran`
  remote in `.git/config` points at the old name.
- It **mirrors upstream**: its branches contain our commits, and work flows in both directions, so
  much of its content is already in `master`/`ex-15` — see the contributor-branch lineage in
  [ex-15](ex-15.md).
- It is a **work area, not a distribution channel**: no CI, no wiki content, no release artefacts.
- Its branches are numerous and long-lived; names follow the author's own conventions (feature
  names, `fix/…` prefixes) and are not a version scheme.

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

## Cross-references

- [ex-15](ex-15.md) — contributor branches merged into ex-15.
- [savegame-versioning](savegame-versioning.md) — versioning mechanism.
- [sync-and-determinism](sync-and-determinism.md) — sync-sensitivity rules.
