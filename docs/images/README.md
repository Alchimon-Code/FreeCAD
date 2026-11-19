# Gallery provenance

The workspace images are captures of the running Qt Widgets / desktop OpenGL application, not illustrations. All three use [panel.dxf](../../examples/drawings/panel.dxf). For the two model views the tool changes layer colours in memory for contrast (the `*` on the tab), selects the two inserts or the central circle, and switches the actual locale/theme actions. It undoes the colour change before showing the **Панель** paper-space tab. It does not edit the example DXF, load plugins or modify the user's settings.

| View | Subject |
|---|---|
| [Dark · RU](workspace-dark-ru.png) | Two inserts, mixed insertion points, layer and property docks |
| [Light · EN](workspace-light-en.png) | Same window, selected circle, radius 27; names from the drawing stay in their original language |
| [A4 layout](layout-eskd.png) | Model viewport, section A–A, ESKD-style frame and title block |
| [PDF output](pdf-output.png) | Independent raster of the vector export, 144 dpi, 297 × 210 mm |

To reproduce from the repository root, configure `linux-release` as in the [build guide](../../README.md#quick-start), then run in a desktop GL session:

```sh
cmake --build --preset linux-release --target pd_capture_workspace pdconvert
mkdir -p out/gallery
QT_SCALE_FACTOR=1.5 build/linux-release/bin/pd_capture_workspace examples/drawings out/gallery
build/linux-release/bin/pdconvert --layout "Панель" examples/drawings/panel.dxf out/gallery/panel-A4.pdf
```

The [capture tool](../../tools/capture_workspace.cpp) grabs the real window and its actual `QOpenGLWidget` framebuffer. It is not included in runtime packages. `pdf-output.png` is **not** that framebuffer: it was rendered from the CLI's PDF with PDFium at two pixels per PDF point. For example, with `pypdfium2==4.30.0` and Pillow installed:

```python
import pypdfium2 as pdfium
pdf = pdfium.PdfDocument("out/gallery/panel-A4.pdf")
page = pdf[0]
page.render(scale=2, rev_byteorder=True).to_pil().save("out/gallery/pdf-output.png")
pdf.close()
```
