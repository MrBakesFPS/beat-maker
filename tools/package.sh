#!/usr/bin/env bash
# Builds a Release binary, runs the tests, builds the docs (if mkdocs is
# available) and packages Beat Maker as a tarball for Linux x86_64:
#   Beat Maker-<version>-linux-x86_64/
#     beat-maker            launcher (sets the working directory-independent paths)
#     bin/Beat Maker        the app
#     assets/               bundled loops
#     docs/                 the user guide as a site (when built)
#     share/                desktop entry and icon; install.sh puts them in ~/.local
#     LICENSE README.md CHANGELOG.md
# Usage: tools/package.sh [--skip-tests] [--mkdocs <path>]
set -euo pipefail
repo="$(cd "$(dirname "$0")/.." && pwd)"
skip_tests=0; mkdocs="$(command -v mkdocs || true)"
while [ $# -gt 0 ]; do case "$1" in --skip-tests) skip_tests=1;; --mkdocs) mkdocs="$2"; shift;; *) echo "unknown option $1" >&2; exit 2;; esac; shift; done

version="$(sed -n 's/^set(BEATMAKER_VERSION_STRING "\(.*\)").*/\1/p' "$repo/CMakeLists.txt")"
name="Beat Maker-$version-linux-x86_64"
build="$repo/build-release"
cmake -S "$repo" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release > /dev/null
cmake --build "$build"
if [ "$skip_tests" = 0 ]; then ctest --test-dir "$build" --output-on-failure -j "$(nproc)" | tail -3; fi
if [ -n "$mkdocs" ]; then python3 "$repo/tools/build_docs.py" --app "$build/ui/BeatMaker_artefacts/Release/Beat Maker" --mkdocs "$mkdocs" > /dev/null; fi

stage="$repo/dist/$name"
rm -rf "$stage"; mkdir -p "$stage/bin" "$stage/share/applications" "$stage/share/icons/hicolor/scalable/apps"
cp "$build/ui/BeatMaker_artefacts/Release/Beat Maker" "$stage/bin/"
strip --strip-debug "$stage/bin/Beat Maker" 2>/dev/null || true   # keep the symbol table for crash backtraces
cp -r "$repo/assets" "$stage/assets"
[ -d "$repo/site" ] && cp -r "$repo/site" "$stage/docs"
cp "$repo/LICENSE" "$repo/README.md" "$repo/CHANGELOG.md" "$stage/"
cp "$repo/packaging/beat-maker.desktop" "$stage/share/applications/"
cp "$repo/assets/icons/beat-maker.svg" "$stage/share/icons/hicolor/scalable/apps/"
cat > "$stage/beat-maker" <<'LAUNCH'
#!/usr/bin/env bash
# Launcher: the app finds assets/ and docs/ relative to its own binary.
exec "$(dirname "$(readlink -f "$0")")/bin/Beat Maker" "$@"
LAUNCH
cat > "$stage/install.sh" <<'INSTALL'
#!/usr/bin/env bash
# Adds a desktop entry and icon for this folder's Beat Maker to the current user's menus.
set -e
here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p ~/.local/share/applications ~/.local/share/icons/hicolor/scalable/apps ~/.local/bin
sed "s|^Exec=beat-maker|Exec=\"$here/beat-maker\"|" "$here/share/applications/beat-maker.desktop" > ~/.local/share/applications/beat-maker.desktop
cp "$here/share/icons/hicolor/scalable/apps/beat-maker.svg" ~/.local/share/icons/hicolor/scalable/apps/
ln -sf "$here/beat-maker" ~/.local/bin/beat-maker
update-desktop-database ~/.local/share/applications 2>/dev/null || true
echo "Installed: Beat Maker is in your application menu and on the PATH as beat-maker."
INSTALL
chmod +x "$stage/beat-maker" "$stage/install.sh" "$stage/bin/Beat Maker"
(cd "$repo/dist" && tar czf "$name.tar.gz" "$name" && sha256sum "$name.tar.gz" > "$name.tar.gz.sha256")
echo "Packaged $repo/dist/$name.tar.gz"
cat "$repo/dist/$name.tar.gz.sha256"
