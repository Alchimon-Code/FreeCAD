# SPDX-License-Identifier: LGPL-2.1-or-later
"""The version that names a release must agree in the three places a release reads it from:
the CMake project, the vcpkg manifest (which the draft-release workflow reads) and the changelog
heading whose section becomes the release notes. A stale entry here fails the release job late, so
the check runs with the ordinary documentation checks instead."""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    version = json.loads((ROOT / "vcpkg.json").read_text(encoding="utf-8"))["version-string"]
    problems = []
    project = re.search(r"project\(\s*PlaneDraft\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)",
                        (ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
    if not project:
        problems.append("CMakeLists.txt has no 'project(PlaneDraft VERSION x.y.z)'")
    elif project.group(1) != version:
        problems.append(f"CMakeLists.txt says {project.group(1)}, vcpkg.json says {version}")
    headings = {line.strip() for line in
                (ROOT / "CHANGELOG.md").read_text(encoding="utf-8").splitlines()}
    if f"## v{version}" not in headings:
        problems.append(f"CHANGELOG.md has no exact '## v{version}' heading, so the release notes "
                        "the workflow copies would be empty")
    for problem in problems:
        print(problem, file=sys.stderr)
    if problems:
        return 1
    print(f"release metadata agrees on {version}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
