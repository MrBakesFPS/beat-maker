# Architecture

```
engine/       Real-time audio graph, transport, DSP, device I/O
model/        Undoable session document
ui/           Surface (simple) and Depth (pro) views on one component tree
plugins/      VST3 / LV2 / AU hosting through JUCE, and the CLAP host
scripting/    Lua engine and host bindings
persistence/  Session format, templates, autosave, import/export, AAF
assets/       Bundled loops, presets, icons
tests/        Catch2 unit tests and offline render tests
docs/         This guide (MkDocs)
tools/        Generators and packaging
```

## Threads and ownership

`engine/` runs on the audio thread and never allocates or locks. The `AudioGraph` mixes an immutable `RenderSnapshot` and advances the `Transport`. `model/` is the undoable `Session` document; the UI only ever mutates it through `Command` objects, and every command is one undo step. Whenever the session changes, the message thread builds a new `RenderSnapshot` and hands it to the graph through a lock-free exchange; retired snapshots are freed back on the message thread.

Step patterns and drum kits are immutable and replaced copy-on-write. Every source (clip, drum voice, instrument, monitored input) is tagged with a channel strip; the graph renders each strip's sources into a scratch buffer, runs its inserts (stateful `Effect` instances owned by the model, parameters immutable and copy-on-write), pre-fader sends, fader and pan, meter, post-fader sends, then routes to the main mix or a bus. Strips are processed in a topological order so that senders come before the aux and keyed strips that read them. Instrument tracks own a stateful `Instrument` instance whose immutable parameters are swapped copy-on-write. MIDI notes carry their own gate length so no note-off is scheduled across blocks.

The `Recorder` receives every input block on the audio thread and pushes it into per-track lock-free ring buffers that a background thread flushes to WAV. `Bouncer` renders a snapshot through a private `Transport` and `AudioGraph` with fresh instrument and effect instances, so an offline bounce is bit-identical to live playback and never shares DSP state with the live thread. Elastic Audio is offline too: `engine::TimeStretch` renders a clip's original audio through a `StretchSpec` and `SetClipElasticCommand` swaps the rendered buffer in, remapping the clip's offset, length, fades and gain line. A `PerformanceMonitor` times each callback and each strip for the System Usage window.

## Persistence

Sessions are `.bmk` folders with `session.json` and an `Audio Files` folder. `AudioFileLoader` caches decoded audio (keyed by path, size, modification time and sample rate) with an LRU byte budget, so shared audio is decoded once. `AafExport` writes AAF through Beat Maker's own Compound File Binary and AAF stored-format writers; the standard AAF dictionaries are embedded as generated data.

## Rules that keep it safe

- No allocation, locks or blocking calls on the audio thread. Snapshots, parameter sets and kits are immutable once published.
- Effect and instrument instances are owned by the model and reconfigured on the message thread; `Effect::paramsChanged` is the hook for work that cannot happen on the audio thread (impulse loading, for example).
- Every mutation is a `Command`; transient state (transport, selection) is not undoable.
- The crash handler only uses signal-safe calls and pre-formatted buffers.
