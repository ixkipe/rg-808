$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Headless DSP and input-layer checks for the R1 punch-in layer.
& build\rg808.exe --test-punch
if ($LASTEXITCODE -ne 0) { throw 'punch-in self-test failed' }

# Render the R1 overlay with one effect highlighted.
$conf = 'test\punch-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $conf | Out-Null
$env:RG808_CONF = $conf
& build\rg808.exe --screenshot test\punch.bmp --mode punch | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\punch.bmp'))
$img.Save((Join-Path (Get-Location) 'test\punch.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\punch.bmp
'PASS: punch-in FX'
'punch screenshot: test\punch.png'
