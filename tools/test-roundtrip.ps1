$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null
Remove-Item -Recurse -Force test\rt-load, test\rt-empty -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path test\rt-load | Out-Null
New-Item -ItemType Directory -Force -Path test\rt-empty | Out-Null

# Legacy v1 layout: a single conf/pattern.txt, no patterns/ library yet.
$lines = @(
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
 'CY 0000000000000000'
)
Set-Content -Path test\rt-load\pattern.txt -Value $lines

# First run must migrate the legacy file into slot 001 and render it.
# sps = 44100*60/(90.5*4) = 7309.39; total = 7309.39*32 + 8192 = 242092 frames
$env:RG808_CONF = 'test\rt-load'
& build\rg808.exe --bounce test\rt-load.wav --loops 2 | Out-Null
$env:RG808_CONF = $null

# Empty conf dir: no slot and no legacy file -> built-in demo at 120 BPM.
# sps = 5512.5; total = 5512.5*32 + 8192 = 184592 frames
$env:RG808_CONF = 'test\rt-empty'
& build\rg808.exe --bounce test\rt-demo.wav --loops 2 | Out-Null
$env:RG808_CONF = $null

$loadFrames = ((Get-Item test\rt-load.wav).Length - 44) / 4
$demoFrames = ((Get-Item test\rt-demo.wav).Length - 44) / 4
if ($loadFrames -eq 242092) { 'PASS: legacy pattern loaded at 90.5 BPM' } else { "FAIL: load frames = $loadFrames" }
if ($demoFrames -eq 184592) { 'PASS: empty library falls back to demo' } else { "FAIL: demo frames = $demoFrames" }
if ((Test-Path test\rt-load\patterns\001.txt) -and (Test-Path test\rt-load\patterns\index.txt)) {
  'PASS: legacy pattern.txt migrated into slot 001'
} else {
  'FAIL: migration did not create slot 001/index'
}
