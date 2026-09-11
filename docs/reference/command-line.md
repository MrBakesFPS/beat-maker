# Command line

Flags are processed in order, so a session or demo flag before an export flag exports that session. Plain file arguments are imported as audio. `--quit` exits when the flags are done, which makes headless renders and exports possible.

## Sessions and content

| Flag | Effect |
|---|---|
| `--session=<bundle>` | open a `.bmk` session |
| `--template=<name>` | new session from a template |
| `--save=<bundle>` | save (a `.bmkt` path saves a template) |
| `--sample-project=<name>` | open Lo-fi Beat, Synth Sketch or Podcast Intro |
| `--drums`, `--synth` | add a Drum Machine track with the starter beat, or a Synth track with an arpeggio |
| `--instrument=<name>[,<preset>]` | add an instrument track by name (case and spaces ignored): synth, fmsynth, wavetable, stack, chip, vox, pad, lead, pulse, sync, texture, sampler, granular, vinylsampler, piano, electricpiano, organ, harpsichord, clavinet, celesta, accordion, melodica, mallets, steeldrum, handpan, tubularbells, gamelan, pluck, strings, harp, guitar, solostrings, brass, solobrass, bigband, flute, clarinet, oboe, sax, harmonica, bass, subbass, slapbass, uprightbass |
| `--sample=<file>` | load a file into the last Sampler track |
| `--loop=<file>` | add a loop, tempo-conformed, at the playhead |
| `--fades=in_ms,out_ms[,gain_dB]` | apply to every audio clip |
| `--import-session=<bundle>[,seconds]` | Import Session Data of every track at an offset |
| `--marker-demo` | a demo session with sections and markers (replaces the session, so put it first) |
| `--script=<file>`, `--lua=<code>` | run Lua at startup |
| `--run=<command id>` | run any registered command |
| `--pref=<id>=<value>` | set a preference |

## Transport and recording

| Flag | Effect |
|---|---|
| `--play`, `--cycle` | start playback; enable Cycle |
| `--record` | add an armed audio track and start recording |
| `--record-mode=<Normal\|QuickPunch\|TrackPunch\|Loop>` | |
| `--pre-roll=<s>`, `--post-roll=<s>`, `--punch=<in>,<out>` | punch range in seconds |
| `--punch-at=<s>`, `--stop-at=<s>` | timed punch and stop |
| `--sync=<clock-out\|mtc-out\|clock-in\|mtc-in>` | synchronization |

## Rendering and export

| Flag | Effect |
|---|---|
| `--bounce=<file>` | render the arrangement and quit |
| `--stems=<dir>` | one WAV per track |
| `--freeze=<track>`, `--unfreeze=<track>`, `--commit=<track>` | track numbers from 1 |
| `--export-aaf=<file>[,link][,whole][,16]` | AAF export; embedded, consolidated, 24-bit unless told otherwise |
| `--elastic-async=<seconds>` | background render threshold |

## Windows and dialogs

`--mixer`, `--io-setup`, `--event-list`, `--sync-window`, `--script-console`, `--prefs-window`, `--palette=<query>`, `--focus`, `--welcome`, `--tour`, `--tutorials`, `--shortcuts`, `--system-usage`, `--bounce-dialog`, `--aaf-dialog`, `--rename=<name>` (renames the open session, as Rename Session... does), `--track-colour=<track>,<#rrggbb>` (colours a track, 1-based), `--instrument-chooser` (the Add Instrument Track chooser), `--kit-chooser` (the Add Drum Machine Track chooser), `--drums=<kit>` (a drum track on a bundled or user kit by name), `--kit-builder[=<kit>]` (the kit builder, starting from a kit), `--preset-builder[=<instrument>[,<preset>]]` (the preset builder), `--open-dialog[=<bundle>]` (the Open Session browser; with a bundle, selects it and opens it the way a double-click or Return would), `--import-dialog=<bundle>`, `--crash-dialog`.

## Demos

`--elastic-demo`, `--sidechain-demo`, `--convolution-demo`, `--pitch-demo`, `--beat-detective-demo`, `--insert-demo`, `--automation-demo`, `--clip-gain-demo`, `--group-demo`, `--playlist-demo`, `--vca` each build a small session showing one feature; combine with `--bounce=` or `--mixer`.

## Plugins, diagnostics and maintenance

| Flag | Effect |
|---|---|
| `--scan-plugins` | scan every format |
| `--insert-plugin=<name>` | insert a known plugin on the first track |
| `--scan-plugin=<format>\|<id>` | the out-of-process validation child (used by the scanner) |
| `--crash` | crash deliberately to check the crash reporter |
| `--diagnostics` | write a diagnostics file |
| `--dump-docs=<dir>` | write the shortcut and preference tables of this build as Markdown |
| `--quit` | exit once the flags have run |
