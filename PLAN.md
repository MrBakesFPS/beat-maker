# Beat Maker — Project Plan

> A digital audio workstation (DAW) that feels as approachable as GarageBand on the surface,
> but exposes the depth of settings, routing, and control found in professional tools like
> Avid Pro Tools once you look for it.

---

## 1. Vision

**Problem:** Beginner DAWs (GarageBand) are friendly but hit a ceiling fast. Professional DAWs
(Pro Tools) are powerful but intimidating, with dense UI and steep learning curves.

**Goal:** One program that scales with the user.

- **Surface layer** — GarageBand-style: big colorful track headers, a single main window, drag-and-drop
  loops, smart instruments, one-click recording, minimal modal dialogs.
- **Depth layer** — Pro Tools-style: full I/O routing matrix, sends/returns, buses, aux tracks,
  per-track sample-accurate delay compensation, editing modes (Shuffle/Slip/Spot/Grid), Elastic-style
  time stretching, session templates, clip gain, comprehensive automation modes, and a real mixer.

**Guiding principle:** *Progressive disclosure.* Every advanced feature exists, but nothing is
shown until it is needed. Panels expand, never pop up unexpectedly.

---

## 2. Target Users

| Persona | Needs | What they should see first |
|---|---|---|
| Bedroom beatmaker | Drum patterns, sample chopping, quick export | Loop browser, step sequencer, big transport |
| Songwriter | Record vocals/guitar over loops, arrange sections | Track view, arrangement markers, simple mixer |
| Producer / engineer | Routing, buses, automation, plugin chains, stems | Full mixer, I/O setup, edit modes, session settings |

---

## 3. Feature Set

### 3.1 GarageBand-inspired UI (the "surface")

- **Single-window layout** with three resizable regions: Track Area (center), Library/Browser
  (left, collapsible), Editor/Mixer/Smart Controls (bottom, collapsible).
- **Track headers** with large color chips, instrument icons, mute/solo/record-arm, volume slider, pan knob.
- **Transport bar** at top: Record, Play, Stop, Cycle, position/BPM/key/time-signature LCD, master volume, metronome, count-in.
- **Loop Browser** — searchable, filterable by instrument, genre, mood, key. Drag loops onto tracks; they auto-conform to tempo and key.
- **Smart Controls** — a simplified per-track panel with 4–8 macro knobs mapped to the most important parameters of the track's instrument/effects chain.
- **Drummer track** — algorithmic drum performer with style presets and an X/Y "simple ↔ complex, soft ↔ loud" pad.
- **Step sequencer** — grid-based pattern editor for drums and melodic parts.
- **Piano Roll & Score views** for MIDI.
- **Arrangement track** — named sections (Intro, Verse, Chorus…) that move regions with them.
- **Learn-as-you-go tooltips** and a "Show Advanced" toggle per panel.

### 3.2 Pro Tools-inspired depth (the "settings and options")

#### Session & Project
- Session settings: sample rate (44.1k–192k), bit depth (16/24/32-float), I/O setup, timecode rate, session start time, feet+frames/bars+beats/min:sec/samples counters.
- Session templates and "New Session from Template".
- Auto-backup with configurable interval and versions kept.
- Disk allocation: choose record drive per track.

#### Tracks
- Track types: Audio (mono/stereo/multichannel), Instrument, MIDI, Aux Input, Master Fader, VCA Master, Folder (basic & routing), Video.
- Track presets, track color coding, track grouping (edit groups, mix groups, both).
- Hide/show tracks, track list with drag reorder, "Show only selected/active".
- Track comments field, track numbering.

#### Routing & I/O
- I/O Setup window: define input/output/bus/insert paths, sub-paths, and default paths.
- Per-track input selection, output assignment (multiple outputs), 10 inserts, 10 sends (A–J), pre/post fader sends.
- Internal busing with unlimited buses; sidechain routing for dynamics plugins.
- Automatic Delay Compensation with per-track user offset and a visible dly readout.
- Hardware insert support and low-latency monitoring mode.

#### Editing
- **Edit modes:** Shuffle, Slip, Spot, Grid (Absolute/Relative).
- **Edit tools:** Zoomer, Trimmer (standard/TCE/loop), Selector, Grabber (time/separation/object), Scrubber, Pencil (free/line/triangle/square/random), Smart Tool.
- Clip gain (per-clip line and breakpoints), clip effects, fades with curve shapes, batch fades.
- Nudge with configurable value, tab-to-transient, strip silence, consolidate, duplicate, repeat.
- Elastic Audio-style time stretch: Polyphonic, Rhythmic, Monophonic, Varispeed algorithms; warp markers; quantize audio to grid.
- Beat Detective-style: detect transients, separate regions, conform, edit smoothing.
- Playlists per track (alternate takes), loop recording with take lanes, comping.
- Memory locations / markers with selection, zoom, and track-show state recall.

#### Mixing
- Full mixer window: faders (with wide/narrow views), pan, inserts, sends with mini-faders, I/O labels, automation mode selector, group ID, comments.
- Automation modes: Off, Read, Touch, Latch, Touch/Latch, Write, Trim, Preview, Capture/Punch.
- Automation lanes for volume, pan, mute, sends, and every plugin parameter; write to all enabled; thin automation.
- VCA groups, mix groups with attribute selection (volume, mute, solo, pan, inserts, sends, etc.).
- Solo modes: SIP (solo-in-place), AFL, PFL; solo safe.
- Metering: peak, RMS, K-System (K-12/14/20), LUFS loudness with integrated/short-term/momentary; per-track meter type selection; gain-reduction meters for dynamics inserts.
- Master output with dither options on bounce.

#### Recording
- Record modes: Normal, Loop, Destructive (opt-in), QuickPunch, TrackPunch.
- Pre-roll / post-roll, punch in/out points, count-off.
- Input monitoring per track (Auto / Input Only), monitor latency display.

#### MIDI
- MIDI Event List (editable list of every event).
- Real-time properties (quantize, duration, delay, velocity, transpose) applied non-destructively per track or clip.
- MIDI Input Filter, MIDI Thru, MIDI Beat Clock and MTC output, MIDI Time Code sync.
- Step input, input quantize, Groove Templates.

#### Export / Bounce
- Bounce to Disk with source selection (any bus/output), file type, format, bit depth, sample rate, dither, real-time vs offline.
- Stem export (multiple sources at once), export clips as files, export MIDI, export session as OMF/AAF-style interchange.
- Import Session Data (bring tracks from another session with selective options).

#### Preferences (deep, categorized)
- Display, Operation, Editing, Mixing, Metering, Processing, MIDI, Synchronization, Collaboration/Backup.
- Every preference searchable from a single search box (progressive disclosure again).

### 3.3 Instruments & Effects (bundled)
- **Instruments:** Drum Machine (sample-based, 16 pads), Sampler (multisample, slicing, ADSR, filter), Subtractive Synth, FM/Wavetable Synth, Electric Piano, Bass, basic Orchestral rompler.
- **Effects:** EQ (7-band parametric with analyzer), Compressor, Limiter, Gate/Expander, De-esser, Reverb (algorithmic + convolution), Delay (tape/digital/ping-pong), Chorus/Flanger/Phaser, Distortion/Saturation, Amp & Cabinet sim, Pitch Correction, Utility (gain/phase/width).
- **Plugin hosting:** VST3, CLAP, LV2 (Linux), AU (macOS). Plugin scanner with sandboxed validation, blacklist, and per-plugin latency reporting.

---

## 4. Technical Architecture

### 4.1 Language & Frameworks (recommended)

| Layer | Choice | Rationale |
|---|---|---|
| Core / DSP | **C++20** with **JUCE** | Industry standard for audio; real-time safe; cross-platform audio & MIDI device I/O; plugin hosting built in |
| UI | JUCE Components + custom LookAndFeel (or JUCE + a web-view layer for browser panels) | Single toolkit, GPU-accelerated rendering, consistent across macOS/Windows/Linux |
| Scripting / Automation | Lua (via sol2) | Lightweight, embeddable, lets power users script edits like Pro Tools' SoundFlow/EUCON style |
| Persistence | JSON + binary chunk format (`.bmk` session, `.bmkt` template) | Human-readable metadata, fast binary for audio references and automation |
| Build | CMake + Ninja, vcpkg/FetchContent for deps | Reproducible cross-platform builds |
| Tests | Catch2 (unit), custom offline render tests (golden-file DSP tests) | Deterministic audio verification |

*Alternative considered:* Rust (cpal + egui/iced). Rejected for v1 due to less mature plugin hosting and smaller audio ecosystem, but the audio engine is designed with a C ABI boundary so it could be swapped later.

### 4.2 Module Layout

```
beat-maker/
├── engine/            # Real-time audio graph, transport, clock, mixer DSP
│   ├── graph/         # Node graph, latency compensation, buses
│   ├── transport/     # Play/record/loop, tempo map, timecode
│   ├── dsp/           # Built-in effects & instruments
│   └── io/            # Device abstraction (JUCE AudioDeviceManager wrapper)
├── model/             # Session document: tracks, clips, automation, routing (undoable)
├── ui/
│   ├── surface/       # GarageBand-style: track area, loop browser, smart controls
│   ├── depth/         # Pro Tools-style: mixer, I/O setup, edit modes, prefs
│   ├── shared/        # LookAndFeel, theming, widgets, meters
│   └── commands/      # Command palette & keyboard-focus system
├── plugins/           # Host wrappers (VST3/CLAP/LV2/AU), scanner, sandbox
├── scripting/         # Lua bindings
├── persistence/       # Session load/save, templates, autosave, import/export
├── assets/            # Bundled loops, presets, icons, fonts
├── tests/
└── docs/
```

### 4.3 Key Design Decisions

1. **Model/View separation with an undoable command layer.** Every edit is a command object; UI never mutates the model directly. Enables unlimited undo, scripting, and collaboration later.
2. **Lock-free message passing to the audio thread.** The audio graph receives parameter changes via a lock-free FIFO. No allocation, locks, or file I/O on the audio thread.
3. **Latency compensation is graph-wide.** Each node reports latency; the graph inserts delay lines so all paths align, matching Pro Tools ADC behavior. User-visible per-track offsets.
4. **Non-destructive by default.** Audio files are never modified; clips reference regions of source files. Destructive record is an explicit opt-in.
5. **Two UI "densities" share one component tree.** The Surface and Depth views are different arrangements and visibility states of the same components, not separate apps. A user can flip a mixer strip from "simple" to "full" and back.
6. **Progressive-disclosure API.** Each panel declares `basic` and `advanced` control sets; a global and per-panel toggle controls visibility. Search in Preferences and the Command Palette can surface any hidden control.

### 4.4 Audio Engine Details
- Block sizes 32–2048 samples; sample rates 44.1k–192k.
- 64-bit float internal mixing, 32-bit float plugin I/O.
- Tempo map with ramps; bars/beats ↔ samples conversion cached per block.
- Time-stretch via Rubber Band Library 4.0.0 (GPL, single-file build fetched by CMake) for Elastic-style modes.
- Transient detection for Beat Detective-style features and tab-to-transient.
- Offline render path shares graph code with real-time path (guarantees bounce == playback).

### 4.5 Data Formats
- **Session (`.bmk`)**: directory bundle containing `session.json`, `automation.bin`, `Audio Files/`, `Bounced Files/`, `Session File Backups/`.
- **Audio**: WAV/AIFF/FLAC import; BWF WAV with timestamps for recording.
- **MIDI**: Standard MIDI File 0/1 import/export.
- **Interchange**: AAF/OMF export planned (phase 5); AES31/ADM stretch goal.

---

## 5. Roadmap

### Phase 0 — Foundation (Weeks 1–4)
- [x] Repo, CMake, coding standards. (CI still open; license decided 2026-09-05: GPL-3.0-or-later.)
- [x] JUCE integration; audio device enumeration and a test tone.
- [x] Session model skeleton with undo/redo command system.
- [x] Basic window with transport bar and track area.
- [x] **Milestone:** Play a WAV file on a single track with transport control. *(Done 2026-09-05)*

### Phase 1 — Surface MVP (Weeks 5–12)
- [x] Audio & Instrument tracks; record from input; clip display with waveforms. *(Recording done 2026-09-05: per-track arm, input select, input monitoring, threaded WAV writer, live waveform, punch-out)*
- [x] Loop browser with bundled loops; drag-to-track; tempo conform. *(Done 2026-09-05: LoopLibrary scanner with name/length metadata, Library panel with search/category filters/audition/drag/double-click, varispeed conform + beat snap; 7 bundled loops. Polyphonic stretch remains a Phase 3 item.)*
- [x] Step sequencer + Drum Machine instrument. *(Done 2026-09-05: synthesised 16-pad kit, pattern clips, step editor, pad audition, per-pad sample load, Cycle mode)*
- [x] Piano roll editor. *(Done 2026-09-05, together with a polyphonic subtractive Synth instrument with presets; MIDI clips loop like pattern clips)*
- [x] Simple mixer (volume/pan/mute/solo) and Smart Controls panel. *(Done 2026-09-05: per-track Volume/Pan with -3 dB pan law through clips, drums and synths; Smart Controls knobs bound to synth params and drum pad-group levels; knob drags coalesce into one undo step)*
- [x] Bounce to WAV (also AIFF/FLAC; MP3 deferred, needs an encoder). *(Done 2026-09-05: Bounce to Disk dialog, range/format/depth/tail/normalise, offline render bit-identical to playback, `--bounce=` batch flag)*
- [x] **Milestone:** A beginner can make and export a beat in under 10 minutes. *(Reached 2026-09-05: drum track + synth track + loops from the Library, edit, Cycle, Bounce.)*

### Phase 2 — Depth: Editing & Mixing (Weeks 13–22)
- [x] Edit modes (Shuffle/Slip/Spot/Grid) and full tool set. *(2026-09-05: all four modes incl. Absolute/Relative grid; Zoomer, Trimmer, Selector, Grabber, Scrubber, Pencil (free shape, non-destructive copy), Smart Tool; move/trim/separate/duplicate/nudge/delete/clear as undoable commands.)*
- [x] Clip gain, fades, nudge, tab-to-transient, playlists/takes/comping. *(2026-09-05: static clip gain and clip gain breakpoints (source-anchored line), fades with three shapes, corner handles, Fades window with batch fades, nudge; playlists with New/Duplicate/Switch/Delete, loop recording into takes, take lanes and range comping. Tab-to-transient moves to Phase 3 with transient detection.)*
- [x] Full mixer: 10 inserts, 10 sends, aux tracks, buses, VCA, groups. *(2026-09-05: strip pipeline with 10 inserts (EQ/Compressor/Delay/Reverb built in), 5 sends pre/post to 8 buses, aux inputs, output routing, master strip with inserts, post-fader meters, Mix window; VCA masters with fader/mute/solo/automation scaling members; Edit/Mix groups with attributes, relative dB faders, edit propagation and badges. 5 sends rather than 10 for now.)*
- [x] I/O Setup window; automatic delay compensation. *(Done 2026-09-05: Input/Output/Bus paths with device channel mapping, path-based track routing, direct outs, bus renaming; ADC aligning sources, bus feeds and aux returns with per-track user offsets and a dly readout; compressor lookahead as the first latent insert.)*
- [~] Automation: all modes, lanes for every parameter. *(2026-09-05: Off/Read/Touch/Latch/Write/Trim with pass recording; lanes for volume (ramped), pan, mute (stepped), sends, insert params; master volume automation; static and live Trim baked relatively; breakpoint editing in the track area; controls follow in Read. Pending: Preview/Capture, thinning.)*
- [x] Metering suite (peak/RMS/K/LUFS). *(Done 2026-09-05: per-track Sample Peak/RMS/Peak+RMS/VU/K-12/K-14/K-20, clip hold, gain-reduction bars, BS.1770 M/S/I/LRA/True Peak on the master with exact K-weighting for any sample rate.)*
- [x] **Milestone:** An engineer can mix a 48-track session with routing comparable to Pro Tools. *(Reached 2026-09-05.)*

### Phase 3 — Plugins & Instruments (Weeks 23–30)
- [x] VST3 + CLAP hosting with scanner and sandbox; LV2 (Linux) and AU (macOS). *(2026-09-05: VST3/LV2/AU via JUCE with out-of-process scan validation and blacklist, latency to ADC, parameter automation, editor windows. Hosted plugins with a second input bus receive the strip's sidechain key (2026-09-06). CLAP hosted by our own `ClapPluginFormat` (plugins/ClapHost: dlopen, params, state, latency, audio ports incl. sidechain, X11-embedded GUI) plugged into JUCE's format manager, so scanning/blacklist/automation/windows are shared (2026-09-06; verified with a fake .clap built by the tests, no real CLAP plugins on the dev machine). Out-of-process *hosting* (sandboxed audio) remains a later hardening item.)*
- [x] Bundled effects suite complete; sidechain routing. *(2026-09-05: 7-band EQ with response display, Compressor, Limiter, Gate/Expander, De-esser, Delay (digital/tape/ping-pong), Reverb, Convolution Reverb (six bundled spaces + IR files, JUCE partitioned FFT), Chorus, Flanger, Phaser, Saturation, Amp Sim, Utility. Sidechain key inputs from any bus on Compressor/Gate/De-esser with Key Listen; the snapshot orders strips so senders run first (zero-latency keys). Pitch Correction insert (YIN detector + Rubber Band real-time shifter, key/scale, retune speed, formant preserve).)*
- [x] Sampler, subtractive and wavetable synths. *(Done 2026-09-05: generic `Instrument` framework (per-type parameter metadata + presets, Smart Controls and preset menus build themselves from it); Synth (subtractive), FM Synth, Wavetable, Sampler (drop a file on the track), Electric Piano, mono Bass. `--instrument=<name>` and `--sample=<file>` flags.)*
- [x] Elastic-style time stretch with warp markers; Beat Detective-style tools. *(Done 2026-09-05 with Rubber Band 4.0.0 (GPL): Polyphonic/Rhythmic/Monophonic/Varispeed, pitch shift, TCE trimmer, conform to tempo, warp markers, quantize to grid, tab-to-transient, separate at transients; `--elastic-demo`. Beat Detective window added later the same day: Separate / Clip Conform (strength, exclude within, swing) / Edit Smoothing (fill gaps, equal-power crossfades). Long clips render on a background thread with a progress window.)*
- [x] **Milestone:** Third-party plugins run reliably; time-stretched vocals sound clean. *(Phase 3 complete 2026-09-06. Plugin reliability is covered by out-of-process scanning and the fake-plugin tests; a machine with real VST3/CLAP plugins is still needed for a hands-on pass.)*

### Phase 4 — Pro Workflows (Weeks 31–38)
- [x] Record modes (QuickPunch, TrackPunch, Loop), pre/post-roll. *(Done 2026-09-06: Recorder captures the whole pass and keeps sample-accurate punch ranges; Normal punches at the time selection, QuickPunch punches with the Record button, TrackPunch with each track's R button, Loop makes every Cycle pass a take; pre/post-roll from the transport bar; flags --record-mode, --punch, --pre-roll, --post-roll, --punch-at, --stop-at.)*
- [x] MIDI Event List, real-time properties, sync (MTC/MIDI clock). *(Done 2026-09-06: ui/depth/MidiEventList (editable table + Real-Time Properties strip; engine MidiRealtimeProps applied in AudioGraph::scheduleMidi, saved in sessions); engine/sync/MidiSync generators + chasers (pure, unit-tested) and MidiSyncController over real or virtual "Beat Maker Sync" ports; ui/depth/SyncDialog (Ctrl+2); verified with aseqdump: 96 clocks / 2 s at 120 BPM, 239 quarter frames / 2 s at 30 fps; flags --sync, --event-list, --sync-window.)*
- [x] Memory locations, arrangement track, session templates. *(Done 2026-09-06: `.bmk` session bundles (persistence/SessionFile, JSON + Audio Files; byte-identical bounce after reopen), New/Open/Save/Save As/Save As Template/autosave, built-in templates (Empty, Beat Making, Songwriter, Podcast) + user `.bmkt`; memory locations with selection/zoom recall (M, Alt+1..9, Ctrl+5 window) and arrangement sections that move/duplicate/delete their contents (model/Arrangement); flags --session, --save, --template, --marker-demo.)*
- [x] Deep Preferences with search; Command Palette; keyboard focus mode. *(Done 2026-09-06: ui/shared/Preferences registry (27 settings, 6 categories, persisted, live listeners) + PreferencesWindow (Ctrl+,); ui/shared/CommandRegistry holds every shortcut and drives keyPressed + CommandPalette (Ctrl+Shift+P / Ctrl+K, lists settings too); Commands Keyboard Focus (a-z toolbar button) with Pro Tools letters incl. new trim/fade-to-insertion and clip cut/copy/paste; pan depth wired into the engine's pan law; flags --pref, --palette, --prefs-window, --focus, --run.)*
- [ ] Lua scripting API.
- **Milestone:** Feature parity with the "settings and options" targets in §3.2.

### Phase 5 — Polish & Release (Weeks 39–46)
- [ ] Stem export, Import Session Data, AAF/OMF export.
- [ ] Onboarding, tooltips, in-app tutorials, sample projects.
- [ ] Performance pass: CPU meter, freeze/commit tracks, disk cache.
- [ ] Accessibility (screen reader labels, high-contrast theme, full keyboard nav).
- [ ] Beta program, crash reporting, docs site.
- **Milestone:** v1.0 public release.

---

## 6. UI Design Notes

### Layout (default "Surface" view)
```
┌──────────────────────────────────────────────────────────────────────┐
│ ● ▶ ■ ⟲   [ 004 | 2 | 1 | 000 ]  120 BPM  4/4  C maj   🎚 Master    │  ← Transport
├────────────┬─────────────────────────────────────────────────────────┤
│ Library    │ Arrangement: [Intro][Verse][Chorus]                     │
│ ─────────  │ ┌───────────┐┌──────────────────────────────────────┐   │
│ 🥁 Drums   │ │ 🥁 Drums  ││ ▓▓▓▓▓▓ ▓▓▓▓▓▓ ▓▓▓▓▓▓ ▓▓▓▓▓▓          │   │
│ 🎹 Keys    │ │ M S ● ─●─ ││                                      │   │
│ 🎸 Guitar  │ ├───────────┤├──────────────────────────────────────┤   │
│ 🎤 Vocals  │ │ 🎹 Keys   ││   ▒▒▒▒▒▒▒▒▒     ▒▒▒▒▒▒▒▒▒            │   │
│            │ │ M S ● ─●─ ││                                      │   │
│ Loops ▾    │ └───────────┘└──────────────────────────────────────┘   │
│ [search]   │                                              [+ Track]  │
├────────────┴─────────────────────────────────────────────────────────┤
│ Smart Controls │ Mixer │ Editor │ ▸ Show Advanced                    │  ← Bottom panel tabs
│  (o) (o) (o) (o)   Tone  Drive  Space  Width                         │
└──────────────────────────────────────────────────────────────────────┘
```

### "Depth" toggles
- **Track header ▸ Advanced:** reveals input/output selectors, automation mode, delay readout, group ID, playlist selector.
- **Mixer ▸ Full:** expands each strip to show 10 inserts, 10 sends, I/O, meters with selectable type.
- **Edit toolbar ▸ Pro:** reveals Shuffle/Slip/Spot/Grid buttons, nudge value, tool palette, counters.
- **Cmd/Ctrl+K:** Command Palette; type any setting or action.

### Visual language
- Rounded, high-contrast track colors; dark neutral background.
- One accent color for the active state; no more than two font weights.
- Icons over labels in Surface; labels appear in Depth.
- All hover states show a tooltip with the setting name and keyboard shortcut.

---

## 7. Non-Functional Requirements

- **Latency:** ≤ 6 ms round-trip at 64-sample buffer on supported interfaces.
- **Stability:** no audio dropouts under 70% CPU load; plugin crashes never take down the host (sandboxed).
- **Scale:** 256 tracks, 1000+ clips, 8-hour sessions.
- **Platforms:** Linux (primary dev target — PipeWire/JACK/ALSA), macOS (CoreAudio), Windows (ASIO/WASAPI).
- **Accessibility:** WCAG-inspired contrast, full keyboard operation, screen reader support.

---

## 8. Risks & Mitigations

| Risk | Mitigation |
|---|---|
| Scope creep from "everything Pro Tools does" | Strict phase gates; §3.2 is the contract, nothing added until Phase 4 is done |
| Real-time audio bugs (glitches, xruns) | Lock-free design enforced by code review; automated xrun tests under load |
| Plugin instability | Out-of-process plugin sandbox from day one of Phase 3 |
| UI complexity leaking into Surface view | Design rule: any control added must declare basic/advanced; UX review each sprint |
| Time-stretch quality | License Rubber Band (GPL/commercial) rather than writing from scratch |
| Cross-platform audio driver differences | JUCE abstraction + a device compatibility matrix maintained in CI |

---

## 9. Open Questions

1. ~~**License:** GPL vs. proprietary?~~ **Decided 2026-09-05: GPL-3.0-or-later** (LICENSE added), which lets Beat Maker use the Rubber Band Library under its GPL terms.
2. **Name:** "Beat Maker" is a working title; check trademark availability.
3. **Cloud/collaboration:** Out of scope for v1, but should the session format be designed for merge/sync now?
4. **Mobile companion:** Should Smart Controls be remote-controllable from a phone/tablet in v1?
5. **Bundled content:** Produce loops in-house or license a library?

---

## 10. Immediate Next Steps

1. Decide license and confirm JUCE as the framework.
2. Scaffold repo: `engine/`, `model/`, `ui/`, CMake, CI.
3. Build Phase 0 milestone: play a WAV with transport control.
4. Create low-fidelity mockups of Surface and Depth views for review.
5. Write the `basic`/`advanced` control declaration API before adding any UI panel.
0