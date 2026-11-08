param(
    [string]$OutputPath = "$PSScriptRoot\..\resources\app.ico"
)

Add-Type -AssemblyName System.Drawing

function New-RoundedRectanglePath {
    param(
        [float]$X,
        [float]$Y,
        [float]$Width,
        [float]$Height,
        [float]$Radius
    )
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $diameter = $Radius * 2
    $path.AddArc($X, $Y, $diameter, $diameter, 180, 90)
    $path.AddArc($X + $Width - $diameter, $Y, $diameter, $diameter, 270, 90)
    $path.AddArc($X + $Width - $diameter, $Y + $Height - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($X, $Y + $Height - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function New-PolygonPoints {
    param([float]$CenterX, [float]$CenterY, [float]$Radius, [float]$Rotation, [int]$Count)
    $points = [System.Drawing.PointF[]]::new($Count)
    for ($i = 0; $i -lt $Count; $i++) {
        $angle = $Rotation + (2 * [Math]::PI * $i / $Count)
        $points[$i] = [System.Drawing.PointF]::new(
            [float]($CenterX + [Math]::Cos($angle) * $Radius),
            [float]($CenterY + [Math]::Sin($angle) * $Radius))
    }
    return $points
}

function New-IconPng {
    param([int]$Size)
    $scale = $Size / 256.0
    $bitmap = [System.Drawing.Bitmap]::new($Size, $Size, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $graphics.Clear([System.Drawing.Color]::Transparent)

    $outer = [System.Drawing.RectangleF]::new(0, 0, $Size, $Size)
    $path = New-RoundedRectanglePath -X (2 * $scale) -Y (2 * $scale) `
        -Width ($Size - 4 * $scale) -Height ($Size - 4 * $scale) `
        -Radius (54 * $scale)
    $background = [System.Drawing.Drawing2D.LinearGradientBrush]::new(
        $outer,
        [System.Drawing.Color]::FromArgb(255, 16, 36, 52),
        [System.Drawing.Color]::FromArgb(255, 26, 62, 67),
        55.0)
    $graphics.FillPath($background, $path)
    $background.Dispose()

    $ringPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 31, 138, 86), [float](7 * $scale))
    $ballSize = 132 * $scale
    $ballX = ($Size - $ballSize) / 2
    $ballY = ($Size - $ballSize) / 2
    $ringRect = [System.Drawing.RectangleF]::new($ballX - 12 * $scale, $ballY - 12 * $scale, $ballSize + 24 * $scale, $ballSize + 24 * $scale)
    $graphics.DrawEllipse($ringPen, $ringRect)

    $white = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 250, 252, 251))
    $graphics.FillEllipse($white, $ballX, $ballY, $ballSize, $ballSize)
    $white.Dispose()

    $ink = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 19, 45, 59))
    $centerX = $Size / 2.0
    $centerY = $Size / 2.0
    $centralRadius = 27 * $scale
    $central = New-PolygonPoints -CenterX $centerX -CenterY $centerY -Radius $centralRadius -Rotation (-[Math]::PI / 2) -Count 5
    $graphics.FillPolygon($ink, $central)

    $linePen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 19, 45, 59), [float](5 * $scale))
    $linePen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $linePen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    for ($i = 0; $i -lt 5; $i++) {
        $angle = -[Math]::PI / 2 + (2 * [Math]::PI * $i / 5)
        $startX = $centerX + [Math]::Cos($angle) * ($centralRadius + 2 * $scale)
        $startY = $centerY + [Math]::Sin($angle) * ($centralRadius + 2 * $scale)
        $endX = $centerX + [Math]::Cos($angle) * (51 * $scale)
        $endY = $centerY + [Math]::Sin($angle) * (51 * $scale)
        $graphics.DrawLine($linePen, [float]$startX, [float]$startY, [float]$endX, [float]$endY)
    }
    $linePen.Dispose()

    for ($i = 0; $i -lt 5; $i++) {
        $angle = -[Math]::PI / 2 + [Math]::PI / 5 + (2 * [Math]::PI * $i / 5)
        $patchRadius = 14 * $scale
        $patchCenterX = $centerX + [Math]::Cos($angle) * (58 * $scale)
        $patchCenterY = $centerY + [Math]::Sin($angle) * (58 * $scale)
        $patch = New-PolygonPoints -CenterX $patchCenterX -CenterY $patchCenterY -Radius $patchRadius -Rotation ($angle + [Math]::PI / 5) -Count 5
        $graphics.FillPolygon($ink, $patch)
    }
    $ink.Dispose()

    $accent = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(255, 31, 138, 86))
    $graphics.FillEllipse($accent, $Size - 54 * $scale, $Size - 54 * $scale, 30 * $scale, 30 * $scale)
    $checkPen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(255, 255, 255, 255), [float](4 * $scale))
    $checkPen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $checkPen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $graphics.DrawLines($checkPen, [System.Drawing.PointF[]]@(
        [System.Drawing.PointF]::new($Size - 47 * $scale, $Size - 39 * $scale),
        [System.Drawing.PointF]::new($Size - 40 * $scale, $Size - 32 * $scale),
        [System.Drawing.PointF]::new($Size - 28 * $scale, $Size - 45 * $scale)
    ))
    $checkPen.Dispose()
    $accent.Dispose()
    $path.Dispose()
    $ringPen.Dispose()

    $stream = [System.IO.MemoryStream]::new()
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $stream.ToArray()
    $stream.Dispose()
    $graphics.Dispose()
    $bitmap.Dispose()
    return ,$bytes
}

$sizes = @(16, 24, 32, 48, 64, 128, 256)
$images = @()
foreach ($size in $sizes) {
    $images += ,(New-IconPng -Size $size)
}

$dir = Split-Path -Parent $OutputPath
if (-not (Test-Path $dir)) {
    New-Item -ItemType Directory -Path $dir | Out-Null
}

$stream = [System.IO.File]::Open($OutputPath, [System.IO.FileMode]::Create)
$writer = [System.IO.BinaryWriter]::new($stream)
$writer.Write([UInt16]0)
$writer.Write([UInt16]1)
$writer.Write([UInt16]$sizes.Count)
$offset = 6 + 16 * $sizes.Count
for ($i = 0; $i -lt $sizes.Count; $i++) {
    $size = $sizes[$i]
    $dimension = if ($size -ge 256) { 0 } else { $size }
    $writer.Write([Byte]$dimension)
    $writer.Write([Byte]$dimension)
    $writer.Write([Byte]0)
    $writer.Write([Byte]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]32)
    $writer.Write([UInt32]$images[$i].Length)
    $writer.Write([UInt32]$offset)
    $offset += $images[$i].Length
}
foreach ($image in $images) {
    $writer.Write($image)
}
$writer.Dispose()
$stream.Dispose()
Write-Host "Icon written to $OutputPath"
