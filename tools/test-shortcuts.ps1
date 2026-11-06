$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Headless check of the shoulder -> menu mapping.
& build\rg808.exe --test-shortcuts
if ($LASTEXITCODE -ne 0) { throw 'shoulder shortcut test failed' }

# Render the four hint overlays for visual inspection.
$conf = 'test\shortcut-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $conf | Out-Null
$env:RG808_CONF = $conf
foreach ($m in 'l1','l2','r1','r2') {
  & build\rg808.exe --screenshot "test\shortcut-$m.bmp" --mode $m | Out-Null
}
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
foreach ($m in 'l1','l2','r1','r2') {
  $img = [System.Drawing.Image]::FromFile((Resolve-Path "test\shortcut-$m.bmp"))
  $img.Save((Join-Path (Get-Location) "test\shortcut-$m.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  Remove-Item "test\shortcut-$m.bmp"
}
'PASS: shoulder shortcut map'
'hint screenshots: test\shortcut-l1.png .. test\shortcut-r2.png'
