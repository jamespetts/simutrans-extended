---
status: draft
verified: ex-15 @ d6bf8f3e7
---
# Visual feedback for GUI work

**Covers:** the UI-automation hooks in simmain.cc (compiled only under
`SIMUTRANS_UI_AUTOMATION`), the screenshot mechanism (display/simgraph16.cc, pathes.h),
and the build gates in Simutrans-Extended.vcxproj, Makefile, CMakeLists.txt +
cmake/SimutransCompileOptions.cmake.

Read when an agent or developer needs rendered-pixel feedback on a dialog, or wants to iterate
GUI design autonomously: design → build → screenshot → examine → refine → screenshot → present.
For text-only geometry verification (no pixels) see [gui-layout-dump](layout-dump.md).

## Why a compiled hook

Squirrel scripting is non-working in Extended (→ [scripting-and-tests](../scripting-and-tests.md)),
so scenarios cannot drive the GUI. The automation surface is therefore two small hooks in the
simmain.cc main loop, compiled ONLY when `SIMUTRANS_UI_AUTOMATION` is defined, so shipped and
CI binaries never carry it. The feature is for UI iteration only; do not grow it into
gameplay/test automation.

## Build gates

- MSVC: the `Profile|x64` configuration of Simutrans-Extended.vcxproj defines
  `SIMUTRANS_UI_AUTOMATION` (deliberately only that configuration; Debug/Release unaffected).
- GNU make: `make UI_AUTOMATION=1` adds `-DSIMUTRANS_UI_AUTOMATION` (Makefile, next to the
  PROFILE gate).
- CMake: `-DSIMUTRANS_UI_AUTOMATION=ON` (option in cmake/SimutransCompileOptions.cmake;
  `add_definitions` in CMakeLists.txt).

## Environment contract

All variables are read in the simmain.cc main loop (`while (!env_t::quit_simutrans)`):

- `SIMUTRANS_UI_SHOT_TOOL=<id>` — before the loop, opens the dialogue tool with
  `tool_t::dialog_tool[id]->init()` on the public player. Guarded: only ids below the
  dialog-tool count whose `get_id()` carries the `DIALOG_TOOL` mask are accepted; anything
  else is a no-op. Tool ids: simmenu.h (`DIALOG_PRICES` = 130). Dialogue tools only, so
  opening the window never alters game state.
- `SIMUTRANS_UI_SHOT` (any value) — once `welt->interactive()` returns in the main loop:
  dismiss transient `news_img` boxes left on top by loading, force a redraw
  (`view->display(true)`, `intr_refresh_display(true)`), call `display_snapshot()` on the
  top window's rect (full screen if no window is open), then set
  `env_t::quit_simutrans = true` (clean exit; no savegame is written).
- `SIMUTRANS_UI_SHOT_CLICKS="x,y;x,y;..."` — optional. Window-relative left-click points
  injected as paired EVENT_CLICK + EVENT_RELEASE events via `queue_event`, pumped through
  `eventmanager->check_events()` synchronously in the same pass, then captured. Use it to
  switch tabs/pages; one run can carry several clicks in order (e.g. top tab then sub-tab).
  Coordinates are window-relative from the window's top-left INCLUDING the title bar, so
  read them off the previous screenshot PNG (the crop is anchored at `win_get_pos(top)` and
  PNG pixels equal click coordinates). Re-derive them from each fresh PNG; estimates from
  older screenshots drift.

Clicks are processed synchronously before the snapshot in the same loop pass. An earlier
revision deferred the shot by one extra `interactive()` pass via `continue`; that never
worked — `interactive()` returns immediately once `quit_month` is reached (and sets
`env_t::quit_simutrans`, exiting the main loop), and a single NORMAL-mode step never pumps
GUI events. Do not reintroduce the extra-pass design.

The snapshot rect is computed AFTER `view->display(true)`/`intr_refresh_display(true)`, not
before: a frame may resize itself during its first draw (e.g. `prices_frame_t` fits to its
content), and a rect read earlier would capture the pre-fit size.

## Run recipe (verified on Windows, Profile|x64)

- cwd = repo root. ALWAYS pass `-until <year>.<month>`: it exists only in DEBUG/PROFILE
  builds (simmain.cc), and is what makes `karte_t::interactive(quit_month)` return
  autonomously once the game month reaches the target. Without it the main loop never
  returns and no shot is taken. Use a target at or below the save's date (e.g. `1900.1`
  for a 2010 save) so it returns immediately with no fast-forward.
- Copy the fixture into `simutrans\save\` first: `-load` resolves relative to the save
  directory, not cwd. A save with long economic history suits economy/inflation UI; e.g.
  `ai\temp\perf\save\bb6-apr-2010.sve` (loads with recoverable vehicle-desc errors).
- `-objects` path is relative to the data dir: use `pak128.Britain-Ex/`, NOT
  `simutrans\pak128.Britain-Ex\` (the latter double-resolves and silently fails to load
  the pakset).
- With `-singleuser` the user dir is the data dir, so screenshots land in
  `simutrans\screenshot\simscrNN.png` (NN = lowest unused index in the current process)
  and settings write to `simutrans\settings-extended.xml`. Delete `simscr*.png` before each
  run for deterministic numbering, and ensure the `screenshot\` directory exists beforehand
  (otherwise the write fails silently).
- Example (PowerShell, repo root; all env vars in the SAME call as the launch — they do
  not persist between separate shell/tool calls):

```powershell
Copy-Item ai\temp\perf\save\bb6-apr-2010.sve simutrans\save\ -Force
Remove-Item simutrans\screenshot\simscr*.png -ErrorAction SilentlyContinue
$env:SIMUTRANS_UI_SHOT = "1"
$env:SIMUTRANS_UI_SHOT_TOOL = "130"          # DIALOG_PRICES
$env:SIMUTRANS_UI_SHOT_CLICKS = "60,25;255,40"  # optional: Graphs tab, then Fuel prices sub-tab
$p = Start-Process .\simutrans\Simutrans-Extended-Profile.exe -ArgumentList "-singleuser","-objects","pak128.Britain-Ex/","-load","bb6-apr-2010.sve","-until","1900.1","-lang","en","-debug","1","-fps","100","-nosound" -PassThru -RedirectStandardOutput ai\temp\shot-out.log -RedirectStandardError ai\temp\shot-err.log
$p.WaitForExit(180000); if (-not $ok) { $p.Kill() }
```

  Success criterion = PNG exists + `UI-AUTO:` lines in stdout (`queued clicks` /
  `clicks processed` / `capturing …` / `display_snapshot returned 1`). Afterwards clear the
  variables (`Remove-Item Env:\SIMUTRANS_UI_SHOT*`) so unrelated runs do not shoot.
- Run ONE game process at a time (concurrent launches exhaust memory). Check
  `Get-Process -Name Simutrans*` before and after.
- Do not trust `$p.ExitCode` from `Start-Process` (observed blank even on success). For a
  numeric exit code use `cmd /c "set ... && exe args & echo EXIT %ERRORLEVEL%"` instead.
- Telemetry `printf`s (`UI-AUTO: …`, `fflush`ed) live inside the ifdef; harmless, keep them
  while iterating.
- Build: `MSBuild.exe Simutrans-Extended.vcxproj /p:Configuration=Profile /p:Platform=x64 /m /v:q /nologo`,
  then grep the log for `: error`. Do NOT pipe MSBuild output into `Select-Object -First`
  (orphaned cl.exe processes observed).

## Loop procedure

1. Edit GUI code; rebuild the gated configuration (incremental builds suffice).
2. Delete `simutrans\screenshot\simscr*.png`; run the recipe (plus
   `SIMUTRANS_UI_SHOT_CLICKS` for the view under test).
3. Examine the PNG with the Read tool (it renders images).
4. List defects (alignment, clipping, overlap, padding, text overflow) → fix code → repeat
   from 1. Present final views to the user.

Verified click coordinates for the Prices-and-rates window (Pak128.Britain-Ex theme):
Graphs top tab `(60,25)`; sub-tabs (window-relative, measured in the fitted graph window)
Interest-and-tax-rates `(140,40)`, Fuel-prices `(255,40)`, Staff-wages `(330,40)`, used as
e.g. `"60,25;255,40"`. Keep top-tab clicks a few pixels below the title bar (y≈25): y=18
grazes the title-bar bottom edge and intermittently hits the `?` help button instead.
The Prices window now fits its own width and height to the active sub-tab, so a sub-tab
click must use the coordinates for the post-resize view (fuel at x≈255, not the ~221 of the
older wide layout) — a stale x can land outside the window and open another dialog. Window
geometry is theme- and content-dependent; re-derive for other dialogs.

## Constraints

- Only the TOP window is captured; runs show a real window on the desktop during fast forward
  (GDI; overlap is harmless — the shot reads the back buffer, not the desktop).
- The captured rect is clipped to screen size; a dialog taller than the screen is cut off at
  the bottom — check `min_windowsize` against the default viewport.
- `-generate_map` opens a modal banner that blocks the loop; always use `-load`.
- `SIMUTRANS_UI_SHOT_CLICKS` is consumed once per run; multi-view inspection means multiple
  runs (several clicks per run are fine).
- Loading the fixture can occasionally pop the message centre on top of the dialog under
  test (fixture server/admin message spam); only `news_img` boxes are auto-dismissed. If a
  capture shows the wrong window, just re-run.
- `ai\temp\perf\save\Rollmaterial 2018.sve` crashes during load on current ex-15 Profile;
  unresolved — use `bb6-apr-2010.sve` for price-history UI until that is investigated.

## Open questions

- Is a headless GDI backend possible (render without a visible desktop window)?
- Would multi-window views (two dialogs open at once) ever be needed by the loop?
