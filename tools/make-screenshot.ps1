$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)
cmake --build build | Out-Null
if (-not (Test-Path build\assets\wav\bd01.wav)) {
  cmake -E copy_directory assets build/assets
}
$env:RG808_CONF = 'build\conf\__no_such_file__.txt'
& build\rg808.exe --screenshot port\rg808\screenshot.bmp | Out-Null
Remove-Item Env:\RG808_CONF
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile('port\rg808\screenshot.bmp')
$img.Save('port\rg808\screenshot.png', [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item port\rg808\screenshot.bmp
'screenshot updated'
