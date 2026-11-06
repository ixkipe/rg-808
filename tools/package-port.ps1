# Windows fallback packager: assembles dist/rg808.zip from an existing
# dist/rg808-staging/ (produced by a previous docker build run) + port/ files.
# Use when you only changed assets or launch scripts and don't want Docker.
# NOTE: Compress-Archive does not preserve the unix +x bit; RG808.sh chmods
# the binary itself on launch, so installed ports still run.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$stage = Join-Path $root 'dist\build'
if (-not (Test-Path 'dist\rg808-staging')) { throw 'no binaries yet — run the docker build first' }

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path "$stage\rg808\assets\wav" | Out-Null

Copy-Item 'port\RG808.sh' "$stage\RG808.sh"
Copy-Item 'dist\rg808-staging\rg808.*' "$stage\rg808\\"
Copy-Item 'assets\wav\*.wav' "$stage\rg808\assets\wav\\"
Copy-Item 'port\rg808\licenses' "$stage\rg808\licenses" -Recurse
if (Test-Path 'port\rg808\screenshot.png') { Copy-Item 'port\rg808\screenshot.png' "$stage\rg808\\" }
Copy-Item 'port\README.md' "$stage\rg808\README.md"
Copy-Item 'port\gameinfo.xml' "$stage\rg808\gameinfo.xml"

Compress-Archive -Path "$stage\RG808.sh", "$stage\rg808" -DestinationPath 'dist\rg808.zip' -Force
"dist/rg808.zip: " + (Get-Item 'dist\rg808.zip').Length + " bytes"
