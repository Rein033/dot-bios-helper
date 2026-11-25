# The Kode-watch version

The same profile language and player (`../main/script.c`, `keys.c`, `builtin.c`) run as the **BIOS helper** app of Kode-watch OS (from 0.42.0). The Kode-watch is an ESP32-S3 watch, so the code here is only its glue:

- `bios.c` / `bios.h`: the profiles and the player. Profiles are read from `/data/bios.txt` first, then `/data/bios/<name>.txt` (sorted), then the built-in ones. A file `/data/bios/<name>.txt` replaces the built-in profile of that name. Edit the files with the watch's Files app or the hub.
- `hidkb.c` / `hidkb.h`: the S3's one USB PHY as a keyboard (esp_tinyusb). It starts only when you press Start, Arm or Manual, and gives the PHY back to the console when you leave the app. This version adds hold times and taps of modifiers alone.
- `os_bios_ui.inc`: the app's screens. They replace the BIOS helper section of the watch's `os.c`.

On the watch:

- swipe left or right to pick a profile;
- tap the circle (Start or Arm);
- an `arm plug` profile also arms by itself when USB power comes in (once per plug-in). Opening the app while the watch is already on power arms nothing;
- while it runs, only the back bar leaves the app.

The watch has no web editor; use files instead.
