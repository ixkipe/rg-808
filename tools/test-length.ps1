$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null
Remove-Item -Recurse -Force test\lenlib -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path test\lenlib | Out-Null

# Same one-bar groove saved with different pattern lengths. No LEN line means
# the pre-feature default of 16 steps.
$base = @(
 'RG808P1','BPM 120.00',
 'BD 1000100010001000','SD 0000100000001000',
 'LT 0000000000000000','MT 0000000000000000','HT 0000000000000000',
 'CH 1110111011101110','OH 0001000100010001','CL 0000000000000000',
 'CP 0000100000001000','CB 0000001000000010','CY 0000000000000000'
)
Set-Content test\lenlib\len4.txt  ($base + 'SWING 0' + 'LEN 4')
Set-Content test\lenlib\len8.txt  ($base + 'SWING 0' + 'LEN 8')
Set-Content test\lenlib\len16.txt ($base + 'SWING 0' + 'LEN 16')
Set-Content test\lenlib\len-old.txt $base

# Persistence, clamping, live clock and song snapshot checks.
$env:RG808_CONF = 'test\lenlib'
& build\rg808.exe --test-length
if ($LASTEXITCODE -ne 0) { throw 'pattern length self-test failed' }

# Bounce frame-count math: sps = 44100*60/(120*4) = 5512.5 frames per 16th.
function Bounce([string]$name, [int]$loops) {
  $env:RG808_CONF = "test\lenlib\$name.txt"
  & build\rg808.exe --bounce "test\len-$name-$loops.wav" --loops $loops | Out-Null
  return ((Get-Item "test\len-$name-$loops.wav").Length - 44) / 4
}
function Expected([int]$steps, [int]$loops) {
  return [long](5512.5 * $steps * $loops) + 8192
}

$f4 = Bounce 'len4' 1
$f8 = Bounce 'len8' 1
$f16 = Bounce 'len16' 1
$fold = Bounce 'len-old' 1
$f4x2 = Bounce 'len4' 2
$env:RG808_CONF = $null

$ok = $true
if ($f4 -eq (Expected 4 1))       { 'PASS: LEN 4 bounce is 4 steps' } else { "FAIL: LEN 4 = $f4"; $ok = $false }
if ($f8 -eq (Expected 8 1))       { 'PASS: LEN 8 bounce is 8 steps' } else { "FAIL: LEN 8 = $f8"; $ok = $false }
if ($f16 -eq (Expected 16 1))     { 'PASS: LEN 16 bounce is 16 steps' } else { "FAIL: LEN 16 = $f16"; $ok = $false }
if ($fold -eq $f16)               { 'PASS: missing LEN defaults to 16 steps' } else { "FAIL: old pattern = $fold"; $ok = $false }
if ($f4x2 -eq (Expected 4 2))     { 'PASS: loop count multiplies the pattern length' } else { "FAIL: LEN 4 x2 = $f4x2"; $ok = $false }

# Song export must use each bar's own length too.
New-Item -ItemType Directory -Force -Path test\lenlib\patterns | Out-Null
Copy-Item test\lenlib\len4.txt test\lenlib\patterns\001.txt
(Get-Content test\lenlib\len8.txt) -replace '^BPM .*','BPM 125.00' | Set-Content test\lenlib\patterns\002.txt
Set-Content test\lenlib\song.txt @('RG808SONG1','LOOP 0','BAR 001','BAR 002')
$env:RG808_CONF = 'test\lenlib'
& build\rg808.exe --song --bounce test\len-song.wav | Out-Null
$env:RG808_CONF = $null
$songFrames = ((Get-Item test\len-song.wav).Length - 44) / 4
$songExpected = [long](5512.5 * 4 + 5292 * 8) + 88200
if ($songFrames -eq $songExpected) { 'PASS: song export follows per-bar lengths' } else { "FAIL: song = $songFrames (expected $songExpected)"; $ok = $false }

# Length editor screenshot.
$env:RG808_CONF = 'test\lenlib\len8.txt'
& build\rg808.exe --screenshot test\len-editor.bmp --mode length | Out-Null
$env:RG808_CONF = $null

Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\len-editor.bmp'))
$img.Save((Join-Path (Get-Location) 'test\len-editor.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\len-editor.bmp
'test screenshot written: test\len-editor.png'

if (-not $ok) { throw 'pattern length bounce checks failed' }
