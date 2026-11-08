$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force -Path test | Out-Null

# Save semantics and empty-slot creation.
$conf = 'test\pattern-conf'
Remove-Item -Recurse -Force $conf -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $conf | Out-Null
$env:RG808_CONF = $conf
& build\rg808.exe --test-pattern
$rc = $LASTEXITCODE
$env:RG808_CONF = $null
if ($rc -ne 0) { throw 'pattern action self-test failed' }
'PASS: pattern actions'
