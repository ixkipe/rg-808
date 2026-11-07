$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Headless sample bank, browser, persistence and song-bank checks.
& build\rg808.exe --test-sampler
if ($LASTEXITCODE -ne 0) { throw 'sampler self-test failed' }

# Render the browser with the local asset tree (if present).
$conf = 'test\sampler-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $conf | Out-Null
$env:RG808_CONF = $conf
& build\rg808.exe --assets assets\wav --screenshot test\sampler.bmp --mode sampler | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\sampler.bmp'))
$img.Save((Join-Path (Get-Location) 'test\sampler.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\sampler.bmp
'PASS: sampler'
'sampler screenshot: test\sampler.png'
