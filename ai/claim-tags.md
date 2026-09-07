---
status: reviewed
verified: none
---

# Claim tags (inline)

- `[CODE]` — verified against the code at a stated branch+commit, e.g. `[CODE ex-15 @ b06e8fa14]`.
- `[FORUM:<url>]` — from a Simutrans forum thread; cite thread and post date where possible.
- `[RECOLLECTION:<date>]` — user statement from memory. An indication, not a verified fact:
  verify against code before relying on it (the user has been away from the code for some years).
- `[UNVERIFIED]` — agent inference, not yet confirmed. Must be verified or removed at review.
- `[PRIOR]` — model training-data knowledge. Search hint only, never a claim; convert to
  `[CODE]` or delete. Applies to Standard AND Extended priors.

Untagged prose in a `draft`/`reviewed` doc is implicitly `[CODE]` at the frontmatter `verified:` value.