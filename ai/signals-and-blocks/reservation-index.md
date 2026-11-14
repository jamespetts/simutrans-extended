---
status: draft
verified: ex-15 @ ed1461fce plus working-tree call-on, reservation-index and signalling-refactor changes (2026-11-14)
---
# Per-holder reservation index

**Covers:** the replacement for the map-wide scans in `convoi_t::unreserve_route` and
`rail_vehicle_t::clear_token_reservation`; registration in `schiene_t` and `runway_t`; the retained
route-unreserve workers in `simworld.*` and their planned removal; and the memory, determinism and validation
requirements for that work, together with the approach to subsequent signalling refactoring and incidental
bug fixes. Current reservation mechanics: [block-reservations](block-reservations.md). Player-facing
signalling: [signals-and-blocks](../signals-and-blocks.md). Consist recombination:
[consist-recombination](../consist-recombination.md).

The implementation adds no saved fields and does not change savegame or network versions. The canonical
performance suite and large-save profiling have not been run, so no performance improvement is claimed;
the predicted effects remain [UNVERIFIED].

Read [sync-and-determinism](../sync-and-determinism.md),
[threading](../threading.md), [network](../network.md), [performance](../performance.md) and
[savegame-versioning](../savegame-versioning.md) before changing this area. Any change that adds saved
fields or changes savegame or network versions must be presented to the user before implementation.

## Why the route alone cannot replace the scan

The previous implementation of `convoi_t::unreserve_route()` scanned `weg_t::get_alle_wege()` and released every
matching rail-family or runway reservation. `rail_vehicle_t::clear_token_reservation()` performed another complete
scan when `needs_full_route_flush` required it. With **W** denoting the number of way objects in the world, each
operation cost O(W), divided among workers but with pointer reads into scattered way objects.

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

- admission continues to use `schiene_t::can_reserve` and the way's reservation fields;
- clearing continues to call the existing reservation-release methods;
- no signal aspect, crossing state or movement rule is read from the index;
- bucket membership and back-indices are not saved and are not included in the network checklist.

The table belongs to the world, not to a player or an individual convoy. One bucket exists for each possible
`convoihandle_t` quickstone entry if the table is preallocated, or for each allocated entry if the table uses
the high-water alternative described below. Each bucket contains only entries with that raw holder ID. No
per-player copy exists.

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
reservation entries. This fixed table is per world, **not per convoy or player**. The user judges this
fixed world-level cost acceptable [RECOLLECTION:2026-11-14]. Size-to-high-water is unnecessary unless
implementation constraints make preallocation unsuitable.

The additional back-index field may occupy existing alignment padding, but measure `sizeof(schiene_t)`
on the supported compilers rather than assuming zero object-size growth. Vector capacity can exceed the
live entry count. Release a destroyed holder's pointer-buffer allocation, keep the world table's bucket
headers, and measure whether buckets belonging to surviving holders need capacity reduction after an
unusually large full clear before adding such churn.

Membership follows the **raw holder ID**: a way with raw ID zero belongs to no bucket, while a way with a
nonzero raw ID belongs to exactly one bucket. This applies even when the quickstone handle does not currently
resolve to a `convoi_t`; loading and handle reuse must not change the existing raw-ID clearing semantics by
omitting such an entry. Invariant checks must verify these membership rules and verify that each back-index
identifies the same way in the bucket for its stored raw holder ID.

## Registration and ownership

Every reservation-holder membership change routes through the index maintenance. The ordinary
write boundary is narrow: `schiene_t::reserve`, `schiene_t::unreserve(convoihandle_t)` and
`schiene_t::unreserve(vehicle_t*)`; direct holder assignments also occur during deserialisation.

### Reserve

The early directional no-op over an existing `block` or `stale_block` reservation is preserved. On a
zero-to-nonzero raw-holder-ID transition, the way is appended and its back-index is set. Re-reservation by the same
holder does not change membership. A successful request that replaces a compatible directional holder unregisters
the old holder and registers the new holder as one ownership change. Raw IDs are compared for these
index transitions: an old nonzero raw ID whose quickstone handle does not resolve still denotes a registered
way; it is not classified as raw ID zero.

### Release

The entry is removed using the **old stored holder**, then the way's reservation is cleared through the
existing semantics. The `vehicle_t*` overload does not establish that the supplied vehicle's convoy is the holder;
using the caller's identity for registry removal would be wrong. `set_stale` and direction-only changes do
not alter membership.

Neither overload's reservation-admission or release semantics changes. In particular, the unchecked nature of
`unreserve(vehicle_t*)` is a separate behavioural question.

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
stored holder still matches, using the same checked-handle release semantics as the previous complete scan.
Iteration accounts for swap-removal mutating the bucket during the walk.

The `needs_full_route_flush` branch of `rail_vehicle_t::clear_token_reservation()` visits the same holder's
bucket but releases only entries satisfying the existing `is_stale()` test. Holder type and direction changes
that do not change holder identity do not change bucket membership.

A registry/holder mismatch is an implementation defect, not permission to skip a reservation or silently
introduce a different recovery rule. Production clearing does not periodically scan every way to conceal
index drift.

The existing per-tile `rail_vehicle_t::leave_tile` and `reserve_own_tiles` operations continue through the
same registration methods and need no route-history additions.

For shared-contact coupling, the later handoff will transfer the single holder through the same central
ownership-change operation: unregister the old holder and register the authorised remaining holder. Never place
the same way in two buckets. Physical occupancy by two convoys does not itself imply two reservation holders.

## Threading

Bucket operations and both full-clear consumers run on the main simulation thread. The existing
workers cannot concurrently swap-remove entries and update back-indices without new synchronisation.
Serial clearing is expected to reduce work enough to justify the main-thread cost [UNVERIFIED]; measure
both total processor consumption and main-thread latency.

The route-unreserve worker pool still exists but is unused by `convoi_t::unreserve_route()`. Remove it in a
separate change only after the new clearing path has been profiled and shown to be a real saving; the removal
then covers worker creation and teardown, `unreserve_route_barrier`, `unreserve_route_threaded`,
`convoi_t::unreserve_route_range`, `current_unreserver`, `current_waytype` and the range type. Do not change
`parallel_operations` or unrelated worker systems.

Do not add parallel bucket clearing without evidence that a single holder can retain enough reservations to
cause unacceptable main-thread latency. If that case is measured, design a separate protocol in which index
mutation and way-reservation mutation are not concurrent; it is not part of the current implementation.

## Global reserved-way list as an optional prototype

One global vector of every reserved way, using the same per-way back-index, would change clearing from O(W)
to O(R) and avoid per-holder buckets. It was considered as an implementation prototype, but a clear would
still examine other convoys' reservations. The implementation uses the per-holder index with O(Rc) clearing;
do not retain both the global pointer list and per-holder buckets without measured justification.

## Outstanding work

- The shared-contact coupling handoff (Phase 4) is not implemented. `schiene_t::change_reservation_holder()`
  supplies the single-holder operation it will use.
- Remove the retained route-unreserve worker pool.
- Measure the canonical large-save W/S/R/Rc distribution, bucket memory, main-thread clearing latency and
  pre-change `sizeof(schiene_t)` on each supported compiler.
- Extend validation to world teardown under every destructor order and every supported thread configuration.

## Signalling refactoring and incidental bug fixes

Use the reservation and aspect observations from the call-on test work to establish behaviour coverage before
refactoring the signalling algorithms. Prefer small, separately verifiable changes at narrow interfaces, such
as `schiene_t` reservation ownership and exactly duplicated side-effect-free predicates. Do not make failed
paths pure or reorder reservation and aspect writes without proving the resulting behaviour; existing callers
can depend on partial state changes and on writes made during the reservation walk.

The first refactor pass reused `rail_vehicle_t::try_choose_call_on()` at the two later admission sites,
consolidated the duplicated `will_choose` predicates, removed a redundant signal lookup, and added the
invalid-raw-ID load recovery described below. It did not change `schiene_t::unreserve(vehicle_t*)` release
semantics, remove the unused route-unreserve worker pool, or correct the platform-starter `block_reserver`
argument order, because the intended values for the last item are not established by the code or design
documents.

**Fix incidental bugs found in the signalling code while refactoring it.** Do not preserve a defect merely
because an existing execution trace exhibits it, and do not leave a confirmed incidental defect unfixed solely
to keep the surrounding refactor small. For each such bug:

- identify the incorrect mechanism and the intended rule before changing it; ask the user when existing code
  and design documents do not establish that rule;
- add a focused regression test that distinguishes the corrected behaviour from the defect, and review any
  pre-existing characterisation expectation that recorded the defect;
- identify the change explicitly as a bug fix rather than describing it as behaviour-preserving refactoring,
  even when it is delivered in the same work package as nearby refactoring;
- rerun the relevant signalling, save/load and network-determinism tests; and
- present any change to load/save behaviour, savegame or network version constants, or checklist/desync-relevant
  code to the user before implementation, as required by the repository rules.

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
- a full-map differential validator during development, which reported zero mismatches and was removed;
- a malformed-save load with holder ID 65535, which was discarded with a diagnostic and loaded without an
  index fatal.

Not yet covered:

- shared-contact reservation-holder transfer, which belongs to the not-yet-implemented coupling handoff;
- world teardown under every destructor order;
- every supported thread configuration;
- canonical large-save memory, `Rc` distribution and main-thread latency.

Heavy-mode hashes cover the saved reservation fields on ways, not the derived buckets. A bucket-order or
pointer-address difference must not become saved state. Performance acceptance must include peak memory and
main-thread step latency, not only a reduction in worker-thread processor time.

## Open questions

- What are the large-save S, R and Rc distributions, and the pre-change `sizeof(schiene_t)` on each supported
  compiler? The current MSVC x64 size is 216 bytes; a pre-change measurement is missing.
- Does any measured workload need bucket-capacity reduction for a surviving holder after a full clear?
- Does main-thread bucket clearing meet the network frame-latency requirement on the most
  reservation-heavy real save, without a separate parallel protocol?

Do not resolve these questions by assumption; measure them and report the results.

## Provenance

Current-code facts and the representation were checked against ex-15 @ ed1461fce plus the
working-tree call-on, reservation-index and signalling-refactor changes on 2026-11-14. The design and its
projected performance effects were engineering proposals recorded at the user's request. The implementation
has been functionally tested as listed above, but the canonical performance suite and large-save profiling
have not been run, so no performance improvement is claimed. The measured hotspot baseline and profiling
method are in [performance](../performance.md).
