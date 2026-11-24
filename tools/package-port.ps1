# Windows fallback packager: assembles dist/<port>.zip from an existing
# dist/<port>-staging/ (produced by a previous docker build run) + port/ files.
# Use when you only changed assets or launch scripts and don't want Docker.
# NOTE: Compress-Archive does not preserve the unix +x bit; the launcher chmods
# the binary itself on launch, so installed ports still run.
#
#   .\tools\package-port.ps1 -Machine smp     # RG-SMP (default)
#   .\tools\package-port.ps1 -Machine 808     # RG-808
param(
  [ValidateSet('smp', '808')]
  [string]$Machine = 'smp'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

if ($Machine -eq '808') {
  $port = 'rg808'; $launcher = 'RG808.sh'
} else {
  $port = 'rgsmp'; $launcher = 'RGSMP.sh'
}

$stage = Join-Path $root 'dist\build'
if (-not (Test-Path "dist\$port-staging")) { throw "no binaries yet - run the docker build first ($Machine)" }

if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force -Path "$stage\$port\assets\wav" | Out-Null

Copy-Item "port\$launcher" "$stage\$launcher"
Copy-Item "dist\$port-staging\$port.*" "$stage\$port\"
Copy-Item 'assets\wav\*' "$stage\$port\assets\wav\" -Recurse
Copy-Item "port\$port\licenses" "$stage\$port\licenses" -Recurse
if (Test-Path "port\$port\screenshot.png") { Copy-Item "port\$port\screenshot.png" "$stage\$port\" }
Copy-Item 'port\README.md' "$stage\$port\README.md"
Copy-Item 'port\gameinfo.xml' "$stage\$port\gameinfo.xml"

Compress-Archive -Path "$stage\$launcher", "$stage\$port" -DestinationPath "dist\$port.zip" -Force
"dist/$port.zip: " + (Get-Item "dist\$port.zip").Length + " bytes"
