# SPDX-License-Identifier: LGPL-2.1-or-later
"""Independently verify an extracted or installed payload against its CPack inventory."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

ROW = re.compile(r"\| `([^`]+)` \| ([^|]+) \| `([0-9a-f]{64})` \|")


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def verify(root, installed=False):
    # Both the walk and relative_to use the SAME normalized root (Windows TEMP can use 8.3 names).
    root = root.resolve(strict=True)
    files = {}
    for file in root.rglob("*"):
        if file.is_file() and file.suffix.lower() in {".dll", ".exe", ".qm"}:
            key = file.relative_to(root).as_posix().casefold()
            if key in files:
                raise ValueError(f"Duplicate runtime path: {key}")
            files[key] = file
    if installed and files.pop("unins000.exe", None) is None:
        raise ValueError("Missing generated Inno uninstaller")
    count, records = 0, {}
    for line in (root / "RUNTIME_INVENTORY.md").read_text(encoding="utf-8-sig").splitlines():
        if not line.startswith("| `"):
            continue
        row = ROW.fullmatch(line)
        if not row:
            raise ValueError(f"Malformed inventory row: {line}")
        path, owner, expected = row.groups()
        file = files.pop(path.casefold(), None)
        if file is None:
            raise ValueError(f"Missing/duplicate runtime {path!r}; remaining: {sorted(files)}")
        if sha256(file) != expected:
            raise ValueError(f"Wrong SHA-256 for {path}")
        count += 1
        if owner.startswith(("PlaneDraft (", "Microsoft Visual C++ Redistributable (")):
            continue
        port = owner.partition(" ")[0]
        if not re.fullmatch(r"[a-z0-9-]+", port):
            raise ValueError(f"Invalid port name: {port!r}")
        if port not in records:
            metadata = root / "LICENSES" / "ports" / port
            notice = metadata / "copyright"
            if not notice.stat().st_size:
                raise ValueError(f"Empty notice for {port}")
            sbom = json.loads((metadata / "vcpkg.spdx.json").read_bytes())
            recipes = [p for p in sbom["packages"] if p["name"] == port]
            if len(recipes) != 1 or not recipes[0].get("versionInfo"):
                raise ValueError(f"Wrong SPDX record for {port}")
            recipe = recipes[0]
            records[port] = (f"{port} {recipe['versionInfo']}: vcpkg {recipe['licenseConcluded']}; "
                             f"copyright SHA-256 {sha256(notice)}")
    if not count or files:
        raise ValueError(f"Incomplete inventory ({count} rows); unlisted runtimes: {sorted(files)}")
    # The pinned pcre2 port installs COPYING, which only points to the actual licence.
    # Check the complete upstream texts as well as the unmodified port record.
    if "pcre2" in records:
        for name, digest in (
            ("PCRE2-10.47.txt", "197d8a73ffee0d6b09adba2f9c677b5f5aede24edf89258a68e48248d010d811"),
            ("SLJIT-45f910b.txt", "5f216505c0f6ea3273caec89e766eef93cdeb7bbb0c429f9360116d7c938feeb"),
        ):
            if sha256(root / "LICENSES" / name) != digest:
                raise ValueError(f"Missing or altered upstream licence: {name}")
    print(f"Verified {count} runtime files and {len(records)} port records in {root.name}")
    notices = "\n".join(records[k] for k in sorted(records))
    if not installed and os.environ.get("GITHUB_ACTIONS") == "true":
        print("::notice title=Notices in this Windows payload::" + notices.replace("%", "%25").replace("\n", "%0A"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path)
    parser.add_argument("--installed", action="store_true", help="expect Inno's extra root unins000.exe")
    args = parser.parse_args()
    try:
        verify(args.root, args.installed)
    except (OSError, KeyError, ValueError) as exc:
        sys.exit(str(exc))
