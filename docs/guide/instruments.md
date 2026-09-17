# Instruments, loops and MIDI

## Drum Machine

A Drum Machine track has a synthesised 16-pad kit and a one-bar pattern clip with a starter beat. Add Track > Drum Machine Track lists the bundled kits; **Other...** at the bottom (or "New Drum Machine Track (choose kit)..." in the command palette) opens the kit chooser: every kit by category with a description and its sixteen pads. The play button on a kit row plays a bar of a beat on it, the one on a pad row plays that pad. The **Kit** menu in the drum editor's header swaps the kit of an existing track (the pattern and the Smart Controls levels stay, since every kit shares the same pad layout). Smart Controls add levels for Kick, Snare, Clap/Rim, Hats, Toms, Cymbals, Perc and Sub.

Twenty-five kits come bundled, six per category. **Acoustic**: Studio Kit (tight and dry, the default), Rock, Jazz, Vintage, Room and Brush. **Electronic**: 808, 909, 606, Linn, Electro, Techno and House. **Hip-Hop**: Lo-Fi, Boom Bap, Trap, Drill, Neo Soul and SP Vintage. **World**: Percussion, Latin, Afro, Taiko, Indian and Middle East, each sixteen hand and stick instruments in place of a kit. Every kit shares the pad layout, so a pattern and the Smart Controls levels move from kit to kit. A session remembers which kit each track uses.

**Build Your Own** (in the kit chooser, in the Drum Machine Track submenu, or "Build Your Own Drum Kit..." in the command palette) assembles a kit from the pads of any other kits: sixteen slots on the left, a source kit and its pads on the right; pick a slot, pick a pad, press Use (or double-click the pad), with play buttons on both sides to hear them. Use whole kit fills every slot from the source as a starting point. Save Kit stores it as one JSON file in `~/Music/Beat Maker/Kits`; it then lists under **My Kits** everywhere a kit is chosen, sessions save it by name, Edit... in the chooser changes it (tracks on it pick up the new pads) and Remove deletes it (tracks keep their sounds).

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

Add Track > Instrument Track (or **Ctrl+I** for the Synth) creates a track driven by MIDI clips. The submenu lists one instrument per category (Synth, Sampler, Piano, Mallets, Pluck, Brass, Flute, Bass); **Other...** at its bottom (also **Ctrl+Shift+I**, or "New Instrument Track (choose)..." in the command palette) opens the instrument chooser: every instrument by category (Synths, Keys, Bass, Samplers), a description of what each is for, and the presets it can start from. Double-click an instrument or a preset, or press Add Track. New instrument tracks start with a two-bar arpeggio clip that plays once. The Sound menu lists each instrument's presets, eight to eleven bundled ones per instrument plus your own; Smart Controls show one knob per parameter the instrument describes.

- **Synth**: polyphonic subtractive synth (PolyBLEP saw, square, triangle and sine, a detuned second oscillator, state-variable low-pass with envelope, ADSR).
- **FM Synth**: two-operator FM with ratio, decaying index and feedback, for bells, keys and basses.
- **Wavetable**: two detuned oscillators morphing across band-limited tables with a filter envelope.
- **Sampler**: drop an audio file onto the track to load it; plays it pitched around C3 with tune, one-shot or loop, ADSR and a filter. Presets keep the loaded sample.
- **Electric Piano**: a tine model with velocity-dependent brightness, an inharmonic bell partial, per-note decay and tremolo.
- **Bass**: monophonic with last-note priority, legato filter retrigger, glide, a sub oscillator, a filter envelope and drive.
- **Stack**: up to seven detuned saws per note fanned across the stereo field (Voices, Detune, Width, Mix), through a resonant filter with envelope. Trance leads, wide pads, stacked chords.
- **Chip**: an 8-bit sound chip: 12.5 % and 25 % pulses, a square, a stepped triangle and LFSR noise, quantised to 2 to 16 bits, with vibrato and the chip arpeggio (Arp: octave, major, minor or fifth at Arp Rate) that turns one held note into a rolling chord.
- **Vox**: a detuned saw pair through three formant filters morphing between the vowels A, E, I, O and U (Vowel), with Drift wandering the vowel by itself, breath noise and a tone control. Choirs, ahhs, talking leads.
- **Organ**: nine drawbars (16' to 1'), each a sine at its harmonic, percussion on the second harmonic, key click and vibrato. Jazz, full, church, flute and rock presets.
- **Pluck**: a physically modelled plucked string (Karplus-Strong): Bright and Position shape the pick, Damping and Decay the ring, Body adds resonance. Nylon and steel guitars, harp, koto, muted plucks.
- **Piano**: an acoustic-style piano from decaying inharmonic partials: Hardness (with velocity) sets how bright the hammer strikes, Stiffness stretches the upper partials, the two strings of each note beat at Beat Hz, Thump adds the hammer, higher notes die sooner. Grand, bright, upright, felt and honky-tonk presets.
- **Strings**: an ensemble of bowed strings: three saws per note Ensemble cents apart whose detune slowly wanders (Movement), vibrato that arrives after the attack, and a Bow filter. Section, solo violin, cellos, pizzicato and a slow pad.
- **Mallets**: struck bars modelled by their modes: Bars picks marimba, vibraphone (tremolo), glockenspiel or kalimba; Hardness brings out the upper modes, Decay scales the ring, Strike adds the mallet.
- **Brass**: two detuned saws with the filter opening in a Blat over Blat Time, a pitch Dip into each note and late vibrato. Section, trumpet, French horn, trombone and synth brass.
- **Flute**: a soft tone with Breath noise shaped at the note (Air sets how whistly or hissy), a Chiff at the start, Overblow into the octave and vibrato after Vib Delay. Flute, pan pipes, recorder, shakuhachi.

The second batch, twenty-eight more, fills every category out:

- **Synths**: **Pad** (three detuned saws spread in stereo, a filter that sweeps by itself, a breath of noise), **Lead** (mono and legato with glide, saw or PWM pulse, sub, filter envelope, vibrato and drive), **Pulse** (pulse-width modulation under an LFO with a sub), **Sync** (a saw hard-synced to the note with its pitch swept by an envelope) and **Texture** (white noise through three resonances tuned to the note, their colour drifting, with grit).
- **Samplers**: **Granular** (a loaded file as a cloud of grains: size, density, spray and position) and **Vinyl Sampler** (a sampler with wow, bit reduction, a dark filter and hiss). Both load a file dropped onto the track, like the Sampler.
- **Keys**: **Harpsichord**, **Clavinet** (with its pickup comb and key click) and **Celesta** on the partials engine; **Accordion** (paired reeds beating, a musette tremolo) and **Melodica** on the reed engine.
- **Mallets**: **Steel Drum**, **Handpan**, **Tubular Bells** and **Gamelan** (its bars tuned in beating pairs), each from the modes of the real thing.
- **Strings**: **Harp** (long, bright, resonant), **Guitar** (notes that arrive together are strummed across the strings) and **Solo Strings** (one bowed string, mono and legato, with rosin noise, glide and late vibrato; violin to double bass by preset).
- **Brass**: **Solo Brass** (one player: mono, a blat and a dip on every attack, glide for slurs) and **Big Band** (up to five layers per note, staggered and detuned).
- **Winds**: **Clarinet**, **Oboe**, **Sax** (with a growl) and **Harmonica** (paired reeds, a bend into the note, hand tremolo) on the reed engine.
- **Bass**: **Sub Bass** (a sine that drops into pitch, click and drive), **Slap Bass** (a plucked string with the thumb's slap and drive) and **Upright Bass** (dark, with the finger's thump and a big body).

**Your own presets.** Turn a track's knobs in Smart Controls and press **Save...** next to the Sound menu in the note editor (or "Save Sound as Preset..." in the command palette): the sound is stored as a preset of your own, one JSON file under `~/Music/Beat Maker/Presets/<Instrument>/`, and lists under **My Presets** in every Sound menu and in the chooser. **Build Your Own...** in the chooser (or "Build Your Own Preset..." in the palette, which starts from the selected track's sound) opens the preset builder: pick an instrument and a preset to start from, turn every knob the instrument has, press Play to hear the phrase on the sound as it stands (knobs move while it plays), name it and Save Preset or Save and Add Track. A preset of yours shows Edit... and Remove in the chooser; saving an edit updates tracks that use it, removing leaves their sound in place. A bundled preset's name cannot be taken.

In the chooser (Instrument Track > Other...), every instrument and preset row has a small play button at its right: it plays a short phrase (C, E, G, then the chord) on that sound so you can hear it before adding the track, and turns into a stop button while it plays. Selecting a row does not play it. The preview plays straight to the main outputs, outside the mixer.

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

## Sample Library

**+ Track > Add from Sample Library...** (or **L**, which also closes it) opens a GarageBand-style browser window over the bundled loops; its Close button, Escape or the title bar close it plus `~/Music/Beat Maker/Loops` and any folders you add. Tempo, key and category are read from the file names, with the tempo estimated from the length when missing. Click a loop to audition it, double-click to add it at the playhead, or drag it onto a track. Loops are conformed to the session tempo with Elastic Audio (Rhythmic for drum loops, Polyphonic for everything else, so the pitch stays put), remember their source tempo and snap to the beat grid.
