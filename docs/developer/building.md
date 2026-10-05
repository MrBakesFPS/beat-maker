# Building and testing

## Build

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build
```

On Windows, run these from a *Developer PowerShell for VS* (Visual Studio 2022+ with Desktop development with C++), and put the build directory outside a OneDrive folder, e.g. `-B $env:LOCALAPPDATA\BeatMaker\build`. MSVC builds use the static runtime (no Visual C++ redistributable needed), `/utf-8` and `/bigobj`, and write a `.pdb` for Release too so crash backtraces have names.

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

The site lands in `site/`; Help > User Guide opens it when it exists. On Windows use `py -m venv .venv; .venv\Scripts\pip install mkdocs` and pass `--mkdocs .venv\Scripts\mkdocs.exe`.

## Packaging

`tools/package.sh` makes the Linux tarball. `tools/package.ps1` makes the Windows installer: it builds Release, runs the tests, builds the docs when mkdocs is available, stages `dist\Beat Maker-<version>-windows-x64\` (the exe, its `.pdb`, `assets\`, `docs\`, license and notes) and compiles `packaging\windows\beat-maker.iss` with Inno Setup into `dist\Beat Maker-<version>-windows-x64-setup.exe`. The app finds `assets\` and `docs\` next to its own executable, so the staged folder also runs as is.
