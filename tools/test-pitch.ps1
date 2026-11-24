param([string]$Exe = 'build\rg808.exe')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Headless DSP / cache / persistence / page checks.
& $Exe --test-pitch
if ($LASTEXITCODE -ne 0) { throw 'pitch self-test failed' }

# A transposed pattern must render differently from the same pattern without
# the interval, and must keep the render length.
$conf = 'test\pitch-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$conf\patterns" | Out-Null

$isSmp = (& $Exe --help) -match 'test-sampler'
if ($isSmp) {
  $sample = Get-ChildItem assets\wav -Filter *.wav -Recurse | Select-Object -First 1
  if (-not $sample) { throw 'no sample available for the pitch bounce test' }
  $rel = $sample.FullName.Substring((Resolve-Path 'assets\wav').Path.Length + 1).Replace('\', '/')
  $first = '01'
  $rows = @("$first 1000100010001000") + ((2..11) | ForEach-Object { "$('{0:d2}' -f $_) 0000000000000000" })
  $head = @('RGSMP1', 'BPM 120.00', "SMP $first $rel")
} else {
  $first = 'BD'
  $rows = @("$first 1000100010001000") + ('SD','LT','MT','HT','CH','OH','CL','CP','CB','CY' |
          ForEach-Object { "$_ 0000000000000000" })
  $head = @('RG808P1', 'BPM 120.00', 'KIT BD 1')
}

$base = $head + $rows + @('SWING 0', 'LEN 16')
Set-Content "$conf\patterns\001.txt" $base
$env:RG808_CONF = $conf
& $Exe --pattern 1 --bounce test\pitch-dry.wav --loops 1 | Out-Null

# Insert the interval right after the first level row.
$pitched = @()
foreach ($line in $base) {
  $pitched += $line
  if ($line -eq $rows[0]) { $pitched += "PIT $first 12" }
}
Set-Content "$conf\patterns\001.txt" $pitched
& $Exe --pattern 1 --bounce test\pitch-up.wav --loops 1 | Out-Null
$env:RG808_CONF = $null

$dry = (Get-FileHash test\pitch-dry.wav -Algorithm MD5).Hash
$up = (Get-FileHash test\pitch-up.wav -Algorithm MD5).Hash
$drySize = (Get-Item test\pitch-dry.wav).Length
$upSize = (Get-Item test\pitch-up.wav).Length
if ($dry -eq $up) { 'FAIL: pitch did not change the audio' } else { 'PASS: pitch changes the audio' }
if ($drySize -eq $upSize) { 'PASS: the render length is preserved' } else { 'FAIL: pitch changed the render length' }
if ($pitched -notcontains "PIT $first 12") { 'FAIL: the PIT line was not written' }

# Screenshot of the pitch page.
& $Exe --screenshot test\pitch.bmp --mode pitch | Out-Null
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\pitch.bmp'))
$img.Save((Join-Path (Get-Location) 'test\pitch.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\pitch.bmp
Remove-Item -Recurse -Force $conf

'pitch screenshot: test\pitch.png'
