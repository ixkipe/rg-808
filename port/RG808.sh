#!/bin/bash
# RG-808 — PortMaster launch script
# Based on the standard PortMaster launchscript template (portmaster.games/packaging.html),
# hardened so it also works on a plain muOS Ports folder without PortMaster installed.

XDG_DATA_HOME=${XDG_DATA_HOME:-$HOME/.local/share}

if [ -d "/opt/system/Tools/PortMaster/" ]; then
  controlfolder="/opt/system/Tools/PortMaster"
elif [ -d "/opt/tools/PortMaster/" ]; then
  controlfolder="/opt/tools/PortMaster"
elif [ -d "$XDG_DATA_HOME/PortMaster/" ]; then
  controlfolder="$XDG_DATA_HOME/PortMaster"
else
  controlfolder="/roms/ports/PortMaster"
fi

if [ -f "$controlfolder/control.txt" ]; then
  source "$controlfolder/control.txt"
else
  # manual install without PortMaster: stub the helpers, derive the arch
  DEVICE_ARCH="$(uname -m)"
  [ "$DEVICE_ARCH" = "armv7l" ] && DEVICE_ARCH=armhf
  directory="roms"
  sdl_controllerconfig=""
  get_controls() { :; }
  pm_platform_helper() { :; }
  pm_finish() { :; }
  GPTOKEYB=""
  controlfolder=""
fi

[ -f "${controlfolder}/mod_${CFW_NAME}.txt" ] && source "${controlfolder}/mod_${CFW_NAME}.txt"

get_controls

GAMEDIR=""
for cand in "/$directory/ports/rg808" "/roms/ports/rg808" "/storage/rom/ports/rg808" "/ports/rg808"; do
  [ -d "$cand" ] && GAMEDIR="$cand" && break
done
if [ -z "$GAMEDIR" ]; then
  echo "RG808: could not find its port folder (expected in <card>/ports/rg808)"
  sleep 5
  exit 1
fi

CONFDIR="$GAMEDIR/conf"
cd "$GAMEDIR" || exit 1
> "$GAMEDIR/log.txt" && exec > >(tee "$GAMEDIR/log.txt") 2>&1

mkdir -p "$CONFDIR"
export XDG_DATA_HOME="$CONFDIR"

[ -d "$GAMEDIR/libs.${DEVICE_ARCH}" ] && export LD_LIBRARY_PATH="$GAMEDIR/libs.${DEVICE_ARCH}:$LD_LIBRARY_PATH"
export SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"

# the app reads samples from <exe dir>/assets/wav by default; fullscreen on 640x480
export RG808_FULLSCREEN=1

BIN="$GAMEDIR/rg808.${DEVICE_ARCH}"
[ -x "$BIN" ] || chmod +x "$BIN" 2>/dev/null

[ -n "$GPTOKEYB" ] && $GPTOKEYB "rg808.${DEVICE_ARCH}" &
pm_platform_helper "$BIN"
"$BIN"

pm_finish
