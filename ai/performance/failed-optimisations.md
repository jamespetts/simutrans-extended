---
status: draft
verified: ex-15 @ 6fafcbb122 (2026-11-24)
---
# Failed optimisation attempts

**Covers:** measured optimisation attempts that were implemented far enough to test and then
rejected, with the measurement that rejected them and the reason. Measurement method, fixture and
the current hotspot inventory: [performance](../performance.md). Profiling workflow, A/B hygiene
and the run-to-run noise of the canonical fixture: [profiling-method](profiling-method.md).
Current bugs: [known-bugs](../known-bugs.md).

Read an entry before starting a change in the same area. Each entry states what was attempted,
what the measurement showed, why the approach could not pay, and what would have to be different
for a retry to be worth measuring.

## How to use this document

- These are negative results, not current code. None of the rejected changes is in the tree.
- The measurements use the canonical fixture and the forced-window recipe in
  [profiling-method](profiling-method.md). The primary metric is the inclusive sample count of
  `karte_t::step` per 1000 process CPU samples (defined in
  [profiling-method](profiling-method.md)). The fixture's run-to-run spread is about 1.9% on that
  metric, so a result inside that band is not evidence of a gain, even when the sign is
  consistent.
- A retry needs a changed premise, not a changed implementation of the same idea. The "what would
  change the verdict" line gives the premise that was missing.
- Positive results and the current costs are in [performance](../performance.md); do not infer
  from an entry here that the surrounding path is unimportant.

## Per-car leader cache in `road_space::following_limit`

**Attempted change.** Store, in each `private_car_t`, the leader and limit found by the last scan
of `road_space::following_limit` (vehicle/road_vehicle.cc), then reuse the stored result when the
car's own state, both tiles' object lists and the leader's state are unchanged. A stronger variant
cached only the leader identity and recomputed the limit and gap from it.

**Measurement.** A TEST-only census ran both proposed validation rules before every scan, ran the
scan anyway, and compared the rule's result with the scan's result. Over the whole run there were
383,861,850 calls; 43.2% left through the existing occupancy fast path (the early return based on
`karte_t::get_road_occupancy`) and 218,103,808 entered the scan.

- Exact cached-result rule: **1.55% of scans**. 79.79% of scans failed because the car's own
  state changed (`steps` and speed change whenever the car moves); 14.31% because a second
  same-direction road user entered the two-tile window; 1.98% because the leader changed.
- Leader-identity rule: **52.69% of scans**, but 40.4% of *all* scans were the "no road user in my
  direction" case, and only 12.3% of scans reused a leader. Misses were other cars 21.97%, leader
  gone 9.94%, no leader with same-direction traffic 6.96%, and tile/next-tile changes 4.26%.
- The 40.4% no-leader case needs no per-car state: `objlist_t::road_dir_counts` already proves it.
  A stateless early-out for that case was implemented and measured on paired 120 s captures:
  `following_limit` inclusive 35.71% to 35.54% (-0.46%), and steps per 1000 process CPU samples
  33.96 to 33.67 (-0.85%), inside the 1.9% spread.
- Equivalence: 0 mismatches in 3,380,840 comparisons (exact rule) and 114,924,709 comparisons
  (leader-identity rule).

**Why it cannot pay.** The scan's cost is in reading the candidate objects: for each candidate it
loads `steps`, direction, length and speed from a scattered heap object, and those reads are the
measured cache misses. A cache of the *search* removes only the loop that visits the candidates;
it cannot remove the candidate reads that a hit still needs to validate the leader, and the 40.4%
no-leader case is already provable without any per-car record. The stateless early-out that covers
that case measured neutral, so the ceiling for the cached-search family is about 0.5% of
`following_limit`, not the function's full 35% share.

**Cost.** A leader record of about 32 bytes increases `private_car_t` from 192 bytes (three
64-byte cache lines of payload) to 224 bytes (+4.8 MB at 119,000 cars), so a car spans a fourth
cache line. `objlist_t::mutation_version` is an 8-bit counter that wraps, so a production cache
would also need a wider counter.

**What would change the verdict.** An approach that avoids the candidate reads rather than the
search, for example a per-tile compact record of the fields the scan reads, updated when a road
user moves; or a change to the occupancy and direction data that lets a car skip the scan outright.
Measure the candidate-read cost and the new structure's write traffic first. A cache of the search
alone will not repay its memory or its fourth cache line.

## Force-inlining the `float32e8_t` arithmetic operators

**Attempted change.** Move `float32e8_t::operator+`, `operator-`, `operator*` and `operator/`
(utils/float32e8_t.cc) into the class in utils/float32e8_t.h and mark them with a conditional
force-inline macro, with the cold overflow and division-by-zero diagnostics moved to two
out-of-line helpers.

**Measurement.** Plain `inline` did not make MSVC x64 expand the bodies even when they were in the
class; only `__forceinline` did. The change was a byte-identical transcription: a normalised
comparison found 0 differences in 202 non-empty operator lines, and a PROFILE-only equivalence
check found 0 mismatches in at least 1,560,281,088 comparisons. `etl-lines` confirmed that the
operators expand: their self share fell from 4.95% to 0.72%, and every residual sample mapped to
an inline frame in `float32e8_t.h`, with no sample on an out-of-line operator body.

Two paired canonical 120 s captures put the primary metric at +0.69% and -1.56%; the
order-corrected mean (the mean of the two paired ratios, which cancels the within-session order
effect) was **+0.45%**, inside the 1.9% run-to-run spread, and the two pairs disagreed in sign. The two functions holding the hot
expansions, `convoi_t::calc_acceleration` and `convoy_t::calc_move`, consistently lost 2.2-6.7% of
their cost, about 0.4-0.7% of process CPU, which is the size of the measured mean but not
separable from noise on this fixture. The `.text` executable section grew by 34,672 bytes (+0.56%)
and the executable by 35,328 bytes (+0.44%), concentrated in the hottest convoy functions.

**Why it cannot pay.** The operator bodies are short and branch-light; the call and return plus
register saves are a small part of a physics step whose cost is dominated by memory access and the
surrounding `calc_move` arithmetic. Moving the operator frames into the callers redistributes the
same work and adds instruction-cache pressure in exactly the functions that are already hot.
Static reasoning overstates the call overhead; the paired measurement cannot resolve it.

**What would change the verdict.** A lower-variance measurement method, such as a fixed-step CPU
benchmark rather than wall-clock canonical captures, or a workload with a much larger operator
share; or a combined change that also removes work from `calc_move` rather than only moving it.
Do not apply the patch on static reasoning alone.

## Candidate prefetching inside `road_space::following_limit`

**Attempted change.** The function is memory-latency-bound, so the experiment prefetched the
next-tile `grund_t` before the occupancy early-out and prefetched two candidates ahead for the
first cache line and one ahead for the second, in both the current-tile and next-tile scans.

**Measurement.** In 60 s traced captures the next-tile lookup line fell from 3.91% to 2.54% of
samples and the candidate-type load line fell from 3.29% to 2.80%, but the total
`following_limit` share was unchanged (about 33.7% in both) and steps per 1000 process CPU samples
remained inside the run-to-run spread. The new prefetch instructions themselves accounted for
about 0.9 percentage points of samples. The experiment was reverted.

**Why it cannot pay.** The prefetches moved the stall rather than removing it: the candidate
objects are scattered, the prefetch distance was one or two iterations (too short to cover a DRAM
miss), and the extra instructions offset the line-level improvement. The total function cost and
the whole-simulation metric did not move.

**What would change the verdict.** A prefetch scheme with enough lead time across sync-list
entries rather than within one scan, or a layout change that makes the candidate fields contiguous.
Measure the whole-function and whole-simulation metrics, not only the line attribution.

## Open questions

- Would a compact per-tile record of the fields `road_space::following_limit` reads (direction,
  steps, length, speed, lane state) repay its update traffic? The scan is candidate-read-bound, so
  this is the remaining structural direction; it is unmeasured.
- Would a fixed-sync-step CPU benchmark resolve sub-1% effects on this fixture better than the
  canonical wall-clock captures? The inlining result is the motivating case.
