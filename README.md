# spink0
![pic of the spink-0 PCB adaption](hardware/spink-0.jpg)

This repository contains the source code and the schematics for an Ableton Link to clock/reset converter.

And the necessary changes & hardware files for the spink-0 adaption.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | `pinkd`, the Link daemon (C++17): clock/reset via PWM audio, MIDI out, front panel |
| `modules/link` | git submodule: [Ableton Link](https://github.com/Ableton/link) 4.1.1 |
| `support/network/` | `spink-net`: WLAN mode switching and the setup page (Python, NetworkManager) |
| `support/systemd/` | `pinkd.service`, `spink-netd.service` |
| `hardware/spink0_adaption/` | spink0 main board (`060_spink0_MB`, KiCad 9) and display board (`060_spink0_DB`, KiCad 5) |
| `hardware/old_pink-0_hw/` | original pink-0 board (Eagle) |

## How it works

- **`pinkd`** joins the Link session and renders the clock (right channel) and reset (left channel)
  as audio on the Pi's PWM pins (GPIO13 / GPIO18), buffered by the op-amp stage. It also sends
  MIDI clock on GPIO14 and runs the front panel. It writes its state to `/run/spink/status.json`.
- **`spink-netd`** keeps the module reachable: NetworkManager joins saved WLAN networks by itself;
  if none is connected for about a minute (or on request), the module opens its own access point
  `spink-XXXX` with a setup page.
- Both run as systemd services and are restarted automatically.

## Front panel

| Control | Action |
|---|---|
| encoder | change the value shown: tempo (0.25 BPM steps), loop length, clock multiplier |
| encoder button, short press | next page: tempo → length → clock multiplier → Link peers (`2P`) |
| encoder button, **held 3 s** | switch WLAN mode: shows `----`, then `AP` (access point) or `NET` (joined a network) |
| run button | start / stop the clock (on press) |

## Installation

### Requirements

- Raspberry Pi Zero W with the spink0 boards.
- **Raspberry Pi OS Lite (32-bit), current release** (Debian 13 "Trixie" based or newer: the
  build needs libgpiod 2, Bookworm only has 1.x), written with Raspberry Pi Imager. In the
  Imager settings, enable SSH and set the **WLAN country** (and optionally your WLAN, so the Pi is
  reachable for the installation).
- Solder jumpers J5/J6 on the main board set so that **GPIO20 drives the tempo LED** and GPIO14
  goes to the MIDI driver.

### Install

On the Pi:

```bash
sudo apt-get install -y git
git clone --recursive https://github.com/seismicindustries/spink0.git
cd spink0
./install.sh
sudo reboot
```

Optional: `SPINK_AP_PSK=yourpassword ./install.sh` sets the access point password (default
`spinkspink`), `SPINK_WIFI_COUNTRY=CH ./install.sh` sets the WLAN country if the Imager did not.

`install.sh`:

1. installs the build tools and `libgpiod-dev`, `portaudio19-dev`, `libasound2-dev`,
   `network-manager`,
2. fetches the Link submodule, builds `pinkd` and installs it to `/usr/local/bin/pinkd`,
3. installs `spink-net` to `/usr/local/bin` and the setup page to `/usr/local/share/spink/www`,
4. configures NetworkManager: WLAN power saving off, the access point connection `spink-ap`
   (`spink-` + the end of the MAC address, WPA2), and a DNS rule for the captive portal,
5. adds to `/boot/firmware/config.txt`: `dtparam=audio=on`, `force_turbo=1`, `enable_uart=1`,
   `dtoverlay=disable-bt`, `dtoverlay=midi-uart0`; removes the serial console from
   `cmdline.txt` (backup: `cmdline.txt.bak`). Bluetooth is not available afterwards,
6. installs and enables `pinkd.service` and `spink-netd.service`.

To update: `git pull && git submodule update --init --recursive && ./install.sh && sudo reboot`.

### Manual build

```bash
sudo apt-get install -y cmake build-essential pkg-config libgpiod-dev portaudio19-dev libasound2-dev
git submodule update --init --recursive
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
sudo cmake --install build   # /usr/local/bin/pinkd
```

| CMake option | Default | Meaning |
|---|---|---|
| `USE_PI_ZERO` | `ON` | front panel (display, encoder, buttons, LEDs) via libgpiod |

On a desktop (macOS: `brew install cmake pkg-config portaudio`) the same build runs without GPIO
and MIDI, for development.

## Using it

### Logs

```bash
journalctl -u pinkd -f        # audio device, Link peers, start/stop, underruns, timeline jumps
journalctl -u spink-netd -f   # WLAN mode changes, saved networks
```

### Setup page and WLAN

- In access point mode, join `spink-XXXX` (password `spinkspink` unless changed). Most phones open
  the setup page by themselves; otherwise go to `http://10.42.0.1/`.
- The page shows tempo, Link peers and clock state, and manages the saved WLAN networks.
- **Changes are only possible in access point mode.** On a venue network the page is read only,
  so nobody else on that network can reconfigure the module.
- "Join saved network" leaves access point mode; the module then joins the best saved network in
  range. Without one, it opens the access point again after about a minute.
- On the command line: `spink-net status|ap|client|toggle`.

### Audio device

`pinkd` uses the Pi's PWM audio output ("Headphones"). To use another device, set
`Environment=PINK_AUDIO_DEVICE=<part of the device name>` in `pinkd.service`. The sample rate is
the lowest of 11025 / 22050 / 44100 / 48000 Hz the device accepts.

### MIDI out

The MIDI OUT jack sends MIDI clock (24 per beat) locked to the Link session:

- clock runs continuously, also while stopped, so connected devices follow the tempo,
- **Start** right before the clock tick on beat 0, the same point where the CV clock and reset
  outputs start (run button),
- **Stop** when the run button stops the clock,
- if a start comes too late for beat 0, **Song Position Pointer + Continue** on the next 16th,
  so devices still lock in phase.

MIDI clock is always 24 per beat; the clock multiplier only affects the CV clock output.

Hardware: solder jumpers J5/J6 route GPIO14 to the MIDI driver (and GPIO20 to the tempo LED),
J2/J3 select TRS type A or B for the 3.5 mm jack.

`pinkd` opens `/dev/serial0` at "38400" baud, which `dtoverlay=midi-uart0` turns into 31250.
Without that setup (or on a desktop build) MIDI out is disabled and a warning is logged.

## Not yet tested on hardware

This version (Raspberry Pi OS, Link 4, libgpiod, NetworkManager) has been compile-checked and its
network part tested against a simulated NetworkManager, but not yet run on a spink0. Points to
watch on the first run:

- the display digit on **GPIO15** (UART RX): with the UART enabled, libgpiod must be able to take
  that pin over as an output; if not, `pinkd` logs "front panel unavailable" and runs headless,
- the PWM pin setup in `pinkd.service` (`pinctrl set 18 a5`, `pinctrl set 13 a0`),
- the audio device and sample rate in `journalctl -u pinkd` ("audio: …").
