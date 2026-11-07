$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Headless engine/UI checks for the performance mute (Y in the sequencer).
& build\rg808.exe --test-mute
if ($LASTEXITCODE -ne 0) { throw 'mute self-test failed' }

# Render the sequencer with two muted rows for visual inspection.
$conf = 'test\mute-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $conf | Out-Null
$env:RG808_CONF = $conf
& build\rg808.exe --screenshot test\mute.bmp --mode mute | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\mute.bmp'))
$img.Save((Join-Path (Get-Location) 'test\mute.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\mute.bmp
'PASS: performance mute'
'mute screenshot: test\mute.png'
