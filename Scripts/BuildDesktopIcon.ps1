param(
    [Parameter(Mandatory=$true)][string]$OutputPath,
    [string]$SourcePath = (Join-Path (Split-Path -Parent $PSScriptRoot) 'Content\Splash\WowUnrealProjectIcon.png')
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
# Use the approved gold W / silver U artwork for every application icon.
$artwork = [Drawing.Image]::FromFile($SourcePath)
$images = @()
try {
foreach ($size in @(16,24,32,48,64,128,256)) {
    $bitmap = New-Object Drawing.Bitmap($size,$size)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $graphics.Clear([Drawing.Color]::Transparent)
    $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $scale = [Math]::Min($size / [double]$artwork.Width, $size / [double]$artwork.Height)
    $width = [single]($artwork.Width * $scale)
    $height = [single]($artwork.Height * $scale)
    $graphics.DrawImage($artwork,[single](($size-$width)/2),[single](($size-$height)/2),$width,$height)
    $stream = New-Object IO.MemoryStream
    $bitmap.Save($stream,[Drawing.Imaging.ImageFormat]::Png)
    $images += ,@($size,$stream.ToArray())
    $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose()
}
} finally { $artwork.Dispose() }
$outputStream = New-Object IO.MemoryStream
$writer = New-Object IO.BinaryWriter($outputStream)
try {
    $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$images.Count)
    $offset = 6 + 16 * $images.Count
    foreach ($entry in $images) {
        $dimension = if ($entry[0] -eq 256) { 0 } else { $entry[0] }
        $writer.Write([byte]$dimension); $writer.Write([byte]$dimension)
        $writer.Write([byte]0); $writer.Write([byte]0)
        $writer.Write([uint16]1); $writer.Write([uint16]32)
        $writer.Write([uint32]$entry[1].Length); $writer.Write([uint32]$offset)
        $offset += $entry[1].Length
    }
    foreach ($entry in $images) { $writer.Write([byte[]]$entry[1]) }
    $bytes = $outputStream.ToArray()
} finally { $writer.Dispose(); $outputStream.Dispose() }
# Avoid invalidating the native resource build when the icon is unchanged.
if (!(Test-Path -LiteralPath $OutputPath) -or
    [Convert]::ToBase64String([IO.File]::ReadAllBytes($OutputPath)) -ne [Convert]::ToBase64String($bytes)) {
    [IO.File]::WriteAllBytes($OutputPath,$bytes)
}
