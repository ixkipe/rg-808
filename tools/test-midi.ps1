param([string]$Exe = 'build\rg808.exe')

$ErrorActionPreference = 'Stop'

# Core check: the MIDI engine (running-status parser, clock -> BPM lock,
# transport + song position) and the engine's external-clock mode, driven by a
# synthetic clock stream - no hardware and no assets needed.
& $Exe --test-midi
if ($LASTEXITCODE -ne 0) { throw 'midi self-test failed' }
'PASS: midi engine'
