param([string]$Exe = 'build\rg808.exe')

$ErrorActionPreference = 'Stop'

# Core check: the USB-MIDI input module (descriptor parsing, endpoint
# selection in both T-8 personalities, packet expansion, feed-through into
# the P4 clock). The usbfs reader itself is exercised on the device.
& $Exe --test-usbmidi
if ($LASTEXITCODE -ne 0) { throw 'usbmidi self-test failed' }
'PASS: usbmidi input'
