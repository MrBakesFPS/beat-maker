# Mixing and effects

## The Mix window

**X** opens a Pro Tools-style Mix window with one channel strip per track plus a Master strip (it takes the editor panel's place: opening either closes the other). When the strips do not fit, the wheel over them or the scrollbar underneath scrolls sideways; the Master stays at the right. Each strip has 10 insert slots (five shown, and more as they fill) hosting the built-in effects or plugins (click an empty slot to add one, click an effect to switch it on or off, right-click it for Edit..., Bypass and Remove, the same choices as the track header's FX button, plus Replace With), 5 sends to 8 stereo buses (each send is a button showing its bus, pre/post and level; click it for the bus, pre-fader or no send), a pan knob (right-click it for the nine positions from Hard Left to Hard Right), a fader with a post-fader meter, mute and solo, an automation mode box, and output routing (Main or a bus). Right-click a strip's background for the **Pan** and **Automation** submenus the track header has: pan positions, the automation mode, and which lane the track's row shows. Aux Input tracks read a bus and can carry inserts and sends of their own. Every knob and fader gesture is one undo step, and the same strip pipeline runs live and in Bounce.

The pan law is centre-compensated; the depth (-2.5, -3, -4.5 or -6 dB) is a preference and goes through the engine.

### The sends panel

The **SENDS** button above a strip's sends opens the sends panel over the strips: every send on a wide dB scale with a scale drawn under it, a level you can drag or type in dB (-inf to +6), its bus and its pre/post switch. Levels are set here (the strip's send buttons show them). A drag is one undo step and writes automation like the fader does, and in Read mode the panel follows the lane. The panel stays open while you work in it, including while a bus is being chosen, and closes on a click anywhere else, its close button, Escape, or a second click on SENDS.

## Built-in effects

7-band **EQ** (HPF, low shelf, three parametric peaks, high shelf, LPF) with a live frequency-response display, **Compressor** with lookahead, **Limiter** (2 ms lookahead, ceiling), **Gate/Expander** (threshold, ratio, attack, hold, release, range), **De-esser** (split-band), **Delay** (digital, tape with saturated feedback, ping-pong), **Reverb**, **Chorus**, **Flanger**, **Phaser**, **Saturation** (soft, hard, tube), **Amp Sim** (drive, tone stack, presence, cabinet), **Utility** (gain, phase, width, mono), **Convolution Reverb** and **Pitch Correction**. Dynamics show a gain-reduction bar on their slot, and every parameter is automatable.

### Sidechain routing

The Compressor, Gate/Expander and De-esser editors have a **Key** input menu (Internal or any of the 8 buses) and a **Key Listen** button. Send the kick pre-fader to a bus, key the pad's compressor from that bus, and it pumps. Strips that feed a key bus are always processed before the strips they key, so the sidechain has no latency; a keyed insert shows [key] on its slot. Hosted plugins with a sidechain input bus get the same Key menu.

### Convolution Reverb

Six bundled spaces (Concert Hall, Chamber, Room, Plate, Ambience, Cathedral) or **Load IR...** for any audio file, with pre-delay, decay trim, low and high cut, width and mix. Responses are built on the message thread and swapped in without touching the audio thread.

### Pitch Correction

Detects the pitch of a monophonic source and pulls it to the nearest note of a Key and Scale (Chromatic, Major, Minor, pentatonics) through a real-time shifter. Retune Speed sets how fast it glides (0 ms is hard tune), Amount how far, Transpose adds an interval, Formant: Preserve keeps the voice's character, and Mix blends the delayed dry signal. The editor shows the detected note, its cents offset and the target note live; the shifter's latency is reported to delay compensation.

## Automation

Every track has an automation mode (Off, Read, Touch, Latch, Write) in its header and strip. Lanes exist for volume, pan, mute, send levels and insert parameters. Move a fader, pan or send while playing in Touch, Latch or Write to record a pass (one undo step; Write drops to Latch afterwards). In Read the mixer controls follow the lane. Switch a track's view from Clips to a lane to draw breakpoints: click to add, drag to move, right-click or Alt-click to delete. Volume ramps sample-accurately across each block. The Master strip has the same modes and a volume lane.

**Trim** adjusts existing volume automation relatively: while playing, fader moves are recorded as a trim pass and baked into the lane on stop; while stopped, a fader move scales the whole lane. Trim faders spring back and show the offset while you hold them.

## Metering and loudness

Right-click any strip meter to pick Sample Peak, RMS, Peak + RMS, VU (0 VU = -18 dBFS), or K-12, K-14 and K-20. Meters have a clip indicator that holds until clicked, dynamics inserts show gain reduction, and the Master strip carries an ITU-R BS.1770 loudness readout: Momentary, Short-term, gated Integrated, Loudness Range and True Peak (4x oversampled). Right-click the master meter to reset.

## Delay compensation

Every insert reports its latency (the Compressor's lookahead adds real latency) and each strip is delayed so all paths to the mix align, including tracks feeding an aux return. The mixer shows the applied delay per strip (dly, with the insert latency in brackets); Alt-click it to type a user offset. Frozen tracks have no insert latency. Compensation can be switched off in I/O Setup.

## System Usage

**Ctrl+Shift+U**, or click the CPU readout in the transport bar. The window shows the audio CPU load with a peak hold and a count of callbacks that overran their budget, the heaviest tracks by name (per-strip timing on the audio thread, so a reverb aux or a plugin shows up), the session audio in RAM, and the audio cache. The cache decodes each audio file once per sample rate and shares the buffer between every clip, pad and sample that uses it; the least recently used files that nothing references go first when the budget (Preferences > Processing) is exceeded, and audio in use is never dropped. If the readout turns red, freeze the heaviest tracks or raise the device buffer size.
