# Hughes & Kettner GM40 Remote — Linux

A native Linux desktop control interface and preset library manager for the **Hughes & Kettner GrandMeister Deluxe 40** guitar amplifier, built with **C++ / Qt 6** and **ALSA MIDI**.

## Features

* **Real-Time Amp Control**: Bidirectional SysEx & MIDI Program Change synchronization (Gain, Volume, 3-Band EQ, Presence, Resonance, Reverb, Delay with Tap Tempo & BPM sync, Modulation, Boost, Noise Gate, FX Loop, Power Soak, Global Mute).
* **Preset Management**:
  * Browse, rename, compare (`Compare to HD`), and revert (`Restore Orig.`) any of the 128 presets.
  * Save active settings directly to disk (`Save to HD` / `Save to HD number`) or write them to the amplifier's internal flash memory (`Save to Amp` / `Save to Amp number`).
  * Automatic prompt when leaving an unsaved modified preset.
* **Library Editor**: Dual-pane preset organizer to import presets from external `.gm40` banks, copy, swap, reorder setlists, rename, or audition presets live on the amp.
* **Full Backup & Restore**: One-click 128-preset dump from/to the amplifier and Factory Reset support.
* **Global Settings & Custom Shortcuts**: Configurable MIDI channel (1–16 or OMNI), Global Power Soak / Global Power Amp EQ toggles, and customizable multi-key shortcuts.
* **Single Preset Import/Export (`.gmp`)**: Export or import individual presets with overwrite confirmation.
* **Synchronized Disk & Amp Workflow**: Unified Save / Save As actions that keep your local `.gm40` library and amplifier flash memory in sync.
* **Enhanced Library Editor**: Dual-pane organizer with live parameter preview, drag-and-drop, multi-select batch import, and Auto-Audition on click.


## Dependencies

To build from source on Linux, you need a C++17 compiler, **Qt 6** (`Core`, `Gui`, `Widgets`), and **ALSA** development libraries:

### Debian / Ubuntu / Linux Mint
```bash
sudo apt update
sudo apt install build-essential qt6-base-dev libasound2-dev
```

### Fedora
```bash
sudo dnf install gcc-c++ make qt6-qtbase-devel alsa-lib-devel
```

### Arch Linux / Manjaro
```bash
sudo pacman -S base-devel qt6-base alsa-lib
```

## Build & Run

```bash
git clone https://github.com/altern67/GM40Remote.git
cd GM40Remote
qmake6
make -j4
./GM40Remote
```

*(Note: On first launch, configuration and factory bank files are automatically initialized in `~/.config/HughesKettner/`).*

## Author & License

* **Author**: altern67 ([altern67@yahoo.com](mailto:altern67@yahoo.com))
* **License**: GNU General Public License v3.0 (GPLv3)
