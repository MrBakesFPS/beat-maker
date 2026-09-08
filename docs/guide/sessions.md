# Sessions, bounce and interchange

## Sessions and templates

A session is a `.bmk` folder holding `session.json` (every track, clip, pattern, MIDI sequence, instrument, insert with parameters and plugin state, send, automation lane, playlist, group, marker, I/O path and transport setting) plus an `Audio Files` folder for audio that only existed in memory, such as pencil edits, freezes and recordings. Audio kept outside the bundle is referenced by path and reported if missing. Elastic clips store their source file and stretch settings and are re-rendered on open, so a reopened session bounces byte-for-byte the same.

**File...** (or **Ctrl+N**, **Ctrl+O**, **Ctrl+S**, **Ctrl+Shift+S**) creates, opens and saves. In the Open browser a session bundle (`Name.bmk`) opens on a double-click, Return or the Open button; other folders still step inside. **Rename Session...** (File... menu, or the command palette) renames the open session: an unsaved one just takes the name for its title and first save, a saved one is saved and its `.bmk` folder renamed on disk (the media inside moves with it), then reopened from the new folder. A name already in use in that folder is refused. **Save As Template** writes a `.bmkt` to `~/Music/Beat Maker/Templates`; New Session offers those alongside the built-in Empty, Beat Making, Songwriter and Podcast templates. Sessions with a file autosave to `Session File Backups` inside the bundle every few minutes (interval and count in Preferences > Operation); after a crash the newest backup is offered on the next launch.

## Bounce to Disk

**Ctrl+B** renders the whole arrangement or the cycle range to WAV, AIFF or FLAC at 16, 24 or 32-bit float, with a configurable tail that lets drums ring out, trailing-silence trim and optional normalisation. It renders offline on a background thread through the same graph code as playback, so the file matches what you heard. Clipping is reported.

## Export Stems

**Ctrl+Alt+B** bounces each chosen track to its own file, post-fader with automation and sends, all the same length so they line up. Other tracks keep feeding the buses, so an aux return stem contains exactly the returns of the mix, and the stems sum back to the mix. Options: include aux returns in each stem, render through the master's inserts, cycle range only, file type, bit depth, tail.

## Import Session Data

**Shift+Alt+I** opens another session bundle so you can tick the tracks you want and bring them in with their clips, instruments, inserts, sends, automation and playlists. Place them at their original time or at the playhead, as new tracks or appended to existing tracks with the same name, optionally with the source's memory locations and tempo. VCA links survive when the VCA master comes along. Everything arrives as one undo step.

## Export AAF

**Ctrl+Alt+A** writes the session as an Advanced Authoring Format file for Pro Tools, Media Composer, Nuendo, Resolve and anything else that reads AAF.

- One timeline slot per track; stereo tracks become `.L` and `.R` slots, as Pro Tools expects.
- Clips become source clips with their positions and lengths, through master and source mobs.
- A timecode slot at 24, 25, 29.97 (drop) or 30 fps, and memory locations as markers.
- **Embed audio** puts 16 or 24-bit PCM essence in the file; **Link** writes WAV files to a `<name> Media` folder beside it. Move the folder with the file.
- **Consolidate clips** renders each clip's gain, gain line and fades into its own audio, so the other DAW hears exactly the session but has no trim handles. **Whole source files** keeps the full sources with handles and drops gain and fades.
- Instrument tracks are included once frozen.

OMF is not offered: Avid deprecated it and current DAWs import AAF. The writer is Beat Maker's own and its files are checked against two independent readers (pyaaf2 and LibAAF); they have not yet been opened in Pro Tools itself, so please report how an import went.
