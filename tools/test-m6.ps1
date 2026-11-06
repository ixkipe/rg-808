$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

$base = @(
 'RG808P1','BPM 120.00',
 'BD 2000200020002000','SD 0000200000002000','LT 0000000000000000',
 'MT 0000000000000000','HT 0000000000000000','CH 1111111111111111',
 'OH 0000000000000000','CL 0000000000000000','CP 0000000000000000',
 'CB 0000000000000000','CY 0000000000000000',
 'KIT BD 1','KIT SD 1','KIT CH 1'
)
# A: plain   B: + ratchets (rolls on snare hits)   C: + swing 60%
Set-Content test\confA.txt ($base + 'RCH SD 0000000000000000' + 'SWING 0')
Set-Content test\confB.txt ($base + 'RCH SD 0000300000003000' + 'SWING 0')
Set-Content test\confC.txt ($base + 'RCH SD 0000000000000000' + 'SWING 60')

$results = @()
foreach ($n in 'A','B','C') {
  $env:RG808_CONF = "test\conf$n.txt"
  & build\rg808.exe --bounce "test\bounce$n.wav" --loops 2 | Out-Null
  $h = (Get-FileHash "test\bounce$n.wav" -Algorithm MD5).Hash
  $s = (Get-Item "test\bounce$n.wav").Length
  $results += "$n : $s bytes : $h"
}
$env:RG808_CONF = $null
$results
$ha = ($results[0] -split ':')[-1].Trim()
$hb = ($results[1] -split ':')[-1].Trim()
$hc = ($results[2] -split ':')[-1].Trim()
if ($ha -ne $hb) { 'PASS: ratchet alters audio' } else { 'FAIL: ratchet no-op' }
if ($ha -ne $hc) { 'PASS: swing alters audio' } else { 'FAIL: swing no-op' }
$sizes = $results | ForEach-Object { [int](($_ -split ':')[1].Trim() -split ' ')[0] }
if (($sizes[0] -eq $sizes[1]) -and ($sizes[1] -eq $sizes[2])) { 'PASS: frame grid preserved (same length)' } else { 'FAIL: length changed' }

# screenshot of RCH mode with rolls present
$env:RG808_CONF = 'test\confB.txt'
& build\rg808.exe --screenshot test\shot-rch.bmp --mode rch | Out-Null
Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile((Resolve-Path 'test\shot-rch.bmp'))
$img.Save((Join-Path (Get-Location) 'test\shot-rch.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
Remove-Item test\shot-rch.bmp
$env:RG808_CONF = $null
'test screenshots written'
