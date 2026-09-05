---
status: draft
verified: none
---
# Documentation conventions

Rules for this knowledge base. Read before interpreting tags/stamps or writing/updating docs.
Content-architecture rules (what belongs in docs vs. code, visibility, size):
[documentation-architecture](documentation-architecture.md).

## Lifecycle

Frontmatter `status:` — `stub` → `draft` → `reviewed`. Stubs hold scope, retrieval key and
planned sections only (plus tagged seed facts). `reviewed` means the user has checked the content.

## Provenance

Frontmatter `verified:` — `<branch> @ <short-sha>` against which the doc's content was last
checked, or `none`. Stubs keep `none`; seed facts carry their own inline stamps instead.
Re-stamp on every substantive update.

## Claim tags (inline)

- `[CODE]` — verified against the code at a stated branch+commit, e.g. `[CODE ex-15 @ b06e8fa14]`.
- `[FORUM:<url>]` — from a Simutrans forum thread; cite thread and post date where possible.
- `[RECOLLECTION:<date>]` — user statement from memory. A lead, not a fact: verify against
  code before relying on it (the user has been away from the code for some years).
- `[UNVERIFIED]` — agent inference, not yet confirmed. Must be verified or removed at review.
- `[PRIOR]` — model training-data knowledge. Search hint only, never a claim; convert to
  `[CODE]` or delete. Applies to Standard AND Extended priors.

Untagged prose in a `draft`/`reviewed` doc is implicitly `[CODE]` at the frontmatter stamp.

## Doc shape

Retrieval keys ("read when") live ONLY in [index](index.md) — the single source of truth.
An agent must be able to pick the right docs from the index alone, without opening them.
Every new doc gets its read-when key added to the index at creation.

Each doc: Title → **Covers** (files/dirs) → content sections → **Open questions**.
Typical sections: overview & architecture · invariants (flag savegame/network sensitivity) ·
gotchas & history · coarse provenance notes (inherited-from-Standard vs fork-born; never
detailed delta lists — they are unmaintainable).

## Mechanics

- Relative markdown links only (Obsidian + GitHub compatible). AGENTS.md uses plain paths.
- Target ≤ 300 lines per doc; when exceeded, split into an `ai/` subfolder and update
  [index](index.md). Filenames: lowercase-kebab-case.
- Agents propose doc changes; the user approves before commit (AGENTS.md rule 5).
- Prefer an open question over an unverified claim, always.
