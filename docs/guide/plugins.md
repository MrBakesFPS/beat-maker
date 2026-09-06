# Plugins

Beat Maker hosts **VST3**, **LV2** and **CLAP** plugins on Linux (and AU on macOS). VST3, LV2 and AU go through JUCE; CLAP has its own host that loads `.clap` libraries from `~/.clap`, `/usr/lib/clap`, `/usr/local/lib/clap` and `$CLAP_PATH`, and presents each plugin to the same machinery: parameters, state, latency, audio ports (a second input port becomes the sidechain bus) and an X11-embedded editor when the plugin has one.

## Scanning

From any insert slot choose **Plugins > Scan for Plugins...**, then pick a plugin by format. Scanning validates each plugin in a separate process (the app relaunches itself for each one), so a plugin that crashes or hangs is blacklisted instead of taking the host down. The list persists in `~/.config/Beat Maker/plugins.xml`; delete that file to rescan from scratch.

## Using plugins

Loaded plugins report their latency to delay compensation, expose their parameters to automation lanes and to scripts, keep their state in the session file, and open their own editor window (or a generic one) when clicked. Plugins run in-process once loaded, so a plugin that misbehaves after passing the scan can still crash the app; the [crash report](troubleshooting.md) names the last actions, which usually points at it.

A plugin with a sidechain input bus (a compressor's "Sidechain" or "Key" bus) gets the same Key menu as the built-in dynamics; the bus is enabled when the plugin loads and the key is copied into it every block.
