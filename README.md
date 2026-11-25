# Dot BIOS Helper

A Kode Dot app that types into a PC's BIOS for you. The Dot acts as a USB keyboard and plays a **profile**, a short key script such as "tap F10 while the PC boots, go right twice, press Enter". You can **arm** a profile so it starts by itself the moment a PC switches on. With batch mode you can do one PC after another.

Profiles are plain text files on the Dot's microSD card. You edit them in a browser over Wi-Fi, with a check that points at the broken line. You can also edit them on the Dot's glass, or with any text editor.

> **Not tested on a real Kode Dot.** It is built with ESP-IDF v6.1 and kode-sdk v0.1.1, and has run only in the kode-sim emulator and in host tests. See [What is tested](#what-is-tested).

Dot BIOS Helper is an independent app. Kode did not make it and does not endorse it.

## Using it

1. Start **BIOS Helper** from Apps.
2. Choose a profile with the pad (up/down) or by tapping it.
3. Press **Start** (or **Arm**, for a plug profile), or press TOP.
4. Connect the Dot to the PC:
   - **Dock pads (the default).** Use a magnetic USB cable on the Dot's dock pads. This is the port the Dot's USB device apps use.
   - **USB-C (test).** Settings → USB port → USB-C. The app swaps the P4's two full-speed PHYs so the keyboard is on the USB-C socket. While it runs, the console and flashing are not on the cable; a reset gives them back. **Not tried on a board.**
5. A **plug** profile waits, shown as ARMED, until a PC takes the keyboard. That happens as the PC boots or as you plug the cable in. Then it runs.

| On the Dot | Does |
|---|---|
| Pad up/down | Choose a profile |
| TOP | Start / Arm / Go now / Next / OK |
| Pad left / right | Settings / the keyboard view |
| BOT | Back to the list (hold: back to kodeOS) |
| During an `ask` | The pad sends arrow keys and TOP sends Enter, so you can fix things by hand |

**Keyboard** (from the list) turns the glass into a keyboard: Esc, F1-F12, Del, Tab, Enter, Ctrl+Alt+Del and more. The pad sends arrow keys.

### The web editor

When the Dot is on Wi-Fi (join a network once in kodeOS Settings), the list shows `Edit profiles: http://<dot>`. Open that address in a browser on the same network. You can:

- edit profiles, with line numbers, buttons that insert commands and keys, and a live error check;
- save to `/sdcard/bioshelper/<name>.txt`, delete, and pick the profile the Dot uses;
- follow what the Dot is doing and press Start, Arm, Go, Next and Stop from the browser, or type the keypad's code there;
- send keys and text to the PC right away.

The editor has **no login**. Anyone on the same network can open it. Switch it off under Settings → Web editor on networks you do not trust.

## Profiles

```text
title HP t550: Secure Boot off
desc F10 into setup, the Secure Boot page, save with F10, then F9
arm plug          # start when a PC takes the keyboard
confirm on        # ask "Run on this PC?" first
gap 200           # ms between keys
spam F10 10       # tap F10 for 10 s after the PC starts listening
wait 2000
keys RIGHT RIGHT UP ENTER F10 RIGHT F10 F10 ENTER ENTER
spam F9 10
```

**Settings**

| Line | Means |
|---|---|
| `title <text>` / `desc <text>` | The name and the line under it in the list |
| `arm manual` | Start runs at once. The keys wait until a PC takes them. |
| `arm plug` | Start arms. The run begins when a PC takes the keyboard. If a PC already has the keyboard when you arm, unplug it or switch it off first. |
| `arm delay <s>` | Start counts down, then runs |
| `after stop` | The default: done is done |
| `after rearm` | **Batch mode.** When the cable comes out, the profile arms again for the next PC. The Dot counts the PCs. |
| `after chain <title>` | Run another profile next, on the same PC |
| `confirm on` | A yes/no question before arming |
| `timeout <min>` | An armed profile disarms itself after this many minutes. The default is 30; 0 means never. |

**Commands**

| Line | Does |
|---|---|
| `spam <KEY> <secs> [every_ms]` | Taps KEY while the PC boots. The seconds count from the first key the PC takes. **Next** ends it early; it stops after 120 s at most. |
| `keys <KEY> ...` | Taps keys one by one. `DOWN*3` taps DOWN three times. |
| `hold <KEY> <ms>` | Presses and holds a key |
| `type <text>` / `line <text>` | Types text on a US layout. `line` adds Enter. |
| `wait <ms>` / `wait 2s` | Pauses |
| `waitpc [secs]` | Waits until a PC takes the keyboard. 0 or no number waits forever. |
| `ask <text>` | Pauses until **Next**. The pad and TOP work as arrow keys and Enter meanwhile. |
| `msg <text>` | Shows a line on the glass and goes on |
| `code [n] [prompt]` | Shows a keypad. Type the code the PC shows; the Dot types it plus Enter. |
| `input <name> [n] [prompt]` | Shows a keypad and stores the number in `$name`. Nothing is typed. |
| `set <name> <value>` | Sets a variable. `$name` puts it into `type`, `line`, `keys`, `ask` and `msg`. |
| `repeat <n>` ... `end` | Loops, up to 4 deep. `repeat 0` loops until you press Stop. |
| `gap <ms>` / `tap <ms>` | The time between keys (default 300) and how long a key stays down (default 40) |
| `beep` | Makes the Dot buzz |

**Keys:** `ENTER ESC TAB SPACE BKSP DEL INSERT HOME END PGUP PGDN UP DOWN LEFT RIGHT F1`-`F24 PRTSC PAUSE MENU CAPSLOCK NUMLOCK KP0`-`KP9`, any single character, and combinations with `CTRL ALT SHIFT WIN` (also `RALT`/`ALTGR`, `RCTRL`, ...): for example `CTRL+ALT+DEL`, `WIN+R`, `ALT+F4`, `SHIFT+TAB`.

`#` starts a comment. A profile can hold up to 64 commands and 4 KB. The list marks a profile with errors in red, and the Dot will not run it.

### Built in

| Profile | Arm |
|---|---|
| HP t550: Secure Boot off | plug, confirm |
| Boot menu (F12) / BIOS setup (DEL) / BIOS setup (F2) / HP: boot menu (F9) | plug |
| Batch: F12 on every PC | plug, rearm |
| Example: every command | delay 3, confirm |

The HP t550 key order comes from one technician's notes for one firmware version. **Try any profile on one machine before running it on many.** If you save a card file with a built-in's name (for example `boot-menu-f12.txt`), it replaces that built-in.

## Safety

- A profile **never** starts just because the app opens. It starts only after Start (manual or delay) or Arm (plug). Opening the app while the Dot is plugged into your own computer types nothing.
- An armed plug profile does not fire into a PC that already has the keyboard at the moment you arm. It waits for that PC to go away first.
- An armed profile disarms itself after `timeout` minutes.
- Stop and Disarm are always on the glass, and in the editor.

## What is tested

| Part | Status |
|---|---|
| Profile language: parser, player, arming, batch, chain, keypad, variables, loops (`test/script_test.c`, 16 tests) | Host, also under ASan and UBSan |
| Every view of the glass (`test/ui_render.c`) | Host renders, checked by eye |
| Build for the ESP32-P4 with ESP-IDF v6.1 | Yes |
| Boot in kode-sim: keyboard up, built-ins listed, card read, Wi-Fi joined, editor served | Yes |
| kode-sim end to end (`tools/sim_e2e.py`): check and save through the editor, arm, plug the simulated PC into the dock port, the run, what the PC typed, the editor's remote typing | Passed |

**Not tested on a real Dot:** the keyboard on the dock pads with a real PC and a real BIOS (the simulator's computer reads boot reports, but is no BIOS); the USB-C mode (PHY swap); the microSD on real hardware; the web editor over real Wi-Fi.

## Building

ESP-IDF **v6.1**:

```bash
idf.py build
```

The first build fetches kode-sdk v0.1.1 and TinyUSB. The image is `build/dot_bios_helper.bin`. A prebuilt one is on the Releases page.

Host tests:

```bash
cd test && cc -std=c11 -Wall -Wextra -I../main script_test.c ../main/script.c ../main/keys.c ../main/builtin.c -o script_test && ./script_test
```

## Installing

With `kcli.py` from a clone of the KodeOS repository:

```bash
python KodeOS/tools/kcli.py -p PORT install build/dot_bios_helper.bin
```

## How it works

- `script.c`: the profile language. It is pure C and talks to the keyboard through four calls, so the host test can drive it with a fake PC.
- `hidkb.c`: TinyUSB with one boot-protocol HID keyboard (the protocol a BIOS reads), on the P4's full-speed OTG port. One task owns TinyUSB and plays the key queue.
- `app.c`: the Active Object. It ticks the player, draws the glass (`ui.c`), reads and writes the card through AO_Storage, and takes the editor's commands.
- `web.c` with `web/index.html`: the editor.

## Licence

GPL-3.0-or-later, like the kode-sdk it links: see `LICENSE`. The fonts are Outfit and Terminus (SIL OFL 1.1, `tools/fonts/`). Kode, Kode Dot and kodeOS are trademarks of Kode; this app uses them only to say what it runs on.
