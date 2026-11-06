# RG-808 asset preparation: copy every variant family from the source pack
# into assets/wav as <id>NN.wav (loader probes NN=01..16). Re-run any time;
# edit the arrays to add/remove variants (first gap ends the family).
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Roland TR-808'
$dst  = Join-Path $root 'assets\wav'

# fresh start
if (Test-Path $dst) { Remove-Item "$dst\*.wav" -Force }
New-Item -ItemType Directory -Force -Path $dst | Out-Null

$families = [ordered]@{
  bd = @('TR-808Kick01','TR-808Kick02','TR-808Kick03','TR-808Kick04','TR-808Kick05','TR-808Kick06',
         'TR-808Kick07','TR-808Kick08','TR-808Kick09','TR-808Kick10','TR-808Kick11','TR-808Kick12',
         'TR-808Kick13','TR-808Kick14','TR-808Kick15','TR-808Kick16')
  sd = @('TR-808Snare01','TR-808Snare02','TR-808Snare03','TR-808Snare04','TR-808Snare05','TR-808Snare06',
         'TR-808Snare07','TR-808Snare08','TR-808Snare09','TR-808Snare10','TR-808Snare11','TR-808Snare12',
         'TR-808Snare13','TR-808Snare14','TR-808Snare15','TR-808Snare16')
  lt = @('TR-808Tom01','TR-808Tom02','TR-808Tom03','TR-808Tom04','TR-808Tom05','TR-808Tom06','TR-808Tom07')
  mt = @('TR-808Tom01','TR-808Tom02','TR-808Tom03','TR-808Tom04','TR-808Tom05','TR-808Tom06','TR-808Tom07')
  ht = @('TR-808Tom01','TR-808Tom02','TR-808Tom03','TR-808Tom04','TR-808Tom05','TR-808Tom06','TR-808Tom07')
  ch = @('TR-808Hat_C01','TR-808Hat_C02','TR-808Hat_C03','TR-808Hat_C04','TR-808Hat_C05')
  oh = @('TR-808Hat_O01','TR-808Hat_O02','TR-808Hat_O03')
  cl = @('TR-808Clave','Claves')
  cp = @('TR-808Clap01','TR-808Clap02','Clap')
  cb = @('TR-808Cow','Cowbell')
  cy = @('TR-808Ride01','TR-808Ride02','TR-808Ride03','TR-808Ride04','Crash-01','Crash-02')
}

$total = 0
foreach ($id in $families.Keys) {
  $n = 0
  foreach ($name in $families[$id]) {
    $from = Join-Path $src "$name.wav"
    if (-not (Test-Path $from)) { throw "missing source: $from" }
    $n++
    Copy-Item $from ("{0}\{1}{2:D2}.wav" -f $dst, $id, $n) -Force
  }
  "{0}: {1} variants" -f $id, $n
  $total += $n
}
"kit ready: $total files"
