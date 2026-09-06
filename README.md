# Beat Maker

A DAW with a GarageBand-style surface and Pro Tools-style depth. See [PLAN.md](PLAN.md) for the full plan and the [user guide](docs/index.md) (`python3 tools/build_docs.py` builds it as a site; Help > User Guide opens it).

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
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --pre-roll=0.5 --post-roll=0.5 --punch=1,2 --record   # punch record 1-2 s
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --record-mode=QuickPunch --record --punch-at=1 --punch-at=2 --stop-at=3
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --template="Beat Making" --save=song.bmk        # new from template, save
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --session=song.bmk --marker-demo                # open, add sections/markers
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --pref=mixing.panDepth=3 --palette=zoom         # set a preference, open the palette
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --drums --play --sync=clock-out               # MIDI clock on the "Beat Maker Sync" port
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --synth --event-list --sync-window            # open the Event List and Sync windows
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --script=build_song.lua --bounce=song.wav      # build a session from Lua and render it
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --elastic-demo --stems=stems --quit            # one WAV per track
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --synth --import-session=song.bmk,4 --save=merged.bmk --quit   # import at 4 s
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" "--sample-project=Lo-fi Beat" --tour                # open a sample project, start the tour
"./build/ui/BeatMaker_artefacts/Debug/Beat Maker" --drums --synth --freeze=2 --bounce=frozen.wav # freeze track 2, then bounce
```

Run the tests with `ctest --test-dir build`. `tools/package.sh` makes a Release tarball (binary, assets, user guide, desktop entry, `install.sh`); `beat-maker --version` prints the version.

## Status

**Phases 0 to 4 complete; Phase 5 (polish and release) nearly so.** The status below is the feature list; the [user guide](docs/index.md) explains how to use it all.

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
  starter beat, and a step-sequencer editor panel drawn on the session
  timeline like the note editor: linked to the tracks' view by default (the
  steps sit under their clip, scrolling or zooming either view moves both),
  every repeat of a looping pattern shown with the later ones as ghosts, and
  Unroll (or the first edit in a repeat) writing the loop out so each bar is
  its own. Click a step to toggle it, Shift-click for a soft hit, drag to
  paint (one undo step per stroke); the Selector rubber-bands hits, the
  Grabber moves them, the Zoomer zooms, right-click clears, Ctrl-click adds
  to the selection, and a velocity lane edits the hits of the pad you last
  touched. Keys: Up/Down move hits to another pad, Ctrl+Up/Down velocity,
  Left/Right nudge a step, Delete, Ctrl+A/C/X/V/D. Painting past the clip's
  end extends the clip to the next bar. Click a pad name to audition it, drop
  an audio file on a pad row to replace its sample.
- Smart Controls (B): a macro-knob strip for the selected track. Every track
  has Volume and Pan (-3 dB centre-compensated pan law); instrument tracks add
  one knob per parameter the instrument describes (a Wave menu where it has
  one); drum tracks add levels for Kick, Snare, Clap/Rim, Hats, Toms, Cymbals,
  Perc and Sub. A knob drag is one undo step.
- Instrument tracks (Add Track > Instrument Track, or Ctrl+I for the Synth):
  six bundled instruments driven by MIDI clips edited in the note editor (the
  Sound menu lists the instrument's presets). New instrument tracks start with
  a two-bar arpeggio.
- Note editor: the piano roll is drawn on the session timeline and, with
  Link on (the default), follows the tracks' view: the same ruler and zoom,
  the notes sit under their clip, and scrolling or zooming either view moves
  both (Link off gives it its own view). A looping clip shows every repeat
  of its sequence, the later ones as ghosts; the first edit inside a ghost,
  or the Unroll button, writes the loop out so every bar of the clip is
  editable on its own, and a note added past the clip's end grows the clip.
  The piano roll shares the edit window's settings. Grid mode
  snaps to the toolbar's grid value (Relative snaps the movement), Slip is
  free, Spot (or a double-click) types a note's bar|beat|tick and length. The
  Zoomer zooms (click, Alt-click, drag a range), the Trimmer drags either edge,
  the Selector rubber-bands, the Grabber moves, the Pencil adds notes and
  paints velocities, and the Smart Tool does the right one by where you click
  (edges trim, body grabs, empty space adds a note or, dragged, selects). Notes
  can be multi-selected (Shift adds, Ctrl toggles, Ctrl+A all), moved or
  resized together, and edited from the velocity lane at the bottom. The
  keyboard vocabulary matches the tracks: Up/Down transpose (Shift an octave),
  Ctrl+Up/Down velocity, Left/Right or , . nudge by the grid, Delete, Ctrl+C/X/V
  copy, cut and paste at the insertion, Ctrl+D duplicate, Alt+Q quantize
  (Alt+Shift+Q lengths too), Ctrl+L legato, Tab next note, Alt+Z fit, plus the
  Commands Focus letters. Ctrl+Alt+N (or the Notes button in the toolbar)
  switches keyboard focus between the editor panel (notes or drums) and the
  tracks; the focused editor shows the accent outline, the toolbar hint
  changes with it, the Edit and View commands in the palette act on whichever
  has focus, and focus survives switching away from and back to the window.
  The editor panel's height is yours: drag the bar above it (Preferences >
  Display remembers it), and the note editor zooms vertically on its own,
  independent of the tracks: Alt+wheel over the notes or the - and + buttons
  set the pixels per semitone, the scrollbar at the right or a drag on the
  keyboard scrolls the pitch range. Ctrl+
  wheel zooms, Shift+wheel scrolls, clicking the ruler locates the playhead.
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
- Record modes (the Rec button in the transport bar): Normal records until
  Stop, or between the punch points when there is a time selection (the
  transport rolls from the pre-roll, records exactly the selection, and stops
  after the post-roll). QuickPunch rolls with the inputs captured and the
  Record button punches in and out as often as you like; each punch becomes a
  clip. TrackPunch does the same per track: arm the tracks, press Record to
  roll, then each track's R button punches that track in or out (amber =
  waiting, red = punched in). Loop makes every Cycle pass a take on its own
  playlist. Pre/Post sets pre-roll and post-roll in bars or seconds. The
  whole pass is always kept on disk, so punches are sample-accurate and never
  lose audio.
- Bounce to Disk (Ctrl+B): whole arrangement or the cycle range, to WAV, AIFF
  or FLAC at 16/24-bit or 32-bit float, with a configurable tail that lets
  drums ring out, trailing-silence trim, and optional normalisation. Renders
  offline on a background thread through the same graph code as playback, so
  the file matches what you heard. Clipping is reported.
- Sessions and templates (File... button, or Ctrl+N / Ctrl+O / Ctrl+S /
  Ctrl+Shift+S): a session is a `.bmk` folder holding `session.json` (every
  track, clip, pattern, MIDI sequence, instrument, insert with parameters and
  plugin state, send, automation lane, playlist, group, marker, I/O path and
  transport setting) plus an `Audio Files` folder for audio that only existed
  in memory, such as pencil edits. Audio kept outside the bundle is referenced
  by path and reported if missing. Elastic clips store their source file and
  stretch settings and are re-rendered on open, so a reopened session bounces
  byte-for-byte the same. Save As Template writes a `.bmkt` to
  `~/Music/Beat Maker/Templates`; New Session offers those alongside the
  built-in Empty, Beat Making, Songwriter and Podcast templates. Sessions with
  a file autosave to `Session File Backups` inside the bundle every three
  minutes.
- Memory locations and arrangement: the strip above the ruler holds point
  markers (M adds one at the playhead, numbered like Pro Tools memory
  locations; Alt+1..9 recalls by number; each can recall a selection and zoom)
  and sections (Shift+M turns the time selection into one). Right-click a
  section to move it earlier or later, duplicate it, or delete its time; the
  clips of every track move with it, as one undo step. Ctrl+5 opens the
  Memory Locations window.
- Preferences (Ctrl+,): Pro Tools-depth settings in Display, Operation,
  Editing, Mixing, Processing and MIDI categories with a search box across
  all of them, and every setting applies immediately: track height, pan depth
  (-2.5/-3/-4.5/-6 dB, through the engine's pan law), default meter type and
  automation mode for new tracks, latch record enable and solo latch,
  timeline insertion follows playback, auto-backup interval and count,
  record bit depth and audio files folder, default fade length and shape,
  loop conform mode, background render threshold, piano-roll velocities and
  more. Changed settings show a blue mark and can be reset per setting or per
  category; they live in `~/.config/Beat Maker/Beat Maker.preferences`.
- Command palette (Ctrl+Shift+P or Ctrl+K): type to find any command or
  setting, Enter runs it. Every shortcut in the app is a registered command,
  so the palette shows the key next to it.
- Commands Keyboard Focus (the a-z button, Ctrl+Alt+K): single letters run
  edit commands like Pro Tools: A/S trim start/end to the insertion, D/G fade
  in/out to the insertion, F default fades, B separate, H duplicate, X/C/V
  cut/copy/paste clips at the insertion, R/T zoom out/in, E zoom to the
  selection. With focus on, the panel toggles keep working through
  Ctrl+Shift+E/L/B/X and record through Ctrl+Space.
- MIDI Event List (Ctrl+Alt+E): every note of the selected instrument track
  in time order with bar|beat|tick start, note name, velocity, length and
  clip, all editable in place (type F#3 or a note number); click a row to
  locate, Delete removes selected events, Insert Note adds one at the
  playhead. Above the list, the track's Real-Time Properties (Quantize with
  grid and strength, Transpose, Velocity scale and offset, Delay, Duration)
  apply while playing and leave the stored notes untouched, like Pro Tools.
- Synchronization (Ctrl+2): send MIDI Beat Clock (24 ppqn, Start/Continue/
  Stop, Song Position Pointer on locate) or MIDI Time Code (quarter frames at
  24/25/29.97/30 fps, full frames on locate, session start offset) to a MIDI
  output, or chase incoming clock (tempo, start/stop, position) or MTC
  (locate and run). "Beat Maker Sync" is a virtual ALSA/CoreMIDI port other
  applications can connect to without hardware.
- Lua scripting (Script Console on Ctrl+Alt+L): a sandboxed Lua 5.4 with
  `beatmaker.transport` (play, stop, position, bpm, locate_bar...),
  `beatmaker.session` (tracks, add_track of any kind, gain/pan/mute/solo/arm,
  clips and their move/trim/delete/duplicate, markers and sections, inserts
  and their parameters by name, sends, MIDI notes, undo/redo) and
  `beatmaker.app` (status, run_command for any registered command, bounce,
  save, open, preferences, register_command). Every change is an undoable
  command like the UI's. Scripts saved to `~/Music/Beat Maker/Scripts`
  appear in the command palette as "Script: name"; scripts can register
  their own commands too. `--script=<file>` and `--lua=<code>` run at
  startup, which makes scripted smoke tests and batch bounces possible.
- Track freeze and commit (right-click a track header, or the Track commands
  in the palette): Freeze renders the track's clips, instrument and inserts
  to audio and plays that instead, with the fader, pan, sends and their
  automation still live; insert-parameter automation is baked in and the
  inserts' latency is removed from the render so it sits on the grid. A frozen
  track shows a FROZEN badge and its render as one read-only clip; Unfreeze
  brings everything back. Commit writes the same render to a new audio track
  after the source (named `<track>.cm`, with the source's fader, sends and
  automation copied) and mutes the source. Renders are written to the
  session's Audio Files folder, or `~/Music/Beat Maker/Freeze` for unsaved
  sessions, and survive save and reopen.
- Import Session Data (Shift+Alt+I, or File... > Import Session Data): open
  another session bundle, tick the tracks you want, and bring them in with
  their clips, instruments, inserts, sends, automation and playlists. Place
  them at their original time or at the playhead, as new tracks or appended
  to existing tracks with the same name, optionally with the source's memory
  locations and tempo. VCA links survive when the VCA master comes along.
  Everything arrives as one undo step.
- Export Stems (Ctrl+Alt+B, or File... > Export Stems): each chosen track
  bounced to its own file, post-fader with automation and sends, all the same
  length so they line up. Other tracks keep feeding the buses, so an aux
  return stem contains exactly the returns of the mix, and the stems sum back
  to the mix. Options: include aux returns in each stem, render through the
  master's inserts, cycle range only, file type, bit depth, tail.
- Onboarding: a Welcome window on first launch (templates, sample projects,
  tour, tutorials, shortcuts; switch it off there or in Preferences), a
  two-minute spotlight Tour of the transport, Library, tracks, edit tools,
  editor panel, Smart Controls, Mix window and command palette, Tutorials
  whose steps tick themselves off as you work in the session (with a "Do it"
  button per step), a searchable Keyboard Shortcuts window (Ctrl+/), and
  tooltips on every control. Three sample projects (Lo-fi Beat, Synth Sketch,
  Podcast Intro) are generated from the bundled loops and instruments into
  `~/Music/Beat Maker/Sessions/Sample Projects` the first time you open them.
- Accessibility: three themes in Preferences > Display (Dark, High Contrast
  with WCAG AAA contrast for all text, and Light; every view and the standard
  widgets follow the palette), an interface scale of 100 to 175%, screen
  reader names on controls and knobs, a track-and-selection description
  exposed to assistive technology and read on demand with Ctrl+Shift+/, and
  spoken status messages (switchable). Full keyboard navigation of the edit
  window: Up/Down select tracks, Left/Right move the playhead by the grid
  value, Shift with arrows extends the selection, Shift+Return selects a
  track's clips, Shift+M/S/R mute, solo or arm the selected track, and every
  command is reachable from the palette. The focused edit window shows an
  accent outline.
- Export AAF (Ctrl+Alt+A, or File... > Export AAF): the session as an
  Advanced Authoring Format file for Pro Tools, Media Composer, Nuendo,
  Resolve and anything else that reads AAF. One timeline slot per track
  (stereo tracks become .L and .R slots, as Pro Tools expects), clips as
  source clips with their positions and lengths, a timecode slot at 24, 25,
  29.97 or 30 fps, and memory locations as markers. Audio is embedded as
  16- or 24-bit PCM essence, or linked to WAV files written to a "<name>
  Media" folder beside the file. Consolidated mode renders each clip's gain,
  gain line and fades into its own audio so the other DAW hears exactly the
  session; whole-file mode keeps the full source files with trim handles but
  drops gain and fades. Instrument tracks are included once frozen. The
  writer is Beat Maker's own (Compound File Binary container plus the AAF
  stored format, with the standard dictionaries embedded), and its files
  are checked against two independent readers, pyaaf2 and LibAAF. OMF is not
  offered: Avid deprecated it and current DAWs import AAF.
- Crash reporting and recovery: if Beat Maker crashes, a signal handler
  writes a report (version, session path, audio device, the last 32 actions
  and a symbolised backtrace) using only signal-safe calls, to
  `~/.config/Beat Maker/Crash Reports`. The next launch shows the report,
  can copy it or open the folder, and offers the session's newest auto-backup
  when it is newer than the last save. Help > Report a Problem writes a
  diagnostics file (system, audio device, session summary, recent actions,
  the last crash report and the tail of the rolling log in
  `~/.config/Beat Maker/Logs`) to attach to an issue. Nothing is sent
  anywhere; the reports stay on your computer, and Preferences > Operation
  can switch them off.
- System Usage (Ctrl+Shift+U, or click the CPU readout in the transport bar):
  audio CPU load with a peak hold and a count of callbacks that overran their
  budget, the heaviest tracks (per-strip timing on the audio thread, so a
  reverb aux or a plugin shows up by name), session audio in RAM, and the
  audio cache, plus an Interface section: message-thread stalls and the
  operations that took the most time (paints, edits, snapshots, saves), so a
  laggy interface can be traced to its cause. `--profile-ui[=seconds]` prints
  the same profile and quits. The cache decodes each audio file once per sample rate and
  shares the buffer between every clip, pad and sample that uses it; loops you
  drop twice cost one decode, reopening a session reuses what is resident, and
  the least recently used files that nothing references go first when the
  budget (Preferences > Processing > Audio cache size, 2 GB by default) is
  exceeded. Audio in use is never dropped.
- Tempo and time signature: double-click the tempo LCD to type a BPM, drag it
  up or down (Shift for tenths), roll the wheel, or right-click it for 2/4 to
  7/4; Set Tempo... (Ctrl+Shift+T) is the dialog and Tap Tempo (Ctrl+Alt+T)
  averages your taps. A tempo change is one undo step in which clips, memory
  locations and automation keep their bar positions (tick-based, like
  GarageBand), the cycle range and playhead included; library loops are then
  re-conformed with Elastic Audio to the new tempo as a second step, with a
  progress window when there is a lot to render. Both behaviours can be
  switched off in Preferences > Editing. Scripts set the tempo through the
  same command.
- Transport with bar|beat|tick and time LCDs, and a Cycle mode that loops the
  arrangement sample-accurately.
- Tracks have mute/solo; every edit is an undoable command.

Keys: Space play/stop, R record, Return back to start, C cycle, L library, B Smart Controls, E editor panel,
Ctrl+Shift+D new drum track, Ctrl+I new Synth track, Ctrl+B bounce, Ctrl+Z / Ctrl+Shift+Z undo/redo, Ctrl+O open,
Ctrl+wheel zoom, wheel scroll, click the ruler to locate.

This is the 1.0.0-beta.1 feature set. `tools/package.sh` builds the beta tarball, [CHANGELOG.md](CHANGELOG.md) lists what is in it and [RELEASING.md](RELEASING.md) describes the beta program and the release checklist. 1.0 follows the beta feedback.

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
