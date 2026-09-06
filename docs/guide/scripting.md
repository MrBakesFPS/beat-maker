# Scripting with Lua

The **Script Console** (**Ctrl+Alt+L**) runs a sandboxed Lua 5.4 (no `io`, no process control, no `dofile`) against the open session. Every change a script makes is an undoable command, exactly like the UI's. Scripts saved to `~/Music/Beat Maker/Scripts` appear in the command palette as "Script: name", and scripts can register their own commands.

`--script=<file>` and `--lua=<code>` run at startup, which makes scripted smoke tests and batch bounces possible:

```
"Beat Maker" --script=build_song.lua --bounce=song.wav --quit
```

Indices are 1-based. Times are seconds unless a function says beats or bars. Track kinds: `audio`, `drums`, `synth`, `fm`, `wavetable`, `sampler`, `electricpiano`, `bass`, `aux`, `vca`. `print` writes to the console and the log.

## beatmaker.transport

| Function | Effect |
|---|---|
| `play()`, `stop()`, `is_playing()` | transport control |
| `position()`, `set_position(seconds)` | playhead |
| `bpm()`, `set_bpm(bpm)` | tempo (20 to 400) |
| `bar()`, `locate_bar(n)` | current bar; locate to the start of bar n |
| `beats_to_seconds(beats)` | tempo map conversion |
| `set_cycle(on)` | cycle on or off |

## beatmaker.session

| Function | Effect |
|---|---|
| `tracks()`, `track(i)`, `num_tracks()` | track tables with `index`, `id`, `name`, `kind`, `gain`, `gain_db`, `pan`, `mute`, `solo`, `armed`, `num_clips`, `output_bus` |
| `add_track(kind[, name])` | returns the new track's index |
| `rename_track(i, name)`, `remove_track(i)` | |
| `set_gain(i, g)`, `set_gain_db(i, db)`, `set_pan(i, p)` | gain 0 to 2, pan -1 to 1 |
| `set_mute(i, on)`, `set_solo(i, on)`, `set_arm(i, on)` | |
| `clips(i)` | clip tables with `index`, `kind` (`audio`, `pattern`, `midi`), `name`, `start`, `length`, `end` in seconds |
| `move_clip(i, c, start)`, `trim_clip(i, c, start, length)` | |
| `delete_clip(i, c)`, `duplicate_clip(i, c)`, `set_clip_gain(i, c, g)` | clip gain 0 to 4 |
| `markers()` | tables with `id`, `name`, `seconds`, `end_seconds`, `is_section` |
| `add_marker(name, seconds[, end_seconds])` | returns the id; an end makes a section |
| `remove_marker(id)` | |
| `set_insert(i, slot, effect)` | `i = 0` is the master; effect by name (`Compressor`, `EQ`, ...) or nil to clear |
| `set_insert_param(i, slot, name_or_number, value)` | parameters by name or 1-based number |
| `insert_params(i, slot)` | tables with `name`, `value`, `min`, `max` |
| `set_send(i, slot, bus[, gain[, pre]])` | bus 1 to 8 or nil to clear |
| `midi_notes(i, c)` | tables with `pitch`, `velocity`, `start`, `length` in beats |
| `set_midi_notes(i, c, notes)` | replaces the clip's notes with a list of such tables |
| `add_midi_note(i, c, pitch[, velocity[, start[, length]]])` | |
| `undo()`, `redo()`, `length()` | session length in seconds |

## beatmaker.app

| Function | Effect |
|---|---|
| `status(text)`, `log(text)` | status bar and log |
| `run_command(id)` | any registered command by id (see the shortcuts reference); returns whether it ran |
| `bounce(path)`, `save(path)`, `open(path)` | paths relative to the working directory |
| `pref(id)`, `set_pref(id, value)` | preferences by id |
| `register_command(id, name, fn)` | adds a command to the palette |

## Example

```lua
local s, t, app = beatmaker.session, beatmaker.transport, beatmaker.app
t.set_bpm(92)
local drums = s.add_track('drums', 'Beat')
local keys = s.add_track('electricpiano', 'Keys')
s.set_midi_notes(keys, 1, { {pitch=60, start=0, length=2}, {pitch=64, start=0, length=2}, {pitch=67, start=2, length=2} })
s.set_insert(keys, 1, 'Compressor'); s.set_insert_param(keys, 1, 'Threshold', -24)
s.set_send(keys, 1, 1, 0.4, false)
s.add_marker('Intro', 0, t.beats_to_seconds(8))
app.register_command('script.louder', 'Keys +3 dB', function() s.set_gain_db(keys, s.track(keys).gain_db + 3) end)
app.status('song built')
```
