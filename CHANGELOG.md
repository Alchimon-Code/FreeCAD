# Changelog

## v0.1.0

First PlaneDraft 2D release series: draw at model scale, compose a sheet and issue it as a vector file.

- **Drafting:** analytic 2D geometry, nested blocks, hatching with islands, five dimension types, ISO/ESKD styles and scaled paper-space viewports. The included mounting-panel drawing has a dimensioned view and hatched section A–A on an A4 sheet.
- **Editing:** move, copy, rotate, scale, mirror, trim, extend, offset, fillet and erase, plus object snaps, Ortho/polar tracking and grips. Multi-object property changes and spatial index updates share one undo step.
- **Workbench:** independent drawing tabs, dockable properties, layer manager, five SVG toolbars, bilingual command line, live Russian/English and light/dark switching. Fractional-DPI rendering covers the entire widget framebuffer.
- **Exchange:** native support for a documented subset of ASCII DXF; vector SVG/PDF output from the application and the Qt-free `pdconvert` CLI. Optional external DWG conversion is off by default.
- **Extensions:** a sized C ABI v2 with v1 compatibility, two sample modules and a transactional PlaneDraft Lisp interpreter with an example script.
- **Distribution:** Windows x64 portable ZIP and per-user Inno Setup recipes. The installer offers an optional DXF handler without replacing another application's default. Runtime files carry a SHA-256 inventory and third-party notices.

### Compatibility notes

A desktop OpenGL 4.5 Core driver is required for the GUI, not for `pdconvert`. Unsupported DXF content can be lost: review import diagnostics and save a copy. Dimensions and hatches are not associative with adjacent entities. No DWG converter, proprietary CAD SDK or graphics driver is bundled. Host: LGPL-2.1-or-later; public SDK and examples: MIT. Consult the package notices for third-party terms.
