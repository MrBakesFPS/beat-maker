# Instruments, loops and MIDI

## Drum Machine

A Drum Machine track has a synthesised 16-pad kit and a 4-bar pattern clip with a starter beat. The step sequencer panel edits it: click a step to toggle it, Shift-click for a soft hit, drag to paint, click a pad name to audition it, and drop an audio file on a pad row to replace its sample. Smart Controls add levels for Kick, Snare, Clap/Rim, Hats, Toms, Cymbals, Perc and Sub.

## Instruments

Add Track > Instrument Track (or **Ctrl+I** for the Synth) creates a track driven by MIDI clips. New instrument tracks start with a two-bar arpeggio. The Sound menu lists each instrument's presets; Smart Controls show one knob per parameter the instrument describes.

- **Synth**: polyphonic subtractive synth (PolyBLEP saw, square, triangle and sine, a detuned second oscillator, state-variable low-pass with envelope, ADSR).
- **FM Synth**: two-operator FM with ratio, decaying index and feedback, for bells, keys and basses.
- **Wavetable**: two detuned oscillators morphing across band-limited tables with a filter envelope.
- **Sampler**: drop an audio file onto the track to load it; plays it pitched around C3 with tune, one-shot or loop, ADSR and a filter. Presets keep the loaded sample.
- **Electric Piano**: a tine model with velocity-dependent brightness, an inharmonic bell partial, per-note decay and tremolo.
- **Bass**: monophonic with last-note priority, legato filter retrigger, glide, a sub oscillator, a filter envelope and drive.

### Note editor

The piano roll in the editor panel shares the edit window's settings, so the toolbar means the same thing in both places:

- **Modes**: Grid snaps note starts and lengths to the toolbar's grid value (Relative snaps the movement and keeps a note's offset from the grid); Slip is free; Spot, or a double-click on a note, opens a dialog to type its start as bar|beat|tick and its length.
- **Tools**: the Zoomer zooms (click in, Alt-click out, drag a range), the Trimmer drags whichever edge is nearer, the Selector rubber-bands, the Grabber moves, the Pencil adds a note where you click (keep dragging to set its length) and paints velocities in the lane, and the Smart Tool does the right one by where you click: edges trim, the body grabs, empty space adds a note on release or selects when dragged.
- **Selection**: click selects, Shift-click adds, Ctrl-click toggles, Ctrl+A selects all, Escape clears, Tab and Shift+Tab step through the notes. Everything selected moves, resizes, transposes and nudges together.
- **Velocity lane** along the bottom: drag a note's bar (or the whole selection's), or paint across notes with the Pencil.
- **Keys**, with the note editor focused: Up/Down transpose a semitone, Shift+Up/Down an octave, Ctrl+Up/Down velocity by 10 (Ctrl+Shift by 1), Left/Right or , . nudge by the grid (Shift a quarter grid), Delete, Ctrl+C/X/V copy, cut and paste at the insertion (the playhead when it is inside the clip, else the selection start), Ctrl+D duplicate after the selection, Alt+Q quantize starts (Alt+Shift+Q lengths too), Ctrl+L legato, Alt+Z zoom to fit. The Commands Focus letters R, T, E, H, X, C, V and Q work here too.
- **View**: Ctrl+wheel zooms, Shift+wheel scrolls in time, the plain wheel scrolls pitches, and a click on the ruler locates the playhead inside the clip.

**Keyboard focus** decides who gets the keys and what the Edit and View commands act on. Ctrl+Alt+N, or the Notes button in the toolbar, switches between the note editor and the tracks; clicking in either does the same. The focused editor shows the accent outline, the header of the note editor reads out the selection (count, pitch range, bar|beat|tick span) and the toolbar hint changes to the note vocabulary. Velocities for new notes come from Preferences > MIDI.

### MIDI Event List and real-time properties

**Ctrl+Alt+E** lists every note of the selected instrument track in time order with bar|beat|tick start, note name, velocity, length and clip, all editable in place (type F#3 or a note number). Click a row to locate, Delete removes selected events, Insert Note adds one at the playhead. Above the list, the track's **Real-Time Properties** (Quantize with grid and strength, Transpose, Velocity scale and offset, Delay, Duration) apply while playing and leave the stored notes untouched.

## Loop Library

**L** opens a GarageBand-style browser over the bundled loops plus `~/Music/Beat Maker/Loops` and any folders you add. Tempo, key and category are read from the file names, with the tempo estimated from the length when missing. Click a loop to audition it, double-click to add it at the playhead, or drag it onto a track. Loops are conformed to the session tempo with Elastic Audio (Rhythmic for drum loops, Polyphonic for everything else, so the pitch stays put), remember their source tempo and snap to the beat grid.
