---
status: draft
verified: none
---
# Documentation architecture

Content-architecture constraints for this KB. Mechanics (tags, stamps, links, lifecycle)
live in [conventions](conventions.md). Read before authoring or restructuring any doc.

## Core rules

1. **Document systems, not volatile details.** Docs describe stable structures, mechanisms
   and invariants; per-subsystem, per-version detail belongs to the code and rots in docs.
   Example: [savegame-versioning](savegame-versioning.md) documents the versioning
   mechanism only — never which subsystem stores which data at which version; that lives
   in the `rdwr()` methods and changes constantly.
2. **Point, don't duplicate.** Prefer symbol anchors (file + function/class/constant
   name) over copied code or data listings. Never use line-number anchors — line numbers
   shift with every commit and silently mislead.
3. **Visibility rule.** Cross-cutting facts an agent might not know to look for (e.g. the
   no-floating-point rule for network sync) go in [project-notes](project-notes.md) —
   short, and read on most tasks — not only in the domain doc, which a reader consults
   only if they already suspect the constraint applies.
4. **One canonical home per fact.** Binding invariants → [project-architecture](project-architecture.md);
   narrative/history/lineage → [project-overview](project-overview.md); branch & feature
   status → [ex-15](ex-15.md). Everything else cross-links; never copies.
5. **Size discipline.** Small docs; ≤300 lines (conventions). When a domain outgrows its
   doc, split into an `ai/` subfolder and update [index](index.md).
6. **Provenance.** Every claim tagged per [conventions](conventions.md); prefer open
   questions over unverified claims, always.
7. **No transient or git-derivable data.** Docs must NOT record commit counts, churn
   statistics, include/file/line counts, current version values, branch-tip hashes, or
   "as of" sync claims. Record stable structure and interpretation instead; agents derive
   live numbers from git and the code at task time.

## Inventory policy

- New docs get an index key at creation; stubs start with Covers + planned sections only.
- Stubs may carry tagged seed facts; drafts carry verified system descriptions; `reviewed`
  means the user has checked the content.
