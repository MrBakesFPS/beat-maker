# Changelog

All notable changes to Beat Maker. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Drum editor (step sequencer) drawn on the session timeline with the same link, ghost repeats, Unroll and clip extension as the note editor; multi-selection with rubber band, Grabber moves, a velocity lane, paint strokes as one undo step, and the shared keyboard vocabulary. The Editor button and Ctrl+Alt+N cover both editors; keyboard focus in an editor survives window activation.
- Note editor drawn on the session timeline, linked to the tracks' view (Link toggle), with looping clips showing every repeat (ghosts), Unroll to write a loop out, and clips that grow when a note is added past their end.
- Note editor rebuilt around the shared edit settings: modes (Grid with Relative, Slip, Spot), tools (Zoomer, Trimmer, Selector, Grabber, Pencil, Smart), multi-selection with rubber band, a velocity lane, zoom and scroll, a selection readout, and the track editor's keyboard vocabulary (transpose, velocity, nudge, quantize, legato, duplicate, copy and paste, Tab through notes). Ctrl+Alt+N and a Notes toolbar button switch keyboard focus between the note editor and the tracks; the Edit and View commands follow the focus. `--notes-focus` flag.
- Editable tempo and time signature: the tempo LCD accepts typing, dragging, the wheel and a right-click signature menu; Set Tempo... (Ctrl+Shift+T) and Tap Tempo (Ctrl+Alt+T). Tempo changes are undoable, keep clips, markers and automation on their bars, and re-conform library loops (both switchable in Preferences > Editing). `--tempo=<bpm>` flag; Lua `set_bpm` goes through the same command.

## [1.0.0-beta.1] - 2026-09-06

The first public beta. Everything below is new.

### Surface
- Audio, Drum Machine, Instrument, Aux Input and VCA Master tracks; waveform, pattern and MIDI clips.
- Loop Library with audition, drag to track and tempo conform; bundled loops plus your own folders.
- Smart Controls, step sequencer, piano roll, transport with bar|beat|tick and time LCDs, Cycle.
- Welcome window, spotlight Tour, self-completing Tutorials, Keyboard Shortcuts window, tooltips, three sample projects.

### Editing
- Shuffle, Slip, Spot and Grid modes; Zoomer, Trimmer, Selector, Grabber, Scrubber, Pencil and Smart Tool; TCE trimming.
- Fades (three shapes, batch Fades window), clip gain and clip gain breakpoints, separate, duplicate, nudge, clear.
- Playlists and comping, loop recording into takes, Edit and Mix groups, VCA masters.
- Memory locations, sections with move, duplicate and delete time; Commands Keyboard Focus.
- Elastic Audio (Polyphonic, Rhythmic, Monophonic, Varispeed; pitch; warp markers; Quantize to Grid), transient navigation, Beat Detective, background rendering of long stretches.

### Recording and sync
- Arm, input select, input monitoring; takes written on a background thread, sample-accurate placement.
- Normal (with punch range and pre/post-roll), QuickPunch, TrackPunch and Loop record modes.
- MIDI Beat Clock and MTC out, clock and MTC chase, a virtual "Beat Maker Sync" port.

### Mixing
- Mix window with 10 inserts, 5 sends, 8 buses, pan, fader, meter, mute/solo, routing, Master strip.
- 15 built-in effects plus Convolution Reverb and Pitch Correction; sidechain key inputs with Key Listen, also for hosted plugins.
- Automation (Off, Read, Touch, Latch, Write, Trim) on volume, pan, mute, sends and insert parameters, plus master automation.
- Meter types (Sample Peak, RMS, Peak + RMS, VU, K-12/14/20), gain reduction, BS.1770 loudness with true peak.
- I/O Setup with named paths and buses; automatic delay compensation with a user offset.
- Track freeze and commit; System Usage window (CPU, overruns, heaviest tracks, memory, audio cache).

### Instruments
- Synth, FM Synth, Wavetable, Sampler, Electric Piano and Bass, with presets and Smart Controls.
- MIDI Event List with in-place editing and Real-Time Properties.

### Plugins
- VST3, LV2 and AU hosting through JUCE and a native CLAP host; out-of-process scan validation with a persistent blacklist; latency reporting, automation, state in sessions, editors.

### Sessions and interchange
- `.bmk` session bundles with autosave; templates; Import Session Data; Bounce to Disk (WAV, AIFF, FLAC); Export Stems; AAF export (embedded or linked audio, consolidated or whole files, timecode, markers).

### Scripting, preferences, accessibility
- Sandboxed Lua 5.4 with transport, session and app APIs, a Script Console, scripts as palette commands, `--script` and `--lua`.
- Preferences in six categories with search and per-setting reset; command palette; a decoded-audio cache with a budget.
- Dark, High Contrast (WCAG AAA) and Light themes, interface scale, screen reader names and announcements, full keyboard navigation.

### Reliability
- Crash reports with breadcrumbs and backtraces, auto-backup recovery on the next launch, diagnostics for bug reports, a rolling log.

### Known limitations
- AAF files have been verified with pyaaf2 and LibAAF but not yet inside Pro Tools or Media Composer.
- Screen reader output depends on the platform accessibility backend; tested on Linux only.
- Linux is the supported platform for this beta; macOS builds are untested.
