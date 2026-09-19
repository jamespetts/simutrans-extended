---
status: draft
verified: master @ f378cf551
---
# Frame pacing & network-play smoothness

**Covers:** the frame-pacing branches of `karte_t::interactive`, `karte_t::reset_timer`,
`karte_t::process_network_commands`, `karte_t::sync_step`, `karte_t::update_frame_sleep_time` and
`karte_t::step` (simworld.cc), the interrupt machinery (simintr.cc), the pacing broadcasts
(`nwc_step_t`, `nwc_check_t`) and the pacing tunables: `settings_t::frames_per_second` /
`frames_per_step` / `server_frames_ahead`, `env_t::fps` / `additional_client_frames_behind` /
`server_sync_steps_between_checks`, and their simuconf keys (`frames_per_second`,
`server_frames_per_step`, `server_frames_ahead`, `additional_client_frames_behind`,
`server_frames_between_checks`). Sync model → [network](network.md); simulation-side rules →
[sync-and-determinism](sync-and-determinism.md).

Read when investigating network-play jerkiness/smoothness, frame or step cadence, client pacing
margins/clamps, or when proposing changes to FIX_RATIO pacing or the pacing tunables.

## Problem

Single-player movement is smooth; network play is jerky in a repeating pattern — a burst of
movement of on-screen assets, then a short hang, then another burst. Experienced by graphical
clients and graphical servers alike; the normal configuration is a headless server with graphical
clients. Worse on larger games; smaller games not recently tested. Occasional distinctly deeper
hitches occur on top of the regular rhythm. [RECOLLECTION:2026-09-19]

## Mechanism

Network mode forces FIX_RATIO stepping and disables interrupts (`karte_t::reset_timer`:
`step_mode = FIX_RATIO`, `intr_disable()`). Frame length
`fix_ratio_frame_time = 1000/clamp(settings.get_frames_per_second(), min_fps, max_fps)`;
`settings_t::frames_per_second` is savegame-stored and server-authoritative — the server refreshes
it from its own `env_t::fps` (simuconf `frames_per_second`, default 25) whenever it saves
(`settings_t::rdwr`), clients adopt the stored value. Normally 40 ms.

Every executed frame advances the world by exactly one fixed quantum:
`sync_step(fix_ratio_frame_time*time_multiplier/16, true, true)` — moving-object interpolation
(the `sync` list: convoys, road traffic, pedestrians), display (`intr_refresh_display`) and events
(`eventmanager->check_events`) happen ONLY here; `time_multiplier` is held at 16 in network play
(`network_game_set_pause`; fast forward is blocked). Every `settings.get_frames_per_step()` frames
(simuconf `server_frames_per_step`, default 4 → every ~160 ms) the frame additionally runs the
full heavy `karte_t::step()` sweep. Every peer also creates a checklist every frame
(`LCHKLST(sync_steps)`).

Deterministic lockstep: every peer — headless server AND every graphical client — runs the
identical full simulation locally, including the periodic `step()`.

**Single-player contrast (NORMAL mode).** Display and world movement are driven independently at
an adaptive frame cadence by `interrupt_check()` (simintr.cc) → `sync_step(real-time-diff, true,
true)`, fired from INT_CHECK points in the interactive loop AND inside `karte_t::step()` itself
("karte_t::step" … "karte_t::step 9"), city/factory stepping and route finding — heavy work never
freezes the screen or quantises movement; `step()` runs only every ~200 ms
(`next_step_time = time + 3200/time_multiplier`). NORMAL mode additionally adapts frame time and
degrades drawing when frames get slow (`update_frame_sleep_time`: `simple_drawing` adjustment,
`increase_frame_time`, auto zoom-out). In network mode every INT_CHECK is a no-op and the
FIX_RATIO branch of `update_frame_sleep_time` only updates `simloops` — no adaptation, no
degradation.

**Server pacing** (`karte_t::interactive`, FIX_RATIO branch): fixed schedule
(`next_step_time += fix_ratio_frame_time`); after an overrun the late frames execute
back-to-back (a sprint — the `select` waits in `process_network_commands` are non-blocking when
late). Every frame is broadcast: `nwc_step_t`, or `nwc_check_t` every
`env_t::server_sync_steps_between_checks` sync steps (simuconf `server_frames_between_checks`,
default 24) or when lagging at a step boundary; a "server lagging by" warning is logged when the
timelag exceeds one step period.

**Client pacing** (`karte_t::process_network_commands`): the client targets an offset of
M = `settings.get_server_frames_ahead()` (savegame, default 4) +
`env_t::additional_client_frames_behind` (client-local simuconf, default 4) frames behind the
latest server sync step. On each NWC_STEP/NWC_CHECK:

- **"running way ahead"** (ahead of target by more than M/2 frames): `next_step_time` is hard-set
  into the future — no frame (no display, no events, no movement) runs until then: a complete
  freeze (successive packets can shorten the wait).
- **"running ahead"** (within half margin): the wait is shortened to the amount ahead; else, on
  NWC_CHECK only, gentle slowdown via `ms_difference`.
- **"running behind"** while still waiting more than ¼ frame: immediate frame ("get going") plus
  `ms_difference` catch-up; behind on NWC_CHECK: gentle catch-up.
- Per-frame correction in `interactive`: `nst_diff = clamp(ms_difference, -2*ft, 8*ft)/10` —
  frame period from ft/5 (**5× rate**) to 1.2×ft (83%); the client also accumulates its own local
  frame overruns into `ms_difference`.
- **Hard barrier**: `sync_steps_barrier` = latest received server sync step. A client at the
  barrier runs display-only frames (`sync_step(0,false,true)`): screen alive, world completely
  frozen (no interpolation, no ticker).

## Root cause of the burst–hang pattern

1. **Per-peer serial spikes.** On large games the main-thread cost of `step()` — and even of
   `sync_step` (the sync-list walk is the top measured leaf cost, →
   [performance](performance.md)) — routinely exceeds the 40 ms budget. During a spike nothing
   renders and nothing moves (interrupts disabled; display exists only inside `sync_step`).
2. **Catch-up sprints = the bursts.** After a spike the overrun is repaid: the server sprints its
   late frames back-to-back; clients accelerate to up to 5×. Each catch-up frame advances a full
   40 ms movement quantum but renders in a fraction of that time — a visible burst. Cycle ≈ step
   period + overrun → a ~2–3 Hz stutter at BB-scale load.
3. **Server-stall propagation.** During the server's `step()` no pacing packets arrive. Clients
   coast through their margin (default M=8 ≈ 320 ms), then pin at the barrier (display-only
   freeze). When packets resume, the coasted overshoot typically exceeds M/2 — the "running way
   ahead" hard-reset freeze fires while the margin rebuilds, followed by catch-up bursts. With
   the default margin this triggers after essentially every server stall longer than ~2 frame
   times, so even fast clients stutter at the server's cadence, on top of their own local
   `step()` spikes.
4. **No adaptive relief.** Network clients get none of NORMAL mode's frame-time/drawing
   degradation; a display-bound client chronically overruns the fixed budget and lives in the
   pin/catch-up cycle permanently.
5. **Amplifiers (the deeper hitches).** `new_month()`; the deferred monthly autosave inside
   `step()` (`autosave_pending` → `karte_t::save`); season/snowline tile loops; the periodic
   `path_explorer_t::refresh_all_categories` (every `reroute_check_interval_steps`); post-load
   reroute waves (→ [performance](performance.md)). Individual steps far heavier than average —
   consistent with the reported deeper hitches [RECOLLECTION:2026-09-19].
6. **Scale dependence.** When `step()` fits inside a frame slot (small games) nothing overruns
   and the pattern is invisible; the mechanism predicts this, matching the observation that
   larger games are worse.

## Design constraints (why the solution space is narrow)

- Clients must never overtake the server's current sync step: the server validates each client's
  checklist against its own history at every tool command (`nwc_tool_t` path in
  `process_network_commands` — a too-fast client's commands are skipped or the client is kicked).
  Buffering is therefore only possible in the "client behind server" direction.
- Moving-object advance is checklist-policed synced state (`convoi_t::sync_step` feeds
  `debug_sums`; `rands[]` checkpoints bracket the sync lists) — it cannot be advanced or
  predicted locally during a hang without diverging →
  [sync-and-determinism](sync-and-determinism.md).
- The single-player INT_CHECK mechanism (world advances by real-time diff during heavy work) is
  incompatible with network mode: fixed quantum per frame, RNG-checkpoint sequencing and command
  execution at frame boundaries require `sync_step`/`step` to run exactly once per frame in fixed
  sequence on every peer.
- Pacing itself (when frames execute in wall time) does not feed the simulation: frame order,
  quanta and command stamps are unchanged by wall-time spacing, so pacing-only changes should be
  sync-safe [UNVERIFIED — mechanism argument; confirm via the join-sync harness
  (`scripts/run-join-sync-test.sh` → [scripting-and-tests](scripting-and-tests.md)) when
  implementing].

Reducing the serial cost of `step()`/`sync_step()` shortens the hangs directly, but that is the
general performance programme (→ [performance](performance.md)) and is not an option of this doc.

## Options

Assessed against the root causes above; letters match the 2026-09-19 discussion with the user.

### B — larger client margin (config-only)

Raise `additional_client_frames_behind` (client simuconf) and/or `server_frames_ahead` (server
simuconf → savegame). If M exceeds the stall length in frames AND the post-stall overshoot stays
below M/2 (i.e. roughly M ≥ 2× stall frames — ~16–32 frames ≈ 0.6–1.3 s at 25 fps for 300–600 ms
stalls), clients coast smoothly through server stalls — no barrier pinning, no hard-reset freeze —
and trim back via the ≤20% NWC_CHECK slowdown, which is nearly invisible. Costs: world-view and
tool-effect latency equal to the margin (drag-build feedback appears that much later); does
nothing for client-local `step()` spikes or graphical-server hangs — if client-local spikes
dominate on BB (untested, → open questions), the benefit is small. Determinism-neutral. Trial on
BB is immediate, incremental (e.g. 8 → 16 → 24).

### C — gentler catch-up clamp (small code change)

Narrow the positive bound of `clamp(ms_difference, -2*ft, 8*ft)` in `karte_t::interactive`
(currently allows 5× rate); optionally soften the "get going" immediate-frame branch; consider
making the bound simuconf-tunable. Hangs are then repaid over ~10+ slightly-fast frames instead
of 2–3 sprint frames — removes the teleport-burst artefact; the hangs themselves remain. Under
chronic overload a slower repayment drifts the client toward the barrier (hard freeze), so C is
safest paired with B. Determinism-neutral (pacing-only, see constraints).

### D — cadence ratio retune (config/savegame; experiments only)

- Higher `frames_per_second` with proportional `frames_per_step` (finer quanta): doubles the
  per-frame serial cost (sync-list walk, display, checklist and packets per sim-second) — likely
  counterproductive on large games; only useful where per-frame cost ≪ budget.
- Higher `server_frames_per_step` alone (e.g. 8 at 25 fps): halves `step()` calls per sim-second
  (total serial step overhead roughly halves — per-call work is approximately constant
  [UNVERIFIED — sweep-dominated inference]), doubles the smooth run between hangs (7×40 ms vs
  3×40 ms) and cuts the freeze duty cycle (e.g. ~63%→52% at a 300 ms step); hang length
  unchanged. Partial improvement only.
- Companion retuning required so game-time rates stay constant: step-counted intervals —
  `reroute_check_interval_steps`, path-explorer `limit_set_t` work quanta (network-negotiated,
  symmetric across peers → [network](network.md)); behaviour testing required (per-step systems
  act at 2× game-time intervals; per-step delta doubles).
- No ratio fixes the root condition (serial step cost ≫ frame budget). Server-authoritative
  savegame value → applies to all peers at once; cheap to experiment.

### F — server even-cadence pacing (small code change)

Server resets `next_step_time = now + fix_ratio_frame_time` after an overrun instead of `+=`
(never sprint; mirrors the client's lag branch). Removes broadcast volleys and the server's own
catch-up sprints, so client pacing input becomes evenly spaced; the stall gaps remain (the server
is silent during its `step()`). Trade-off, accepted by the user [RECOLLECTION:2026-09-19]: under
sustained load, in-game time drifts permanently against wall time (deficits are never recovered),
whereas today the server sprints and keeps game time aligned with wall time. "server lagging by"
logging and the lag-triggered `nwc_check_t` condition change meaning slightly.
Determinism-neutral.

### G — adaptive drawing detail for network clients (small)

Extend the `simple_drawing`/zoom-hint degradation of `update_frame_sleep_time` (NORMAL-only
today) to network clients, measuring per-frame wall time against `fix_ratio_frame_time`. Must NOT
adapt the frame period itself (that is the synced schedule) — detail level only. Gives
display-bound clients the same relief single-player already has (plainer tiles in heavy views in
exchange for steady cadence); no effect for step()-bound peers. Whether BB clients are
display-bound is untested (→ open questions). Determinism-neutral (drawing detail is not
simulation state).

### H — sliced/amortised stepping (far future)

Slice `step()`'s subsystem sweeps into per-frame portions so no frame exceeds its budget — the
only true structural fix of the quantisation. Determinism is preserved IF the slicing is
identical on every peer (a fixed schedule), but the order of state changes relative to command
boundaries changes (a tool can land mid-sweep — consistent across peers, but semantically
different from today), and the `rands[]` checkpoint layout and checklist semantics need redesign;
simultaneous peer upgrades are already enforced by versioning. Assessed as impractical for the
foreseeable future; retained as a far-future direction [RECOLLECTION:2026-09-19].

### Considered and rejected

- Display/event-only refresh during network-mode heavy work (a network-safe INT_CHECK variant
  that redraws and processes events without advancing the world): rejected — the perceived
  problem is in-world movement, not GUI responsiveness during the hangs
  [RECOLLECTION:2026-09-19].

## Measurement plan

1. Classify episodes before tuning: temporary instrumentation (or `-debug` logging) recording
   per-frame wall time, `step()` duration, and which client pacing branch fired (barrier-pin
   display-only frames; "running way ahead" hard-reset freeze; local frame overrun with no
   packet-side cause). Server side: the existing "server lagging by" warnings give the per-episode
   overrun.
2. Server-side per-frame cost distribution on the canonical fixture: perf-suite `Capture` mode →
   [performance](performance.md).
3. Peak vs off-peak comparison on BB — separates server-propagated stall from client-local
   spikes (open question below).
4. B/C/D config trials on BB — requires the current configuration values first (open question).
5. Desync safety for the pacing code changes (C/F): join-sync harness
   (`scripts/run-join-sync-test.sh` → [scripting-and-tests](scripting-and-tests.md)) and
   accelerated soaks via `-fast-network-sync` (`fast_network_sync_factor`, simmain.cc — note it
   multiplies the server time multiplier, changing the world-time rate).

## Open questions

- BB's actual pacing configuration (server simuconf: `frames_per_second`,
  `server_frames_per_step`, `server_frames_ahead`, `server_frames_between_checks`, autosave
  setting; clients: `additional_client_frames_behind`) — unknown; obtainable with some effort,
  worth obtaining only once B/C/D trials are seriously contemplated [RECOLLECTION:2026-09-19].
- Does the jerkiness persist off-peak (few players active)? Not tested
  [RECOLLECTION:2026-09-19] — would separate server-propagated from client-local causes.
- Do the deeper hitches correlate with autosaves/month boundaries? (Compare server log timestamps
  with observed hitch times.)
- Are BB clients display-bound or step()-bound within the frame budget? (Decides whether G, and
  how much of B, pays off.)

## Provenance

Verified against master @ f378cf551 (read-only investigation 2026-09-19: simworld.cc
`interactive`/`reset_timer`/`process_network_commands`/`sync_step`/`update_frame_sleep_time`/
`step`, simintr.cc, dataobj/settings.cc, dataobj/environment.cc). The pacing branches were
additionally spot-checked as present identically on ex-15 (the `interactive` clamp/barrier/
"server lagging" branches and simintr.cc `interrupt_check`) [CODE]. Hotspot magnitudes from
[performance](performance.md). User observations and option verdicts [RECOLLECTION:2026-09-19].
FIX_RATIO lockstep pacing is inherited from the Standard Simutrans network base; Extended
additions are the per-frame checklist feeders and the path-explorer work-quanta negotiation —
coarse provenance only, per conventions.
