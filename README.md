# Album Lite

A small Windows desktop app for designing photo albums: import photos, get spreads
laid out automatically, tweak them, export a print-ready PDF.

![toolbar → tray → spreads](docs/screenshot.png)

## Run

```powershell
npm install
npm start
```

`npm test` runs the layout-heuristic checks (`node --test`, no framework).

## What it does

- **Import** — button, or drag files onto the window.
- **Auto-layout** — photos are chunked into spreads (panoramas get their own),
  then each spread picks the template whose slot shapes best match the photos
  that will land in them. Photos fill slots in reading order, so chronology survives.
- **Tweak** — drag a thumbnail onto a slot to replace it, drag the `⠿` handle to
  swap two slots, drag inside a photo to re-frame the crop, double-click to recentre,
  `◀ ▶` to cycle layouts, `Best fit` to re-pick.
- **Export** — one PDF, one page per spread, sized `spread + bleed`. Source JPEGs
  are embedded losslessly at full resolution, so a 24 MP photo in a 12×8 in slot
  prints at ~500 DPI.
- Work autosaves to local storage and reloads on next launch (photos are referenced
  by path, so don't move the originals).

## How it works

The whole thing is the DOM. Spreads are absolutely-positioned `<div>`s sized in
inches; export is `webContents.printToPDF` with `preferCSSPageSize` against a
generated `@page` rule, and print CSS hides the UI. No canvas, no compositing,
no image library.

`layout.js` is pure data + two functions (`pickTemplate`, `autoFlow`) and is the
only part with real logic, so it's the only part with tests.

## Settings

| Setting | Default | Notes |
|---|---|---|
| Size | 12 × 12 in page | Spread is two pages wide |
| Photos / spread | 4 | 1–6 |
| Bleed | 0.125 in | Edge photos extend into it; most labs require this |
| Gutter | 0.15 in | Space between photos |

## Deliberately not built

Cover/spine design, text and captions, per-photo zoom, colour correction, crop
marks, cloud sync, undo history, `.album` project files. Add whichever one you
actually miss after using it.
