param([string]$Source = "$PSScriptRoot/../dist/branding/lichii_icon.png")
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$dist = [IO.Path]::GetFullPath("$PSScriptRoot/../dist")
$sourceImage = [Drawing.Image]::FromFile([IO.Path]::GetFullPath($Source))
function Get-IconPng([int]$size) {
    $bitmap = New-Object Drawing.Bitmap($size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb))
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $stream = New-Object IO.MemoryStream
    try {
        $graphics.Clear([Drawing.Color]::Transparent)
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $scale = [Math]::Min($size / $sourceImage.Width, $size / $sourceImage.Height)
        $width = [int][Math]::Round($sourceImage.Width * $scale)
        $height = [int][Math]::Round($sourceImage.Height * $scale)
        $graphics.DrawImage($sourceImage, [int](($size-$width)/2), [int](($size-$height)/2), $width, $height)
        $bitmap.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
        return ,$stream.ToArray()
    } finally { $stream.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }
}
function Write-BigEndian32($stream, [uint32]$value) {
    $bytes = [BitConverter]::GetBytes($value)
    [Array]::Reverse($bytes)
    $stream.Write($bytes, 0, 4)
}
try {
    $sizes = @(16, 24, 32, 48, 64, 128, 256)
    $frames = @($sizes | ForEach-Object { ,(Get-IconPng $_) })
    $stream = [IO.File]::Create("$dist/lichii.ico")
    $writer = New-Object IO.BinaryWriter($stream)
    try {
        $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]$sizes.Count)
        $offset = 6 + 16 * $sizes.Count
        for ($i = 0; $i -lt $sizes.Count; $i++) {
            $dimension = [byte]($sizes[$i] % 256)
            $writer.Write($dimension); $writer.Write($dimension)
            $writer.Write([uint16]0); $writer.Write([uint16]1); $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$i].Length); $writer.Write([uint32]$offset)
            $offset += $frames[$i].Length
        }
        foreach ($frame in $frames) { $writer.Write([byte[]]$frame) }
    } finally { $writer.Dispose(); $stream.Dispose() }
    $png = Get-IconPng 256
    [IO.File]::WriteAllBytes("$dist/lichii.png", $png)
    $types = @('ic07', 'ic08', 'ic09', 'ic10')
    $frames = @(128, 256, 512, 1024 | ForEach-Object { ,(Get-IconPng $_) })
    $stream = [IO.File]::Create("$dist/lichii.icns")
    try {
        $magic = [Text.Encoding]::ASCII.GetBytes('icns')
        $stream.Write($magic, 0, 4)
        $length = 8
        foreach ($frame in $frames) { $length += 8 + $frame.Length }
        Write-BigEndian32 $stream $length
        for ($i = 0; $i -lt $types.Count; $i++) {
            $type = [Text.Encoding]::ASCII.GetBytes($types[$i])
            $stream.Write($type, 0, 4)
            Write-BigEndian32 $stream (8 + $frames[$i].Length)
            $stream.Write($frames[$i], 0, $frames[$i].Length)
        }
    } finally { $stream.Dispose() }
} finally { $sourceImage.Dispose() }
