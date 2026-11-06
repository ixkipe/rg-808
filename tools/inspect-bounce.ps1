$b = [System.IO.File]::ReadAllBytes("$PWD\test-default.wav")
$peak = 0; $nonzero = 0; $n = 0
for ($i = 44; $i -lt $b.Length -and $n -lt 400000; $i += 2) {
  $s = [BitConverter]::ToInt16($b, $i)
  if ([Math]::Abs($s) -gt $peak) { $peak = [Math]::Abs($s) }
  if ($s -ne 0) { $nonzero++ }
  $n++
}
"samples checked: $n  peak: $peak / 32767 ({0:P1})  nonzero: {1:P1}" -f ($peak/32767), ($nonzero/$n)
