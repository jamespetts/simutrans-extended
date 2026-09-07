---
status: reviewed
verified: none
---
# Documentation architecture

Content-architecture constraints for this KB. Mechanics (tags, provenance records, links,
lifecycle) live in [conventions](conventions.md). Read before authoring or restructuring any doc.

## Core rules

1. **Document systems, not volatile details.** Docs describe stable structures, mechanisms
   and invariants; per-subsystem, per-version detail belongs to the code and rots in docs.
   Example: [savegame-versioning](savegame-versioning.md) documents the versioning
   mechanism only — never which subsystem stores which data at which version; that is
   in the `rdwr()` methods and changes constantly.
2. **Point, don't duplicate.** Prefer symbol anchors (file + function/class/constant
   name) over copied code or data listings. Never use line-number anchors — line numbers
   shift with every commit and silently mislead.
3. **Visibility rule.** Cross-cutting facts an agent might not know to look for (e.g. the
   no-floating-point rule for network sync) go in [project-notes](project-notes.md) —
   short, and read on most tasks — not only in the domain doc, which a reader consults
   only if they already suspect the constraint applies.
4. **One canonical location per fact.** Mandatory invariants → [project-architecture](project-architecture.md);
   narrative/history/origin → [project-overview](project-overview.md); branch & feature
   status → [ex-15](ex-15.md). Everything else cross-links; never copies.
5. **Size limits.** Small docs; ≤300 lines (conventions). When a domain's doc exceeds the
   limit, split into an `ai/` subfolder and update [index](index.md).
6. **Provenance.** Every claim tagged per [conventions](conventions.md); prefer open
   questions over unverified claims, always.
7. **No transient or git-derivable data.** Docs must NOT record commit counts, churn
   statistics, include/file/line counts, current version values, branch-tip hashes, or
   "as of" sync claims. Record stable structure and interpretation instead; agents derive
   live numbers from git and the code at task time.
8. **Reviews are interactive.** A doc reaches `reviewed` only after the agent has walked
   the user through it in a structured question session (question tool), in batches:
   presenting claims for confirmation or correction, asking the user about the doc's open
   questions, and recording answers with `[RECOLLECTION:<date>]` tags. Agent-side checking
   alone never sets `reviewed`.
9. **Lazy elaboration.** Stubs are fleshed out only when real work needs them. Docs that
   the current work programme makes mandatory reading are elaborated proactively; other
   domain docs (e.g. [gui](gui.md), [rendering](rendering.md)) stay stubs until a first
   task in that domain requires detail. Do not generate detail speculatively.
   [RECOLLECTION:2026-09-06]

## Inventory policy

- Top-level docs get an index key at creation; nested docs get their read-when keys from their
  parent doc, chaining up through any number of layers to the index [RECOLLECTION:2026-09-07].
  Stubs start with Covers + planned sections only.
- Stubs may contain tagged initial facts; drafts contain verified system descriptions; `reviewed`
  means the user has checked the content in an interactive walkthrough (core rule 8).
