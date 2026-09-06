# Recording

## Basics

Arm an audio track (**R** in its header), pick its input from the I/O menu, optionally enable input monitoring (**I**), then press **Record** in the transport (or **R** on the keyboard). Takes are written as WAV (24-bit by default, see Preferences > Operation) to `~/Music/Beat Maker/Audio Files` on a background thread and land on the track at the exact sample where recording began. Stop ends the take; pressing Record again while playing punches out.

The whole pass is always kept on disk, so punches are sample-accurate and never lose audio.

## Record modes

The **Rec** button in the transport bar chooses the mode:

- **Normal** records until Stop, or between the punch points when there is a time selection: the transport rolls from the pre-roll, records exactly the selection and stops after the post-roll.
- **QuickPunch** rolls with the inputs captured; the Record button punches in and out as often as you like, and each punch becomes a clip.
- **TrackPunch** does the same per track: arm the tracks, press Record to roll, then each track's R button punches that track in or out (amber = waiting, red = punched in).
- **Loop** makes every Cycle pass a take on its own playlist (see [Playlists and comping](tracks-and-editing.md#playlists-and-comping)).

**Pre/Post** sets pre-roll and post-roll in bars or seconds.

## Inputs, outputs and buses

**I/O Setup** (**Ctrl+Alt+I** or the I/O... button) has Input, Output and Bus tabs that define named paths mapped to device channels, mono or stereo. Track inputs and outputs pick paths by name; an output path other than Main is a direct out that bypasses the master. Buses can be renamed. Automatic delay compensation can be switched off here too.

## Synchronization

**Ctrl+2** opens the Synchronization window. Beat Maker can send MIDI Beat Clock (24 ppqn with Start, Continue, Stop and Song Position Pointer on locate) or MIDI Time Code (quarter frames at 24, 25, 29.97 or 30 fps, full frames on locate, with a session start offset) to a MIDI output, or chase incoming clock (tempo, start/stop, position) or MTC (locate and run). "Beat Maker Sync" is a virtual ALSA or CoreMIDI port that other applications can connect to without hardware.
