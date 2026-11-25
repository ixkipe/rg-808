param([string]$Exe = 'build\rg808.exe')
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Headless slice math / persistence / page checks.
& $Exe --test-trim
if ($LASTEXITCODE -ne 0) { throw 'slice self-test failed' }

# A sliced step must render differently from the same step at full length,
# while the render length stays the same.
$conf = 'test\trim-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path "$conf\patterns" | Out-Null

$isSmp = (& $Exe --help) -match 'test-sampler'
if ($isSmp) {
  $sample = Get-ChildItem assets\wav -Filter *.wav -Recurse | Select-Object -First 1
  if (-not $sample) { throw 'no sample available for the slice bounce test' }
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
& $Exe --pattern 1 --bounce test\trim-full.wav --loops 1 | Out-Null

# Cut the first hit to its first half: 16 zero starts, then step 1 ends at 50 %.
$starts = @(0) * 16
$ends = @(10000) * 16
$ends[0] = 5000
$trm = "TRM $first " + (($starts + $ends) -join ' ')
Set-Content "$conf\patterns\001.txt" ($base + $trm)
& $Exe --pattern 1 --bounce test\trim-cut.wav --loops 1 | Out-Null
$env:RG808_CONF = $null

$full = (Get-FileHash test\trim-full.wav -Algorithm MD5).Hash
$cut = (Get-FileHash test\trim-cut.wav -Algorithm MD5).Hash
$fullSize = (Get-Item test\trim-full.wav).Length
$cutSize = (Get-Item test\trim-cut.wav).Length
if ($full -eq $cut) { 'FAIL: the slice did not change the audio' } else { 'PASS: the slice changes the audio' }
if ($fullSize -eq $cutSize) { 'PASS: the render length is preserved' } else { 'FAIL: the slice changed the render length' }

# Screenshot of the slice page.
& $Exe --screenshot test\slice.bmp --mode slice | Out-Null
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\slice.bmp'))
$img.Save((Join-Path (Get-Location) 'test\slice.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\slice.bmp
Remove-Item -Recurse -Force $conf

'slice screenshot: test\slice.png'
