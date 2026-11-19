#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# CI-only: one package transaction, without refreshing unrelated hosted-image PPAs.
set -euo pipefail
missing=()
for package in "$@"; do
    if [[ $(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || :) != installed ]]; then
        missing+=("$package")
    fi
done
if ((${#missing[@]} == 0)); then
    echo 'All requested Ubuntu packages are already installed.'
    exit 0
fi
source /etc/os-release
[[ $ID == ubuntu && $VERSION_CODENAME == noble ]]
sources="$RUNNER_TEMP/planedraft-ubuntu.list"
cat > "$sources" <<'SOURCES'
deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://archive.ubuntu.com/ubuntu noble main universe
deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://archive.ubuntu.com/ubuntu noble-updates main universe
deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://security.ubuntu.com/ubuntu noble-security main universe
SOURCES
options=(-o "Dir::Etc::sourcelist=$sources" -o Dir::Etc::sourceparts=-
         -o APT::Get::List-Cleanup=false -o Acquire::Languages=none -o Acquire::Retries=2
         -o Acquire::https::Timeout=25 -o DPkg::Lock::Timeout=60)
python3 .github/scripts/checked.py sudo apt-get "${options[@]}" -o APT::Update::Error-Mode=any update
python3 .github/scripts/checked.py sudo apt-get "${options[@]}" install -y --no-install-recommends "${missing[@]}"
