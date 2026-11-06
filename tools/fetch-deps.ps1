# Fetches third-party build dependencies into vendor/:
#  - SDL2 MinGW devel (headers + import libs for native Windows dev builds)
#  - font8x8_basic.h (public-domain 8x8 font)
# Re-run any time; safe to edit versions.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vendor = Join-Path $root 'vendor'
New-Item -ItemType Directory -Force -Path $vendor | Out-Null

if (-not (Test-Path (Join-Path $vendor 'sdl2\SDL2-2.32.10'))) {
  $zip = Join-Path $vendor 'SDL2-devel-mingw.zip'
  curl.exe -sL -o $zip 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.zip'
  Expand-Archive $zip -DestinationPath (Join-Path $vendor 'sdl2') -Force
  Remove-Item $zip
}
if (-not (Test-Path (Join-Path $vendor 'font8x8_basic.h'))) {
  curl.exe -sL -o (Join-Path $vendor 'font8x8_basic.h') 'https://raw.githubusercontent.com/dhepper/font8x8/master/font8x8_basic.h'
}
"deps ok"
