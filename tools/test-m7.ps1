$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null
Remove-Item -Recurse -Force test\m7lib -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path test\m7lib | Out-Null

$legacy = @(
 'RG808P1',
 'BPM 90.50',
 'BD 1000000010000000',
 'SD 0000200000002000',
 'LT 0000000000000000',
 'MT 0000000000000000',
 'HT 0000000000000000',
 'CH 1111111111111111',
 'OH 0000000000000000',
 'CL 0000000000000000',
 'CP 0000000000000000',
 'CB 0000000000000000',
 'CY 0000000000000000',
 'KIT BD 1','KIT SD 1','KIT CH 1','SWING 0'
)
Set-Content -Path test\m7lib\pattern.txt -Value $legacy

$env:RG808_CONF = 'test\m7lib'

# 1. first run migrates the legacy v1 file into slot 001
& build\rg808.exe --bounce test\m7-slot1.wav --loops 1 | Out-Null
$slot1 = Test-Path test\m7lib\patterns\001.txt
$index = Test-Path test\m7lib\patterns\index.txt
if ($slot1 -and $index) { 'PASS: legacy pattern migrated to slot 001' } else { 'FAIL: migration' }

# 2. changing the legacy file must not re-import over an existing slot
(Get-Content test\m7lib\pattern.txt) -replace '^BPM .*','BPM 220.00' | Set-Content test\m7lib\pattern.txt
& build\rg808.exe --pattern 1 --bounce test\m7-slot1b.wav --loops 1 | Out-Null
$h1 = (Get-FileHash test\m7-slot1.wav -Algorithm MD5).Hash
$h1b = (Get-FileHash test\m7-slot1b.wav -Algorithm MD5).Hash
if ($h1 -eq $h1b) { 'PASS: slot 001 not overwritten by later legacy edits' } else { 'FAIL: re-imported legacy' }

# 3. headless save/reload round-trip: copy slot 1 to slot 2 and render both
& build\rg808.exe --pattern 1 --save-slot 2 | Out-Null
& build\rg808.exe --pattern 2 --bounce test\m7-slot2.wav --loops 1 | Out-Null
$h2 = (Get-FileHash test\m7-slot2.wav -Algorithm MD5).Hash
if ($h1 -eq $h2) { 'PASS: save slot + reload is bit-identical' } else { 'FAIL: save/reload differs' }
$idx = Get-Content test\m7lib\patterns\index.txt
if (($idx -contains 'ACTIVE 2') -and ($idx -contains '002 PATTERN 002')) {
  'PASS: index stores active slot and name'
} else {
  'FAIL: index contents'
}

# 4. atomic writes leave no temp files behind
$tmp = Get-ChildItem test\m7lib\patterns -Filter *.tmp -ErrorAction SilentlyContinue
if (-not $tmp) { 'PASS: no .tmp files left behind' } else { 'FAIL: temp files remain' }

# 5. deleting every slot must not resurrect the legacy file on the next run
$wipe = 'test\m7wipe'
Remove-Item -Recurse -Force $wipe -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $wipe | Out-Null
Copy-Item test\m7lib\pattern.txt $wipe\pattern.txt
$env:RG808_CONF = $wipe
& build\rg808.exe --bounce test\m7wipe-a.wav --loops 1 | Out-Null
Remove-Item $wipe\patterns\001.txt
& build\rg808.exe --bounce test\m7wipe-b.wav --loops 1 | Out-Null
if (-not (Test-Path $wipe\patterns\001.txt)) {
  'PASS: deleting the library does not re-import the legacy file'
} else {
  'FAIL: legacy file re-imported after delete'
}
$env:RG808_CONF = $null

# 6. pattern browser + rename screenshots for visual inspection
$env:RG808_CONF = 'test\m7lib'
& build\rg808.exe --pattern 2 --screenshot test\m7-browser.bmp --mode pattern | Out-Null
& build\rg808.exe --pattern 2 --screenshot test\m7-rename.bmp --mode rename | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
foreach ($n in 'browser','rename') {
  $img = [System.Drawing.Image]::FromFile((Resolve-Path "test\m7-$n.bmp"))
  $img.Save((Join-Path (Get-Location) "test\m7-$n.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  Remove-Item "test\m7-$n.bmp"
}
'test screenshots written: test\m7-browser.png, test\m7-rename.png'
