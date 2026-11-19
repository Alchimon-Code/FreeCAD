**English** · [Русский](CONTRIBUTING.ru.md)

# Contributing to PlaneDraft

Start with a drawing task and a repeatable example. For a bug, include the preset, OS, compiler/Qt version, input commands and expected result. For rendering, include the GPU, driver and display scale. Please share only drawings you have permission to redistribute.

1. Build using the [quick start](README.md#quick-start). Change the narrowest module that owns the behaviour: `core/` has no Qt dependency, and native plugins must not rely on private C++ classes.
2. Add a regression test. For an edit, check the geometry, stable IDs, spatial query and Undo/Redo. Test invalid input without losing an existing redo branch. For I/O, test with an independent reader as well as our own writer. For ABI changes load an actual shared module and keep the v1 tests passing.
3. Format C and C++ using `.clang-format` (version 18 baseline). Translate new user-visible strings using `tr()` and `cmake --build --preset windows-msvc --target update_translations`. Check that switching language keeps an active command and that `_` global command names still work.
4. Submit the change with its test commands and limitations. UI changes should include a real screenshot; performance claims need a scene, hardware and measurement method. Keep build trees, downloaded SDKs, generated `.qm` files and binary release assets out of Git.

## Tests beyond the default preset

```sh
# Full GUI tests need a working desktop GL 4.5 context (a software Mesa/Xvfb session is fine).
python -m pip install -r tests/requirements-audit.txt   # in a virtual environment
cmake --preset linux-release -DPLANEDRAFT_TEST_GL=ON -DPLANEDRAFT_TEST_EXCHANGE_AUDIT=ON
cmake --build --preset linux-release --parallel
ctest --preset linux-release
QT_SCALE_FACTOR=1.5 ctest --preset linux-release -R '^viewport_fbo$'
QT_SCALE_FACTOR=2 ctest --preset linux-release -R '^viewport_fbo$'

cmake --preset linux-sanitize -DPLANEDRAFT_TEST_EXCHANGE_AUDIT=ON -DPD_ENABLE_LIBREDWG=ON
cmake --build --preset linux-sanitize --parallel
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1 ctest --preset linux-sanitize
python tools/check_docs.py
```

The [CI definition](.github/workflows/ci.yml) and [Windows packaging instructions](packaging/windows/README.md) describe the MSVC jobs. Run installer tests only in a disposable profile. Hosted smoke tests verify loading and packaging, not a hardware OpenGL driver.

## Licensing

Contributions follow the license of the component ([host](LICENSE), [SDK and examples](LICENSES/MIT.txt)). Identify upstream source, version, license and inclusion in the runtime for any new dependency or asset. Update the baseline, CI checkout and notices together. Don't copy closed CAD SDKs, fonts, templates or customer data. Review [distribution obligations](docs/LICENSING.md) and the actual package inventory before publishing binaries.
