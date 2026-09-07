---
status: reviewed
verified: none
---
# Documentation conventions

Rules for this knowledge base. Read before interpreting tags/provenance records or writing/updating docs.
Content-architecture rules (what belongs in docs vs. code, visibility, size):
[documentation-architecture](documentation-architecture.md).

## Lifecycle

Frontmatter `status:` — `stub` → `draft` → `reviewed`. Stubs hold scope, retrieval key and
planned sections only (plus tagged initial facts). `reviewed` means the user has checked the
content in an interactive walkthrough ([documentation-architecture](documentation-architecture.md)
core rule 8).

## Provenance

Frontmatter `verified:` — `<branch> @ <short-sha>` against which the doc's content was last
checked, or `none`. Stubs keep `none`; initial facts contain their own inline branch+commit
records instead. Update this value on every substantive update.

## Doc structure

Retrieval keys ("read when") live in exactly one place per doc: top-level docs are keyed in
[index](index.md); nested docs are keyed in their parent doc, chaining up through any number of
layers to the index [RECOLLECTION:2026-09-07]. An agent must be able to pick the right docs by
following these keys from the index, without speculative opening. Every new top-level doc gets
its read-when key added to the index at creation.

Each doc: Title → **Covers** (files/dirs) → content sections → **Open questions**.
Typical sections: overview & architecture · invariants (flag savegame/network sensitivity) ·
known problems & history · coarse provenance notes (inherited-from-Standard vs originated-in-the-fork; never
detailed delta lists — they are unmaintainable).

Never, ever record temporally specific or ephemeral information, e.g. "After the last update to...". The documents
must always be temporally universal. Simply store the *latest* correct position. Do not include history unless it
should be genuinely required to make sense of the current status, which will be exceptionally rare. Updating is 
preferred to appending where things change. When updating, take a wholistic view of the entire document and craft
what would most make sense to the AI reading it next time to get all the necessary information and no unnecessary
information in the minimum number of tokens. If this involves re-arranging, do it: but be extremely careful to
preserve all necessary data.

## Mechanics

- Relative markdown links only (Obsidian + GitHub compatible). AGENTS.md uses plain paths.
- Target ≤ 300 lines per doc; when exceeded, split into multiple linked documents.
- For complex topics, documents should be nested (e.g. Index > topic > subtopic 1 | subtopic 2 | subtopic 3 > sub-subtopic 1A | sub-subtopic 1B, etc.).
- Filenames: lowercase-kebab-case.
- Agents propose doc changes; the user approves before commit (AGENTS.md hard rule).
- Do not use references to other docments that might change as the documents are edited.
- Prefer an open question over an unverified claim, always.

## Adding a feature
- When adding a feature, make sure to check whether an outstanding feature in [project-roadmap](project-roadmap.md) needs to be closed.
- When closing an outstanding feature, add it to a closed features list. There is currently no such list, so, create one and update this note to refer to it.

## Bugs
- Before investigating a reported or suspected bug, check [known-bugs](known-bugs.md). After fixing a bug, DELETE its entry (and dedicated bug doc, if any) from that doc. HARD RULE: only open bugs are listed there; fixed bugs are never kept — fix history lives in git `FIX:` commits.
- Add any newly discovered open bug (including ones deliberately not fixed) to [known-bugs](known-bugs.md) with a priority 0–4 (0 = critical showstopper, reserved; scale defined in that doc). Simple bugs are described inline there; complex bugs get their own dedicated doc linked from it.
