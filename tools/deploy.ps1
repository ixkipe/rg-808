# RG-808 deploy: scp the port zip + install script to the device (muOS SSH/SFTP).
# Usage: .\tools\deploy.ps1 192.168.1.50
# Enable SSH on muOS first: Configuration > Connectivity > ... (see muos.dev/web/ssh).
param(
  [Parameter(Mandatory=$true)][string]$Ip,
  [string]$User = "root",
  [string]$Zip = "dist\rg808.zip"
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$zipPath = Join-Path $root $Zip
if (-not (Test-Path $zipPath)) { throw "missing $zipPath — build it first (see tools/portmaster-build)" }

"copying $zipPath -> ${User}@${Ip}:/tmp/"
scp -o StrictHostKeyChecking=no $zipPath "${User}@${Ip}:/tmp/rg808.zip"
scp -o StrictHostKeyChecking=no "$root\tools\device\install.sh" "${User}@${Ip}:/tmp/install.sh"

"installing on device..."
ssh -o StrictHostKeyChecking=no "${User}@${Ip}" "sh /tmp/install.sh /tmp/rg808.zip"

"done. Launch RG808 from Explore Content > Ports on the device."
