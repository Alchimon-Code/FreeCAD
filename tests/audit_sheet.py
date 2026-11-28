# SPDX-License-Identifier: LGPL-2.1-or-later
"""Opt-in exchange tests. Only the fixture/CLI (not these readers) use pd_core."""
import math
import subprocess
import sys
import tempfile
from contextlib import closing
from pathlib import Path
from xml.etree import ElementTree as ET

import ezdxf
import pypdfium2 as pdfium
from pypdf import PdfReader


def audited(path):
    doc = ezdxf.readfile(path)
    result = doc.audit()
    assert not result.errors and not result.fixes, (result.errors, result.fixes)
    return doc


def native_entities(path):
    doc = audited(path)
    assert doc.layouts.names_in_taborder() == ["Model", "Plate", "Деталь"]
    model = doc.modelspace()
    assert model.query("INSERT").first.dxf.name == "Assembly"
    assert doc.blocks["Assembly"].query("INSERT").first.dxf.name == "Штифт"
    assert len(model.query("HATCH").first.paths) == 2
    dims = sorted(model.query("DIMENSION"), key=lambda d: d.dimtype)
    assert [d.dimtype for d in dims] == [1, 3, 5]
    for dim, value in zip(dims, [40, 6, 90]):
        assert math.isclose(dim.get_measurement(), value)
        assert dim.dxf.geometry in doc.blocks and list(dim.virtual_entities())
        assert dim.dxf.dimstyle in doc.dimstyles
    assert dims[-1].dxf.dimstyle == "ESKD"
    plate = doc.layout("Plate")
    labels = {t.dxf.text for t in plate.query("TEXT")}
    assert {"Корпус", "ПД.01", "1:2", "Лист 1", "Листов 2"} <= labels
    vp = next(v for v in plate.query("VIEWPORT") if v.dxf.id != 1)
    assert vp.dxf.height / vp.dxf.view_height == 0.5
    assert tuple(vp.dxf.view_center_point)[:2] == (1e7, -1e7)
    assert plate.dxf_layout.dxf.paper_width == 297


def physical_page(directory):
    svg = ET.parse(directory / "sheet.svg").getroot()
    assert (svg.get("width"), svg.get("height")) == ("297mm", "210mm")
    ns = {"s": "http://www.w3.org/2000/svg"}
    assert any(r.attrib == dict(x="65", y="70", width="100", height="50")
               for r in svg.findall(".//s:clipPath/s:rect", ns))
    assert svg.findall(".//s:g[@clip-path]", ns)
    page = PdfReader(directory / "sheet.pdf").pages[0]
    for actual, mm in zip(page.mediabox[2:], (297, 210)):
        assert math.isclose(float(actual), mm * 72 / 25.4, abs_tol=1e-8)
    assert b"re W n" in page.get_contents().get_data()
    with closing(pdfium.PdfDocument(directory / "sheet.pdf")) as doc:
        bmp = doc[0].render(scale=2, rev_byteorder=True)
        pixels = bmp.to_numpy()

        def red_at(x, y):
            ix, iy = round(x * 144 / 25.4), round((210 - y) * 144 / 25.4)
            patch = pixels[iy-2:iy+3, ix-2:ix+3, :3]
            return bool(((patch[:, :, 0] > 150) & (patch[:, :, 1] < 90)).any())

        assert red_at(80, 95) and red_at(135, 98), "missing ruler or misplaced 1:2 tick"
        assert not red_at(55, 95) and not red_at(175, 95), "ink outside viewport"


def foreign_input(directory, converter):
    # Do not let two matching mistakes in PlaneDraft's reader and writer pass the round trip.
    doc = ezdxf.new("R2000")
    doc.units = 4
    model = doc.modelspace()
    block = doc.blocks.new("Datum", base_point=(3, 5))
    block.add_line((3, 5), (8, 5))
    model.add_blockref("Datum", (20, 30), dxfattribs={"xscale": 2, "rotation": 90})
    model.add_linear_dim((0, 16), (1, 3), (11, 7), dimstyle="Standard").render()
    hatch = model.add_hatch()
    loop = hatch.paths.add_edge_path(flags=1)
    loop.add_arc((0, 0), 10, start_angle=0, end_angle=90, ccw=False)
    loop.add_line((10, 0), (0, 0))
    loop.add_line((0, 0), (0, 10))
    hatch.paths.add_edge_path().add_arc((2, 2), 1, start_angle=0, end_angle=360)
    layout = doc.layouts.new("Plan")
    layout.page_setup(size=(297, 210))
    layout.add_viewport((110, 110), (160, 100), (400, 200), 200)
    src, dst = directory / "external.dxf", directory / "back.dxf"
    doc.saveas(src)
    subprocess.run([converter, src, dst], check=True)
    restored = audited(dst)
    model = restored.modelspace()
    line, = model.query("INSERT").first.virtual_entities()
    assert line.dxf.start.isclose((20, 30)) and line.dxf.end.isclose((20, 40))
    assert math.isclose(model.query("DIMENSION").first.get_measurement(), 10)
    outer, inner = model.query("HATCH").first.paths
    x, y, bulge = outer.vertices[0]
    assert abs(x) < 1e-9 and math.isclose(y, 10) and math.isclose(bulge, -math.tan(math.pi / 8))
    assert len(inner.vertices) == 2 and all(abs(p[2]) == 1 for p in inner.vertices)
    vp = next(v for v in restored.layout("Plan").query("VIEWPORT") if v.dxf.id != 1)
    assert vp.dxf.center.isclose((110, 110)) and vp.dxf.view_center_point.isclose((400, 200))
    assert vp.dxf.height / vp.dxf.view_height == 0.5


if __name__ == "__main__":
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        subprocess.run([sys.argv[1], root], check=True)
        native_entities(root / "source.dxf")
        native_entities(root / "roundtrip.dxf")
        physical_page(root)
        foreign_input(root, sys.argv[2])
    print("Three output DXFs: 0 audit errors/fixes; A4 PDF/SVG and clipped 1:2 PDF raster verified.")
