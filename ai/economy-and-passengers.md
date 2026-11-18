---
status: draft
verified: cost-of-capital @ d80af444a (players and finance); master @ 206db8379 (city growth); ex-15 @ 422122533 (private cars)
---
# Economy, finance & passengers

**Covers:** simcity.*, simware.*, player/, bauer/hausbauer.* (shared with [objects](objects.md)), gui city/money frames (→ [gui](gui.md)). NOT covered here: factories/industries, factory & goods descriptors, industry density, chain building, contracts, closure/upgrade, industry generation → [industry](industry.md).

## Initial facts

- `player/simplay.h` is very widely included — the central finance/player header [CODE].
- `simcity.h` is widely included [CODE].
- `player/simplay.cc` receives periodic ex-15 attention [CODE].
- The root contains local-only diagsession artefacts named "…passenger-gen-efficiency-changes-phase-1/2…" — passenger generation received performance work in the past; an indication, not a verified fact [local-only].

## Money and currency

- The currency unit is the **Simucredit** (plural **Simucredits**), abbreviated **Cr**. The
  **Simucent** (plural **Simucents**) is its subunit: 1 Simucredit = 100 Simucents. This is the
  traditional meaning of "cent" as one hundredth of the currency unit.
  [RECOLLECTION:2026-11-17 user statement]
- Engine money amounts are stored as signed integers in **Simucents** (1/100 Cr). The account
  balance, finance history and starting money are `sint64`; some per-item costs are `sint32`,
  for example `vehicle_t::get_running_cost`. [CODE ex-15 @ dafe7031e:
  `player/finance.h` `finance_t::account_balance` and `convert_money`;
  `dataobj/settings.h` `settings_t::starting_money`; `vehicle/vehicle.h`
  `vehicle_t::get_running_cost`]
- Display conversion: `convert_money(value)` returns `(value + 50) / 100`, which rounds a stored
  Simucent amount to whole Simucredits. GUI code that needs decimals divides by 100.0.
  `money_to_string` formats the resulting double and appends `$`. The `$` is the game's money
  symbol for the Simucredit, not a US dollar. [CODE ex-15 @ dafe7031e:
  `player/finance.h` `convert_money`; `utils/simstring.cc` `money_to_string`;
  `utils/cbuffer_t.cc` `append_money`]
- Code naming: `simuconf.tab` calls the stored unit "Credit cents (1/100 Cr)", which is correct;
  the stored unit is the Simucent. `vehicle.h` calls the stored running cost "Simucents/km",
  which is also correct for a stored amount. GUI code comments that say "convert to simucents"
  before dividing by 100.0 are describing the stored unit; the division converts it to displayed
  Simucredits. [CODE ex-15 @ dafe7031e: `simutrans/config/simuconf.tab` "starting_money";
  `vehicle/vehicle.h`; `gui/goods_stats_t.cc`]
- `simuconf.tab` money settings (`starting_money`, construction and purchase costs,
  `minimum_loan_limit` and similar) are in **Simucents**, not Simucredits. A value of
  30,000,000 is 300,000 Simucredits. [CODE ex-15 @ dafe7031e: `dataobj/settings.cc`
  `settings_t::get_starting_money` and the "money (in 1/100 Cr)" comment]
- Pakset sterling conversions are quoted per **Simucredit**, not per Simucent. In the PakBritain
  New pakset in 1900, one Simucredit is about £0.005208 at the revenue conversion and about
  £0.396 at the capital conversion; one Simucent is therefore about £0.00005208 or £0.003963
  respectively. A `.dat` or `simuconf` amount in Simucents must be divided by 100 to obtain
  Simucredits before applying a per-Simucredit rate. The revenue and capital conversions also
  differ from each other by a factor of about 76 and must not be mixed. Both the
  Simucent/Simucredit factor of 100 and the revenue/capital factor are known sources of cost
  error in pakset work. Some pakset notes label the per-Simucredit sterling figure "1 SimuCent";
  strictly that figure is per Simucredit and one Simucent is 1/100 of it.
  [RECOLLECTION:2026-11-17 user statement; pakset pricing notes]

## Planned sections

- Cities & growth (simcity; city buildings; passenger demand origins).
- Passenger generation: Extended-specific mechanics & the efficiency work above [PRIOR → verify; ask the user].
- Goods shipments (`simware_t`) in transit; the goods *descriptor* and category system live with the industry domain → [industry](industry.md).
- Fares: how they are set and calibrated [TODO → verify].
- Load/save coupling → [savegame-versioning](savegame-versioning.md).

## Verified facts (city growth config)

- City road/house placement is driven by pattern rules in the pakset's `config/cityrules.tab`
  (`stadt_t::cityrules_init`; a `<user_dir>/cityrules.tab` overrides the pakset copy). Each rule's
  evaluation weight gates it at `simrand(8 + weight) == 0`: negative = more likely; `-8` → `rd=0`
  → guaranteed AND consumes no RNG draw (`simrand` early-returns when `max<=1`, utils/simrandom.cc).
  [CODE master @ 206db8379]
- Property names: the weight key is `.distribution_weight`; the legacy `house_N.chance`/
  `road_N.chance` spelling is also read, `.distribution_weight` overriding `.chance` where
  present (Extended paksets write `.chance`). Weights stay network-synced via the
  unchanged `rule_t::rdwr`/`stadt_t::cityrules_rdwr` path.
  [CODE master @ 206db8379; EXECUTION-VERIFIED:2026-09-18]

## Private cars

- Car ownership is per passenger wealth class: `karte_t::get_private_car_ownership(monthyear, g_class)` interpolates that class's series from `config/privatecar.tab` (`car_ownership[class]`), falling back to the single untagged `car_ownership` series; with no data, the highest class of a multi-class pakset has 100% ownership and the others use `default_car_ownership_percent`. [CODE ex-15 @ 422122533]
- `karte_t::generate_passengers_or_mail` draws one car type per packet that has access to a private car, before the mode choice, from the era-filtered, distribution-weighted pool for the packet's class (`private_car_t::pick_citycar_desc(g_class)`). A class whose pool is empty makes no private car journeys, whatever its ownership percentage. [CODE ex-15 @ 422122533 plus working-tree private-car class changes]
- The pools are rebuilt monthly by `private_car_t::build_timeline_list` from the city-car descriptors the timeline makes available. Each descriptor's available classes come from the `class` key of its `.dat` file (comma-separated integer class indices, 0 lowest); a descriptor without the key is available to every class. [CODE ex-15 @ 422122533 plus working-tree private-car class changes]
- Private car journey times are per class: a journey whose destination is the origin city uses the generic road time per tile at that class's average speed, and a routed journey to another city or to an industry or attraction uses the time recorded for each class along the actual road route. The model and its storage: [comfort-routing-integration](path-explorer-optimisation/comfort-routing-integration.md).

## Players and finance

Each company is a `player_t` (`player/simplay.h`) that owns a `finance_t`
(`player/finance.h`). Player 0 is the human company and player 1 is the public service player
(`player_t::is_public_service`), which is exempt from borrowing limits, insolvency and tax.
[CODE cost-of-capital @ d80af444a]

Money is held in Simucents, one hundredth of a Simucredit; `convert_money` divides by 100 for
display. All money paths work in Simucents. [CODE]

`finance_t` holds:

- `account_balance`, the cash position, which may be negative;
- the history arrays `com_month[26][ATC_MAX]`, `com_year[26][ATC_MAX]` for company-wide
  entries and `veh_month[TT_MAX][26][ATV_MAX]`, `veh_year[TT_MAX][26][ATV_MAX]` for entries
  distinguished by transport type (`finance_t::transport_type`, not `waytype_t`);
- `maintenance[TT_MAX]`, the recurring infrastructure cost, rebuilt from the map objects on
  load rather than saved ([way-maintenance-accounting](way-maintenance-accounting.md));
- the Asset Ledger, Tax Written-Down Pool and Term Instruments described in
  [cost-of-capital](cost-of-capital.md).

[CODE]

Booking methods are inline in `finance.h` (`book_revenue`, `book_running_costs`,
`book_new_vehicle`, `book_construction_costs`, `book_maintenance`, `book_way_renewal`,
`book_interest`, `book_tax`, ...). Each adjusts the current month and current year entry and,
for a cash amount, `account_balance`. `player_t` wraps them for the paths that also need a
Money Message ([cost-of-capital](cost-of-capital.md)) or an asset update. [CODE]

`finance_t::new_month` performs, in order: `calc_finance_history` (derives the aggregate
entries: revenue, expenditure, operating profit, profit, margin, net wealth), the monthly
history roll, the yearly roll in January, the Term Instrument interest and principal, the
Overdraft interest, corporation tax, infrastructure maintenance, asset depreciation and the tax
allowance. `player_t::new_month` then recomputes the vehicle value with `player_t::calc_assets`,
recalculates the Credit Limits and the Credit Rating, and raises the insolvency warnings.
[CODE]

Interest, tax and maintenance are booked as negative amounts, so an expense reduces
`account_balance` and the profit entries directly. [CODE]

**Credit Limits.** `finance_t::calc_credit_limits` sets a negative hard limit as the worse
(more negative) of `credit_limit_by_profits` and `credit_limit_by_assets`, and a soft limit at
half of it. The profit-based limit divides the last twelve months' `ATV_OPERATING_PROFIT` by
the overdraft rate; the asset-based limit is the negative of `ATV_NON_FINANCIAL_ASSETS`.
`finance_t::can_afford` compares the post-purchase balance with the soft limit and returns true
in freeplay or when `allow_purchases_when_insolvent` is set. The finance window shows the soft
limit as the Overdraft Limit and the hard limit as the Overdraft Solvency Limit. [CODE]

**Insolvency.** `player_t::check_solvency` returns `solvent` when the game is in freeplay, when
insolvency is disabled, or for the public service player; otherwise it counts the consecutive
months in which `ATC_CASH` was below `ATC_HARD_CREDIT_LIMIT`
(`finance_t::get_number_of_months_insolvent`). Up to twelve months gives `in_administration`
and more gives `in_liquidation`. `player_t::begin_liquidation` locks the player, sends its
convoys to depots and grants every other player access to its ways;
`player_t::complete_liquidation` removes the convoys, stops, signals, way objects and buildings,
takes the ways out of private ownership or downgrades them, and marks the player inactive.
`player_t::new_month` completes the liquidation once the company has been insolvent for 24
months. [CODE]

**Takeover.** `player_t::can_take_over` requires `available_for_takeover` (voluntary, or not
solvent) and affordability of `calc_takeover_cost`. The cost refunds the target's starting
capital, subtracts its cash balance, subtracts its Secured Debt and adds its vehicle value.
`player_t::take_over` books the price, transfers vehicles, ways, stops and lines, transfers the
Term Instruments, and recalculates the acquirer's assets, Credit Limits, Credit Rating and
history. [CODE]

**Inflation and scale.** `karte_t::get_inflation_adjusted_price` converts a 1900-priced figure
through the series in `config/prices.tab` for the current timeline month (identity when the
timeline is off). Starting money is converted this way on construction of `finance_t`.
`karte_t::calc_adjusted_monthly_figure` converts a per-month figure through `bits_per_month`,
for costs that accrue per tick rather than once per month
([scale-factoring](scale-factoring.md)). [CODE]

## Performance hotspots

Measured on the gargantuan fixture (method and full inventory: [performance](performance.md))
[EXECUTION-VERIFIED:2026-09-10 master @ d40847e90]: passenger generation is a moderate,
monthly-cadence cost — `karte_t::generate_passengers_or_mail` 4.8% incl, `karte_t::find_destination`
3.0% incl of in-game CPU. City *growth* is not hot. City traffic (private cars) is much hotter:
[objects](objects.md).

## Open questions

- Where is passenger generation implemented (simcity? simhalt? simworld?) — verify, do not assume.
