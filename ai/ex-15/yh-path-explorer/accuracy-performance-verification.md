---
status: draft
verified: ex-15 @ bf447619a
---
# ex-15: Y/H path explorer — accuracy, performance and verification

**Covers:** the worked accuracy examples (cell-for-cell test oracle), the performance model against
virtual concatenation, and the per-stage verification plan for the Y/H traversal design. Kernel
layout and SIMD → [path-explorer-optimisation](../../path-explorer-optimisation.md). Parent design: [yh-path-explorer](../yh-path-explorer.md); method and
fixtures: [performance](../../performance.md), [sync-and-determinism](../../sync-and-determinism.md),
[threading](../../threading.md).

## Accuracy: worked examples (test oracle)

All times in minutes. These are the expected values for the synthetic maps in Verification (below).

Average case: source `A-B-C-D-E` half-hourly (wait at `A` 15), target `C-F-G` hourly (wait at `C`
30), walking transfer at `C` 6, through dwell at `C` 3, rides `A->C` 22 and `C->G` 25. True `A->G`
= 15 + 22 + 3 + 25 = **65**. A naive recording that adds a second wait and the walking transfer
gives 98; the design (Edge generation and Through-journey substitution in the parent doc) gives 65.

Extreme case: as average, plus a second divide at `G` (dwell 4, walking transfer 8) to `G-H-I`
(ride `G->I` 18), an H-exchange at `D` (dwell 2, stop lists unchanged), and a forcing-discharge
stop `F` on the `C-F-G` branch. True `A->I` = 15 + 22 + 3 + 25 + 4 + 18 = **87**. A direct
recorded `A->I` edge gives 87. If instead the search composes `A->G` with `G->I`, the second cell's
walking transfer at `G` (8) is used unless the through-journey substitution replaces it with the
dwell (4), giving 91 (+4); the error per intermediate divide is walking transfer minus dwell.
`F->I` as one through movement is invalid because `F` forces discharge. The H-exchange at `D` adds
2 minutes and no new origin pair. Both the cross-boundary condition and portion-sensitive discharge
must yield exactly these cells.

## Performance model (against virtual concatenation)

Let `L` be the number of linkages, `S` the mean expanded stop count, `Y` the number of coupling
entries, and (for virtual concatenation) `b` the branching factor and `D` the followed depth. Base
phase-2 work is about `L * S^2` connexion insertions (each with allocation, waiting lookup, journey
lookup and table comparison). The design adds about `Y * S^2` insertions, localised at coupling
entries, plus about `|inbound| * k` relaxations at each affected transfer. Virtual concatenation
adds about `Y_s * b^D * (D*S)^2` insertions per source. At `b=2, D=2` it is roughly 16 times a
source's base work and at `b=2, D=3` roughly 72 times, so it needs only a modest live density to
dominate a pass; the design grows in proportion to the number of coupling entries. The
through-journey substitution adds one direct lookup per transport-cluster pair in phase 5, outside
the inner loop.
Neither method changes the number of working halts or the matrix sizes.

## Kernel layout and SIMD

The staged SIMD position and the phase-5 kernel requirements are in
[path-explorer-optimisation](../../path-explorer-optimisation.md). For Y/H in particular, the
through-journey substitution is one constant per transport-cluster pair, applied outside the inner
loop, and through edges need distinct first and last transports.

## Verification

Per stage: a loopback server-plus-client run with a checklist comparison every sync step and zero
divergence; a performance-suite pass-duration and total-iteration comparison (throughput, not CPU
share, because the controller holds CPU share near its target); worst-step hitch against the
midpoint; and load/join full-refresh timing.
