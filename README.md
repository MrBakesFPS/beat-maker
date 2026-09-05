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

Requires CMake 3.22+, Ninja, a C++20 compiler, and on Linux the usual JUCE
dependencies (alsa-lib, freetype2, libx11, libxrandr, libxinerama, libxcursor,
libxext, mesa, curl, webkit2gtk-4.1, gtk3, fontconfig). JUCE itself is fetched
automatically on first configure.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" "assets/loops/Drum Loop 120.wav"
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --drums --cycle --play   # instant beat
```

Run the tests with `ctest --test-dir build`.

## Status

**Phase 0 complete. Phase 1 in progress.**

- Audio tracks: import files (open dialog, drag-and-drop, command line), shown
  as waveform clips on colour-coded tracks.
- Drum Machine tracks: a synthesised 16-pad kit, a 4-bar pattern clip with a
  starter beat, and a step-sequencer editor panel. Click a step to toggle it,
  Shift-click for a soft hit, drag to paint, click a pad name to audition it,
  drop an audio file on a pad row to replace its sample.
- Recording: arm an audio track (R), pick its input, optionally enable input
  monitoring (I), then press Record. Takes are written as 24-bit WAV to
  `~/Music/Beat Maker/Audio Files` on a background thread and land on the
  track at the exact sample where recording began. Stop ends the take; Record
  again while playing punches out.
- Transport with bar|beat|tick and time LCDs, and a Cycle mode that loops the
  arrangement sample-accurately.
- Tracks have mute/solo; every edit is an undoable command.

Keys: Space play/stop, R record, Return back to start, C cycle, E editor panel,
Ctrl+D new drum track, Ctrl+Z / Ctrl+Shift+Z undo/redo, Ctrl+O open,
Ctrl+wheel zoom, wheel scroll, click the ruler to locate.

Still to come in Phase 1: the loop browser, piano roll, Smart Controls,
bounce. See PLAN.md §5.

## Architecture in one paragraph

`engine/` runs on the audio thread and never allocates or locks: the
`AudioGraph` mixes an immutable `RenderSnapshot` and advances the `Transport`.
`model/` is the undoable `Session` document; the UI only ever mutates it
through `Command` objects. Whenever the session changes, the message thread
builds a new `RenderSnapshot` and hands it to the graph through a lock-free
exchange; retired snapshots are freed back on the message thread. Step
patterns and drum kits are immutable and replaced copy-on-write; the graph
schedules pattern hits into a `DrumMachine` voice pool and kills any voice
whose kit is no longer referenced by the active snapshot before that snapshot
is retired. The `Recorder` receives every input block on the audio thread and
pushes it into per-track lock-free ring buffers that a background thread
flushes to WAV; start/stop hand the session across threads with an atomic
pointer plus a busy flag so teardown never races the callback.
