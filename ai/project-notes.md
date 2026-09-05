---
status: draft
verified: ex-15 @ 1b236a4f1
---
# Project general notes

Short cross-cutting notes that are easy to miss — read at the start of most tasks
(AGENTS.md). Kept deliberately brief per [documentation-architecture](documentation-architecture.md)
rule 3; grows via interview. Each note ≤3 lines, with tags/anchors.

- **No floating point in sync-critical code.** Windows and Linux clients compute different
  floating-point results; any network-synced simulation code using floats causes desyncs.
  Use the fixed-point class `float32e8_t` (utils/float32e8_t.h) for physics-style simulation
  math [RECOLLECTION:2026-09-05; class and physics use CODE ex-15 @ 1b236a4f1: convoy.h,
  simunits.h]. Full constraint: [project-architecture](project-architecture.md).
