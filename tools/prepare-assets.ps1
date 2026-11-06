# RG-808 asset preparation: copy the 11 chosen samples from the source pack
# into assets/wav with short ids. Change the mapping here if a sample doesn't fit.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$src  = Join-Path $root 'Roland TR-808'
$dst  = Join-Path $root 'assets\wav'
New-Item -ItemType Directory -Force -Path $dst | Out-Null

$map = [ordered]@{
  bd = 'TR-808Kick07.wav'   # long boom, classic 808 kick
  sd = 'TR-808Snare01.wav'  # bright crack
  lt = 'TR-808Tom01.wav'
  mt = 'TR-808Tom04.wav'
  ht = 'TR-808Tom06.wav'
  ch = 'TR-808Hat_C01.wav'
  oh = 'TR-808Hat_O01.wav'
  cl = 'TR-808Clave.wav'
  cp = 'TR-808Clap01.wav'
  cb = 'TR-808Cow.wav'
  cy = 'Crash-02.wav'       # 808 cymbal slot
}

foreach ($id in $map.Keys) {
  $from = Join-Path $src $map[$id]
  if (-not (Test-Path $from)) { throw "missing source: $from" }
  Copy-Item $from (Join-Path $dst "$id.wav") -Force
  "  {0} <- {1}" -f $id, $map[$id]
}
"assets ready: " + (Get-ChildItem $dst).Count + " files"
