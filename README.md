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
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --drums --synth --cycle --play   # instant beat
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --drums --bounce=beat.wav # batch render, no UI interaction
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" "--loop=assets/loops/Hip Hop Beat 90.wav"  # conformed to 120
```

Run the tests with `ctest --test-dir build`.

## Status

**Phase 0 and Phase 1 complete. Phase 2 in progress.**

- Edit modes and tools (Pro Tools layout): Shuffle, Slip, Spot and Grid modes
  (F1-F4; Grid has a value selector and Absolute/Relative toggle), and the
  Zoomer, Trimmer, Selector, Grabber and Smart Tool (F5-F9). Clips can be
  moved (also across compatible tracks), trimmed non-destructively at either
  edge, separated at the playhead or selection (Ctrl+E), duplicated (Ctrl+D),
  nudged by the grid value (, and .), deleted, or cleared from a time
  selection; every edit is one undo step, Shuffle re-packs the track. A time
  selection sets the Cycle range and the play start. Spot opens a dialog to
  type a bar|beat or seconds position. Alt+Z zooms to fit.
- Playlists and comping: every audio track can hold alternate playlists
  (takes). The P button in the header offers New, Duplicate, Switch To,
  Delete and Show Take Lanes. Recording with Cycle on is loop recording: each
  pass becomes a take, the last pass on the main playlist and the earlier
  ones as alternates, all from one continuous file. Show the take lanes,
  drag a time selection on an alternate and press Comp (or Ctrl+Alt+V) to
  copy that range into the main playlist, replacing what was there; Main
  swaps a whole take in.
- Groups and VCA masters: Ctrl+G creates a group from the selected track
  (Edit, Mix, or both; choose whether Volume, Mute, Solo, Pan and Record
  follow). Mix groups move member faders relatively in dB and propagate mute
  and solo; Edit groups extend time selections across members and apply clip
  moves, trims and deletes to same-start clips on member tracks. Group badges
  (a, b, c...) appear on strips and headers; click one to toggle, edit, leave
  or delete the group. A VCA Master track (from the + Track menu) has a fader,
  mute, solo and automation but no audio: assign tracks to it in their strip
  and its fader (and volume automation) scales theirs.
- Metering: right-click any strip meter to pick Sample Peak, RMS, Peak + RMS,
  VU (0 VU = -18 dBFS), or K-12 / K-14 / K-20 (RMS with 0 at -12/-14/-20
  dBFS). Meters have a clip indicator that holds until clicked, dynamics
  inserts show a gain-reduction bar, and the Master strip carries an ITU-R
  BS.1770 loudness readout: Momentary, Short-term, gated Integrated, Loudness
  Range and True Peak (4x oversampled). Right-click the master meter to reset.
- I/O Setup (Ctrl+Alt+I or the I/O... button): Input, Output and Bus tabs
  define named paths mapped to device channels (mono or stereo). Track inputs
  and outputs pick paths by name; an output path other than Main is a direct
  out that bypasses the master. Buses can be renamed.
- Automatic Delay Compensation: every insert reports its latency (the
  Compressor's Lookahead adds real latency) and each strip is delayed so all
  paths to the mix align, including tracks feeding an aux return. The mixer
  shows the applied delay per strip (dly, with the insert latency in
  brackets); Alt-click it to type a user offset. ADC can be switched off in
  I/O Setup.
- Automation: every track has an automation mode (Off, Read, Touch, Latch,
  Write) in its header and in the mixer strip. Lanes exist for volume, pan,
  mute, send levels and insert parameters. Move a fader, pan or send while
  playing in Touch/Latch/Write to record a pass (one undo step; Write drops to
  Latch afterwards, as in Pro Tools). In Read the mixer controls follow the
  lane. Switch a track's view from Clips to a lane to draw breakpoints: click
  to add, drag to move, right-click or Alt-click to delete. Volume ramps
  sample-accurately across each block.
- Mixer (X): a Pro Tools-style Mix window with one channel strip per track
  plus a Master strip. Each strip has 10 insert slots (5 shown) hosting the
  built-in EQ, Compressor, Delay and Reverb (click to edit the knobs in a
  callout, right-click to bypass, replace or remove), 5 sends to 8 stereo
  buses with pre/post-fader switching and level, a pan knob, a fader with a
  post-fader meter, mute/solo and output routing (Main or a bus). Aux Input
  tracks read a bus and can carry inserts and sends of their own. Every
  knob and fader gesture is one undo step, and the same strip pipeline runs
  live and in Bounce.
- Fades and clip gain on audio clips: drag the top corners of a clip with the
  Smart Tool to set fade in/out, Ctrl+drag a clip vertically for clip gain
  (or Ctrl+Shift+Up/Down in 0.5 dB steps), and Ctrl+F opens the Fades window
  to set length, shape (Standard, Equal Power, S-Curve) and gain for every
  selected clip at once (batch fades). Fades render in the same code path as
  live playback and bounce; separating a clip makes the cut hard on both
  sides; trims clamp the fades.

- Audio tracks: import files (open dialog, drag-and-drop, command line), shown
  as waveform clips on colour-coded tracks.
- Drum Machine tracks: a synthesised 16-pad kit, a 4-bar pattern clip with a
  starter beat, and a step-sequencer editor panel. Click a step to toggle it,
  Shift-click for a soft hit, drag to paint, click a pad name to audition it,
  drop an audio file on a pad row to replace its sample.
- Smart Controls (B): a macro-knob strip for the selected track. Every track
  has Volume and Pan (-3 dB centre-compensated pan law); synth tracks add
  Cutoff, Resonance, Filter Env, ADSR, Detune, Level and a Wave selector bound
  to the preset; drum tracks add levels for Kick, Snare, Clap/Rim, Hats, Toms,
  Cymbals, Perc and Sub. A knob drag is one undo step.
- Synth tracks (Ctrl+I): a polyphonic subtractive synth (PolyBLEP saw, square,
  triangle, sine; detuned second oscillator; state-variable low-pass with
  envelope; ADSR) with presets, driven by MIDI clips edited in a piano roll:
  click to add a note, drag to move, drag the right edge to resize, right-click
  or Delete to remove, click a key to audition. New synth tracks start with a
  two-bar arpeggio.
- Loop Library (L): a GarageBand-style browser over bundled loops plus
  `~/Music/Beat Maker/Loops` and any folders you add. Tempo, key and category
  are read from file names (with tempo estimated from length when missing).
  Click a loop to audition it, double-click to add it at the playhead, or drag
  it onto a track. Loops are conformed to the session tempo by varispeed
  resampling (pitch follows tempo until Phase 3 adds polyphonic stretching)
  and snapped to the beat grid.
- Recording: arm an audio track (R), pick its input, optionally enable input
  monitoring (I), then press Record. Takes are written as 24-bit WAV to
  `~/Music/Beat Maker/Audio Files` on a background thread and land on the
  track at the exact sample where recording began. Stop ends the take; Record
  again while playing punches out.
- Bounce to Disk (Ctrl+B): whole arrangement or the cycle range, to WAV, AIFF
  or FLAC at 16/24-bit or 32-bit float, with a configurable tail that lets
  drums ring out, trailing-silence trim, and optional normalisation. Renders
  offline on a background thread through the same graph code as playback, so
  the file matches what you heard. Clipping is reported.
- Transport with bar|beat|tick and time LCDs, and a Cycle mode that loops the
  arrangement sample-accurately.
- Tracks have mute/solo; every edit is an undoable command.

Keys: Space play/stop, R record, Return back to start, C cycle, L library, B Smart Controls, E editor panel,
Ctrl+Shift+D new drum track, Ctrl+I new synth track, Ctrl+B bounce, Ctrl+Z / Ctrl+Shift+Z undo/redo, Ctrl+O open,
Ctrl+wheel zoom, wheel scroll, click the ruler to locate.

Still to come in Phase 2: Trim automation and master automation, clip gain
breakpoints, Scrubber and Pencil tools. See PLAN.md §5.

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
is retired. Every source (clip, drum voice, synth, monitored input) is tagged
with a channel strip; the graph renders each strip's sources into a scratch
buffer, runs its inserts (stateful `Effect` instances owned by the model,
parameters immutable and copy-on-write), pre-fader sends, fader/pan, meter,
post-fader sends, then routes to the main mix or a bus; aux strips read a bus
afterwards; the master strip's inserts and fader feed the device. Synth tracks get a `Synth` slot in the graph keyed by track id;
MIDI notes carry their own gate length so no note-off has to be scheduled
across blocks, and all synths release on stop and at the cycle wrap. The `Recorder` receives every input block on the audio thread and
pushes it into per-track lock-free ring buffers that a background thread
flushes to WAV; start/stop hand the session across threads with an atomic
pointer plus a busy flag so teardown never races the callback. `Bouncer`
renders a snapshot through a private `Transport` + `AudioGraph`, so an offline
bounce is bit-identical to live playback of the same session.
