# 生成 assets/app.ico（v2：纯色修正版）
#
#   powershell -File tools/make-icon.ps1 -Preview build/icon-preview.png
#
# 设计：
#   底    圆角方块，竖直渐变 #0FCB6E（顶）→ #05BC5C（底）。
#         两端色相都在 ~149°，只变明度 —— 不偏黄也不偏青，绿是"纯"的。
#   字形  纯白 #FFFFFF 圆头对勾。无投影、无白色高光、无白色内描边：
#         白叠在绿上会变成奶绿（色不纯），投影会在勾边压出一圈脏边。
# 编码：
#   48px 及以下用 32 位 BMP 条目（旧版 shell 取小图标更稳），
#   64px 及以上用 PNG 条目（Vista+，体积小）。
param(
    [string]$Out = "assets/app.ico",
    [string]$Preview = ""
)

Add-Type -AssemblyName System.Drawing
$ErrorActionPreference = 'Stop'

$sizes = 16, 20, 24, 32, 40, 48, 64, 96, 128, 256

function New-IconBitmap([int]$s) {
    $bmp = New-Object System.Drawing.Bitmap($s, $s, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode     = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $g.PixelOffsetMode   = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    # 透明底上的绘制：用 SourceOver 合成本身没有白底混入
    $g.CompositingMode   = [System.Drawing.Drawing2D.CompositingMode]::SourceOver

    $f = $s / 256.0
    $m = 8 * $f                 # 外边距
    $w = $s - 2 * $m
    $d = 2 * (56 * $f)          # 圆角直径

    $rect = [System.Drawing.RectangleF]::new($m, $m, $w, $w)
    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($rect.X, $rect.Y, $d, $d, 180, 90)
    $path.AddArc($rect.X + $w - $d, $rect.Y, $d, $d, 270, 90)
    $path.AddArc($rect.X + $w - $d, $rect.Y + $w - $d, $d, $d, 0, 90)
    $path.AddArc($rect.X, $rect.Y + $w - $d, $d, $d, 90, 90)
    $path.CloseFigure()

    # 纯绿竖直渐变：同色相（~149°），只变明度
    $top    = [System.Drawing.Color]::FromArgb(255, 0x0F, 0xCB, 0x6E)
    $bottom = [System.Drawing.Color]::FromArgb(255, 0x05, 0xBC, 0x5C)
    $grad = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $top, $bottom, 90)
    $g.FillPath($grad, $path)

    # 纯白对勾，圆头圆角接合，无投影
    $p1 = [System.Drawing.PointF]::new((70 * $f), (132 * $f))
    $p2 = [System.Drawing.PointF]::new((112 * $f), (176 * $f))
    $p3 = [System.Drawing.PointF]::new((190 * $f), (88 * $f))
    # 小尺寸按像素加粗：16px 下 24/256*16=1.5px 会断成虚线，取 2.2px 才连贯
    $cw = if ($s -le 24) { [Math]::Max(2.2, 26 * $f) } else { 24 * $f }
    $pen = [System.Drawing.Pen]::new([System.Drawing.Color]::White, $cw)
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap   = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $g.DrawLines($pen, @($p1, $p2, $p3))

    $pen.Dispose(); $grad.Dispose(); $path.Dispose()
    $g.Dispose()
    return $bmp
}

function Get-Dib([System.Drawing.Bitmap]$bmp) {
    $s = $bmp.Width
    $ms = [System.IO.MemoryStream]::new()
    $bw = [System.IO.BinaryWriter]::new($ms)
    $bw.Write([UInt32]40)
    $bw.Write([Int32]$s)
    $bw.Write([Int32]($s * 2))      # XOR + AND
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]0)
    $bw.Write([UInt32]0)
    $bw.Write([Int32]0); $bw.Write([Int32]0)
    $bw.Write([UInt32]0); $bw.Write([UInt32]0)
    $rect = [System.Drawing.Rectangle]::new(0, 0, $s, $s)
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $row = New-Object byte[] ($s * 4)
    for ($y = $s - 1; $y -ge 0; $y--) {          # 自下而上
        [System.Runtime.InteropServices.Marshal]::Copy(
            [IntPtr]($data.Scan0.ToInt64() + $y * $data.Stride), $row, 0, $s * 4)
        $bw.Write($row)
    }
    $bmp.UnlockBits($data)
    $maskRow = New-Object byte[] ([Math]::Ceiling($s / 32.0) * 4)
    for ($y = 0; $y -lt $s; $y++) { $bw.Write($maskRow) }
    $bw.Flush()
    return [byte[]]$ms.ToArray()
}

function Get-Png([System.Drawing.Bitmap]$bmp) {
    $ms = [System.IO.MemoryStream]::new()
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    return [byte[]]$ms.ToArray()
}

$blobs = @()
foreach ($s in $sizes) {
    $bmp = New-IconBitmap $s
    $bytes = if ($s -le 48) { Get-Dib $bmp } else { Get-Png $bmp }
    $bytes = [byte[]]$bytes
    $blobs += ,@{ Size = $s; Bytes = $bytes }
    if ($Preview -and $s -eq 256) { $bmp.Save($Preview, [System.Drawing.Imaging.ImageFormat]::Png) }
    $bmp.Dispose()
}

$fs = [System.IO.File]::Create([System.IO.Path]::GetFullPath($Out))
$bw = [System.IO.BinaryWriter]::new($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$blobs.Count)
$offset = 6 + 16 * $blobs.Count
foreach ($b in $blobs) {
    $dim = if ($b.Size -ge 256) { 0 } else { $b.Size }
    $bw.Write([byte]$dim)
    $bw.Write([byte]$dim)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$b.Bytes.Length)
    $bw.Write([UInt32]$offset)
    $offset += $b.Bytes.Length
}
foreach ($b in $blobs) { $bw.Write([byte[]]$b.Bytes) }
$bw.Flush(); $bw.Close(); $fs.Close()

Write-Host ("wrote " + $Out + " (" + (Get-Item $Out).Length + " bytes, " + $blobs.Count + " sizes)")
