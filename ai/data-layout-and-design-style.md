---
status: draft
verified: master @ fd4a025a2
---
# Data layout & design style

**Covers:** the codebase-wide design paradigm (object-oriented vs data-oriented design), where
OOP abstraction is concentrated, and the local data-layout and concurrency optimisations used on
hot paths. Container mechanics → [utilities](utilities.md); measured costs and the profiling method →
[performance](performance.md); mandatory constraints → [project-architecture](project-architecture.md).

## Verdict

Simutrans-Extended is an object-oriented C++ engine with local, opportunistic data-oriented
optimisation. There is no entity-component system, no global struct-of-arrays, and no
component/system split: simulation entities are polymorphic objects owning their own behaviour,
allocated individually and referenced by pointer. Data-oriented design appears only as hand-tuned
memory layout, pooling, flat iteration and plain-data aggregation on paths measured to be hot.

## Object-oriented mechanisms (the dominant model)

- **Deep inheritance hierarchies.** `obj_t` → `obj_no_info_t` → `baum_t` / `gebaeude_t` /
  `roadsign_t` / `wayobj_t` (obj/); `vehicle_base_t` → `vehicle_t` (vehicle/vehicle.h);
  `convoy_t` → `lazy_convoy_t` → `convoi_t` (convoy.h).
- **Multiple inheritance to attach interfaces.** e.g. `gebaeude_t : public obj_t, sync_steppable`;
  `roadsign_t : public obj_t, public sync_steppable`; `vehicle_t : public vehicle_base_t, public test_driver_t`.
- **Hot-path iteration relies on virtual dispatch.** `karte_t::sync_list_t::sync_step` walks a
  `vector_tpl<sync_steppable*>` and calls the virtual `sync_steppable::sync_step`, then switches on
  the returned `sync_result` (simworld.cc; ifc/sync_steppable.h). Convoy physics is virtual
  (`convoy_t::get_starting_force`, `get_current_friction`) and ground queries
  (`grund_t::get_weg_ribi`, `get_weg_ribi_unmasked`) are virtual. That a hot path contains virtual
  dispatch does not establish the dispatch as the cause of the cost; see the caution below.
- **Behaviour lives on the entity.** Physics, routing triggers and rendering state are methods of
  the entity (`convoy_t::calc_move`, `calc_max_speed`), not free functions over component arrays.
- **Per-entity heap allocation and identity handles.** Vehicles, convoys, pedestrians, private cars
  and map objects are `new`-allocated and referred to by raw pointer or by the `quickstone_tpl`
  tombstone handle idiom (`convoihandle_t`, `halthandle_t`, `linehandle_t`).
- **Descriptor hierarchies.** Pak objects are class hierarchies (`vehicle_desc_t`,
  `building_desc_t`) referenced by pointer, not flat records.
- **Heterogeneous pointer containers.** The tile object list stores `obj_t*`; a convoy stores
  `array_tpl<vehicle_t*> vehicle`; the world stores `vector_tpl<convoihandle_t> convoi_array`.

## Local optimisations

These are the deliberate breaks from the OOP model. They divide into genuinely data-layout
techniques and concurrency/performance techniques that are often loosely grouped under
"data-oriented". Container implementations and gotchas are in [utilities](utilities.md); only the
codebase-specific structural patterns are recorded here.

### Data-layout patterns

- **Contiguous map storage.** The world is a flat `planquadrat_t *plan` indexed by
  `i + j*grid_width` with inline checked/unchecked accessors (`karte_t::access`,
  `access_nocheck`, simworld.h); height uses a parallel flat array (`grid_hgts`).
- **`planquadrat_t` union + count.** A tile holds either one ground or an array of grounds behind a
  `union DATA { grund_t **some; grund_t *one; }` and a `uint8 ground_size`, so the single-ground
  case (the overwhelming majority) avoids an allocation and a bounds scan (simplan.h).
- **Bitmask flags and narrow fields.** `obj_t::flag_values`, `grund_t::flag_values` and
  `weg_t`/`gebaeude_t` unions/bitfields pack state into bytes; `uint8` counts (`vehicle_count`,
  `ground_size`, `size`/`count` in minivec_tpl).
- **Packed coordinates and structs.** `koord3d` is `GCC_PACKED` (dataobj/koord3d.h); `minivec_tpl`,
  `way_constraints_t` and `objlist_t` are packed ([utilities](utilities.md)).
- **`objlist_t` cache packing.** One tagged-pointer word holds either a single inlined object or an
  array whose element 0 is a capacity header, so the common one-object tile avoids an allocation
  (dataobj/objlist.h). Read safety is provided by a seqlock (see concurrency, below). The list is
  still an array-of-pointers to a heterogeneous `obj_t` hierarchy, i.e. packed AoS, not SoA.
- **Global flat way registry.** `weg_t::get_alle_wege()` returns a static
  `vector_tpl<weg_t*> alle_wege` (boden/wege/weg.cc) covering every way in the world. It enables
  index-range partitioning for parallel work — `convoi_t::unreserve_route_range` is handed a
  `route_range_specification` computed by slicing this vector across worker threads
  (simworld.cc, simconvoi.cc) — and linear sweeps elsewhere.
- **Pooled A\* nodes.** `route_t` keeps a `thread_local ANode *_nodes[MAX_NODES_ARRAY]` pool and a
  flat `ANode**` with `MAX_STEP`/`max_used_steps`, and uses a `binary_heap_tpl<ANode*>` open list
  (dataobj/route.h, route.cc). This is a data-oriented structure inside an otherwise OOP subsystem.
- **Chunked pool with active mask (dead).** `tpl/freelist_iter_tpl.h` sketches a chunked memory
  pool with a `std::bitset` active-object mask for contiguous iteration, and a matching
  `freelist_tpl`; both are unused and the iter variant does not compile
  ([utilities](utilities.md), [known-bugs](known-bugs.md)). It is the intended shape of a
  pool-based replacement for pointer-list stepping.
- **Sparse connectivity storage.** `sparse_tpl<T>` (Compressed Row Storage) backs per-city
  passenger-destination maps (simcity.h); the path explorer works on a connectivity representation
  rather than per-object state ([utilities](utilities.md), [routing-and-scheduling](routing-and-scheduling.md)).
- **Value summaries.** `vehicle_summary_t`, `weight_summary_t`, `freight_summary_t`,
  `nearby_halt_t` and `path_explorer_t`'s `path_element_t` are plain data computed from the entity
  graph and passed/aggregated by value (convoy.h, path_explorer.h).
- **Invalidate-by-flag caches.** `lazy_convoy_t` keeps computed summaries plus an `is_valid`
  bitmask of `convoy_detail_e` flags, recomputing only invalidated entries (convoy.h).
- **Copy-on-write way lists.** `weg_t` shares stored destination lists between slots behind a
  `shared` flag and a `uint8 link_mode:3` bitfield, with copy-on-write on first write, deduplicating
  memory across the many private-car route maps (boden/wege/weg.h).
- **Record arrays.** `car_ownership` is a static `vector_tpl<car_ownership_record_t>`, a plain
  record array rather than a set of objects (simworld.h).

### Concurrency and performance optimisations (not data-layout)

Real optimisations, but not data-oriented design; listed so the two are not conflated.

- **Seqlock-protected object lists.** `objlist_t` bumps a `mutation_version` before and after each
  structural write; lockless readers retry while it is odd or changed (dataobj/objlist.h) —
  lock-freedom for concurrent tile readers against the single main-thread writer.
- **Atomic flags and race-avoiding byte separation.** `obj_t::flags` is a `std::atomic<uint8>` and
  `owner_n` was split into its own byte because the main thread writes flags while workers read the
  owner (obj/simobj.h).
- **Thread-local scratch and staging.** `lazy_convoy_t` recomputes summaries into thread-local
  temporaries while a worker runs, and threaded simulation appends to per-thread staging vectors
  (`private_cars_added_threaded`, `pedestrians_added_threaded`, `transferring_cargoes`,
  `start_halts`, `destination_list`, simworld.cc) merged in a deterministic sorted order.
- **Determinism measures.** Fixed-point `float32e8_t`, integer-only sync math and platform-stable
  RNG are performance/determinism techniques documented in [utilities](utilities.md) and
  [performance](performance.md), not data-oriented design.

## OOP mechanisms on hot paths, and DOD directions

The measured magnitudes are in the [performance](performance.md) hotspot inventory. A hot path that
contains an OOP mechanism does not by itself show that the mechanism causes the cost: the dominant
cost may instead be cache misses, object-list traversal or call frequency, and real attribution
must come from a profiling trace. This section therefore records the mechanism *present* and a
feasible data-oriented direction, not a measured attribution. Directions are [UNVERIFIED]
engineering inference until measured. Any change here touches `step()`/`sync_step()` state: read
[sync-and-determinism](sync-and-determinism.md) first, plus [threading](threading.md) for the
threaded paths and [savegame-versioning](savegame-versioning.md) for persisted fields.

| Path (anchor) | OOP mechanism present | Feasible DOD direction | Risk |
|---|---|---|---|
| `karte_t::sync_list_t::sync_step` (simworld.cc) | one `vector_tpl<sync_steppable*>` walk calling a virtual per scattered heap object, then a `sync_result` switch | split the dominant homogeneous types (`private_car_t`, `pedestrian_t`, convoys) out of the generic list; iterate them from a contiguous pool with an active mask (the `freelist_iter_tpl` shape) | sync-sensitive; the self-time is itself an open question in [performance](performance.md) — confirm with a stack-resolved trace before acting |
| `grund_t::get_weg_nr` / `get_weg` → `obj_bei` → `objlist_t::bei` (grund.h, objlist.h) | per lookup: flags test, tagged-pointer decode, **seqlock** (two atomic loads + fences), array load, `static_cast` | cache the decoded way pointers / waytypes directly in `grund_t`, beside the existing `has_way1`/`has_way2` flags; this potentially removes a seqlock-protected lookup and an indirection, though which component dominates the measured cost is not established | threaded readers vs builder/loading mutation; the seqlock exists for concurrent readers — verify safety before removing |
| `convoi_t::sync_step` / `calc_move`, `vehicle_base_t::do_drive` (simconvoi.cc, convoy.h) | pointer-chase over individually allocated `vehicle_t` objects, each with a vtable and its own `float32e8_t` physics state | large architectural experiment rather than a local optimisation: keep a convoy-local struct-of-arrays of physics scalars (speed, remaining distance, position, length, weight) and batch the per-vehicle drive loop; leave `vehicle_t` as the identity/render/persistence object | strongly sync- and physics-determinism-sensitive and save-order-sensitive; a subsystem redesign, not a local change |
| `private_car_t::sync_step`, `pedestrian_t::sync_step` (vehicle/simroadtraffic.cc, pedestrian.cc) | same per-object pointer chase; already threaded and batched | contiguous pool + active-mask iteration | sync-sensitive, threading |
| `karte_t::lookup` / `planquadrat_t::get_boden_in_hoehe` (simworld.h, simplan.h) | `grund_t*` walk on multi-ground tiles | single-ground union fast path already present; a contiguous per-column height array is the further step | low |

Not a target: `route_t` A\* is already the data-oriented part of routing (node pool + binary heap).

## Open questions

- Is `karte_t::sync_list_t::sync_step`'s self-time real, or inlining attribution? See the matching
  open question in [performance](performance.md); reproduce before changing the list.
- Can way pointers be cached in `grund_t` given the `objlist_t` seqlock is there for concurrent
  readers? Is a way's identity/waytype truly immutable after placement (bridges, signals,
  wayobjects, crossing rebuilds)?
- Is reviving the `freelist_iter_tpl` pool iteration worth it, or is partitioning the sync lists by
  type sufficient?
- Which other hot paths have measurable virtual-dispatch cost rather than cache/memory cost? Needs
  a stack-resolved trace.

## Provenance

All structural claims and symbol anchors are `[CODE master @ fd4a025a2]`; the same structures are
present on `mapgen-perf-fixes`. Measured costs are referenced from [performance](performance.md),
not copied. The DOD directions are [UNVERIFIED] inference and require measurement before adoption.
