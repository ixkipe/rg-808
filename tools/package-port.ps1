# Windows fallback packager: assembles dist/rgsmp.zip from an existing
# dist/rgsmp-staging/ (produced by a previous docker build run) + port/ files.
# Use when you only changed assets or launch scripts and don't want Docker.
# NOTE: Compress-Archive does not preserve the unix +x bit; RGSMP.sh chmods
# the binary itself on launch, so installed ports still run.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$stage = Join-Path $root 'dist\build'
if (-not (Test-Path 'dist\rgsmp-staging')) { throw 'no binaries yet - run the docker build first' }

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path "$stage\rgsmp\assets\wav" | Out-Null

Copy-Item 'port\RGSMP.sh' "$stage\RGSMP.sh"
Copy-Item 'dist\rgsmp-staging\rgsmp.*' "$stage\rgsmp\"
Copy-Item 'assets\wav\*' "$stage\rgsmp\assets\wav\" -Recurse
Copy-Item 'port\rgsmp\licenses' "$stage\rgsmp\licenses" -Recurse
if (Test-Path 'port\rgsmp\screenshot.png') { Copy-Item 'port\rgsmp\screenshot.png' "$stage\rgsmp\" }
Copy-Item 'port\README.md' "$stage\rgsmp\README.md"
Copy-Item 'port\gameinfo.xml' "$stage\rgsmp\gameinfo.xml"

Compress-Archive -Path "$stage\RGSMP.sh", "$stage\rgsmp" -DestinationPath 'dist\rgsmp.zip' -Force
"dist/rgsmp.zip: " + (Get-Item 'dist\rgsmp.zip').Length + " bytes"
