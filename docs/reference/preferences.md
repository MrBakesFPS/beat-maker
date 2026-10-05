# Preferences

Preferences (Ctrl+,) apply immediately and live in `~/.config/Beat Maker/Beat Maker.preferences`. The search box finds a setting by any word in its name or description; changed settings show a mark and can be reset one at a time or per category. Generated from the app by `--dump-docs`.

## Display

| Setting | Default | What it does |
|---|---|---|
| Default track height | Medium | Height of track lanes in the edit window. Choices: Small, Medium, Large, Extra Large. |
| Show memory locations strip | on | The markers and sections strip above the ruler. |
| Theme | Dark | Colour palette for every view. High Contrast meets WCAG AAA contrast. Choices: Dark, High Contrast, Light. |
| Interface scale | 100% | Size of the whole interface (text, controls, tracks). Choices: 100%, 125%, 150%, 175%. |
| Announce status messages | on | Speak status bar messages through the screen reader. |
| Show the Welcome window at startup | on | Templates, sample projects, the tour and tutorials when Beat Maker opens without a session. |
| Show clip gain on clips | on | Append the clip gain in dB to clip names when it is not 0 dB. |
| Editor panel height | 300 px | Height of the note and drum editor panel; drag the bar above the panel to change it. Range 120 to 1200. |
| Note editor row height | 14 px | Vertical zoom of the piano roll: pixels per semitone (Alt+wheel over the notes, or the - and + buttons). Range 6 to 32. |
| Zoom sensitivity | 1.5 x | How much Ctrl+wheel and the R/T focus keys zoom per step. Range 1.1 to 3. |

## Operation

| Setting | Default | What it does |
|---|---|---|
| Timeline insertion follows playback | on | When on, stopping leaves the playhead where it stopped; when off, it returns to where playback started. |
| Latch record enable buttons | on | When off, arming a track disarms every other track. |
| Auto-backup interval | 3 min | Minutes between automatic backups of a saved session (0 = off). Range 0 to 60. |
| Auto-backups to keep | 5 | Newest backups kept in Session File Backups. Range 1 to 50. |
| Record bit depth | 24-bit | Bit depth of new audio files. Choices: 16-bit, 24-bit, 32-bit float. |
| Audio files folder | /home/mrbakesfps/Music/Beat Maker/Audio Files | Where new recordings are written. |
| Crash reports and recovery | on | Write a report if Beat Maker crashes and offer the newest auto-backup on the next launch. Reports stay on this computer. |
| Show New Session on startup | off | Offer the templates dialog when Beat Maker starts. |

## Editing

| Setting | Default | What it does |
|---|---|---|
| Default fade length | 10 ms | Length used by the focus keys D/G/F and new fades. Range 1 to 5000. |
| Default fade shape | Equal Power | Shape of new fades. Choices: Linear, Equal Power, S-Curve. |
| Clips follow tempo changes | on | When the tempo changes, clips, memory locations and automation keep their bar positions (tick-based). Off: they keep their time in seconds. |
| Loops re-conform on tempo change | on | Audio clips with a known source tempo (library loops) are re-stretched with Elastic Audio to the new tempo. |
| Select clips after paste | on | Pasted clips become the selection. |
| Nudge amount follows grid | on | The , and . keys move clips by the grid value. |
| Delete with a time selection | Delete clips in range | What Delete does when a time range is selected. Choices: Delete clips in range, Clear the range (separate first). |

## Metronome

| Setting | Default | What it does |
|---|---|---|
| Click level | -10 dB | How loud the metronome click is. Range -40 to 6. |
| Click sound | Beep | The click's character. Choices: Beep, Click, Wood. |
| Accent the downbeat | on | The first beat of every bar is louder and higher. |
| Only while recording | off | The click stays silent during plain playback. |
| Count-in | Off | Bars of click before the transport starts; clips and the recorder wait for it. Choices: Off, 1 bar, 2 bars, 4 bars. |
| Count in for | Recording only | When the count-in happens. Choices: Recording only, Recording and playback. |

## Mixing

| Setting | Default | What it does |
|---|---|---|
| Pan depth | -3 dB | Centre attenuation of the pan law. Choices: -2.5 dB, -3 dB, -4.5 dB, -6 dB. |
| Default meter type | Sample Peak | Meter type for new tracks. Choices: Sample Peak, RMS, Peak + RMS, VU, K-12, K-14, K-20. |
| Default automation mode | Read | Automation mode for new tracks. Choices: Off, Read, Touch, Latch, Write, Trim. |
| Solo latch | on | When off, soloing a track un-solos the others. |
| Peak hold | 2 s | How long meters hold their peak readout. Range 0 to 10. |

## Processing

| Setting | Default | What it does |
|---|---|---|
| Loop conform mode | Auto (Rhythmic for drums) | Elastic mode used when a library loop is conformed to the session tempo. Choices: Auto (Rhythmic for drums), Polyphonic, Rhythmic, Monophonic, Varispeed. |
| Background render threshold | 8 s | Clips longer than this render Elastic changes on a background thread. Range 0 to 600. |
| Default Elastic mode | Polyphonic | Mode used by TCE trims and warp markers on clips with Elastic off. Choices: Polyphonic, Rhythmic, Monophonic. |
| Audio cache size | 2048 MB | Decoded audio kept in memory for reuse (loops, samples, session files). Audio in use is never dropped. Range 128 to 16384. |
| Transient sensitivity | 50 % | Default sensitivity for tab-to-transient and Separate at Transients. Range 0 to 100. |

## MIDI

| Setting | Default | What it does |
|---|---|---|
| Default note velocity | 100 | Velocity of notes drawn in the piano roll. Range 1 to 127. |
| Soft note velocity | 70 | Velocity of notes drawn with Shift held. Range 1 to 127. |
| Audition notes when drawing | on | Play a note through the track's instrument when it is added or moved. |
