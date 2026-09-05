---
status: stub
verified: none
---
# Rendering & display

**Covers:** display/ (simgraph*, simimg, viewport, etc.), simcolor.h (root), utils/dr_rdpng.* (PNG decode → [utilities-and-threading](utilities-and-threading.md)), sys/simsys.* backends (→ [utilities-and-threading](utilities-and-threading.md)), simloadingscreen.* (root → [simulation-core](simulation-core.md)), minimap (gui/minimap → [gui](gui.md)).

## Initial facts

- Widely-included headers: `display/simimg.h`, `display/simgraph.h`, `display/viewport.h`, `simcolor.h` [CODE].
- The SDL3 backend (`sys/simsys_s3.cc`, `sys/clipboard_s3.cc`, `sound/sdl3_sound.cc`) exists on master only; ex-15 lacks it — rendering/backend docs must state which branch they describe [CODE].
- The root contains a local-only `simgraph.diff` artefact — historical indication [local-only].

## Planned sections

- Display architecture: simgraph abstraction, backend selection (SDL2/SDL3), display/ inventory.
- Image pipeline: simimg, image lists/descriptors (→ [data-and-pak](data-and-pak.md)), PNG loading.
- Viewport & coordinate transforms (screen↔world; ribi/koord → [data-and-pak](data-and-pak.md)).
- Colour system (simcolor; palette — documentation/*.png + simutrans-palette.pal are legacy human docs).
- Clipboards & input (sys/, siminteraction → [simulation-core](simulation-core.md)).

## Open questions

- What exactly the SDL3 backend changes for ex-15 merging (→ [ex-15](ex-15.md)).
