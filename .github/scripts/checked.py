# SPDX-License-Identifier: LGPL-2.1-or-later
"""Stream a CI command normally. Also put its diagnostics on the GitHub check if it fails."""
from collections import deque
import re
import subprocess
import sys


def run(argv):
    # Keep logs bounded even for a full vcpkg build. Do not dump the environment.
    errors, tail = deque(maxlen=12), deque(maxlen=10)
    with subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        for raw in proc.stdout:
            sys.stdout.buffer.write(raw)
            sys.stdout.buffer.flush()
            text = raw.decode("utf-8", errors="replace").rstrip()
            if text:
                tail.append(text[:500])
            if re.search(r"error[: ]|fatal|failure|failed|exception|expected:|actual:", text, re.I):
                errors.append(text[:700])
        rc = proc.wait()
    if rc:
        detail = "\n".join(dict.fromkeys([*errors, *tail]))
        print("::error title=Command failed::" + detail.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A"))
    return rc


if __name__ == "__main__":
    sys.exit(run(sys.argv[1:]))
