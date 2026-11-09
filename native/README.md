# Album Lite — native (Qt 6 / C++20)

A native port of the Electron app in the parent folder. The web app is untouched.

## Build

```sh
cmake -S . -B build -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/album-lite [photos or folders…]
```

Without Qt, `cmake` still builds and tests the Qt-free layout core (`src/layout.h`).

## Layout

| File | Role |
|---|---|
| `src/layout.h` | Templates, template picking, auto-flow, cover-fit, DPI (port of `layout.js`) |
| `src/album.*` | Model: page geometry, slot boxes, photo index, JSON persistence |
| `src/imagecache.*` | Async tiered decode (256 / 1024 / 2560 / full px), EXIF-aware |
| `src/exporter.*` | `QPdfWriter` export: one page per spread, trim + bleed, 300 dpi page space |
| `src/sheetsview.*` | Main canvas; paints only visible spreads; pan, zoom, drag & drop |
| `src/trayview.*`, `src/outlineview.*` | Photo tray and spread outline |
| `src/mainwindow.*` | Toolbar, import, autosave, export flow |

## Projects

Each project is a `.album` file (JSON, same field names as the web app's saved state) that you
can keep anywhere. **File** menu: New (Ctrl+N), Open (Ctrl+O), Open Recent, Save (Ctrl+S),
Save As (Ctrl+Shift+S). `./build/album-lite project.album` opens one directly.

- The title shows the project name, with `*` when there are unsaved changes.
- Opening, creating or closing with unsaved changes asks Save / Discard / Cancel.
- Every 5 minutes a recovery copy of unsaved work is written to the app data folder. After a
  crash the app offers to recover it; saving or a clean exit removes it.
- The next launch reopens the last project.
- Photos are stored as absolute paths, so a project must be opened on a machine where the same
  photo files exist at the same paths.
- The single hidden autosave of earlier versions (`~/.local/share/album-lite/album-lite/album.json`)
  is adopted once as an unsaved untitled project; use Save As to keep it.

## Tests

- `test_layout` — port of `../test.js`, no Qt.
- `test_core` — EXIF orientation, JSON round trip, PDF export, cache tiers.
- `smoke_ui` — drives the real window offscreen (clicks, ctrl+wheel zoom, pan, persistence,
  PDF export). Set `ALBUM_SMOKE_PNG=/path/shot.png` to keep a screenshot.
