$ErrorActionPreference = 'Stop'
function Make-Conf($path, $kitBd) {
  $l = @('RG808P1','BPM 120.00',
  'BD 2000000000000000','SD 0000000000000000','LT 0000000000000000',
  'MT 0000000000000000','HT 0000000000000000','CH 0000000000000000',
  'OH 0000000000000000','CL 0000000000000000','CP 0000000000000000',
  'CB 0000000000000000','CY 0000000000000000',
  "KIT BD $kitBd")
  Set-Content -Path $path -Value $l
}
Make-Conf 'test\conf-bd3.txt' 3
Make-Conf 'test\conf-bd12.txt' 12
$env:RG808_CONF = 'test\conf-bd3.txt'
& build\rg808.exe --bounce test\bd3.wav --loops 1 | Out-Null
$env:RG808_CONF = 'test\conf-bd12.txt'
& build\rg808.exe --bounce test\bd12.wav --loops 1 | Out-Null
$env:RG808_CONF = $null

$h1 = (Get-FileHash test\bd3.wav -Algorithm MD5).Hash
$h2 = (Get-FileHash test\bd12.wav -Algorithm MD5).Hash
$s1 = (Get-Item test\bd3.wav).Length
$s2 = (Get-Item test\bd12.wav).Length
"bd3:  $s1 bytes  $h1"
"bd12: $s2 bytes  $h2"
if ($h1 -ne $h2) { 'PASS: different kit variants produce different audio' } else { 'FAIL: identical audio' }
