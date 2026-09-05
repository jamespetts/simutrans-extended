---
status: stub
verified: none
---
# Rendering & display

**Covers:** display/ (13 files: simgraph*, simimg, viewport, etc.), simcolor.h (root), utils/dr_rdpng.* (PNG decode → [utilities-and-threading](utilities-and-threading.md)), sys/simsys.* backends (→ [utilities-and-threading](utilities-and-threading.md)), simloadingscreen.* (root → [simulation-core](simulation-core.md)), minimap (gui/minimap → [gui](gui.md)).

## Seed facts

- Hubs: `display/simimg.h` (41 includers), `display/simgraph.h` (41), `display/viewport.h` (32), `simcolor.h` (47) [CODE ex-15 @ b06e8fa14].
- SDL3 backend (`sys/simsys_s3.cc`, `sys/clipboard_s3.cc`, `sound/sdl3_sound.cc`) exists on master ONLY (commits `f0638c3d7`, `3b70dd4b3`, Aug 2026); ex-15 lacks it — rendering/backend docs must state which branch they describe [CODE].
- Root holds a local-only `simgraph.diff` artefact — historical lead [local-only].

## Planned sections

- Display architecture: simgraph abstraction, backend selection (SDL2/SDL3), display/ inventory.
- Image pipeline: simimg, image lists/descriptors (→ [data-and-pak](data-and-pak.md)), PNG loading.
- Viewport & coordinate transforms (screen↔world; ribi/koord → [data-and-pak](data-and-pak.md)).
- Colour system (simcolor; palette — documentation/*.png + simutrans-palette.pal are legacy human docs).
- Clipboards & input (sys/, siminteraction → [simulation-core](simulation-core.md)).

## Open questions

- What exactly the SDL3 backend changes for ex-15 merging (→ [ex-15](ex-15.md)).
