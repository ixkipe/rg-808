# Runs the automated suites for BOTH machine shells.
#
# The core branch carries the sampler and the drum machine, so a change in
# src/core/ has to be checked against both. The pattern-based scripts use the
# RG808P1 format (BD/SD labels, KIT lines) and therefore run against the drum
# machine build; the sampler suite runs against the sampler build; the neutral
# suites run against both.
#
#   .\tools\test-machines.ps1
#   .\tools\test-machines.ps1 -SmpBuild build -DrumBuild build-808
param(
  [string]$SmpBuild = 'build',
  [string]$DrumBuild = 'build-808'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location (Split-Path -Parent $root)

# Headless self-tests (no assets needed) that both shells implement.
$headless = @('shortcuts', 'song', 'length', 'fx', 'mute', 'punch',
              'pattern', 'swing', 'levels', 'pitch', 'trim', 'midi')

# Scripts that work with either shell.
$common = @('test-levels', 'test-mute', 'test-pattern', 'test-punch',
            'test-shortcuts', 'test-swing', 'test-pitch', 'test-trim',
            'test-midi')

# Scripts built around the RG808P1 pattern format: the drum machine only.
$drum = @('test-determinism', 'test-kits', 'test-length', 'test-m6',
          'test-m7', 'test-m8', 'test-m9', 'test-roundtrip')

# Sampler-only scripts.
$smp = @('test-sampler')

$script:failed = @()

function Invoke-Scripts([string]$label, [string]$exe, [string[]]$scripts) {
  if (-not (Test-Path $exe)) {
    "SKIP: $label - $exe not built"
    return
  }
  foreach ($s in $scripts) {
    $out = & powershell -NoProfile -ExecutionPolicy Bypass -File "tools\$s.ps1" -Exe $exe 2>&1 | Out-String
    if ($LASTEXITCODE -ne 0 -or $out -match 'FAIL') {
      $script:failed += "$label $s"
      "FAIL: $label $s"
      ($out -split "`n" | Select-String 'FAIL' | Select-Object -First 4) | ForEach-Object { "     $_" }
    }
  }
  "PASS: $label scripts"
}

function Invoke-Headless([string]$label, [string]$exe, [string[]]$suites,
                        [string[]]$labels) {
  if (-not (Test-Path $exe)) {
    "SKIP: $label - $exe not built"
    return
  }
  # The song suite needs a pattern library and a song file to load.
  $conf = "test\mm-$($label -replace '[^A-Za-z0-9]', '')"
  Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
  New-Item -ItemType Directory -Force -Path "$conf\patterns" | Out-Null
  $bpms = @('120.00', '250.00', '90.50')
  for ($i = 0; $i -lt 3; $i++) {
    $lines = @('RG808P1', "BPM $($bpms[$i])")
    foreach ($l in $labels) {
      $lines += "$l 1000100010001000"
    }
    $lines += 'SWING 0'
    Set-Content "$conf\patterns\00$($i + 1).txt" $lines
  }
  Set-Content "$conf\song.txt" @('RG808SONG1', 'LOOP 0',
                                  'BAR 001', 'BAR 002', 'BAR 003', 'BAR 001')
  $env:RG808_CONF = $conf

  foreach ($s in $suites) {
    & $exe "--test-$s" | Out-Null
    if ($LASTEXITCODE -ne 0) { $script:failed += "$label --test-$s" }
  }
  $env:RG808_CONF = $null
  Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
  "PASS: $label headless self-tests"
}

$smpExe = Join-Path $SmpBuild 'rg808.exe'
$drumExe = Join-Path $DrumBuild 'rg808.exe'

$smpLabels = @('01', '02', '03', '04', '05', '06', '07', '08', '09', '10', '11')
$drumLabels = @('BD', 'SD', 'LT', 'MT', 'HT', 'CH', 'OH', 'CL', 'CP', 'CB', 'CY')

Invoke-Headless 'RG-SMP' $smpExe ($headless + @('sampler')) $smpLabels
Invoke-Headless 'RG-808' $drumExe $headless $drumLabels

Invoke-Scripts 'RG-SMP' $smpExe ($common + $smp)
Invoke-Scripts 'RG-808' $drumExe ($common + $drum)

if ($script:failed.Count -gt 0) {
  'FAIL: ' + ($script:failed -join ', ')
  exit 1
}
'PASS: both machine shells'
