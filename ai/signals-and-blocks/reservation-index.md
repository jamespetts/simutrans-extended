---
status: draft
verified: ex-15 @ 23271d64e plus working-tree route-unreserve removal and profiling (2026-11-14)
---
# Per-holder reservation index

**Covers:** the per-holder reservation index in `karte_t` and `schiene_t`; its maintenance in the
reserve, unreserve, load and destruction paths; its memory and determinism properties; the clearing
operations that use it (`convoi_t::unreserve_route()` and the `needs_full_route_flush` branch of
`rail_vehicle_t::clear_token_reservation()`); measured performance; the approach to subsequent
signalling refactoring and incidental bug fixes; and validation. Current reservation mechanics:
[block-reservations](block-reservations.md). Player-facing signalling:
[signals-and-blocks](../signals-and-blocks.md). Consist recombination:
[consist-recombination](../consist-recombination.md).

Measured on the canonical gargantuan fixture (`bb-10-sep-2023.sve`), clearing a convoy's
reservations takes 0.47 microseconds on average and 9.2 microseconds at worst on the main thread,
where a full way-list scan takes 6.73 milliseconds of main-thread time and about 26 milliseconds of
total processor time per call. The measurement and its limits are in
[Measured performance](#measured-performance).

Read [sync-and-determinism](../sync-and-determinism.md),
[threading](../threading.md), [network](../network.md), [performance](../performance.md) and
[savegame-versioning](../savegame-versioning.md) before changing this area.

## Why the route alone cannot replace the scan

A complete sweep of `weg_t::get_alle_wege()` to find one holder's reservations costs O(W), where W
is the number of way objects in the world, and reads every way object whether it is reserved or not.
`rail_vehicle_t::clear_token_reservation()` needs the same sweep when `needs_full_route_flush` is
set. Even divided among workers, the cost scales with the whole map rather than with the
reservations being released.

The `convoi_t::route` is not a complete list of held reservations. Token block and one-train-staff
working deliberately retain stale reservations behind a train; a route recalculation can replace the
route before those reservations are released; and repeated recalculations can occur before that release.
`next_reservation_index` is not an inventory of all ordinary, directional and stale reservations. Thus
clearing must not assume that every reservation lies on the current route.

Caller-specific route-bounded clearing remains useful only where an exact reservation invariant has been
proved. It is not a general replacement for the index.

## Representation

Maintain a **world-owned reservation index** with two components:

1. A bucket table indexed by the raw `uint16` `convoihandle_t` ID. Each bucket is a
   `vector_tpl<schiene_t*>` containing the ways currently held by that holder ID.
2. A `uint32` **back-index** in each `schiene_t`, recording that way's position in its holder's bucket.
   An unregistered way carries `UINT32_MAX_VALUE` as its invalid-index sentinel.

The back-index supports removal by swapping the bucket's last entry into the removed entry's position and
then updating the moved way's back-index. Do not use an order-preserving `vector_tpl::remove`, which would
search and shift entries. Bucket iteration order has no player-visible meaning.

`schiene_t::reserved` remains the authoritative reservation state. The bucket and back-index are derived
bookkeeping only. In particular:

- admission uses `schiene_t::can_reserve` and the way's reservation fields;
- clearing calls the reservation-release methods in `schiene_t`;
- no signal aspect, crossing state or movement rule is read from the index.

The table belongs to the world. One bucket exists for each possible `convoihandle_t` quickstone
entry; the table is preallocated. Each bucket contains only entries with that raw holder ID.

## Complexity and memory

Let:

- **S** = the number of `schiene_t` objects in the world, including runways;
- **R** = the number of registered way entries with nonzero raw holder IDs across all holders;
- **Rc** = the number of registered way entries with the raw holder ID of the convoy being cleared;
- **C** = the number of allocated entries in the world bucket table.

On a 64-bit target, before allocator and vector-capacity overhead, the additional memory is:

| Component | Scope | Additional bytes |
|---|---|---:|
| Back-index | Every `schiene_t` in the world | 4 x S |
| Reservation pointer entries and their allocated capacity | All live holders in the world | At least 8 x R |
| Bucket headers | One world-level table | 16 x C |
| Player allocation | None beyond the ways, convoys and reservations already counted | 0 |
| Per-convoy fixed table | None | 0 |

`quickstone_tpl` limits a holder table to 65,535 entries. Preallocating the world bucket table for every
possible ID therefore requires at most **65,535 x 16 = 1,048,560 bytes**, approximately **1 MiB**, plus the
reservation entries. The fixed cost is accepted [RECOLLECTION:2026-11-14]; allocating the table for every
possible ID is simpler than resizing it to the highest allocated handle ID, and no implementation
constraint makes preallocation unsuitable.

Bucket capacity can exceed the live entry count. `release_reservation_bucket()` releases a destroyed
holder's pointer buffer and keeps the world table's bucket headers. Bucket capacity is not reduced for
surviving holders: on the gargantuan fixture the high-water capacity is 54,454 pointer slots against
37,260 live entries (1.46 times), with a largest bucket of 433 entries, so capacity reduction would add
churn without a measured need.

Measured on `bb-10-sep-2023.sve` with four threads: W = 803,565; S = 248,568; R = 37,260 across 2,645
holders; largest Rc = 433. The index occupies 2,420.4 KiB: 1,048,560 bytes of bucket headers, 435,632
bytes of pointer capacity and 994,272 bytes of back-indices. Holder-size histogram: 1 entry: 773
holders; 2: 568; 3-4: 181; 5-8: 275; 9-16: 193; 17-32: 276; 33-64: 244; 65 or more: 135
[EXECUTION-VERIFIED:2026-11-14, Profile (server)|x64].

Membership follows the **raw holder ID**: a way with raw ID zero belongs to no bucket, while a way with a
nonzero raw ID belongs to exactly one bucket. This applies even when the quickstone handle does not currently
resolve to a `convoi_t`; loading and handle reuse must not change the raw-ID clearing semantics by
omitting such an entry. Invariant checks must verify these membership rules and verify that each back-index
identifies the same way in the bucket for its stored raw holder ID.

## Registration and ownership

All reservation-holder membership changes go through `schiene_t::change_reservation_holder()`: the
write sites are `schiene_t::reserve`, both `schiene_t::unreserve` overloads, deserialisation in
`schiene_t::rdwr` and `runway_t::rdwr`, and the `schiene_t` destructor.

### Reserve

A request for a directional reservation over an existing `block` or `stale_block` reservation returns
true without changing ownership. On a zero-to-nonzero raw-holder-ID transition, the way is appended
and its back-index is set. Re-reservation by the same holder does not change membership. A successful
request that replaces a compatible directional holder unregisters
the old holder and registers the new holder as one ownership change. Raw IDs are compared for these
index transitions: an old nonzero raw ID whose quickstone handle does not resolve still denotes a registered
way; it is not classified as raw ID zero.

### Release

The entry is removed using the **old stored holder**, then the way's reservation is cleared through the
`schiene_t` release methods. The `vehicle_t*` overload does not establish that the supplied vehicle's
convoy is the holder; using the caller's identity for registry removal would be wrong. `set_stale`
and direction-only changes do not alter membership.

The unchecked nature of `unreserve(vehicle_t*)` is a separate behavioural question.

### Way lifetime

A reserved way's destruction removes its registration before its memory can be reused. This does not
require dereferencing its holder: convoy destruction can precede way destruction, and full convoy release
is skipped during world destruction. `schiene_t::cleanup()` does not itself clear a reservation, so the
`schiene_t` destructor performs the explicit destruction-time removal.

### Save and load

The index is not serialised. Loading registers every raw holder ID restored by `schiene_t::rdwr` and
`runway_t::rdwr` without requiring that the corresponding `convoi_t` has already loaded. Subsequent
reservations made during convoy loading and `finish_rd` use the same index-maintenance methods.
Holder-ID changes made while saving, including rejection of a corrupt reservation, leave the
derived index consistent with the live way fields.

Runways have explicit coverage: `runway_t::rdwr` saves and restores the holder ID for the relevant
format revisions but does not save reservation type or direction. Initial state after loading follows
the existing runway rules exactly.

## Clearing operations

`convoi_t::unreserve_route()` visits only the bucket for `self.get_id()` and releases every entry whose
stored holder still matches, using checked-handle release semantics. Iteration accounts for
swap-removal mutating the bucket during the walk.

The `needs_full_route_flush` branch of `rail_vehicle_t::clear_token_reservation()` visits the same holder's
bucket but releases only entries that match the convoy's waytype and satisfy the `is_stale()` test. Holder
type and direction changes that do not change holder identity do not change bucket membership.

A registry/holder mismatch is an implementation defect, not permission to skip a reservation or silently
introduce a different recovery rule. Production clearing does not periodically scan every way to conceal
index drift.

The per-tile `rail_vehicle_t::leave_tile` and `convoi_t::reserve_own_tiles` operations use the same
registration methods and need no additional route bookkeeping.

The shared-contact coupling handoff will transfer the single holder through the same central
ownership-change operation: unregister the old holder and register the authorised remaining holder. Never place
the same way in two buckets. Physical occupancy by two convoys does not itself imply two reservation holders.

## Measured performance

[EXECUTION-VERIFIED:2026-11-14, Profile (server)|x64, `bb-10-sep-2023.sve`, four threads]

The measurement used a profiling build that could select either clearing path; the same fixture ran
each in turn in a 60-second server-paced window. Both paths include the index maintenance in the
reserve and unreserve methods.

| Metric | Map-wide scan (comparison path, four workers) | Per-holder bucket clear |
|---|---:|---:|
| Calls | 2,967 | 3,141 |
| Main-thread time per call | 6.73 ms | 0.47 microseconds |
| Worker processor time per call (summed) | 19.47 ms | 0 |
| Worst main-thread call | 22.0 ms | 9.2 microseconds |
| Process CPU time, all threads, 60-second window | 84.8 s | 62.8 s |
| Game steps, same window | about 348 | about 382 |

A separate microbenchmark (a timed loop over the operation on the loaded world) measured the
map-wide scan at 6.465 ms per call across four workers and a traversal of the largest holder's
bucket (295 entries at that moment) at 0.217 microseconds. The scan reads W = 803,566 ways per call;
the bucket path reads only the holder's entries.

The total-process saving is smaller than the clearing saving because clearing was about 40% of
process processor time in the scan run; the convoy, passenger, path-explorer and private-car workers
are unchanged in both runs. The window comparison is one run per path and covers slightly
different game-time intervals because the scan run completed fewer steps. It does not isolate the
cost of index maintenance against a build without the index: both paths in the comparison pay that
maintenance. Peak working set over the window was 4,348 MB for the bucket path and 4,333 MB for the
scan path; that difference is within the fixture's normal run-to-run variation.

## Threading

Bucket maintenance and both clearing operations run on the main simulation thread. There are no
reservation-clearing workers or barriers.

Serial clearing is sufficient for the measured workload. Do not add parallel bucket clearing without
evidence that a single holder can retain enough reservations to cause unacceptable main-thread
latency. If that case is measured, design a separate protocol in which index mutation and
way-reservation mutation are not concurrent; it is not part of the current implementation.

## Global reserved-way list as an alternative

One global vector of every reserved way, using the same per-way back-index, would change clearing from O(W)
to O(R) and avoid per-holder buckets. A clear would still examine other convoys' reservations. The
implementation uses the per-holder index with O(Rc) clearing; do not retain both the global pointer list
and per-holder buckets without measured justification.

## Outstanding work

- The shared-contact coupling handoff is not implemented. `schiene_t::change_reservation_holder()`
  supplies the single-holder operation it will use.
- Extend validation to world teardown under every destructor order and every supported thread configuration.

## Signalling refactoring and incidental bug fixes

Establish behaviour coverage for reservations and signal aspects before
refactoring the signalling algorithms. Prefer small, separately verifiable changes at narrow interfaces, such
as `schiene_t` reservation ownership and exactly duplicated side-effect-free predicates. Do not make failed
paths pure or reorder reservation and aspect writes without proving the resulting behaviour; existing callers
can depend on partial state changes and on writes made during the reservation walk.

**Fix incidental bugs found in the signalling code while refactoring it.** Do not preserve a defect merely
because an existing execution trace exhibits it, and do not leave a confirmed incidental defect unfixed solely
to keep the surrounding refactor small. For each such bug:

- identify the incorrect mechanism and the intended rule before changing it; ask the user when existing code
  and design documents do not establish that rule;
- add a focused regression test that distinguishes the corrected behaviour from the defect, and review any
  pre-existing characterisation expectation that recorded the defect;
- identify the change explicitly as a bug fix rather than describing it as behaviour-preserving refactoring,
  even when it is delivered in the same work package as nearby refactoring; and
- rerun the relevant signalling, save/load and network-determinism tests.

Suspicious argument orders, duplicated state ownership and unchecked reservation release require the same
classify-test-fix treatment; their presence is a reason to investigate, not proof that every suspicious
expression is defective. Do not compensate for an uncertain rule by introducing a new mechanic or an arbitrary
tuning value.

## Validation

Validation coverage:

- the active Squirrel suite (80/80 tests);
- the paired-runtime cases and the paired save/network/offline reload;
- focused tests for raw unbound-holder registration and transfer, simulated raw-ID binding and reuse,
  directional-to-block holder replacement, same-holder re-reservation without duplication,
  `unreserve(vehicle_t*)` where the supplied vehicle did not identify the holder, way destruction with live
  and unbound holders, an in-memory runway `rdwr` round-trip, and `needs_full_route_flush` preserving
  non-stale reservations and stale reservations of another waytype;
- the clearing-path A/B comparison and microbenchmark under [Measured performance](#measured-performance);
- a malformed-save load with holder ID 65535, which was discarded with a diagnostic and loaded without an
  index fatal.

Not yet covered:

- shared-contact reservation-holder transfer, which belongs to the not-yet-implemented coupling handoff;
- world teardown under every destructor order;
- every supported thread configuration;
- the isolated cost of index maintenance in a workload that performs no broad clearing.

Heavy-mode hashes cover the saved reservation fields on ways, not the derived buckets. A bucket-order or
pointer-address difference must not become saved state. Performance acceptance must include peak memory and
main-thread step latency, not only a reduction in worker-thread processor time.

## Open questions

- How much processor time does index maintenance add in a workload where `convoi_t::unreserve_route()` is
  never called? The comparison above includes maintenance in both paths and does not isolate it.
- Does any real save contain a holder whose Rc is large enough that one serial clear exceeds the network
  frame budget? No such case is known; the measured holder and latency figures are under
  [Measured performance](#measured-performance).

## Provenance

Current-code facts were checked against ex-15 @ 23271d64e plus the working-tree route-unreserve
removal on 2026-11-14. The performance figures are execution measurements on the canonical fixture;
the profiling method and hotspot baseline are in [performance](../performance.md). Functional
coverage is listed under Validation.
