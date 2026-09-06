# Getting started

## Installing

Beat Maker is built from source (packaged builds arrive with the beta program; see [Contributing and releases](developer/contributing.md)). You need CMake 3.22 or newer, Ninja, a C++20 compiler and, on Linux, the usual JUCE dependencies: alsa-lib, freetype2, libx11, libxrandr, libxinerama, libxcursor, libxext, mesa, curl, webkit2gtk-4.1, gtk3 and fontconfig. JUCE, Rubber Band, the CLAP headers, Lua and Catch2 are fetched automatically on the first configure.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
"./build/ui/BeatMaker_artefacts/Release/Beat Maker"
```

Use a Release build for real work. A Debug build is several times slower on the audio thread and the convolution reverb alone can overrun it; the [System Usage](guide/mixing.md#system-usage) window shows you when that happens.

## The first launch

The Welcome window offers the session templates (Empty, Beat Making, Songwriter, Podcast, plus any you saved), the three sample projects, the Tour, the Tutorials and the Keyboard Shortcuts window. Switch it off there or in Preferences > Display when you no longer want it.

- **Sample projects** (Lo-fi Beat, Synth Sketch, Podcast Intro) are generated from the bundled loops and instruments into `~/Music/Beat Maker/Sessions/Sample Projects` the first time you open them, so you can break them freely.
- The **Tour** is a two-minute spotlight walk through the transport, Library, tracks, edit tools, editor panel, Smart Controls, Mix window and command palette.
- **Tutorials** are step lists that tick themselves off as you work in the session; each step has a "Do it" button if you would rather watch.

## An instant beat

1. Press **L** to open the Library and double-click a drum loop. It lands at the playhead, conformed to the session tempo.
2. Press **Ctrl+Shift+D** for a Drum Machine track with the starter beat, or **Ctrl+I** for a Synth track with a two-bar arpeggio.
3. Press **C** for Cycle and **Space** to play.
4. Press **B** for Smart Controls and turn the knobs, or **X** for the Mix window.
5. **Ctrl+B** bounces the arrangement to a file.

Every shortcut is also a command in the palette (**Ctrl+Shift+P** or **Ctrl+K**): type part of a name and press Return.

## Where things live

| What | Where |
|---|---|
| Sessions you save | wherever you choose; the default is `~/Music/Beat Maker/Sessions` |
| Recorded audio (unsaved sessions) | `~/Music/Beat Maker/Audio Files` |
| Templates | `~/Music/Beat Maker/Templates` |
| Your loops | `~/Music/Beat Maker/Loops` and any folder you add to the Library |
| Scripts | `~/Music/Beat Maker/Scripts` |
| Bounces, stems, AAF exports | `~/Music/Beat Maker/Bounces` by default |
| Preferences, plugin list | `~/.config/Beat Maker` |
| Logs and crash reports | `~/.config/Beat Maker/Logs` and `Crash Reports` |
