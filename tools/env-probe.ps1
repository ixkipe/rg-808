foreach ($t in 'git','cmake','ninja','gcc','g++','clang','python','docker','wsl','make','pkg-config','7z','zip') {
  $c = Get-Command $t -ErrorAction SilentlyContinue
  if ($c) { '{0,-12} {1}' -f $t, $c.Source } else { '{0,-12} MISSING' -f $t }
}
'--- docker daemon check ---'
docker info --format '{{.ServerVersion}} / {{.OSType}}/{{.Architecture}}' 2>&1 | Select-Object -First 3
