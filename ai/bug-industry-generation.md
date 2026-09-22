---
status: draft
verified: ex-15-new-industry-generation @ 7f6176960 (rework) / ex-15 @ d1c49a519 (base, where noted)
---
# Industry generation defects (consumption-centric rework)

OPEN defects in the consumption-centric industry-generation rework. Registry rules, priority scale and the hard "delete on fix" rule → [known-bugs](known-bugs.md), which keys this doc. Mechanics of the two designs → [industry-generation-rework](industry-generation-rework.md); subsystem architecture → [industry](industry.md).

Unlike the forum-derived rows in [known-bugs](known-bugs.md), every entry here is a code-inspection finding and is tagged accordingly. Provenance discipline:

- `[CODE]` marks the *existence* of the defect — the missing guard, the arithmetic, the control flow — read on the branch named in the frontmatter.
- `[UNVERIFIED]` marks reachability or frequency in normal play. None of these was reproduced by execution; treat trigger likelihood as reasoned, not observed.
- Entries say which branch they affect. "Rework only" means the base branch does not have the defect.

## P1 — high

### Non-terminating infill loops inside the synced step (rework only)

- Three loops share the shape `while (fails < 3) { if (!increase_industry_density(...)) fails++; }` with no reset on success: the `FILL_MISSING_ONLY` sweep in `karte_t::new_month`, the same sweep in `stadt_t::check_bau_factory`, and the `FILL_MISSING_ONLY`/`FILL_UNDERSUPPLIED` sweeps in `karte_t::init` [CODE].
- The counter increments only on failure, so the loop terminates only after three *total* failures. If the mode keeps succeeding it never exits. The variable names (`fill_missing_fails`, `consecutive_*`) show consecutive-failure semantics were intended; the missing `else` reset is the defect [CODE].
- These sweeps pass `do_not_add_beyond_target_density = false`, so no density cap bounds them, and each iteration performs a whole-world scan plus factory construction [CODE].
- Exposure: the `new_month` and `check_bau_factory` instances run inside the synced step (`karte_t::new_month` is bracketed by checkpoint rands[9]; city stepping by rands[16] — map in [sync-and-determinism](sync-and-determinism.md)), so a hang or a very long month tick is a live-game freeze and a frame-pacing event → [frame-pacing-smoothness](frame-pacing-smoothness.md). The `karte_t::init` instance hangs world generation instead [CODE].
- Whether indefinite success actually occurs is [UNVERIFIED].

### Unguarded divisions in the apportionment helpers (rework only)

- `factory_builder_t::adjust_input_consumption` (both overloads) divides by `competing_supplier_prod` and by `output_prod`; `adjust_output_production` divides by `alt_supplier_prod`; `increase_industry_density` divides by `used_output + consumption_level`. None is guarded [CODE].
- The reachable case for `used_output + consumption_level == 0`: `adjust_input_consumption` returns 0 for a non-consumer-only factory that has no linked consumers (its `largest_adjusted` stays 0), so `consumption_level` is 0; if the supplier also has no other consumers, `used_output` is 0 → 0/0 [CODE]. A manufacturer with no consumers is a normal transient state. Frequency [UNVERIFIED].
- `competing_supplier_prod` / `alt_supplier_prod` are sums over a consumer's supplier list for the good, which normally includes the factory being examined, so they are zero only if link lists are asymmetric or every contributor's `base_production * factor` is 0 [CODE]. Reachability [UNVERIFIED]; note that stale/asymmetric link lists are a reported failure family in [known-bugs](known-bugs.md).
- Related, same loops: `competing_supplier->get_desc()->get_product(output_type)->get_factor()` and `competing_consumer->get_desc()->get_supplier(input_type)->get_consumption()` are dereferenced without a null check; an asymmetric link list makes either return null [CODE]. Reachability [UNVERIFIED].

### Unguarded divisions in `karte_t::recalc_idp` (rework only)

- `karte_t::recalc_idp` divides by `total_cons`, by `old_density`, and twice by `finance_history_month[0][WORLD_CITIZENS]`. None is guarded [CODE].
- Load order is correct: world finance history and fab_list are both read before the recalculation call sites in `karte_t::load`, so the population divisor is populated [CODE].
- `total_cons == 0` is made *more* likely by the rework's own change of `get_global_production` to sum `adjust_output_production`: that returns 0 for a factory with no suppliers, so a save whose chains are broken yields zero adjusted production for every final good, the `global_good_prod > 0` filter skips every good, and `total_cons` stays 0 [CODE]. Frequency [UNVERIFIED].
- `old_density == 0` requires a save with no non-power factories [CODE].
- Consequence is a crash during load, on every peer identically (so not a desync) [CODE].

### Unbounded mutual recursion with no cycle guard (rework only)

- `get_global_consumption` recurses into itself per factory output and into `get_global_production`, which calls `adjust_output_production`, which calls `adjust_input_consumption`, which recurses per consumer. There is no memoisation, no visited set and no depth cap [CODE].
- `adjust_input_consumption` recurses over *runtime* supplier/consumer links; `get_global_consumption` recurses over *descriptor-level* output types. If a pakset permits a goods cycle, or link lists form one, this is infinite recursion [CODE]. Nothing in the descriptor layer validates acyclicity [UNVERIFIED — the loader was not exhaustively checked].
- Even acyclic, cost is combinatorial in chain depth × world size, and diamond-shaped chains recompute shared subproblems once per path. Call sites include the unconditional opening scan of `increase_industry_density` (per factory per supplier slot), the per-oversupplied-good global queries, `get_global_oversupply` inside the placement loops, and `recalc_idp` at load [CODE].
- No measurement exists. Do not assume a magnitude either way; profile per [performance](performance.md) before ranking the cost separately from the crash risk.

### Save-load density basis is wrong for the rework's own version series (rework only)

- `karte_t::load` branches `if (extended_version >= 15) { read proportion; } else if (extended_version < 15) { read proportion; recalc_idp(); } else { …legacy compressed-form conversion…; recalc_idp(); }`. The first two conditions are exhaustive, so the third — which holds the older Extended series' `idp & 0x8000 ? idp & 0x7FFF : idp * 150` conversion, applied on the base branch — is unreachable and that conversion is never performed [CODE]. This alters the behaviour of an existing serialisation entry → [savegame-versioning](savegame-versioning.md) rule 3.
- `EX_SAVE_MINOR` is not bumped, so a save written by the *base* branch and one written by the rework are indistinguishable: both carry the same extended version and revision. Base-branch saves therefore take the `>= 15` path and get **no** recalculation, while their `actual_industry_density` *is* recomputed consumer-only by `karte_t::recalc_actual_density`. The result is a consumer-only actual density against an all-industry target, driving sustained maximum-rate infill from `karte_t::new_month` and `stadt_t::check_bau_factory` [CODE].
- Coverage is therefore inverted: 14.x-series saves are converted, the saves that most need conversion are not. Distinguishing them requires a revision bump and a gate of the form "extended version ≥ 15 and revision ≥ N", per [savegame-versioning](savegame-versioning.md) rule 1 — and the forum record for this feature required exactly that [FORUM:https://forum.simutrans.com/index.php/topic,23478.0.html].
- Change-restricted: AGENTS.md rule 5 requires presenting any load/save change to the user before proceeding.

## P2 — medium

### Unsigned wrap in the `recalc_idp` target-density compensation (rework only)

- `karte_t::recalc_idp` computes `sint32 difference = target_density - consumer_density;` then `(old_density - difference) * target_density / old_density` with `old_density` a `uint32`. When `difference > old_density` the subtraction wraps to near `UINT32_MAX`, producing a gigantic target density and hence a gigantic `industry_density_proportion` → permanent maximum-rate industry growth every month [CODE]. `difference` can exceed `old_density` because `target_density` is scaled by average overproduction, which is unbounded when `total_cons` is small [CODE]. Frequency [UNVERIFIED].

### Consumer-less manufacturers are invisible to the infill (rework only)

- `factory_builder_t::adjust_input_consumption(fab, consumption)` returns 0 for a non-consumer-only factory with no linked consumers, because `largest_adjusted` is never raised above 0 [CODE].
- Consequences: in `increase_industry_density`, `consumption_level` is 0, so `available_for_consumption < consumption_level` is false and a *partially* supplied stranded manufacturer is never listed as undersupplied — only the zero-supplier case is caught separately [CODE]. In `build_chain_link`, demand computes to 0 for a freshly built manufacturer that has no consumers yet, so it attracts no suppliers; the rework's cross-connect step after building a consumer appears to compensate for this [CODE; the compensating intent is UNVERIFIED].
- Gameplay impact: the infill does not repair exactly the stranded-manufacturer condition the rework exists to eliminate, except where the manufacturer has no suppliers at all.

### 32-bit overflow in the oversupplied-goods weight (rework only)

- The weight computed as `global_production * total_consumer_weight / max(global_consumption, 1)` multiplies a `sint32` by an `int` without widening, unlike the deliberate `sint64` widenings elsewhere in the same functions. Both factors grow with map size and with the number of matching factory types [CODE]. Overflow on very large maps [UNVERIFIED].

### `find_valid_factory_pos` early return leaves rotation indeterminate (rework only)

- `factory_builder_t::find_valid_factory_pos` returns without writing its out-parameters when the type wants a city site and `welt->get_cities()` is empty. Callers declare `koord3d pos, pos2;` and `int rotation, rotation2;` beforehand: `koord3d` default-constructs to zero, but `int` does not, so the subsequent `build_link(..., rotation, ...)` reads an indeterminate value used to index building layouts [CODE].
- The base branch is unaffected: it declared `pos` *after* an in-loop `continue` on the same empty-cities condition, so the value was always initialised [CODE base ex-15]. Regression introduced by the refactor.
- Trigger requires an empty city list during generation, which is unlikely in normal play (cities exist at mapgen; `stadt_t::check_bau_factory` implies a city) [UNVERIFIED]. A secondary consequence if it does trigger: placement silently falls back to map coordinate (0,0,0).

### Density cap fails open when actual exceeds target (both branches)

- In `increase_industry_density`, the `do_not_add_beyond_target_density` check compares `100U / weight` against `get_target_industry_density() - get_actual_industry_density()`, an unsigned subtraction. When actual exceeds target it wraps, the comparison fails, and industry is added beyond target [CODE base ex-15; same expression on the rework].
- Pre-existing, but the rework relies on this cap more heavily and its save-load defect above can make actual exceed target routinely.

## P3 — low

### Persisted `actual_industry_density` is never honoured (rework only)

- `karte_t::load` reads `actual_industry_density` and then calls `karte_t::recalc_actual_density()` unconditionally, and a second time under an `extended_version < 15` condition that can never add anything because the first call already ran [CODE]. The method zeroes and recomputes, so it is idempotent — the defect is that the persisted datum is written but discarded for every save, including the rework's own, and that the duplicate call is dead [CODE].

### `adjust_input_consumption` overloads have opposite semantics to the header (rework only)

- `factory_builder_t::adjust_input_consumption(fab, consumption)` returns consumption scaled by the fraction of output actually used. `adjust_input_consumption(fab, good)` returns `output_prod - output_cons`, i.e. the amount **not** used, while its declaration in bauer/fabrikbauer.h documents "the amount of the production of the good that is actually used" [CODE].
- The call site assigning it to `production_left` in `build_chain_link` matches the code, so the header is wrong, not the caller. Two same-named overloads returning opposite quantities is a live maintenance hazard [CODE].

## P4 — very low / backlog

### Dead code and diagnostic noise (rework only)

- `factory_builder_t::is_final_good` is declared and defined with no callers on either branch [CODE].
- `karte_t::init` declares `consecutive_producer_failures` and never uses it [CODE].
- Superseded code is left commented out rather than deleted: the replaced per-consumer consumption loop in `build_chain_link`, the nominal-production lines in `get_global_production`, four `increase_actual_industry_density`/`decrease_actual_industry_density` sites in simfab.cc, several `DBG_MESSAGE` calls, and the old mapgen label in gui/welt.cc [CODE].
- Per-load `DBG_MESSAGE` noise in `karte_t::load` and `recalc_idp`/`recalc_actual_density`, including one "FORCE RECALCED" message that reports an unconditional action as though conditional [CODE].
- Only en.tab and fr.tab carry the renamed mapgen label key; every other language silently falls back to English until retranslated → [translations](translations.md) [CODE].

## Open questions

- Is a goods cycle reachable in any shipping pakset? If yes, the recursion entry escalates: it becomes a deterministic stack overflow rather than a data-dependent one.
- Should the recursion defects be fixed by memoisation, by a visited set, or by validating acyclicity at descriptor load? The three have different costs and different guarantees.
- Are the tolerance bands (`* 8` vs `* 9`) intended to become parameters? They are the extension point for ports/import-export → [industry-generation-rework](industry-generation-rework.md).
- Do any of these reproduce on the base branch through a different path? Only the density-cap entry is currently recorded as affecting both.
