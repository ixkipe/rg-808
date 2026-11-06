foreach ($f in @('port\RG808.sh','tools\device\probe.sh','tools\device\install.sh','tools\portmaster-build\build.sh','tools\portmaster-build\Dockerfile')) {
  $bytes = [IO.File]::ReadAllBytes($f)
  $crlf = 0; $lf = 0
  for ($i = 0; $i -lt $bytes.Length; $i++) {
    if ($bytes[$i] -eq 10) { $lf++; if ($i -gt 0 -and $bytes[$i-1] -eq 13) { $crlf++ } }
  }
  "{0,-36} LF={1} CRLF={2}" -f $f, ($lf - $crlf), $crlf
}
