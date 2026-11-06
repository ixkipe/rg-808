Add-Type -AssemblyName System.Drawing
$img = [System.Drawing.Image]::FromFile("$PWD\test-shot.bmp")
$img.Save("$PWD\test-shot.png", [System.Drawing.Imaging.ImageFormat]::Png)
$img.Dispose()
"png written"
