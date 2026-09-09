# Instruments, loops and MIDI

## Drum Machine

A Drum Machine track has a synthesised 16-pad kit and a one-bar pattern clip with a starter beat. Smart Controls add levels for Kick, Snare, Clap/Rim, Hats, Toms, Cymbals, Perc and Sub.

### Drum editor

The step sequencer in the editor panel works like the [note editor](#note-editor): it is drawn on the session timeline, follows the tracks' view while **Link** is on (the same time and zoom as the clips; scrolling or zooming either view moves both), shows every repeat of a looping pattern with the later ones as ghosts, and **Unroll** (or the first edit inside a repeat) writes the loop out so each bar of the clip is its own. The header reads out the selection.

- **Steps**: click an empty step to switch it on (Shift for a soft hit), drag across empty steps to paint (one undo step per stroke), click a hit to switch it off, right-click to clear. Painting past the clip's end extends the clip to the next bar.
- **Moving and holding**: drag a hit to another step or pad (the whole selection comes along, like notes in the note editor). A moving hit never disturbs the hits already there: two hits cannot share a step, so with the arrow keys a hit skips past an occupied step (or pad) to the next free one, and a drag simply waits over an occupied step until the mouse moves on. Drag a hit's right edge to hold it over more steps: a held hit is cut off at its end, a gate, which is how you shorten an open hat or a long sample; a one-step hit rings out as before. A hold stops at the next hit on the pad. Held hits keep their length when moved, copied, duplicated or unrolled, and are saved with the session.
- **Tools**: the Selector rubber-bands hits, the Grabber drags them (a click only selects), the Trimmer holds them, the Zoomer zooms; the Smart Tool and Pencil do all of it by where you grab. Ctrl-click adds a hit to the selection, Ctrl+A selects all, Escape clears.
- **Velocity lane** along the bottom shows the hits of the pad you last clicked; drag a bar to set its velocity (the whole selection follows when the bar is selected).
- **Keys**, with the drum editor focused: Up/Down move the selected hits to the pad above or below, Ctrl+Up/Down velocity by 10 (Ctrl+Shift by 1), Left/Right or , . nudge by a step, Shift+Left/Right by a quarter step (micro-timing: the hit plays that much late, drawn between the steps and saved with the session), Delete clears, Ctrl+C/X/V copy, cut and paste at the insertion (the playhead when it is inside the clip), Ctrl+D duplicates after the selection, Alt+Z fits the clip.
- Click a pad name to audition it; drop an audio file on a pad row to replace its sample.

Keyboard focus is shared with the note editor: Ctrl+Alt+N or the Editor button in the toolbar switches between the editor panel and the tracks, and the Edit and View commands act on whichever has focus.

## Several clips on a track

The editors show every pattern or MIDI clip of the selected track on the timeline, each named on the ruler. The clip you last clicked is the active one: it is drawn brighter and named in the header ("clip 2 of 3"), and the selection, the keys and the Loop and Unroll buttons apply to it. Click a note or step of another clip, or click inside its time, to make that one active. Placing a note or step in empty time past every clip creates a new one-bar clip there.

To add a clip in the tracks, select a time range on an instrument track (or just put the playhead where you want it) and use **Add Clip for Selection** (**Ctrl+Alt+M**, or right-click the empty lane). The new clip is empty, as long as the selection (one bar when there is none), and plays once.

## Looping clips

A pattern or MIDI clip plays its content once until you choose **Loop** for it: the Loop button in the editor header, or right-click the clip in the tracks. Loop adds one more pass: the clip grows by its own length, or as far as the next clip on the track if that is closer. The extension is then yours to shape: drag the clip's right edge with the Trimmer or Smart Tool for more or fewer passes, or right-click it and pick a Loop Length of 1 to 32 bars (or type a number). Loop measures the clip as you see it: one pass is the clip's length on the timeline at the moment you choose Loop (silence at its end included), and the clip remembers that length. Switching Loop off, or trimming the clip back to that length, returns it to its original state, and Loop must be chosen again to loop it again. With Loop off, a clip longer than its content is silent after the content ends; adding notes or steps there extends the content instead of looping it. Whenever a clip is shrunk (with Loop on or off) and the part cut away holds no notes or steps, the content folds back to the clip, so extending a clip, trying a note in the empty part, deleting it and shrinking the clip again leaves nothing behind: the next Loop starts where the clip ends. Notes or steps that would be lost are kept instead, and Loop then treats the whole content as the pass. Sessions saved before this option keep looping as they did.

## Instruments

Add Track > Instrument Track (or **Ctrl+I** for the Synth) creates a track driven by MIDI clips. The submenu lists the bundled instruments; **Other...** at its bottom (also **Ctrl+Shift+I**, or "New Instrument Track (choose)..." in the command palette) opens the instrument chooser: every instrument by category (Synths, Keys, Bass, Samplers), a description of what each is for, and the presets it can start from. Double-click an instrument or a preset, or press Add Track. New instrument tracks start with a two-bar arpeggio clip that plays once. The Sound menu lists each instrument's presets; Smart Controls show one knob per parameter the instrument describes.

- **Synth**: polyphonic subtractive synth (PolyBLEP saw, square, triangle and sine, a detuned second oscillator, state-variable low-pass with envelope, ADSR).
- **FM Synth**: two-operator FM with ratio, decaying index and feedback, for bells, keys and basses.
- **Wavetable**: two detuned oscillators morphing across band-limited tables with a filter envelope.
- **Sampler**: drop an audio file onto the track to load it; plays it pitched around C3 with tune, one-shot or loop, ADSR and a filter. Presets keep the loaded sample.
- **Electric Piano**: a tine model with velocity-dependent brightness, an inharmonic bell partial, per-note decay and tremolo.
- **Bass**: monophonic with last-note priority, legato filter retrigger, glide, a sub oscillator, a filter envelope and drive.

### Note editor

The piano roll in the editor panel is drawn on the session timeline. With **Link** on (the default) it follows the tracks' view: the same ruler start and zoom, and scrolling or zooming in either view moves both. The editor always spans the whole panel, keyboard at the left, linked or not. Link off gives the note editor a view of its own (Ctrl+wheel zooms, Shift+wheel scrolls, Alt+Z fits the clip).

A looping clip (the default two-bar arpeggio in a four-bar clip, say) shows every repeat of its sequence; the repeats after the first are drawn as ghosts with a loop line between them. Editing a ghost note, or pressing **Unroll**, writes the loop out so that each bar of the clip is its own notes; a note added past the clip's end extends the clip. The header reads out the selection and the loop length. A note dragged onto another note of the same pitch and start does not replace it: both stay (the stack is drawn with a second, inset outline), and dragging one of them away takes only that note.

The editor shares the edit window's settings, so the toolbar means the same thing in both places:

- **Modes**: Grid snaps note starts and lengths to the toolbar's grid value (Relative snaps the movement and keeps a note's offset from the grid); Slip is free; Spot, or a double-click on a note, opens a dialog to type its start as bar|beat|tick and its length.
- **Tools**: the Zoomer zooms (click in, Alt-click out, drag a range), the Trimmer drags whichever edge is nearer, the Selector rubber-bands, the Grabber moves, the Pencil adds a note where you click (keep dragging to set its length) and paints velocities in the lane, and the Smart Tool does the right one by where you click: edges trim, the body grabs, empty space adds a note on release or selects when dragged.
- **Selection**: click selects, Shift-click adds, Ctrl-click toggles, Ctrl+A selects all, Escape clears, Tab and Shift+Tab step through the notes. Everything selected moves, resizes, transposes and nudges together.
- **Velocity lane** along the bottom: drag a note's bar (or the whole selection's), or paint across notes with the Pencil.
- **Keys**, with the note editor focused: Up/Down transpose a semitone, Shift+Up/Down an octave, Ctrl+Up/Down velocity by 10 (Ctrl+Shift by 1), Left/Right or , . nudge by the grid (Shift a quarter grid), Delete, Ctrl+C/X/V copy, cut and paste at the insertion (the playhead when it is inside the clip, else the selection start), Ctrl+D duplicate after the selection, Alt+Q quantize starts (Alt+Shift+Q lengths too), Ctrl+L legato, Alt+Z zoom to fit. The Commands Focus letters R, T, E, H, X, C, V and Q work here too.
- **View**: Ctrl+wheel zooms in time, Shift+wheel scrolls in time, the plain wheel scrolls pitches, and a click on the ruler locates the playhead. Vertical zoom is the editor's own and independent of the tracks: Alt+wheel over the notes or the - and + buttons in the header set the row height (Preferences > Display remembers it); the scrollbar at the right, or dragging the keyboard up and down, scrolls the pitch range.
- **Panel height**: drag the bar between the tracks and the editor panel; Preferences > Display remembers the height.

**Keyboard focus** decides who gets the keys and what the Edit and View commands act on. Ctrl+Alt+N, or the Notes button in the toolbar, switches between the note editor and the tracks; clicking in either does the same. The focused editor shows the accent outline, the header of the note editor reads out the selection (count, pitch range, bar|beat|tick span) and the toolbar hint changes to the note vocabulary. Velocities for new notes come from Preferences > MIDI.

### MIDI Event List and real-time properties

**Ctrl+Alt+E** lists every note of the selected instrument track in time order with bar|beat|tick start, note name, velocity, length and clip, all editable in place (type F#3 or a note number). Click a row to locate, Delete removes selected events, Insert Note adds one at the playhead. Above the list, the track's **Real-Time Properties** (Quantize with grid and strength, Transpose, Velocity scale and offset, Delay, Duration) apply while playing and leave the stored notes untouched.

## Loop Library

**L** opens a GarageBand-style browser over the bundled loops plus `~/Music/Beat Maker/Loops` and any folders you add. Tempo, key and category are read from the file names, with the tempo estimated from the length when missing. Click a loop to audition it, double-click to add it at the playhead, or drag it onto a track. Loops are conformed to the session tempo with Elastic Audio (Rhythmic for drum loops, Polyphonic for everything else, so the pitch stays put), remember their source tempo and snap to the beat grid.
