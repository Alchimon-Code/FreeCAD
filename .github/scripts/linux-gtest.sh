#!/usr/bin/env bash
# SPDX-License-Identifier: LGPL-2.1-or-later
# CI-only test dependency: same GoogleTest release as the Windows vcpkg baseline.
set -euo pipefail
src="$RUNNER_TEMP/gtest-source"
prefix="$RUNNER_TEMP/gtest-prefix"
mkdir -p "$src"
curl --fail --silent --show-error --location --retry 3 \
    https://codeload.github.com/google/googletest/tar.gz/52eb8108c5bdec04579160ae17225d66034bd723 \
    -o "$src/gtest.tar.gz"
(cd "$src" && echo '745c55415660044610f7fcd3af7a6420d5de16a7dbb9ebfe2e131275676232be  gtest.tar.gz' | sha256sum --check)
tar -xzf "$src/gtest.tar.gz" --strip-components=1 -C "$src"
cmake -S "$src" -B "$RUNNER_TEMP/gtest-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_GMOCK=OFF -DCMAKE_INSTALL_PREFIX="$prefix"
cmake --build "$RUNNER_TEMP/gtest-build" --target install --parallel 2
echo "GTest_ROOT=$prefix" >> "$GITHUB_ENV"
