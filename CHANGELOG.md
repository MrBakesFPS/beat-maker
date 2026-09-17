# Changelog

All notable changes to Beat Maker. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- A message-thread profiler: System Usage shows interface stalls and the costliest operations, and `--profile-ui[=seconds]` prints the profile.

### Changed
- The note and drum editors show every clip of the track, with an active clip that edits and clicks switch between; a note or step in empty time creates a new clip. Add Clip for Selection (Ctrl+Alt+M, or right-click an empty lane) makes an empty clip covering the time selection on instrument tracks.
- New pattern and MIDI clips play their content once: a one-bar beat and a two-bar arpeggio. Loop is chosen per clip (editor header button or right-click menu): it adds one pass, growing the clip by its own length or up to the next clip, and the extension can then be trimmed longer or shorter; switching Loop off or trimming the clip back to its content returns it to its original state.
- Smart Controls sit above the tracks instead of between the tracks and the editor panel.
- The mixer's strips scroll sideways with the wheel and a thicker scrollbar.

### Added
- Join Selected Clips (Ctrl+J, and in the clip menus): MIDI and pattern clips merge into one with their contents written out (loops included), audio clips heal when adjacent pieces of one file. Split at Playhead is in the clip menus too.

### Added
- Per-track I/O and fades in the track header's menu: I/O sets the input path, the output (Main, an output path or a bus), input monitoring and opens I/O Setup; Fades sets auto-fades (2 to 250 ms, three shapes) that every recorded or imported clip on the track lands with, saved with the session, and opens the Fades window for every clip on the track at once. `--auto-fade=<track>,<ms>` flag.
- Effects from the track itself: an FX button in every track header and an Effects submenu in its context menu add any built-in effect (EQ, compressor and the rest) to the track's first free insert slot and open its knobs, and list the track's effects with Edit, Bypass and Remove. They are the mixer's insert slots, so both views agree. "Effects on Selected Track..." (Ctrl+Alt+F) in the command palette; `--add-effect=<track>,<effect>` flag.
- Track colours can be set by hand: right-click a track header > Colour for the eight palette colours, Automatic or Custom... (a colour picker), or "Colour Selected Track..." in the command palette. The colour follows into clips and the mixer, is undoable and is saved. Lua `set_track_colour(i, "#rrggbb")`, track tables carry `colour`; `--track-colour=<track>,<#rrggbb>` flag.
- Twenty-eight more instruments, filling every category out to at least three: Pad, Lead, Pulse, Sync and Texture (Synths); Granular and Vinyl Sampler (Samplers, loading files like the Sampler); Harpsichord, Clavinet, Celesta, Accordion and Melodica (Keys); Steel Drum, Handpan, Tubular Bells and Gamelan (Mallets); Harp, Guitar (with a strum) and Solo Strings (Strings); Solo Brass and Big Band (Brass); Clarinet, Oboe, Sax and Harmonica (Winds); Sub Bass, Slap Bass and Upright Bass (Bass). Built on shared engines (decaying partials, reeds, a mono lead, a solo bowed or blown line, plucked-string variants), each with four or five presets.
- More bundled presets: every instrument now has eight to eleven (Synth gained Brass Stab, Warm Keys, Deep Pad, Acid Line, Sine Bell and Hoover; Bass gained Moog, Reese, Rubber, Slap, Wobble and Fretless; Piano a Concert Grand, Soft Ballad, Toy Piano, Electric Grand and Dark Piano; and so on across the sixteen). A test holds every bundled preset inside its parameter ranges and audible.
- Build Your Own Preset: an instrument's knobs in a window with a Play button that keeps sounding while knobs move, saved as a JSON file per preset under `~/Music/Beat Maker/Presets/<Instrument>/`; Save... next to the Sound menu stores a track's current sound the same way. User presets list under My Presets in every Sound menu and in the chooser (Edit... and Remove there), sessions keep them by name, and tracks on an edited preset take the new sound. `--preset-builder[=<instrument>[,<preset>]]` flag.
- Twenty more drum kits, five per category: Rock, Jazz, Vintage, Room and Brush (Acoustic); 606, Linn, Electro, Techno and House (Electronic); Boom Bap, Trap, Drill, Neo Soul and SP Vintage (Hip-Hop); Latin, Afro, Taiko, Indian and Middle East (World, sixteen hand instruments each). The Drum Machine Track submenu lists them by category.
- Build Your Own Kit: assemble a kit from the pads of any other kits, with previews of both sides; saved as a JSON file per kit in `~/Music/Beat Maker/Kits` and listed under My Kits in every kit menu; edit and remove from the kit chooser; sessions keep the kit by name. `--kit-builder[=<kit>]` flag.
- Drum kits: Add Track > Drum Machine Track lists the bundled kits and ends in Other..., a kit chooser with categories, descriptions, the sixteen pads of each kit and play buttons that preview a bar of a beat on a kit or a single pad (an audition kit the audio graph plays on a private strip). Four new kits join the Studio Kit: 808, 909, Lo-Fi and Percussion, all on the same pad layout. The drum editor's header has a Kit menu to swap a track's kit, and sessions remember each track's kit by name. `--drums=<kit>` and `--kit-chooser` flags.
- Five acoustic-style instruments, opening the Keys, Strings, Mallets, Brass and Winds categories: Piano (inharmonic decaying partials, two strings per note), Strings (a wandering three-voice ensemble with delayed vibrato and a bow filter), Mallets (marimba, vibraphone, glockenspiel and kalimba by their modes), Brass (a blat filter, pitch dip and late vibrato) and Flute (breath noise shaped at the note, chiff, overblow). Four or five presets each.
- Five new synths: Stack (a stereo supersaw), Chip (an 8-bit sound chip with bit depth and the chip arpeggio), Vox (a formant synth morphing between vowels), Organ (nine drawbars with percussion, click and vibrato) and Pluck (a Karplus-Strong plucked string). Each has four or five presets.
- Instrument previews: each instrument and preset row in the chooser has a play/stop button that plays a short phrase on that sound before any track exists (an audition instrument the audio graph plays straight to the outputs). Selecting a row does not play it.
- Instrument chooser: Add Track > Instrument Track > Other... (Ctrl+Shift+I) lists every bundled instrument by category with a description and the presets it can start from; the instrument registry now carries a category and a description per instrument, so new instruments appear there by themselves. `--instrument=<name>[,<preset>]` and `--instrument-chooser` flags.
- Rename Session... in the File menu: an unsaved session takes the name for its title and first save; a saved one is saved, its `.bmk` folder renamed on disk and reopened from there. The name given in New Session now shows in the title. `--rename=<name>` flag.
- Drum editor: Shift+Left/Right nudges hits by a quarter step. The fraction is micro-timing kept on the hit (it plays that much late, is drawn between the steps, moves with the hit, and is saved with the session).
- Drum editor: drag a hit to move it (with the selection) to another step or pad, and drag its right edge to hold it over several steps. A held hit is gated at its end (a fade-out, like a choke), a one-step hit rings out; holds stop at the next hit on the pad and travel with copy, paste, duplicate, unroll and the session file. The Trimmer tool holds, the Grabber moves, the Smart Tool and Pencil do both by where you grab.

### Fixed
- Adding a sixth effect to a track crashed: the Mix window had buttons for five of the ten slots, and opening the new effect's knobs anchored to a button that did not exist. Every slot has a button now, the first five show and more appear as they fill, and the callout anchors to the strip when a slot is not shown.
- In the Mix window a click on an effect opened its editor; it now switches the effect on or off, and a right-click offers Edit..., Replace With, Bypass and Remove. Adding an effect from a track's menu opens the Mix window with the new effect's knobs, and Edit... from that menu opens its callout (it was dismissed by the menu's own click).
- The Instrument Track submenu listed every instrument (forty-four of them); it now shows one per category, the first of each, with Other... for the rest.
- Transposing a note with the arrow keys past a neighbour at the same beat handed the selection to the neighbour (the selection named a position, and the neighbour was at it). Selections now name the note itself (pitch, beat, length and velocity), and every edit returns the keys of the notes it changed.
- In the drum editor, moving a hit onto another (arrow keys or a drag) overwrote it. A moving hit now never touches the hits in its way: the arrow keys skip past an occupied step or pad to the next free one, and a drag waits over an occupied step. Held hits are cut where a hit moves in front of them.
- Moving a note onto another note of the same pitch and beat made them one: the selection matched both by position, so the next drag took both and the other note seemed to vanish. A selection now resolves to one note per position, stacked notes are drawn with an inset outline, and dragging one away leaves the other.
- The Open Session browser stepped into a session bundle (it is a folder) on a double-click or Return instead of opening it. Bundles now open on double-click, Return or the Open button; other folders still navigate. `--open-dialog[=<bundle>]` flag.
- With Link on, the note and drum editors kept the tracks' lane offset and left an empty band at the left. The editors now span the whole panel, keyboard or pads at the left, and Link only shares the time and zoom.
- A clip extended (by Loop or by dragging its edge), edited in the extension and then shortened back kept the grown length as its "normal" size, so the next Loop doubled the wrong thing. Loop now measures the clip as shown on the timeline and remembers that length; shrinking any clip folds content that grew with nothing in it back to the clip, and Loop off or a trim back to the remembered length restores the clip.
- Choosing an edit mode or tool lit two buttons (the old one stayed on): JUCE fires the click of the radio button it turns off, which re-selected it. `--click-button=<text>` presses a button by label for smoke tests.
- The track list scrolls when there are more tracks than fit: wheel over the tracks, a scrollbar at the right, and the selected track kept in view; Shift+wheel scrolls time. The ruler stays fixed while headers slide under it. `--scroll-tracks=<px>` flag.
- Interface lag: the note and drum editors repainted everything thirty times a second while linked (the drum grid alone took about 30 ms per paint), and the track area repainted everything every tick. All three now repaint only the playhead columns unless the view or the content changed, and the drum grid draws lines instead of an outline per empty cell.
- A multi-second freeze when opening a session with Elastic Audio clips (every clip was re-rendered on the message thread). Renders are now deferred to a background job with a progress window, applied as one step; the same job serves loop imports, tempo conforming and Import Session Data, and the background decision uses the render speed measured on the machine.
- The editor panel's height is draggable (a bar above it, remembered in Preferences > Display) and the note editor has its own vertical zoom (Alt+wheel, - and + buttons, a pitch scrollbar and keyboard-drag scrolling), independent of the tracks' zoom.
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
