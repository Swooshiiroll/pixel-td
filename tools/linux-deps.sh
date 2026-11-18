#!/usr/bin/env bash
# Linux build dependencies (Debian/Ubuntu): compiler, CMake, Ninja, and the system libraries SDL3
# builds its video/audio/input backends against. CI runs it; on a local Debian/Ubuntu box:
#   sudo tools/linux-deps.sh
# Other distros: install the same packages under their names (see SDL's docs/README-linux.md).
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -q
apt-get install -y -q --no-install-recommends \
  build-essential cmake ninja-build pkg-config \
  libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev \
  libxtst-dev libxkbcommon-dev \
  libwayland-dev wayland-protocols libdecor-0-dev \
  libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
  libdbus-1-dev libudev-dev libibus-1.0-dev
