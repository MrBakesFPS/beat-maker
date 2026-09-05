# Beat Maker

A DAW with a GarageBand-style surface and Pro Tools-style depth. See [PLAN.md](PLAN.md) for the full plan.

Beat Maker is free software under the GNU General Public License v3.0 or later
(see [LICENSE](LICENSE)). Time stretching and pitch shifting use the
[Rubber Band Library](https://breakfastquay.com/rubberband/) under the GPL.

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
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --instrument=bass --instrument=electricpiano   # any bundled instrument
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --instrument=sampler --sample=hit.wav          # Sampler with a sound loaded
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --drums --bounce=beat.wav # batch render, no UI interaction
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" "--loop=assets/loops/Hip Hop Beat 90.wav"  # stretched to 120, pitch kept
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --elastic-demo --bounce=elastic.wav          # conform, quantize, pitch shift, TCE
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --sidechain-demo --mixer                       # kick-keyed compressor on a pad
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --convolution-demo --bounce=cathedral.wav      # e-piano in a Cathedral IR
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --pitch-demo --mixer                           # sharp bass line tuned back to A minor
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --beat-detective-demo                          # slice, swing-conform and crossfade a loop
```

Run the tests with `ctest --test-dir build`.

## Status

**Phases 0, 1 and 2 complete. Phase 3 in progress.**

- Built-in effects suite: 7-band EQ (HPF, low shelf, three parametric peaks,
  high shelf, LPF) with a live frequency-response display, Compressor with
  lookahead, Limiter (2 ms lookahead, ceiling), Gate/Expander (threshold,
  ratio, attack, hold, release, range), De-esser (split-band), Delay
  (digital, tape with saturated feedback, ping-pong), Reverb, Chorus,
  Flanger, Phaser, Saturation (soft, hard, tube), Amp Sim (drive, tone stack,
  presence, cabinet) and Utility (gain, phase, width, mono). Dynamics show a
  gain-reduction bar on their slot; every parameter is automatable.
- Plugin hosting: VST3, LV2 and CLAP on Linux (AU on macOS). VST3/LV2/AU go
  through JUCE; CLAP has its own host (plugins/ClapHost) that loads .clap
  libraries from ~/.clap, /usr/lib/clap, /usr/local/lib/clap and $CLAP_PATH,
  and presents each plugin to the same machinery: parameters, state, latency,
  audio ports (a second input port becomes the sidechain bus) and an
  X11-embedded editor when the plugin has one. From any insert slot choose
  Plugins > Scan for Plugins..., then pick a plugin by format. Scanning
  validates each plugin in a separate process (the app relaunches itself with
  --scan-plugin=...), so a plugin that crashes or hangs is blacklisted instead
  of taking the host down; the list persists in ~/.config/Beat Maker/plugins.xml.
  Loaded plugins report their latency to delay compensation, expose their
  parameters to automation lanes, and open their own editor window (or a
  generic one) when clicked. Plugins run in-process once loaded. The CLAP host
  is verified against a fake .clap built by the test suite; the native-GUI
  embedding path has not yet been exercised with a real plugin.
  --insert-plugin=<name> inserts a known plugin from the command line.

- Edit modes and tools (Pro Tools layout): Shuffle, Slip, Spot and Grid modes
  (F1-F4; Grid has a value selector and Absolute/Relative toggle), and the
  Zoomer, Trimmer, Selector, Grabber, Scrubber, Pencil and Smart Tool
  (F5-F11). The Scrubber drags the audio under the cursor through the track's
  own strip, moving the playhead with it. The Pencil redraws waveform samples
  once you zoom in far enough to see them (clips draw real samples at high
  zoom); edits are non-destructive copies of the clip audio, one undo step
  per stroke, and the clip is marked (edited). Clips can be
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
  sample-accurately across each block. The Master strip has the same modes
  and a volume lane. Trim mode adjusts existing volume automation relatively:
  while playing, fader moves are recorded as a trim pass and baked into the
  lane on stop (existing breakpoints scaled, the trim curve added, untrimmed
  data resumed after the pass); while stopped, a fader move scales the whole
  lane. Trim faders spring back and show the offset while you hold them.
- Mixer (X): a Pro Tools-style Mix window with one channel strip per track
  plus a Master strip. Each strip has 10 insert slots (5 shown) hosting the
  built-in effects or hosted plugins (click to edit the knobs in a
  callout, right-click to bypass, replace or remove), 5 sends to 8 stereo
  buses with pre/post-fader switching and level, a pan knob, a fader with a
  post-fader meter, mute/solo and output routing (Main or a bus). Aux Input
  tracks read a bus and can carry inserts and sends of their own. Every
  knob and fader gesture is one undo step, and the same strip pipeline runs
  live and in Bounce.
- Sidechain routing: the Compressor, Gate/Expander and De-esser editors have
  a Key input menu (Internal or any of the 8 buses) and a Key Listen button.
  Send the kick pre-fader to a bus, key the pad's compressor from that bus,
  and it pumps. Strips that feed a key bus are always processed before the
  strips they key, so the sidechain has no latency; a keyed insert shows
  [key] on its slot. Hosted plugins with a sidechain input bus (a
  compressor's "Sidechain" or "Key" bus) get the same Key menu: the bus is
  enabled as stereo (or mono) when the plugin is loaded and the key is
  copied into it every block.
- Convolution Reverb: six bundled spaces (Concert Hall, Chamber, Room, Plate,
  Ambience, Cathedral) or Load IR... for any audio file, with pre-delay,
  decay trim, low/high cut, width and mix. Responses are (re)built on the
  message thread and swapped in without touching the audio thread.
- Pitch Correction: an insert that detects the pitch of a monophonic source
  (YIN detector) and pulls it to the nearest note of a Key and Scale
  (Chromatic, Major, Minor, pentatonics) through a Rubber Band real-time
  shifter. Retune Speed sets how fast it glides (0 ms = hard tune), Amount
  how far, Transpose adds an interval, Formant: Preserve keeps the voice's
  character, and Mix blends the delayed dry signal. The editor shows the
  detected note, its cents offset and the target note live; the shifter's
  latency is reported to delay compensation.
- Clip gain breakpoints: switch a track's view to Clip Gain to see each
  clip's gain line. Click to add a breakpoint, drag to move it (dB readout),
  right-click or Alt-click to delete. Breakpoint times live in the audio's own
  sample positions, so the line stays glued to the sound through trims,
  splits and moves, and it multiplies with the static clip gain and fades.
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
  has Volume and Pan (-3 dB centre-compensated pan law); instrument tracks add
  one knob per parameter the instrument describes (a Wave menu where it has
  one); drum tracks add levels for Kick, Snare, Clap/Rim, Hats, Toms, Cymbals,
  Perc and Sub. A knob drag is one undo step.
- Instrument tracks (Add Track > Instrument Track, or Ctrl+I for the Synth):
  six bundled instruments driven by MIDI clips edited in a piano roll (click to
  add a note, drag to move, drag the right edge to resize, right-click or
  Delete to remove, click a key to audition; the Sound menu lists the
  instrument's presets). New instrument tracks start with a two-bar arpeggio.
  - **Synth**: polyphonic subtractive (PolyBLEP saw/square/triangle/sine,
    detuned second oscillator, state-variable low-pass with envelope, ADSR).
  - **FM Synth**: two-operator FM with ratio, decaying index and feedback
    (bells, keys, basses).
  - **Wavetable**: two detuned oscillators morphing across band-limited tables
    with a filter envelope.
  - **Sampler**: drop an audio file onto the track to load it; plays it pitched
    around C3 with tune, one-shot/loop, ADSR and a filter. Presets keep the
    loaded sample.
  - **Electric Piano**: tine model with velocity-dependent brightness, an
    inharmonic bell partial, per-note decay and tremolo.
  - **Bass**: monophonic, last-note priority, legato filter retrigger, glide,
    sub oscillator, filter envelope and drive.
- Loop Library (L): a GarageBand-style browser over bundled loops plus
  `~/Music/Beat Maker/Loops` and any folders you add. Tempo, key and category
  are read from file names (with tempo estimated from length when missing).
  Click a loop to audition it, double-click to add it at the playhead, or drag
  it onto a track. Loops are conformed to the session tempo with Elastic
  Audio (Rhythmic for drum loops, Polyphonic for everything else, so pitch
  stays put), remember their source tempo, and snap to the beat grid.
- Elastic Audio (right-click an audio clip): Polyphonic, Rhythmic, Monophonic
  and Varispeed modes, pitch shift in semitones, Conform to Session Tempo,
  warp markers (Add Warp Marker Here, then drag the orange handles at the top
  of the waveform; Alt-click removes one), Quantize to Grid (Alt+Q, uses the
  grid value; every transient gets a warp marker pulled to the nearest line),
  and Reset. The TCE toggle in the toolbar turns the Trimmer into a
  Time Compression/Expansion trimmer: dragging an edge stretches the clip to
  the new length instead of revealing or hiding audio. Stretches are rendered
  through the Rubber Band R3 engine and stay non-destructive: the clip keeps
  its original audio, so modes, ratios and markers can be changed or undone.
  Clips longer than 8 seconds render on a background thread behind a
  cancellable progress window; the result is applied only if the clip has
  not been edited meanwhile.
- Transient tools (Beat Detective-style): Tab / Shift+Tab jump the playhead to
  the next / previous transient of the selected audio track (or all audio
  tracks when none is selected), and Separate at Transients in the clip menu
  splits a clip at every detected hit as one undo step.
- Beat Detective (Ctrl+8, or from the clip menu): a window with Sensitivity,
  Strength, Exclude Within, Swing, smoothing mode and crossfade length, and
  the three steps on the selected clips (or the selected track): Separate at
  transients, Clip Conform (each slice's start moves toward the nearest line
  of the edit grid; Swing delays every second line, 100% being the triplet
  position), and Edit Smoothing (fill gaps by extending each slice to the
  next, optionally overlapping with equal-power crossfades). Do All runs the
  three in a row.
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
Ctrl+Shift+D new drum track, Ctrl+I new Synth track, Ctrl+B bounce, Ctrl+Z / Ctrl+Shift+Z undo/redo, Ctrl+O open,
Ctrl+wheel zoom, wheel scroll, click the ruler to locate.

Phase 3 is complete. Next is Phase 4 (record modes, MIDI event list and
sync, memory locations, arrangement track, templates, deep preferences,
command palette, scripting). See PLAN.md §5.

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
afterwards; the master strip's inserts and fader feed the device. Instrument tracks own a stateful `Instrument` instance
(like an `Effect`) whose immutable `InstrumentParams` are swapped copy-on-write; the snapshot carries both and the graph binds
them to slots keyed by track id, silencing an instance the moment it leaves the snapshot. MIDI notes carry their own gate
length so no note-off has to be scheduled across blocks, and all instruments release on stop and at the cycle wrap. A bounce
renders through fresh instrument and built-in effect instances so the live audio thread never shares DSP state with it. The `Recorder` receives every input block on the audio thread and
pushes it into per-track lock-free ring buffers that a background thread
flushes to WAV; start/stop hand the session across threads with an atomic
pointer plus a busy flag so teardown never races the callback. `Bouncer`
renders a snapshot through a private `Transport` + `AudioGraph`, so an offline
bounce is bit-identical to live playback of the same session. Elastic Audio is
offline too: `engine::TimeStretch` renders a clip's original audio through a
`StretchSpec` (mode, ratio, pitch, warp markers as a Rubber Band key-frame
map) and `SetClipElasticCommand` swaps the rendered buffer in, remapping the
clip's offset, length, fades and gain line between the old and new renderings.
