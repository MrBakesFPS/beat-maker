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

### Piano roll

Click to add a note, drag to move, drag the right edge to resize, right-click or Delete to remove, click a key to audition. Velocities for new notes come from Preferences > MIDI.

### MIDI Event List and real-time properties

**Ctrl+Alt+E** lists every note of the selected instrument track in time order with bar|beat|tick start, note name, velocity, length and clip, all editable in place (type F#3 or a note number). Click a row to locate, Delete removes selected events, Insert Note adds one at the playhead. Above the list, the track's **Real-Time Properties** (Quantize with grid and strength, Transpose, Velocity scale and offset, Delay, Duration) apply while playing and leave the stored notes untouched.

## Loop Library

**L** opens a GarageBand-style browser over the bundled loops plus `~/Music/Beat Maker/Loops` and any folders you add. Tempo, key and category are read from the file names, with the tempo estimated from the length when missing. Click a loop to audition it, double-click to add it at the playhead, or drag it onto a track. Loops are conformed to the session tempo with Elastic Audio (Rhythmic for drum loops, Polyphonic for everything else, so the pitch stays put), remember their source tempo and snap to the beat grid.
