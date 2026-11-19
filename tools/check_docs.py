# SPDX-License-Identifier: LGPL-2.1-or-later
"""Check local Markdown links offline, in a checkout or a staged package (optional directory)."""
from functools import cache
from pathlib import Path
import re
import subprocess
import sys
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


@cache
def content(path):
    # Command samples contain brackets and #comments that aren't document headings/links.
    return re.sub(r"^```[^\n]*\n.*?^```[^\n]*$", "", path.read_text(encoding="utf-8"), flags=re.M | re.S)


@cache
def anchors(path):
    result = set(re.findall(r'(?:id|name)="([^"]+)"', content(path)))
    for heading in re.findall(r"^#{1,6} (.+?)\s*$", content(path), re.M):
        slug = re.sub(r"[^\w -]", "", heading.lower()).replace(" ", "-")
        unique = slug
        n = 0
        while unique in result:
            n += 1
            unique = f"{slug}-{n}"
        result.add(unique)
    return result


def main():
    if len(sys.argv) > 2:
        sys.exit("Usage: check_docs.py [package-directory]")
    root = Path(sys.argv[1]).resolve(strict=True) if len(sys.argv) == 2 else ROOT
    if root == ROOT:
        files = subprocess.check_output(
            ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", "*.md"], cwd=root
        ).decode("utf-8").split("\0")
    else:
        files = [p.relative_to(root) for p in root.rglob("*.md")]
        if not (root / "README.md").is_file():
            sys.exit("Package README missing")
    checked = 0
    errors = []
    for name in filter(None, files):
        path = root / name
        for url in re.findall(r"\]\(([^\s)]+)\)", content(path)):
            link = urlsplit(url)
            if link.scheme or link.netloc:
                continue
            dst = (path.parent / unquote(link.path)).resolve() if link.path else path
            fragment = unquote(link.fragment)
            if not dst.is_relative_to(root) or not dst.exists():
                errors.append(f"{name}: missing target {url}")
            elif fragment and dst.suffix == ".md" and fragment not in anchors(dst):
                errors.append(f"{name}: missing heading {url}")
            checked += 1
    for msg in errors:
        print(msg, file=sys.stderr)
    print(f"{checked} local links checked, {len(errors)} broken")
    return bool(errors)


if __name__ == "__main__":
    sys.exit(main())
