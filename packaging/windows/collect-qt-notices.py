# SPDX-License-Identifier: LGPL-2.1-or-later
"""Refresh the checked-in attribution texts from pristine qtbase and qtsvg v6.9.3 source roots.

This is a maintainer tool, not a build dependency. No networking or code execution from the inputs.
Usage: python packaging/windows/collect-qt-notices.py /path/to/qtbase /path/to/qtsvg > notices.txt
"""
import json
import sys
from pathlib import Path

version = "6.9.3"
modules = {"qtcore", "qtgui", "qtwidgets", "qtopengl", "qtsvg", "qtnetwork"}
texts = {}
entries = []
for repo, root in zip(("qtbase", "qtsvg"), map(Path, sys.argv[1:]), strict=True):
    if f'QT_REPO_MODULE_VERSION "{version}"' not in (root / ".cmake.conf").read_text():
        sys.exit(f"Expected {repo} {version} source tree")
    for path in sorted((root / "src").rglob("qt_attribution.json")):
        records = json.loads(path.read_text(encoding="utf-8"), strict=False)
        for item in records if isinstance(records, list) else [records]:
            if item["QDocModule"] not in modules:
                continue
            files = item.get("LicenseFile", item.get("LicenseFiles", []))
            files = [files] if isinstance(files, str) else files
            licenses = [path.parent / f for f in files] or [root / "LICENSES" / (item["LicenseId"] + ".txt")]
            refs = []
            for file in licenses:
                # Some upstream notices contain non-ASCII copyright names in Latin-1.
                data = file.read_bytes()
                try:
                    text = data.decode("utf-8-sig")
                except UnicodeDecodeError:
                    text = data.decode("latin-1")
                text = "\n".join(line.rstrip() for line in text.splitlines()).strip()
                refs.append(str(texts.setdefault(text, len(texts) + 1)))
            entries.append(f'{item["Name"]} — {item.get("Version", "see source")}\n'
                           f'{item.get("Copyright", "")}\n{item["LicenseId"]} (text {", ".join(refs)})\n'
                           f'https://github.com/qt/{repo}/blob/v{version}/{path.relative_to(root).as_posix()}\n'
                           + " ".join(item.get("QtUsage", "").split()))
print("Qt 6.9.3: upstream third-party attributions\n"
      "This source-module inventory includes conditional features and platforms, not just the DLLs\n"
      "in a particular build. System/vcpkg copies can have different versions; see the package\n"
      "inventory and port notices. Generated from qt_attribution.json for Core, Gui, Widgets,\n"
      "OpenGL, Network and SVG using packaging/windows/collect-qt-notices.py.\n")
print("\n\n".join(entries))
for text, number in texts.items():
    print(f"\n---- License text {number} ----\n\n{text}")
