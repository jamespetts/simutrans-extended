---
status: draft
verified: ex-15-industry-growth-test @ 7c379b4
---
# Scale factoring (months and tiles)

**Covers:** `karte_t::calc_adjusted_monthly_figure` and `karte_t::scale_for_distance_only`
(simworld.h); `ticks_per_world_month` / `ticks_per_world_month_shift` (simworld.h, simworld.cc);
the `settings_t` scale keys `meters_per_tile`, `bits_per_month`, `base_meters_per_tile`,
`base_bits_per_month` (dataobj/settings.*) and their simuconf.tab parsing order (simmain.cc).
Applies to every quantity the pakset defines per month or per tile: industry production and
consumption, passenger/mail/visitor demand, fixed maintenance, fares.

## Mechanism

Pakset quantities are authored "per month" against an assumed base scale. The engine converts
each nominal figure to the actual game scale at use time, via two independent ratios
[CODE simworld.h:1557-1580]:

- month-length ratio: `2^(ticks_per_world_month_shift - base_bits_per_month)` (left shift for a
  longer actual month, right shift for a shorter one);
- tile-distance ratio: `base_meters_per_tile / meters_per_tile`.

`adjusted = nominal × month-length ratio ÷ distance ratio`. The factor is uniform across all
per-month quantities, so *ratios between* per-month quantities (e.g. a factory's consumption vs
its supplier's output) are invariant to these settings; only absolute magnitudes change [CODE].

`ticks_per_world_month_shift` is set from `settings.get_bits_per_month()` when a world is
created or loaded [CODE simworld.cc:1491,8932]. `scale_for_distance_only` applies the distance
ratio alone (e.g. where a quantity is per tile, not per month) [CODE simworld.h:1582-1612].

## The figures are pakset/user settings, not code constants

The engine hard-codes only defaults, and every one of them is a simuconf.tab key. Parse order:
game `config/simuconf.tab` → pakset `config/simuconf.tab` → user `simuconf.tab` → addons
(later files win) [CODE simmain.cc:1163-1229].

| Key | Engine default | Meaning |
|---|---|---|
| `base_meters_per_tile` | 1000 | tile size the nominal figures were authored against |
| `base_bits_per_month` | 18 | month length the nominal figures were authored against |
| `meters_per_tile` | 1000 | actual distance scale |
| `bits_per_month` | 20 | actual month length |

[CODE dataobj/settings.cc:104-108; single-value parse sites dataobj/settings.cc:2004-2015,2532]

Authoring guidance in the simuconf comments: 1000/18 corresponds to the Simutrans-Standard base
scale; `base_meters_per_tile = 7500` makes a month equivalent to a 24-hour day for calibration
purposes [CODE simutrans/config/simuconf.tab:119-126].

Example of pakset-specific values: pak128.Britain-Ex's own config sets `meters_per_tile = 125`
and `bits_per_month = 22`, i.e. that pakset's nominal figures are converted with a ×8 distance
factor and a longer month than the engine default [CODE
simutrans/pak128.Britain-Ex/config/simuconf.tab:237,891]. These values belong to that pakset's
config and can change; only the mechanism is engine behaviour.

## Consequence for calibration

Because the factor is uniform, changing these settings rescales absolute magnitudes but cannot
fix relative imbalances within a chain (e.g. a processor too small for its consumer). Any
real-life calibration review (throughput per citizen, land use, etc.) must reference the
pakset's actual settings, and any *relative* calibration verdict is scale-setting independent.

## Open questions

- Real-time month duration at default settings: the user states 6:24 per month
  [RECOLLECTION:2026-09-23], while a source comment implies 3:12 for the same values
  [CODE simworld.h:1549-1553] and another ties month duration to `2^bits_per_month` ms
  [CODE simutrans/config/simuconf.tab:1119-1120]. These cannot all be correct; the discrepancy
  is untriaged.
- Whether any per-month datum bypasses `calc_adjusted_monthly_figure` (and is therefore
  fixed-rate regardless of these settings) was not enumerated.
