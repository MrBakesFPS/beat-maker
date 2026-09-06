# Tracks and editing

## Track types

- **Audio tracks** hold waveform clips: import files with the open dialog, drag and drop, the Library or the command line.
- **Drum Machine tracks** hold pattern clips played by a synthesised 16-pad kit, edited in the step sequencer panel.
- **Instrument tracks** hold MIDI clips played by one of six bundled instruments, edited in the piano roll.
- **Aux Input tracks** read a bus and can carry inserts and sends of their own.
- **VCA Master tracks** have a fader, mute, solo and automation but no audio; tracks assigned to one follow its fader.

The + Track menu creates any of them. Right-click a track header to rename, freeze, commit or delete it. Tracks have mute, solo, record arm, input monitoring, an automation mode and a view selector (Clips, Clip Gain, or an automation lane).

When there are more tracks than fit, the list scrolls: the mouse wheel over the tracks scrolls the list, Shift+wheel (or a sideways wheel) scrolls time, Ctrl+wheel zooms, and the scrollbar at the right edge does the same. Selecting a track with the keyboard scrolls it into view; the ruler and marker strip stay put.

## Edit modes and tools

The toolbar follows the Pro Tools layout. Modes: **Shuffle**, **Slip**, **Spot** and **Grid** (F1 to F4; Grid has a value selector and an Absolute/Relative toggle). Tools: **Zoomer**, **Trimmer**, **Selector**, **Grabber**, **Scrubber**, **Pencil** and the **Smart Tool** (F5 to F11).

- The Scrubber drags the audio under the cursor through the track's own strip, moving the playhead with it.
- The Pencil redraws waveform samples once you are zoomed in far enough to see them. Edits are non-destructive copies of the clip audio, one undo step per stroke; the clip is marked "(edited)".
- Spot opens a dialog to type a bar|beat or seconds position.
- **TCE** turns the Trimmer into a Time Compression/Expansion trimmer: dragging an edge stretches the clip to the new length (see [Elastic Audio](elastic-audio.md)).

Clips can be moved (also across compatible tracks), trimmed at either edge, separated at the playhead or selection (**Ctrl+E**), duplicated (**Ctrl+D**), nudged by the grid value (**,** and **.**), deleted, or cleared from a time selection. Every edit is one undo step and Shuffle re-packs the track. A time selection sets the Cycle range and the play start. **Alt+Z** zooms to fit.

### Commands Keyboard Focus

The **a-z** button (**Ctrl+Alt+K**) turns single letters into edit commands, as in Pro Tools: A/S trim start or end to the insertion, D/G fade in or out to the insertion, F default fades, B separate, H duplicate, X/C/V cut, copy and paste clips at the insertion, R/T zoom out and in, E zoom to the selection. With focus on, the panel toggles keep working through Ctrl+Shift+E/L/B/X and record through Ctrl+Space. The full list is in [Keyboard shortcuts](../reference/shortcuts.md).

## Tempo and time signature

The tempo LCD in the transport bar is live: double-click it to type a BPM, drag it up or down (hold Shift for tenths), roll the mouse wheel over it, or right-click it for a time signature from 2/4 to 7/4. **Set Tempo...** (**Ctrl+Shift+T**) is the dialog form and **Tap Tempo** (**Ctrl+Alt+T**) sets the tempo from the average of your taps once you stop tapping.

A tempo change is one undo step. With "Clips follow tempo changes" on (Preferences > Editing, the default), clips, memory locations, automation breakpoints, the cycle range and the playhead keep their bar positions, so a two-bar pattern is still two bars long. With "Loops re-conform on tempo change" on, every audio clip with a known source tempo (library loops) is re-stretched with Elastic Audio to the new tempo as a second undo step, behind a progress window when there is a lot to render. Recorded audio without a source tempo keeps its length and moves with its bar.

## Fades and clip gain

Drag the top corners of a clip with the Smart Tool for fade in and out. Ctrl+drag a clip vertically for clip gain (or **Ctrl+Shift+Up/Down** in 0.5 dB steps). **Ctrl+F** opens the Fades window to set length, shape (Standard, Equal Power, S-Curve) and gain for every selected clip at once. Fades render in the same code path as playback and bounce; separating a clip makes the cut hard on both sides, and trims clamp the fades.

Switch a track's view to **Clip Gain** to see each clip's gain line. Click to add a breakpoint, drag to move it (with a dB readout), right-click or Alt-click to delete. Breakpoint times live in the audio's own sample positions, so the line stays glued to the sound through trims, splits and moves, and it multiplies with the static clip gain and the fades.

## Playlists and comping

Every audio track can hold alternate playlists (takes). The **P** button in the header offers New, Duplicate, Switch To, Delete and Show Take Lanes. Recording with Cycle on is loop recording: each pass becomes a take, the last pass on the main playlist and the earlier ones as alternates, all from one continuous file. Show the take lanes, drag a time selection on an alternate and press **Comp** (**Ctrl+Alt+V**) to copy that range into the main playlist; **Main** swaps a whole take in.

## Groups and VCA masters

**Ctrl+G** creates a group from the selected tracks: Edit, Mix, or both, and which of Volume, Mute, Solo, Pan and Record follow. Mix groups move member faders relatively in dB and propagate mute and solo; Edit groups extend time selections across members and apply clip moves, trims and deletes to same-start clips on member tracks. Group badges (a, b, c...) appear on strips and headers; click one to toggle, edit, leave or delete the group. A VCA Master's fader and volume automation scale the faders of the tracks assigned to it in their strips.

## Memory locations and arrangement

The strip above the ruler holds point markers and sections. **M** adds a marker at the playhead, numbered like Pro Tools memory locations; **Alt+1** to **Alt+9** recall by number, and each can recall a selection and zoom. **Shift+M** turns the time selection into a section. Right-click a section to move it earlier or later, duplicate it, or delete its time; the clips of every track move with it as one undo step. **Ctrl+5** opens the Memory Locations window. Markers export with the session to [AAF](sessions.md#export-aaf).

## Track freeze and commit

Right-click a header (or use the Track commands in the palette). **Freeze** renders the track's clips, instrument and inserts to audio and plays that instead, with the fader, pan, sends and their automation still live; insert-parameter automation is baked in and the inserts' latency is removed from the render so it sits on the grid. A frozen track shows a FROZEN badge and its render as one read-only clip; **Unfreeze** brings everything back. **Commit** writes the same render to a new audio track after the source (named `<track>.cm`, with the source's fader, sends and automation copied) and mutes the source. Renders go to the session's Audio Files folder, or `~/Music/Beat Maker/Freeze` for unsaved sessions, and survive save and reopen. A frozen bounce is sample-identical to the live one.
