$files = Get-ChildItem 'Roland TR-808\*.wav'
$seen = @{}
foreach ($f in $files) {
  $b = [System.IO.File]::OpenRead($f.FullName)
  $buf = New-Object byte[] 44
  [void]$b.Read($buf, 0, 44)
  $b.Close()
  $fmtTag  = [BitConverter]::ToUInt16($buf, 20)
  $ch      = [BitConverter]::ToUInt16($buf, 22)
  $rate    = [BitConverter]::ToUInt32($buf, 24)
  $bits    = [BitConverter]::ToUInt16($buf, 34)
  $key = "$fmtTag/$ch/$rate/$bits"
  if (-not $seen.ContainsKey($key)) { $seen[$key] = New-Object System.Collections.ArrayList }
  [void]$seen[$key].Add($f.Name)
}
foreach ($k in $seen.Keys) {
  $fmt, $ch, $rate, $bits = $k -split '/'
  "{0,-16} count={1}  example={2}" -f "fmt=$fmt ch=$ch rate=$rate bits=$bits", $seen[$k].Count, $seen[$k][0]
}
