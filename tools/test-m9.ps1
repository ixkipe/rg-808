$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null
Remove-Item -Recurse -Force test\m9lib -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path test\m9lib\patterns | Out-Null

$notes = @(
 'BD 1000100010001000','SD 0000100000001000',
 'LT 0000000000000000','MT 0000000000000000','HT 0000000000000000',
 'CH 1110111011101110','OH 0001000100010001','CL 0000000000000000',
 'CP 0000100000001000','CB 0000001000000010','CY 0000000000000000'
)

function Pattern([string]$bpm, [string[]]$fx) {
  return @('RG808P1', "BPM $bpm") + $notes + @('SWING 0', 'LEN 16') + $fx
}

$fxOff = @(
 'RG808FX2','FX ON 1',
 'FX DRIVE 0 0 8 4 50 50 55 4 30',
 'FX MOD 0 0 50 50 30 50',
 'FX DELAY 0 0 250 1 30 30',
 'FX REVERB 0 50 50 30'
)

function Fx([string[]]$lines) { Set-Content test\m9lib\fx.txt $lines }
function PatternFile([int]$slot, [string[]]$lines) {
  Set-Content ("test\m9lib\patterns\{0:D3}.txt" -f $slot) $lines
}
function Bounce([string]$name, [int]$slot) {
  & build\rg808.exe --pattern $slot --bounce "test\m9-$name.wav" --loops 2 | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "bounce $name failed" }
  return (Get-FileHash "test\m9-$name.wav" -Algorithm MD5).Hash
}

$env:RG808_CONF = 'test\m9lib'

# Persistence, clamping, algorithm selection, sync math and bypass self-test.
& build\rg808.exe --test-fx
if ($LASTEXITCODE -ne 0) { throw 'effects self-test failed' }

$ok = $true
PatternFile 1 (Pattern '120.00' @())

# The fallback fx.txt applies to old patterns without FX lines.
Fx $fxOff
$dry = Bounce 'dry' 1

$variants = @(
  @{ name='bitcrusher';  line='FX DRIVE 1 0 4 4 50 50 55 4 30' },
  @{ name='overdrive';   line='FX DRIVE 1 1 8 4 90 100 55 4 30' },
  @{ name='compression'; line='FX DRIVE 1 2 8 4 50 50 10 20 100' },
  @{ name='flanger';     line='FX MOD 1 0 50 50 30 50' },
  @{ name='phaser';      line='FX MOD 1 1 50 50 30 50' },
  @{ name='delay';       line='FX DELAY 1 0 250 1 30 50' },
  @{ name='delay-sync';  line='FX DELAY 1 1 250 1 30 50' },
  @{ name='reverb';      line='FX REVERB 1 50 50 50' }
)
foreach ($v in $variants) {
  $f = $fxOff.Clone()
  if ($v.line -like 'FX DRIVE*')  { $f[2] = $v.line }
  elseif ($v.line -like 'FX MOD*')   { $f[3] = $v.line }
  elseif ($v.line -like 'FX DELAY*') { $f[4] = $v.line }
  else                               { $f[5] = $v.line }
  Fx $f
  $h = Bounce $v.name 1
  if ($h -ne $dry) { "PASS: $($v.name) changes audio" }
  else { "FAIL: $($v.name) was a no-op"; $ok = $false }
}

# At 120 BPM a synced 1/8 is 250 ms, so it must match the manual 250 ms delay.
$manual = $fxOff.Clone(); $manual[4] = 'FX DELAY 1 0 250 1 30 50'; Fx $manual
$hManual = Bounce 'delay-manual' 1
$sync = $fxOff.Clone(); $sync[4] = 'FX DELAY 1 1 250 1 30 50'; Fx $sync
$hSync = Bounce 'delay-sync-120' 1
if ($hManual -eq $hSync) { 'PASS: 1/8 sync at 120 BPM equals 250 ms' }
else { 'FAIL: synced delay time mismatch'; $ok = $false }

# Master bypass and all-stages-off must match the dry render bit-for-bit.
$bypass = $fxOff.Clone()
$bypass[1] = 'FX ON 0'
$bypass[2] = 'FX DRIVE 1 1 8 4 90 100 55 4 30'
$bypass[3] = 'FX MOD 1 1 50 50 30 50'
$bypass[4] = 'FX DELAY 1 1 250 1 30 50'
$bypass[5] = 'FX REVERB 1 50 50 50'
Fx $bypass
if ((Bounce 'bypass' 1) -eq $dry) { 'PASS: master bypass is bit-identical' }
else { 'FAIL: bypass changed audio'; $ok = $false }

Fx $fxOff
if ((Bounce 'alloff' 1) -eq $dry) { 'PASS: all stages off is bit-identical' }
else { 'FAIL: all-off chain changed audio'; $ok = $false }

# Per-pattern FX: same notes, different chains in two slots.
$patDry = Pattern '120.00' $fxOff
$patWet = Pattern '120.00' @(
 'FX ON 1',
 'FX DRIVE 1 2 8 4 50 50 10 20 100',
 'FX MOD 1 1 50 50 30 50',
 'FX DELAY 1 1 250 0 30 50',
 'FX REVERB 1 50 50 50'
)
PatternFile 10 $patDry
PatternFile 11 $patWet
$hPatDry = Bounce 'pat-dry' 10
$hPatWet = Bounce 'pat-wet' 11
if ($hPatDry -ne $hPatWet) { 'PASS: per-pattern FX changes a pattern bounce' }
else { 'FAIL: per-pattern FX ignored'; $ok = $false }

# Song bars must switch FX with each pattern.
Set-Content test\m9lib\song.txt @('RG808SONG1','LOOP 0','BAR 010','BAR 011')
& build\rg808.exe --song --bounce test\m9-song-mixed.wav | Out-Null
$hMixed = (Get-FileHash test\m9-song-mixed.wav -Algorithm MD5).Hash
Set-Content test\m9lib\song.txt @('RG808SONG1','LOOP 0','BAR 010','BAR 010')
& build\rg808.exe --song --bounce test\m9-song-same.wav | Out-Null
$hSame = (Get-FileHash test\m9-song-same.wav -Algorithm MD5).Hash
if ($hMixed -ne $hSame) { 'PASS: song bars apply their pattern FX' }
else { 'FAIL: song ignored per-pattern FX'; $ok = $false }

# FX page screenshot uses the wet pattern.
& build\rg808.exe --pattern 11 --screenshot test\m9-fx.bmp --mode fx | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\m9-fx.bmp'))
$img.Save((Join-Path (Get-Location) 'test\m9-fx.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\m9-fx.bmp
'test screenshot written: test\m9-fx.png'

if (-not $ok) { throw 'effects checks failed' }
