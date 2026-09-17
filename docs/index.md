# Beat Maker

Beat Maker is a digital audio workstation with a GarageBand-style surface and Pro Tools-style depth. Drop a loop, press play, and it makes a beat; open the Mix window, the Event List or the I/O Setup and it is a full production tool with automation, delay compensation, sidechains, Elastic Audio, plugin hosting, scripting and interchange with other DAWs.

It is free software under the GNU General Public License v3.0 or later. Time stretching and pitch shifting use the [Rubber Band Library](https://breakfastquay.com/rubberband/) under the GPL.

## Where to start

- [Getting started](getting-started.md): build or install, the first launch, the Welcome window, the tour and the sample projects.
- The [user guide](guide/tracks-and-editing.md) walks through the app by area: tracks and editing, recording, instruments, Elastic Audio, mixing, plugins, sessions and interchange, scripting, accessibility.
- The [reference](reference/shortcuts.md) lists every shortcut, preference and command-line flag. The shortcut and preference tables are generated from the app itself, so they match the build you run.
- [Developers](developer/architecture.md): how the engine, model and UI fit together, how to build and test, and how releases are made.

## The two views

The **Surface** is what opens by default: transport bar, tracks with waveform and pattern clips, Smart Controls and an editor panel for the selected track. The **Depth** windows sit behind it and open on demand: the Mix window, I/O Setup, Memory Locations, MIDI Event List, Synchronization, Beat Detective, Preferences, the Script Console, the System Usage window and the export dialogs. Both are views on the same session document, and every change in either is one undo step.

## Conventions in this guide

- Shortcuts are given for Linux and Windows. On macOS read Cmd for Ctrl.
- Files: sessions are `.bmk` folders, templates `.bmkt`. Audio you record lands in `~/Music/Beat Maker/Audio Files`; settings, plugin lists, logs and crash reports live in `~/.config/Beat Maker`.
- Track numbers in commands and scripts start at 1, as on screen.
