# Elastic Audio and Beat Detective

## Elastic Audio

Right-click an audio clip for the Elastic menu:

- **Modes**: Polyphonic, Rhythmic, Monophonic and Varispeed. Varispeed changes pitch with speed like tape; the others keep it.
- **Pitch shift** in semitones.
- **Conform to Session Tempo** stretches a clip whose source tempo is known (loops remember theirs) to the current tempo.
- **Warp markers**: Add Warp Marker Here, then drag the orange handles at the top of the waveform; Alt-click removes one.
- **Quantize to Grid** (**Alt+Q**) puts a warp marker on every transient and pulls it to the nearest grid line at the current grid value.
- **Reset** returns the clip to its original audio.

The **TCE** toggle in the toolbar turns the Trimmer into a Time Compression/Expansion trimmer: dragging an edge stretches the clip to the new length instead of revealing or hiding audio.

Stretches are rendered through the Rubber Band R3 engine and stay non-destructive: the clip keeps its original audio, so modes, ratios and markers can be changed or undone at any time, and a saved session re-renders them on open. Clips longer than the threshold in Preferences > Processing (8 seconds by default) render on a background thread behind a cancellable progress window; the result is applied only if the clip has not been edited meanwhile.

## Transients

**Tab** and **Shift+Tab** jump the playhead to the next or previous transient of the selected audio track (or of all audio tracks when none is selected). **Separate at Transients** in the clip menu splits a clip at every detected hit as one undo step. Transient sensitivity is a preference.

## Beat Detective

**Ctrl+8** (or the clip menu) opens Beat Detective with Sensitivity, Strength, Exclude Within, Swing, a smoothing mode and a crossfade length, and the three operations on the selected clips (or the selected track):

1. **Separate** at transients.
2. **Clip Conform**: each slice's start moves toward the nearest line of the edit grid by Strength; Swing delays every second line, 100% being the triplet position.
3. **Edit Smoothing** fills the gaps by extending each slice to the next, optionally overlapping with equal-power crossfades.

**Do All** runs the three in a row, as one undo step.
