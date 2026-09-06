# Troubleshooting

## No sound, or the wrong device

Beat Maker opens the default audio device. On Linux that is usually PipeWire or PulseAudio through ALSA. The System Usage window (Ctrl+Shift+U) and Help > Report a Problem both name the device in use, its sample rate and its buffer size.

## Crackles and overruns

Open **System Usage** (**Ctrl+Shift+U**). A red CPU readout or a growing overrun count means the audio thread is missing its deadline. Freeze the heaviest tracks (the window lists them), raise the device buffer size, or use a Release build: a Debug build is many times slower on the audio thread.

## A plugin misbehaves

Scanning runs each plugin in its own process, so a plugin that crashes during the scan is blacklisted and the app is unaffected. Once loaded, plugins run in-process. If the app goes down after inserting a plugin, the crash report's recent actions will show the insert; remove `~/.config/Beat Maker/plugins.xml` to rescan, or leave that plugin out.

## After a crash

A signal handler writes a report (version, session path, audio device, the last 32 actions and a backtrace) to `~/.config/Beat Maker/Crash Reports`, using only signal-safe calls. The next launch shows it, with **Copy Report**, **Show Reports Folder** and, when the session's newest auto-backup is newer than its last save, **Recover Auto-backup**. Recovery opens the backup and points Save at the original session. Nothing is sent anywhere; Preferences > Operation can switch reports off.

## Reporting a problem

**Help > Report a Problem** writes a diagnostics file to the Crash Reports folder with the system, the audio device, a session summary, recent actions, the last crash report and the tail of the rolling log (`~/.config/Beat Maker/Logs/Beat Maker.log`). Attach it to an issue with what you did and what you expected. The file contains file paths and track names but no audio.

## Missing audio files

A session references audio outside its bundle by path. If a file has moved, the open reports it and the clip plays silence; put the file back, or import it again. Recordings, freezes and pencil edits always live inside the bundle.
