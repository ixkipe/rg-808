$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null
$lines = @(
 'RG808P1','BPM 120.00',
 'BD 2000200020002000','SD 0000200000002000','LT 0000000000000000',
 'MT 0000000000000000','HT 0000000000000000','CH 1111111111111111',
 'OH 0000000000000000','CL 0000000000000000','CP 0000000000000000',
 'CB 0000000000000000','CY 0000000000000000'
)
Set-Content test\det.txt $lines
$env:RG808_CONF = 'test\det.txt'
& build\rg808.exe --bounce test\d1.wav --loops 1 | Out-Null
& build\rg808.exe --bounce test\d2.wav --loops 1 | Out-Null
$env:RG808_CONF = $null
$h1 = (Get-FileHash test\d1.wav -Algorithm MD5).Hash
$h2 = (Get-FileHash test\d2.wav -Algorithm MD5).Hash
"$h1"
"$h2"
if ($h1 -eq $h2) { 'DETERMINISTIC' } else { 'NON-DETERMINISTIC' }
# byte-level diff: find first differing sample offset
$b1 = [IO.File]::ReadAllBytes('test\d1.wav'); $b2 = [IO.File]::ReadAllBytes('test\d2.wav')
$diff = -1
for ($i = 44; $i -lt $b1.Length; $i++) { if ($b1[$i] -ne $b2[$i]) { $diff = $i; break } }
"first diff byte: $diff (sample frame $(($diff-44)/4))"
