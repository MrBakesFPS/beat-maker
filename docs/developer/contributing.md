# Contributing and releases

## Reporting problems

Use Help > Report a Problem for a diagnostics file and attach it to an issue with the steps you took. Crash reports are in `~/.config/Beat Maker/Crash Reports`; the launch after a crash offers to copy the report. Nothing leaves your computer unless you attach it.

## Changes

- Keep the audio thread free of allocation and locks; see [Architecture](architecture.md).
- Every user-visible change is an undoable command, gets a test, and is reflected in this guide and the README.
- Run `ctest --test-dir build` and a smoke test of the feature before opening a pull request.
- The project is GPL-3.0-or-later; contributions are accepted under the same license.

## Releases

Releases follow the checklist in `RELEASING.md` in the repository: bump the version in the top-level CMake file, update `CHANGELOG.md`, build Release, run the tests and the smoke tests, build the docs, package with `tools/package.sh`, tag, and publish the tarball with the changelog entry.
