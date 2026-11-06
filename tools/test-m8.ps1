$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null
Remove-Item -Recurse -Force test\m8lib -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path test\m8lib\patterns | Out-Null

# Three patterns with different BPMs/swing/ratchets.
$p1 = @(
'RG808P1','BPM 120.00','BD 1000100010001000','SD 0000100000001000',
'LT 0000000000000000','MT 0000000000000000','HT 0000000000000000',
'CH 1111111111111111','OH 0000000000000000','CL 0000000000000000',
'CP 0000000000000000','CB 0000000000000000','CY 0000000000000000',
'RCH SD 0000000000000000','SWING 0'
)
$p2 = @(
'RG808P1','BPM 250.00','BD 1000000010000000','SD 0000000010000000',
'LT 0000000000000000','MT 0000000000000000','HT 0000000000000000',
'CH 1010101010101010','OH 0000000000000000','CL 0000000000000000',
'CP 0000000000000000','CB 0000000000000000','CY 0000000000000000',
'RCH SD 0000000000000000','SWING 0'
)
$p3 = @(
'RG808P1','BPM 90.50','BD 1000000010000000','SD 0000000000000000',
'LT 0000000000000000','MT 0000000000000000','HT 0000000000000000',
'CH 0000000000000000','OH 0000000000000000','CL 0000000000000000',
'CP 0000100000001000','CB 0000000000000000','CY 0000000000000000',
'RCH SD 0000300000003000','SWING 40'
)
Set-Content test\m8lib\patterns\001.txt $p1
Set-Content test\m8lib\patterns\002.txt $p2
Set-Content test\m8lib\patterns\003.txt $p3
Set-Content test\m8lib\song.txt @('RG808SONG1','LOOP 0','BAR 001','BAR 002','BAR 003','BAR 001')

$env:RG808_CONF = 'test\m8lib'

# Persistence + snapshot + length math.
& build\rg808.exe --test-song
if ($LASTEXITCODE -ne 0) { throw 'song self-test failed' }

# Streaming export.
& build\rg808.exe --song --bounce test\m8-song.wav
if ($LASTEXITCODE -ne 0) { throw 'song export failed' }

function Sps([double]$bpm) { 44100.0 * 60.0 / ($bpm * 4.0) }
$bar1 = Sps 120.0
$bar2 = Sps 250.0
$bar3 = Sps 90.5
$expected = [long]($bar1 * 16 + $bar2 * 16 + $bar3 * 16 + $bar1 * 16) + 88200
$size = (Get-Item test\m8-song.wav).Length
if ($size -eq (44 + $expected * 4)) {
  "PASS: song length math ($expected frames, $size bytes)"
} else {
  "FAIL: expected $((44 + $expected * 4)) bytes, got $size"
}

# First bar must match a normal pattern bounce bit-for-bit.
& build\rg808.exe --pattern 1 --bounce test\m8-bar1.wav --loops 1 | Out-Null
$a = [IO.File]::ReadAllBytes('test\m8-song.wav')
$b = [IO.File]::ReadAllBytes('test\m8-bar1.wav')
$compareFrames = [int]$bar1 - 2
$end = 44 + $compareFrames * 4
$diff = -1
for ($i = 44; $i -lt $end; $i++) {
  if ($a[$i] -ne $b[$i]) { $diff = $i; break }
}
if ($diff -lt 0) { 'PASS: first bar is bit-identical to pattern bounce' }
else { "FAIL: first difference at byte $diff" }

# UI screenshots.
& build\rg808.exe --screenshot test\m8-song.bmp --mode song | Out-Null
& build\rg808.exe --screenshot test\m8-songact.bmp --mode songact | Out-Null
& build\rg808.exe --screenshot test\m8-export.bmp --mode export | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
foreach ($n in 'song','songact','export') {
  $img = [System.Drawing.Image]::FromFile((Resolve-Path "test\m8-$n.bmp"))
  $img.Save((Join-Path (Get-Location) "test\m8-$n.png"), [System.Drawing.Imaging.ImageFormat]::Png)
  $img.Dispose()
  Remove-Item "test\m8-$n.bmp"
}
'test screenshots written: test\m8-song.png, test\m8-songact.png, test\m8-export.png'
