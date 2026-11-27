#!/bin/bash
        ##########    Copyright (C) 2016 Vincenzo Pacella
        ##      ##    Distributed under MIT license, see file LICENSE
        ##      ##    or <http://opensource.org/licenses/MIT>
        ##      ##
##########      ############################################################# shaduzlabs.com #######
##
## spink0 installer for Raspberry Pi OS Lite (32-bit, current release) on a Raspberry Pi Zero W.
## Run as the normal user from the repository root:   ./install.sh
##
## Optional environment variables:
##   SPINK_AP_PSK          password of the module's access point (default: spinkspink)
##   SPINK_WIFI_COUNTRY    WLAN country code (e.g. CH, DE), if not set in Raspberry Pi Imager
##
## The script can be run again after a `git pull` to rebuild and reinstall.

set -euo pipefail

cd "$(dirname "$0")"

AP_PSK="${SPINK_AP_PSK:-spinkspink}"
MARKER="# spink0 (install.sh)"

step() { echo -e "\n[ \033[1m\033[96mspink0\033[m ] $*"; }
fail() { echo -e "[ \033[1m\033[31mERROR\033[m ] $*" >&2; exit 1; }

# Checks -----------------------------------------------------------------------------------------

grep -q "Raspberry Pi" /proc/device-tree/model 2>/dev/null || fail "This installer runs on a Raspberry Pi only."
[ "$(id -u)" -ne 0 ] || fail "Run as the normal user (the script uses sudo where needed)."
[ ${#AP_PSK} -ge 8 ] && [ ${#AP_PSK} -le 63 ] || fail "SPINK_AP_PSK must be 8 to 63 characters."

BOOT=/boot/firmware
[ -f $BOOT/config.txt ] || fail "$BOOT/config.txt not found: this installer needs a current Raspberry Pi OS."

# Packages ---------------------------------------------------------------------------------------

step "Install packages"
sudo apt-get update
sudo apt-get install -y git cmake build-essential pkg-config \
  libgpiod-dev portaudio19-dev libasound2-dev alsa-utils \
  network-manager python3 # pinctrl and raspi-config come with Raspberry Pi OS

step "Fetch submodules (Ableton Link)"
git submodule sync --recursive
git submodule update --init --recursive

# Build ------------------------------------------------------------------------------------------

step "Build pinkd (takes a while on a Pi Zero)"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DUSE_PI_ZERO=ON
cmake --build build -j1 # -j1: the Pi Zero has 512 MB, Link + asio need it
sudo cmake --install build # /usr/local/bin/pinkd

step "Install the network daemon and the setup page"
sudo install -m 755 support/network/spink-net /usr/local/bin/spink-net
sudo install -d /usr/local/share/spink/www
sudo install -m 644 support/network/www/index.html /usr/local/share/spink/www/index.html

# Network ----------------------------------------------------------------------------------------

step "Configure NetworkManager"
sudo install -m 644 support/network/NetworkManager/conf.d/spink.conf /etc/NetworkManager/conf.d/spink.conf
sudo install -d /etc/NetworkManager/dnsmasq-shared.d
sudo install -m 644 support/network/NetworkManager/dnsmasq-shared.d/spink-captive.conf \
  /etc/NetworkManager/dnsmasq-shared.d/spink-captive.conf

if [ -n "${SPINK_WIFI_COUNTRY:-}" ]; then
  sudo raspi-config nonint do_wifi_country "$SPINK_WIFI_COUNTRY"
fi
if [ -z "$(sudo raspi-config nonint get_wifi_country 2>/dev/null || true)" ]; then
  echo "WARNING: no WLAN country set. Set it in Raspberry Pi Imager, with raspi-config, or run"
  echo "         SPINK_WIFI_COUNTRY=CH ./install.sh   (use your country code)"
fi
sudo rfkill unblock wifi || true
sudo systemctl reload NetworkManager || true

# Access point: "spink-" + the last 4 hex digits of the WLAN MAC address
SUFFIX=$(tr -d ':' < /sys/class/net/wlan0/address | tail -c 5)
AP_SSID="spink-${SUFFIX}"
if nmcli -t -f NAME connection show | grep -qx "spink-ap"; then
  sudo nmcli connection modify spink-ap 802-11-wireless-security.psk "$AP_PSK"
else
  sudo nmcli connection add type wifi ifname wlan0 con-name spink-ap autoconnect no \
    ssid "$AP_SSID" \
    802-11-wireless.mode ap 802-11-wireless.band bg 802-11-wireless.channel 6 \
    802-11-wireless.powersave 2 \
    802-11-wireless-security.key-mgmt wpa-psk 802-11-wireless-security.proto rsn \
    802-11-wireless-security.pairwise ccmp 802-11-wireless-security.group ccmp \
    802-11-wireless-security.psk "$AP_PSK" \
    ipv4.method shared ipv4.addresses 10.42.0.1/24 ipv6.method disabled
fi

# Boot configuration -----------------------------------------------------------------------------

step "Configure the boot settings (config.txt, cmdline.txt)"
if ! grep -qF "$MARKER" $BOOT/config.txt; then
  sudo tee -a $BOOT/config.txt > /dev/null <<EOF

[all]
$MARKER
# PWM audio for the clock/reset outputs (pins are set by pinkd.service)
dtparam=audio=on
# fixed CPU clock: frequency scaling causes latency spikes
force_turbo=1
# MIDI out on GPIO14: full UART instead of Bluetooth, 31250 baud (38400 requested -> 31250)
enable_uart=1
dtoverlay=disable-bt
dtoverlay=midi-uart0
EOF
fi
# no serial console on the MIDI UART (backup: cmdline.txt.bak)
sudo sed -i.bak -E 's/ ?console=(serial0|ttyAMA0|ttyS0),[0-9]+//g' $BOOT/cmdline.txt
sudo systemctl disable hciuart.service 2>/dev/null || true
sudo systemctl disable serial-getty@ttyAMA0.service serial-getty@serial0.service 2>/dev/null || true

# Services ---------------------------------------------------------------------------------------

step "Install and enable the services"
sudo install -m 644 support/systemd/pinkd.service /etc/systemd/system/pinkd.service
sudo install -m 644 support/systemd/spink-netd.service /etc/systemd/system/spink-netd.service
sudo systemctl daemon-reload
sudo systemctl enable pinkd.service spink-netd.service

step "Done. Reboot now:  sudo reboot"
cat <<EOF

  Access point (hold the encoder button for 3 s, or automatic when no known network is in range):
    network:   $(nmcli -t -f 802-11-wireless.ssid connection show spink-ap | cut -d: -f2)
    password:  $AP_PSK
    setup:     http://10.42.0.1/  (opens by itself on most phones)

  Logs:  journalctl -u pinkd -f      journalctl -u spink-netd -f

EOF
