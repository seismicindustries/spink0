# spink0
![pic of the spink-0 PCB adaption](hardware/spink-0.jpg)

This repository contains the source code and the schematics for an Ableton Link to clock/reset converter.

And the necessary changes & hardware files for the spink-0 adaption.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | `pinkd`, the Link daemon (C++) |
| `modules/` | git submodules: Ableton Link, PortAudio, RtAudio, nanomsg, nmws, cereal |
| `support/` | init script, hostapd config, web pages, WLAN mode switch scripts, systemd units |
| `hardware/spink0_adaption/` | spink0 main board (`060_spink0_MB`, KiCad 9) and display board (`060_spink0_DB`, KiCad 5) |
| `hardware/old_pink-0_hw/` | original pink-0 board (Eagle) |

## Building and installing

### Requirements

- Raspberry Pi Zero W with the spink0 boards.
- **Raspbian / Raspberry Pi OS up to Buster (Legacy), 32-bit Lite.** The installer needs the
  `wiringpi` apt package, which was dropped in Bullseye. On Bullseye or Bookworm the
  installation fails until the planned move to libgpiod is done.
- Network access on the Pi (the installer clones submodules and installs packages).
- Solder jumpers J5/J6 on the main board set so that **GPIO20 drives the tempo LED**
  (GPIO14 is reserved for MIDI out).

### Quick install (on the Pi)

```bash
git clone --recursive https://github.com/seismicindustries/spink0.git
cd spink0
./install.sh
sudo reboot
```

`./install.sh no-ui` builds the variant without the front panel, controlled only via the
web socket UI (`support/html/pink.php`).

`install.sh` then:

1. syncs and fetches all git submodules,
2. installs `cmake`, `wiringpi`, `libasound-dev` and `librtaudio-dev` via apt,
3. installs RaspAP (web admin / access point). Note: this pulls the *current* RaspAP
   installer, which reconfigures networking and is much newer than the version spink0 was
   developed with,
4. builds and installs PortAudio and nanomsg from `modules/`,
5. builds `pinkd`, installs it to `/usr/sbin/pinkd` with the init script `/etc/init.d/pink`,
6. installs the WLAN mode switch service (`/opt/si/modeswitch/wlan-mode.sh`,
   `wlan-mode.service`),
7. copies `support/hostapd.conf` and the web pages to `/var/www/html`,
8. sets `force_turbo=1` in `config.txt` and enables `wifi-powersave-off.service`.

### Manual build

```bash
git submodule sync --recursive
git submodule update --init --recursive
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DUSE_PI_ZERO=ON -DUSE_WEBSOCKET=OFF ..
make
sudo make install          # /usr/sbin/pinkd and /etc/init.d/pink
```

PortAudio must be built and installed first (`modules/portaudio`: `./configure --with-alsa
--without-oss --without-jack && make && sudo make install`), and nanomsg too if the web socket
UI is enabled. See `install.sh` for the exact steps.

| CMake option | Default | Meaning |
|---|---|---|
| `USE_PI_ZERO` | `ON` | front panel UI (display, encoder, buttons, LEDs) via wiringPi |
| `USE_WEBSOCKET` | `ON` | web socket UI on port 8888 (needs nanomsg, nmws, cereal); `install.sh` turns it off for the front-panel build |
| `USE_RTAUDIO` | `OFF` | RtAudio instead of PortAudio as audio backend |

The CMake setup also has a macOS path for desktop development, where the front panel is
simulated on the console. It links PortAudio from `/usr/local/lib/libportaudio.a` and has not
been tested recently (Homebrew on Apple Silicon installs to `/opt/homebrew` instead).

### Running and logs

- Start / stop: `sudo /etc/init.d/pink start|stop` (starts automatically at boot).
- Run in the foreground for debugging: `sudo /usr/sbin/pinkd no-daemon`
- Logs go to syslog under the tag `pinkd`: `journalctl -t pinkd -f`. Audio underruns,
  timeline jumps, Link peer changes and start/stop are logged there.

### WLAN modes

- Hold the **encoder button** for about 5 s: access point mode (SSID `spink`, see
  `support/hostapd.conf`).
- Hold the **run button** for about 5 s: client mode, joins the networks in
  `/etc/wpa_supplicant/wpa_supplicant.conf`.
- In access point mode, networks can be added at `http://<pi-address>/addnet.php`.
  This page is meant for setup only: it does not validate its input.
