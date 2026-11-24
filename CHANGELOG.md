# Changelog

## Unreleased

- **LINE:** the command keeps drawing segments until Enter, the way AutoCAD's LINE does, with `C`
  to close the chain back to the first point and `U` to take the last segment back. The prompt names
  the options once a segment exists. A chain typed in one line is committed as one transaction, so a
  single `_UNDO` takes the whole command back.
- **Coordinate entry:** `@dx,dy` and `@d<a` are offsets from the last picked point and `d<a` is a
  polar offset from the origin, next to the absolute `x,y` the prompt already accepted. A leading
  `@` before anything was picked repeats the prompt instead of drawing at the origin.
- **Command line:** F2 (the AutoCAD text window key) and Ctrl+9 collapse the prompt to a single
  line and expand it back into the scrolling history; the choice is remembered between sessions.
- **Palettes and clean screen:** Ctrl+1 shows the properties palette, Ctrl+2 the layers palette and
  Ctrl+9 the command line; Ctrl+0 hides the ribbon, both palettes, the toolbars and the menus while
  keeping the prompt, and brings back exactly what was open before.
- **Command aliases:** the acad.pgp keys for the commands the program actually has — `U` for undo,
  `PA` paste, `ZE` zoom extents, `SE` dimension style, and `DL`/`DLI`, `DAL`, `DRA`, `DDI`, `DAN` for
  the five dimension commands.
- **Command line:** Up and Down walk what was typed, as in AutoCAD; a draft typed before the first
  Up returns when the walk goes past the newest entry, and the completion list still owns the keys
  while its popup is open.
- **Dynamic input:** the length/angle label is created once per viewport. It used to be recreated on
  every resize, zoom-extents and model/paper switch, which piled up hidden children and left
  `findChild("dynamicInput")` pointing at an arbitrary label.
- **Ribbon:** tab titles are translated from the source strings they were built from, exactly like
  the panel titles, so the three copies of the same positional list in `MainWindow` are gone.
- **Ribbon, drawn like AutoCAD's:** every panel is its own framed group on the page, the current tab
  merges into the page on a darker band, and the strip is exactly as tall as the tab in front of the
  user, so a short Insert page no longer leaves a block of empty ribbon. Panels lead with one large
  command and list the rest as small entries beside it, and a double click on a tab minimizes the
  ribbon to its tabs.
- **Ribbon captions:** a caption is never elided — the button is measured against the label rectangle
  the style actually paints — and commands are titled the way AutoCAD titles them (`Polyline`,
  `Rectangle`, `Linear`), with the ellipsis kept for the entries that open a dialog.
- **Command line and status bar:** the prompt's dock has no caption bar, and the status bar reads as
  flat cells with a hairline and an accent line under an active toggle, like AutoCAD's. The
  coordinate readout owns the left end of the bar and no hint can hide it any more.
- **Ribbon scrolling:** the panels move with two chevrons on the tab band and with the wheel, the way
  AutoCAD scrolls a ribbon that does not fit; the strip keeps no scroll bar of its own.

## v0.2.0

Verified by the five CI jobs and by the Windows payload that ran its own converters on a real drawing and round-tripped `panel.dxf` through DWG; 27 runtime files in the inventory.

DWG out of the box, a ribbon workbench and the classic AutoCAD command set.

- **DWG:** the pinned LibreDWG 0.14 programs `dwg2dxf` and `dxf2dwg` travel with the portable ZIP, the
  installer and the installed Linux tree under `tools/libredwg/`. PlaneDraft finds them beside the
  executable and then on `PATH`, so no environment variable or manual install is needed. The
  converters stay separate GPLv3 processes: the LGPL core never links, loads or ships libredwg.
  `*.dwg *.dxf` are accepted by the open and save dialogs. Reading uses the native converter
  (R2.10 … R2018+), writing targets R2000 by default (`dxf2dwg -a r2000`). 3D entities are skipped
  with a summary on the command line, and text rotation survives the round trip.
- **Ribbon:** Home, Insert, Annotate, View and Manage pages with Draw, Modify, Annotations, Layers,
  Blocks, Properties, Plugins and Scripts panels; a quick access toolbar for New, Open, Save, PDF,
  Undo and Redo; layer, colour and line-weight drop-downs that edit the selection in one undo step.
  View → Ribbon switches back to the five classic toolbars and remembers the choice. Every page is
  a fixed strip as tall as its tallest panel — three rows of Modify commands and the panel captions
  stay inside it — while a narrow window scrolls the page sideways instead of eliding a caption.
  Captions are never elided, and the panels follow the light and dark palettes while the theme can
  be switched at any time.
- **CAD ergonomics:** Space confirms a step or repeats the last command on an empty prompt (except
  while entering free text), a middle double-click zooms to the drawing extents, the right-button
  menu offers repeat, recent commands, properties, erase, clipboard and a base point, and F12 shows
  dynamic length/angle input beside the crosshair with Tab switching between the fields.
- **Command line:** autocomplete over every command and alias; built-in EN and RU alias tables
  (`L`/`ОТ`, `REC`/`ПРЯМ`, `POL`/`МН`, `CHA`/`ФАС`, …) that a user `aliases.ini` extends or
  overrides; `_ZOOM` routed to the view; `COPYCLIP`, `CUTCLIP`, `COPYBASE` and `PASTECLIP` with the
  usual Ctrl+C/X/V and Ctrl+Shift+C shortcuts.
- **New commands:** `_RECTANG`/`ПРЯМОУГ` (closed polyline), `_POLYGON`/`МН-УГОЛ` (inscribed or
  circumscribed), `_CHAMFER`/`ФАСКА` (two distances), `_ARRAY`/`МАССИВ` (rectangular and polar, one
  undo step), `_MATCHPROP`/`КОПИРОВАТЬСВ`, `_DIST`/`ДИСТ` and `_AREA`/`ПЛОЩАДЬ` reporting to the
  command line. Circles, three-point arcs and polylines accept typed points as well as picks.
- **Tests:** new coverage for every added command, the alias resolution, clipboard, repeat,
  ribbon/QAT/combos, Space-as-Enter, F12 and the extents double-click, plus DWG round-trip tests
  through `pdconvert` on Linux and Windows.

### Compatibility notes

Windows packages now contain two extra GPLv3 executables and their licence text; see the runtime
inventory and notices. DWG output is R2000 unless a newer version is requested. Unsupported DXF/DWG
content is reported and may be simplified on import.

## v0.1.0

First PlaneDraft 2D release series: draw at model scale, compose a sheet and issue it as a vector file.

- **Drafting:** analytic 2D geometry, nested blocks, hatching with islands, five dimension types, ISO/ESKD styles and scaled paper-space viewports. The included mounting-panel drawing has a dimensioned view and hatched section A–A on an A4 sheet.
- **Editing:** move, copy, rotate, scale, mirror, trim, extend, offset, fillet and erase, plus object snaps, Ortho/polar tracking and grips. Multi-object property changes and spatial index updates share one undo step.
- **Workbench:** independent drawing tabs, dockable properties, layer manager, five SVG toolbars, bilingual command line, live Russian/English and light/dark switching. Fractional-DPI rendering covers the entire widget framebuffer.
- **Exchange:** native support for a documented subset of ASCII DXF; vector SVG/PDF output from the application and the Qt-free `pdconvert` CLI. Optional external DWG conversion is off by default.
- **Extensions:** a sized C ABI v2 with v1 compatibility, two sample modules and a transactional PlaneDraft Lisp interpreter with an example script.
- **Distribution:** MSVC 2022 x64 portable ZIP and per-user Inno Setup installer generated from the same payload. Both forms passed cold-PATH loading, named A4 PDF export and runtime inventory checks. The installer offers an optional DXF handler without replacing another application's default. The 25 runtime files have a SHA-256 inventory; matching port records and full PCRE2/SLJIT notices travel with the binaries.

### Compatibility notes

A desktop OpenGL 4.5 Core driver is required for the GUI, not for `pdconvert`. Unsupported DXF content can be lost: review import diagnostics and save a copy. Dimensions and hatches are not associative with adjacent entities. No DWG converter, proprietary CAD SDK or graphics driver is bundled. Host: LGPL-2.1-or-later; public SDK and examples: MIT. Consult the package notices for third-party terms.
