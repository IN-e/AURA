Add-Type -AssemblyName System.Drawing

$root = Split-Path -Parent $PSScriptRoot
$sourceDir = Join-Path $root 'src'
New-Item -ItemType Directory -Force -Path $sourceDir | Out-Null

function New-RoundedPath([Drawing.RectangleF]$rect, [float]$radius) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $diameter = $radius * 2
    $path.AddArc($rect.X, $rect.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($rect.Right - $diameter, $rect.Y, $diameter, $diameter, 270, 90)
    $path.AddArc($rect.Right - $diameter, $rect.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($rect.X, $rect.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Render-Icon([int]$size) {
    $bmp = New-Object Drawing.Bitmap $size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $g.Clear([Drawing.Color]::Transparent)

    $margin = [Math]::Max(1.0, $size * 0.018)
    $body = New-Object Drawing.RectangleF $margin, $margin, ($size - 2 * $margin), ($size - 2 * $margin)
    $radius = [Math]::Max(2.0, $size * 0.22)
    $path = New-RoundedPath $body $radius
    $brush = New-Object Drawing.SolidBrush ([Drawing.Color]::FromArgb(255, 10, 20, 23))
    $g.FillPath($brush, $path)
    $borderPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(160, 42, 91, 82)), ([Math]::Max(1.0, $size * 0.008))
    $g.DrawPath($borderPen, $path)

    $cx = $size / 2.0
    $cy = $size / 2.0
    $ringR = $size * 0.285
    $innerR = $size * 0.185
    $accent = [Drawing.Color]::FromArgb(255, 104, 239, 212)

    if ($size -ge 32) {
        $glowWidths = @(0.115, 0.075, 0.038)
        $glowAlphas = @(34, 60, 105)
        for ($i = 0; $i -lt 3; $i++) {
            $alpha = $glowAlphas[$i]
            $pen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb($alpha, $accent.R, $accent.G, $accent.B)), ($size * $glowWidths[$i])
            $g.DrawEllipse($pen, [float]($cx - $ringR), [float]($cy - $ringR), [float]($ringR * 2), [float]($ringR * 2))
            $pen.Dispose()
        }
    }

    $outerPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(245, $accent.R, $accent.G, $accent.B)), ([Math]::Max(1.15, $size * 0.024))
    $g.DrawEllipse($outerPen, [float]($cx - $ringR), [float]($cy - $ringR), [float]($ringR * 2), [float]($ringR * 2))
    $innerPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(205, $accent.R, $accent.G, $accent.B)), ([Math]::Max(0.9, $size * 0.015))
    $g.DrawEllipse($innerPen, [float]($cx - $innerR), [float]($cy - $innerR), [float]($innerR * 2), [float]($innerR * 2))

    $dotBrush = New-Object Drawing.SolidBrush $accent
    $dotR = [Math]::Max(1.1, $size * 0.035)
    $g.FillEllipse($dotBrush, [float]($cx - $dotR), [float]($cy - $ringR - $dotR), [float]($dotR * 2), [float]($dotR * 2))

    $dotBrush.Dispose(); $innerPen.Dispose(); $outerPen.Dispose(); $borderPen.Dispose(); $brush.Dispose(); $path.Dispose(); $g.Dispose()
    return $bmp
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$pngData = @()
$preview = $null
foreach ($size in $sizes) {
    $bmp = Render-Icon $size
    if ($size -eq 256) { $preview = $bmp.Clone() }
    $stream = New-Object System.IO.MemoryStream
    $bmp.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
    $pngData += ,($stream.ToArray())
    $stream.Dispose()
    $bmp.Dispose()
}
$preview.Save((Join-Path $root 'build\aura_icon_preview.png'), [Drawing.Imaging.ImageFormat]::Png)
$preview.Dispose()

$icoPath = Join-Path $sourceDir 'aura.ico'
$fs = [System.IO.File]::Create($icoPath)
$writer = New-Object System.IO.BinaryWriter $fs
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $size = $sizes[$i]
    $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $writer.Write([Byte]($(if ($size -ge 256) { 0 } else { $size })))
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$pngData[$i].Length)
    $writer.Write([UInt32]$offset)
    $offset += $pngData[$i].Length
}
foreach ($data in $pngData) { $writer.Write($data) }
$writer.Flush()
$writer.Dispose()
$fs.Dispose()
Write-Host "wrote $icoPath"
