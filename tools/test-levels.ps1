$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Ghost/weak/regular/accent ordering, VLV persistence and the hold-A modal.
$conf = 'test\levels-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $conf | Out-Null
$env:RG808_CONF = $conf
& build\rg808.exe --test-levels
$rc = $LASTEXITCODE
& build\rg808.exe --screenshot test\levels.bmp --mode vol | Out-Null
$env:RG808_CONF = $null
if ($rc -ne 0) { throw 'step level self-test failed' }

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\levels.bmp'))
$img.Save((Join-Path (Get-Location) 'test\levels.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\levels.bmp
'PASS: step levels'
'level modal screenshot: test\levels.png'
