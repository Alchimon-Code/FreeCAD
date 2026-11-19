#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# CI-only, on the ubuntu-24.04 runner. Do not refresh unrelated vendor PPAs.
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
printf 'Installing missing packages: %s\n' "${missing[*]}"
source /etc/os-release
[[ $ID == ubuntu && $VERSION_CODENAME == noble ]]

# Use the runner's cloud-local signed mirror and existing indexes first. If the
# Ubuntu endpoints are unreachable, kernel.org mirrors the same signed archive.
fallback="$RUNNER_TEMP/planedraft-ubuntu.list"
printf 'deb [signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] https://mirrors.edge.kernel.org/ubuntu %s main universe\n' \
    noble noble-updates noble-security > "$fallback"
for source_file in /etc/apt/sources.list.d/ubuntu.sources "$fallback"; do
    [[ -s "$source_file" ]] || continue
    apt=(sudo apt-get -o "Dir::Etc::sourcelist=$source_file" -o Dir::Etc::sourceparts=-
         -o APT::Get::List-Cleanup=false -o Acquire::Languages=none -o Acquire::Retries=1
         -o Acquire::http::Timeout=15 -o Acquire::https::Timeout=15 -o DPkg::Lock::Timeout=60)
    install=(install -y --no-install-recommends "${missing[@]}")
    # Bound only network operations; never interrupt dpkg in the middle of installation.
    if [[ "$source_file" != "$fallback" ]] && timeout 150 "${apt[@]}" --download-only "${install[@]}"; then
        "${apt[@]}" --no-download "${install[@]}"
        exit 0
    fi
    if timeout 180 "${apt[@]}" -o APT::Update::Error-Mode=any update &&
       timeout 180 "${apt[@]}" --download-only "${install[@]}"; then
        "${apt[@]}" --no-download "${install[@]}"
        exit 0
    fi
done
echo 'Could not retrieve the required Ubuntu packages from a signed archive.' >&2
exit 1
