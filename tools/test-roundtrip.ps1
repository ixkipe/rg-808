$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path build\conf | Out-Null
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
Set-Content -Path build\conf\pattern.txt -Value $lines
& build\rg808.exe --bounce test-load.wav --loops 2
# expected: sps = 44100*60/(90.5*4) = 7303.9; total = 7303.9*32 + 8192 = 241724 frames = 5.48s
& build\rg808.exe --bounce test-default.wav --loops 2
Remove-Item build\conf\pattern.txt
# default demo at 120: sps=5512.5; total=5512.5*32+8192=184401 = 4.18s
