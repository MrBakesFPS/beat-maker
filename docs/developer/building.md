# Building and testing

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

The first configure fetches JUCE, Rubber Band, the CLAP headers, Lua and Catch2. Use `-DCMAKE_BUILD_TYPE=Release` for a build to make music with; keep a Debug build for development. `JUCE_USE_SIMD` is off on the engine because JUCE's SIMD register template does not compile on current GCC.

## Tests

`tests/` holds Catch2 tests for every module, including offline render tests that drive the same `AudioGraph` code the device callback uses, a CLAP host test against a fake `.clap` built by the suite, AAF export tests with a small structured-storage reader, and the crash reporter. Run one test with `ctest --test-dir build -R <pattern>`; names with commas need `-R`.

## Smoke tests

The app's command-line flags make it scriptable for end-to-end checks without touching the UI: build a session with demo or content flags, then `--bounce=` renders it and `--quit` exits. Compare bounces to check that a change is audibly neutral (a frozen track against its live version, a reopened session against the original). Dialog flags open a window for a screenshot. See the [command-line reference](../reference/command-line.md).

## Documentation

The guide is MkDocs. The shortcut and preference tables come from the app:

```
python3 -m venv .venv && .venv/bin/pip install mkdocs
python3 tools/build_docs.py          # runs --dump-docs, then mkdocs build --strict
```

The site lands in `site/`; Help > User Guide opens it when it exists.
