#!/usr/bin/env python3
"""Builds the user guide: regenerates the shortcut and preference tables from
the app (any build), then runs mkdocs build --strict.

Usage: python3 tools/build_docs.py [--app <path to Beat Maker>] [--mkdocs <path to mkdocs>]
"""
import argparse, glob, os, shutil, subprocess, sys

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ap = argparse.ArgumentParser()
ap.add_argument("--app")
ap.add_argument("--mkdocs", default=shutil.which("mkdocs"))
args = ap.parse_args()

app = args.app or next(iter(sorted(glob.glob(os.path.join(repo, "build*", "ui", "BeatMaker_artefacts", "*", "Beat Maker"))
                                  + glob.glob(os.path.join(repo, "build*", "ui", "BeatMaker_artefacts", "*", "Beat Maker.exe")))), None)
if app and os.path.exists(app):
    subprocess.run([app, "--dump-docs=" + os.path.join(repo, "docs", "reference"), "--quit"], check=True, cwd=repo, timeout=120)
else:
    print("no app build found; keeping the committed reference tables")
if not args.mkdocs:
    sys.exit("mkdocs not found: python3 -m venv .venv && .venv/bin/pip install mkdocs, then --mkdocs .venv/bin/mkdocs "
             "(on Windows: py -m venv .venv; .venv\\Scripts\\pip install mkdocs; --mkdocs .venv\\Scripts\\mkdocs.exe)")
subprocess.run([args.mkdocs, "build", "--strict"], check=True, cwd=repo)
print("site built in", os.path.join(repo, "site"))
