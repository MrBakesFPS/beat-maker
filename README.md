# Beat Maker

A DAW with a GarageBand-style surface and Pro Tools-style depth. See [PLAN.md](PLAN.md) for the full plan.

## Layout

```
engine/       Real-time audio graph, transport, DSP, device I/O
model/        Undoable session document
ui/           Surface (simple) and Depth (pro) views on one component tree
plugins/      VST3 / CLAP / LV2 / AU hosting and sandbox
scripting/    Lua bindings
persistence/  Session format, templates, autosave, import/export
assets/       Bundled loops, presets, icons, fonts
tests/        Unit and golden-file DSP tests
docs/         Design and user documentation
```

## Building

```
cmake -S . -B build -G Ninja
cmake --build build
```

Phase 0 (JUCE integration, first playable WAV) is the current target. See the roadmap in PLAN.md §5.
