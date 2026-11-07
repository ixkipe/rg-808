$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
cmake --build build | Out-Null
if (-not (Test-Path build\assets\wav\909)) {
  cmake -E copy_directory assets build/assets
}
$env:RG808_CONF = 'build\shot-conf'
if (Test-Path build\shot-conf) { Remove-Item -Recurse -Force build\shot-conf }
New-Item -ItemType Directory -Force build\shot-conf | Out-Null
& build\rg808.exe --screenshot port\rgsmp\screenshot.bmp | Out-Null
Remove-Item Env:\RG808_CONF
Remove-Item -Recurse -Force build\shot-conf
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile('port\rgsmp\screenshot.bmp')
$img.Save('port\rgsmp\screenshot.png', [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item port\rgsmp\screenshot.bmp
'screenshot updated'
