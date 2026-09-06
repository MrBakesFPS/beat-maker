# Releasing Beat Maker

## Beta program

Betas are numbered `1.0.0-beta.N`. Each beta is a tarball for Linux x86_64 built by `tools/package.sh`, published with its changelog entry and SHA-256. Testers install with the bundled `install.sh`, use the app on their own material, and report through issues with a diagnostics file (Help > Report a Problem) or a crash report (offered on the launch after a crash). Nothing is collected automatically.

What we ask beta testers to try, in order: the sample projects; a session of their own with recording, editing and mixing; a save, reopen and bounce; an export (stems or AAF) into another DAW; and any plugins they rely on. The bug report template asks for the diagnostics file, the steps, and what was expected.

## Checklist for a release

1. `git status` clean on the release branch; all tests pass: `ctest --test-dir build`.
2. Update `CHANGELOG.md`: move Unreleased items under the new version with today's date. Keep the Known limitations honest.
3. Bump `BEATMAKER_VERSION_STRING` in `CMakeLists.txt` (and `project(... VERSION x.y.z)` for a final release).
4. Build the docs and read the pages that changed: `python3 tools/build_docs.py`.
5. Smoke tests on a Release build (see `docs/developer/building.md`): a sample project plays without overruns in System Usage; `--bounce=` of the Lo-fi Beat sample matches the previous release's bounce within the expected changes; `--export-aaf=` opens in pyaaf2 or aaftool; `--crash` writes a report and the next launch offers it.
6. `tools/package.sh` (runs the tests and builds the docs again). Unpack the tarball somewhere else and run `./beat-maker --version` and `./beat-maker "--sample-project=Lo-fi Beat" --bounce=/tmp/check.wav` from there, so assets resolve relative to the binary.
7. Commit, tag `v<version>`, push the tag.
8. Publish the tarball and its `.sha256` with the changelog entry as the release notes.
9. Announce to testers with the three things you most want tried this round.
