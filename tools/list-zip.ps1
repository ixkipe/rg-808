Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [IO.Compression.ZipFile]::OpenRead("$PWD\dist\rg808.zip")
$zip.Entries | ForEach-Object { "{0,10} {1}" -f $_.Length, $_.FullName }
$zip.Dispose()
