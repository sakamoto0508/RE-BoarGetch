# Builds a labelled grid image from screenshots.
# powershell -File MakeContactSheet.ps1 -Out sheet.png -Columns 3 -ImageList "a.png|b.png" -LabelList "A|B" [-CropX 0 -CropY 0 -CropW 0 -CropH 0]
param(
    [Parameter(Mandatory)] [string] $Out,
    [Parameter(Mandatory)] [string] $ImageList,
    [string] $LabelList = "",
    [int] $Columns = 3,
    [int] $CellWidth = 640,
    [int] $CropX = 0, [int] $CropY = 0, [int] $CropW = 0, [int] $CropH = 0,
    [string] $Title = ""
)
Add-Type -AssemblyName System.Drawing
$Images = @($ImageList.Split("|") | ForEach-Object { (Resolve-Path $_).Path })
$Labels = if ($LabelList) { $LabelList.Split("|") } else { @() }
$first = [System.Drawing.Image]::FromFile($Images[0])
$srcW = if ($CropW -gt 0) { $CropW } else { $first.Width }
$srcH = if ($CropH -gt 0) { $CropH } else { $first.Height }
$first.Dispose()
$cellH = [int]($CellWidth * $srcH / $srcW)
$labelH = 30; $titleH = if ($Title) { 40 } else { 0 }
$rows = [math]::Ceiling($Images.Count / $Columns)
$sheet = New-Object System.Drawing.Bitmap ($Columns * ($CellWidth + 8) + 8), ($titleH + $rows * ($cellH + $labelH + 8) + 8)
$g = [System.Drawing.Graphics]::FromImage($sheet)
$g.Clear([System.Drawing.Color]::FromArgb(18, 24, 36))
# Nearest neighbour keeps retro pixels honest when scaling down/up.
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$font = New-Object System.Drawing.Font "Segoe UI", 14
$brush = [System.Drawing.Brushes]::White
if ($Title) { $g.DrawString($Title, (New-Object System.Drawing.Font "Segoe UI", 18), $brush, 8, 6) }
for ($i = 0; $i -lt $Images.Count; $i++) {
    $img = [System.Drawing.Image]::FromFile($Images[$i])
    $x = 8 + ($i % $Columns) * ($CellWidth + 8)
    $y = $titleH + 8 + [math]::Floor($i / $Columns) * ($cellH + $labelH + 8)
    $label = if ($i -lt $Labels.Count) { $Labels[$i] } else { [IO.Path]::GetFileNameWithoutExtension($Images[$i]) }
    $g.DrawString($label, $font, $brush, $x, $y)
    $src = New-Object System.Drawing.Rectangle $CropX, $CropY, $srcW, $srcH
    $dst = New-Object System.Drawing.Rectangle $x, ($y + $labelH), $CellWidth, $cellH
    $g.DrawImage($img, $dst, $src, [System.Drawing.GraphicsUnit]::Pixel)
    $img.Dispose()
}
$sheet.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $sheet.Dispose()
Write-Output "saved $Out"
