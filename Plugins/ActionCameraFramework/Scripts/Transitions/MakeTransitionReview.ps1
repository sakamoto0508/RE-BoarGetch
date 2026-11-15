# Action Camera Framework - Phase 7D: review material for the 42 transitions (README 33 章「目視確認用の出力」).
#
# Input: the frames of the real game viewport saved by the test ActionCameraFramework.Transitions.Record.Frames
#   (ACF_RECORD_TRANSITIONS=1 Scripts/RunTests.sh --context samples ActionCameraFramework.Transitions.Record)
#   in <Project>/Saved/ACFTransitions/Record (<ID>_<C|O>_<NN>.png, 17 Close + 17 Open points, index.tsv).
# Output in <Project>/Saved/ACFTransitions/Review:
#   Frames/       every frame with its number, name, phase and progress written on it
#   Sheet_*.png   representative frames: one row per effect, Close 0/25/50/75/100 %, Open 25/50/75/100 % played
#   ACF_Transitions_42.mp4   all frames in ID order (H.264, Windows Media Foundation; no extra tools)
#   Spiral_Reference.png     (with -Reference) the reference video's close / open next to 41 / 42 at the same progress
#
#   powershell -ExecutionPolicy Bypass -File Scripts/Transitions/MakeTransitionReview.ps1 -Project C:/Path/Game
#     [-Reference C:/Path/video.mp4 -RefClose 1.27,2.13 -RefOpen 2.90,3.80] [-NoVideo]
#
# Windows PowerShell 5.1 (System.Drawing, WinRT Windows.Media.Editing). Nothing here is cooked or used at runtime.
param(
	[string]$Project = (Resolve-Path (Join-Path $PSScriptRoot '../../../..')).Path,
	[string]$Reference = '',
	[string]$RefClose = '',   # "start,end" seconds of the close in the reference video
	[string]$RefOpen = '',    # "start,end" seconds of the open
	[double]$FrameSeconds = 0.05,
	[switch]$NoVideo
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Runtime.WindowsRuntime

$record = Join-Path $Project 'Saved/ACFTransitions/Record'
$review = Join-Path $Project 'Saved/ACFTransitions/Review'
$framesDir = Join-Path $review 'Frames'
if (-not (Test-Path (Join-Path $record 'index.tsv'))) { throw "No recorded frames in $record (run the Record test first)." }
New-Item -ItemType Directory -Force $framesDir | Out-Null

$effects = @()
foreach ($line in Get-Content -Encoding UTF8 (Join-Path $record 'index.tsv')) {
	if ($line.Trim() -eq '') { continue }
	$p = $line.Split("`t")
	$effects += [pscustomobject]@{ Id = [int]$p[0]; Name = $p[1]; Capture = ($p.Count -gt 2 -and $p[2] -eq 'capture') }
}
$steps = 16
$font = New-Object System.Drawing.Font 'Segoe UI', 11, ([System.Drawing.FontStyle]::Bold)
$small = New-Object System.Drawing.Font 'Segoe UI', 9
$bar = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(190, 0, 0, 0))

# 1. Captions.
$ordered = New-Object System.Collections.Generic.List[object]
foreach ($e in $effects) {
	foreach ($phase in 'C', 'O') {
		for ($n = 0; $n -le $steps; $n++) {
			$src = Join-Path $record ('{0:D2}_{1}_{2:D2}.png' -f $e.Id, $phase, $n)
			if (-not (Test-Path $src)) { continue }
			$dst = Join-Path $framesDir ('{0:D2}_{1}_{2:D2}.png' -f $e.Id, $phase, $n)
			$img = [System.Drawing.Image]::FromFile($src)
			$bmp = New-Object System.Drawing.Bitmap $img.Width, $img.Height
			$g = [System.Drawing.Graphics]::FromImage($bmp)
			$g.DrawImage($img, 0, 0, $img.Width, $img.Height)
			$g.FillRectangle($bar, 0, 0, $img.Width, 24)
			$pct = [int][math]::Round(100.0 * $n / $steps)
			$label = '{0:D2}  {1}' -f $e.Id, $e.Name
			$phaseText = if ($phase -eq 'C') { "Close $pct%" } else { "Open $pct%" }
			$g.DrawString($label, $font, [System.Drawing.Brushes]::White, 6, 2)
			$sz = $g.MeasureString($phaseText, $font)
			$g.DrawString($phaseText, $font, [System.Drawing.Brushes]::Gold, $img.Width - $sz.Width - 6, 2)
			$bmp.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
			$g.Dispose(); $bmp.Dispose(); $img.Dispose()
			$hold = if ($n -eq 0 -or $n -eq $steps) { 6 } else { 1 }
			$ordered.Add([pscustomobject]@{ Path = $dst; Hold = $hold })
		}
	}
}
Write-Output "captioned $($ordered.Count) frames"

# 2. Sheets of representative frames (14 effects per sheet).
$cols = @(@('C', 0), @('C', 4), @('C', 8), @('C', 12), @('C', 16), @('O', 4), @('O', 8), @('O', 12), @('O', 16))
$heads = @('Close 0%', 'Close 25%', 'Close 50%', 'Close 75%', 'Close 100%', 'Open 25%', 'Open 50%', 'Open 75%', 'Open 100%')
$cw = 192; $ch = 108; $lw = 170; $hh = 22; $per = 14
for ($s = 0; $s -lt [math]::Ceiling($effects.Count / $per); $s++) {
	$set = $effects | Select-Object -Skip ($s * $per) -First $per
	$sheet = New-Object System.Drawing.Bitmap ($lw + $cols.Count * ($cw + 3)), ($hh + @($set).Count * ($ch + 3))
	$g = [System.Drawing.Graphics]::FromImage($sheet)
	$g.Clear([System.Drawing.Color]::FromArgb(36, 36, 36))
	for ($c = 0; $c -lt $cols.Count; $c++) { $g.DrawString($heads[$c], $small, [System.Drawing.Brushes]::Gold, $lw + $c * ($cw + 3) + 4, 3) }
	$row = 0
	foreach ($e in $set) {
		$y = $hh + $row * ($ch + 3)
		$g.DrawString(('{0:D2}' -f $e.Id), $font, [System.Drawing.Brushes]::White, 4, $y + 4)
		$g.DrawString($e.Name, $small, [System.Drawing.Brushes]::White, 4, $y + 26)
		if ($e.Capture) { $g.DrawString('(screen capture)', $small, [System.Drawing.Brushes]::Silver, 4, $y + 44) }
		for ($c = 0; $c -lt $cols.Count; $c++) {
			$src = Join-Path $record ('{0:D2}_{1}_{2:D2}.png' -f $e.Id, $cols[$c][0], $cols[$c][1])
			if (Test-Path $src) { $img = [System.Drawing.Image]::FromFile($src); $g.DrawImage($img, $lw + $c * ($cw + 3), $y, $cw, $ch); $img.Dispose() }
		}
		$row++
	}
	$first = @($set)[0].Id; $last = @($set)[-1].Id
	$out = Join-Path $review ('Sheet_{0:D2}-{1:D2}.png' -f $first, $last)
	$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
	$g.Dispose(); $sheet.Dispose()
	Write-Output "sheet $out"
}

# WinRT helpers.
$null = [Windows.Storage.StorageFile, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFolder, Windows.Storage, ContentType = WindowsRuntime]
$null = [Windows.Media.Editing.MediaComposition, Windows.Media.Editing, ContentType = WindowsRuntime]
$null = [Windows.Media.Editing.MediaClip, Windows.Media.Editing, ContentType = WindowsRuntime]
$null = [Windows.Media.MediaProperties.MediaEncodingProfile, Windows.Media.MediaProperties, ContentType = WindowsRuntime]
$asTask1 = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
$asTask2 = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperationWithProgress`2' })[0]
function Await($op, [Type]$type) { $t = $asTask1.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }
function AwaitP($op, [Type]$type, [Type]$prog) { $t = $asTask2.MakeGenericMethod($type, $prog).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }
$addClip = [System.Collections.Generic.ICollection[Windows.Media.Editing.MediaClip]].GetMethod('Add')

function Get-VideoFrame($comp, [double]$t, [int]$w, [int]$h, [string]$path) {
	$stream = Await ($comp.GetThumbnailAsync([TimeSpan]::FromSeconds($t), $w, $h, [Windows.Media.Editing.VideoFramePrecision]::NearestFrame)) ([Windows.Graphics.Imaging.ImageStream])
	$net = [System.IO.WindowsRuntimeStreamExtensions]::AsStreamForRead($stream)
	$fs = [System.IO.File]::Create($path); $net.CopyTo($fs); $fs.Close(); $net.Close()
}

# 3. MP4 of every frame.
if (-not $NoVideo) {
	$comp = New-Object Windows.Media.Editing.MediaComposition
	foreach ($f in $ordered) {
		$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($f.Path)) ([Windows.Storage.StorageFile])
		$clip = Await ([Windows.Media.Editing.MediaClip]::CreateFromImageFileAsync($file, [TimeSpan]::FromSeconds($FrameSeconds * $f.Hold))) ([Windows.Media.Editing.MediaClip])
		$addClip.Invoke($comp.Clips, @($clip)) | Out-Null
	}
	$folder = Await ([Windows.Storage.StorageFolder]::GetFolderFromPathAsync((Resolve-Path $review).Path)) ([Windows.Storage.StorageFolder])
	$outFile = Await ($folder.CreateFileAsync('ACF_Transitions_42.mp4', [Windows.Storage.CreationCollisionOption]::ReplaceExisting)) ([Windows.Storage.StorageFile])
	$profile = [Windows.Media.MediaProperties.MediaEncodingProfile]::CreateMp4([Windows.Media.MediaProperties.VideoEncodingQuality]::Vga)
	$profile.Video.Width = 480; $profile.Video.Height = 270
	$profile.Audio = $null
	$result = AwaitP ($comp.RenderToFileAsync($outFile, [Windows.Media.Editing.MediaTrimmingPreference]::Precise, $profile)) ([Windows.Media.Transcoding.TranscodeFailureReason]) ([double])
	Write-Output "video $($outFile.Path): $result ($([math]::Round($comp.Duration.TotalSeconds, 1)) s)"
}

# 4. Reference comparison for 41 / 42.
$RefCloseRange = @($RefClose.Split(',') | Where-Object { $_ -ne '' } | ForEach-Object { [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) })
$RefOpenRange = @($RefOpen.Split(',') | Where-Object { $_ -ne '' } | ForEach-Object { [double]::Parse($_, [Globalization.CultureInfo]::InvariantCulture) })
if ($Reference -ne '' -and $RefCloseRange.Count -eq 2 -and $RefOpenRange.Count -eq 2) {
	$tmp = Join-Path $review 'Reference'
	New-Item -ItemType Directory -Force $tmp | Out-Null
	$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Resolve-Path $Reference).Path)) ([Windows.Storage.StorageFile])
	$clip = Await ([Windows.Media.Editing.MediaClip]::CreateFromFileAsync($file)) ([Windows.Media.Editing.MediaClip])
	$rc = New-Object Windows.Media.Editing.MediaComposition
	$addClip.Invoke($rc.Clips, @($clip)) | Out-Null
	$points = 9
	$cw = 192; $ch = 108; $lw = 150
	$sheet = New-Object System.Drawing.Bitmap ($lw + $points * ($cw + 3)), (22 + 4 * ($ch + 3))
	$g = [System.Drawing.Graphics]::FromImage($sheet)
	$g.Clear([System.Drawing.Color]::FromArgb(36, 36, 36))
	$rows = @(
		@('Reference close', 'ref', $RefCloseRange), @('41 Spiral Close', 'C', 41),
		@('Reference open', 'ref', $RefOpenRange), @('42 Spiral Open', 'O', 42))
	for ($k = 0; $k -lt $points; $k++) { $g.DrawString(('{0}%' -f [int](100 * $k / ($points - 1))), $small, [System.Drawing.Brushes]::Gold, $lw + $k * ($cw + 3) + 4, 3) }
	for ($r = 0; $r -lt 4; $r++) {
		$y = 22 + $r * ($ch + 3)
		$g.DrawString($rows[$r][0], $small, [System.Drawing.Brushes]::White, 4, $y + 40)
		for ($k = 0; $k -lt $points; $k++) {
			$a = $k / ($points - 1)
			if ($rows[$r][1] -eq 'ref') {
				$range = $rows[$r][2]
				$path = Join-Path $tmp ('ref_{0}_{1}.jpg' -f $r, $k)
				Get-VideoFrame $rc ($range[0] + $a * ($range[1] - $range[0])) $cw $ch $path
			} else {
				$path = Join-Path $record ('{0:D2}_{1}_{2:D2}.png' -f $rows[$r][2], $rows[$r][1], [int][math]::Round($a * $steps))
			}
			if (Test-Path $path) { $img = [System.Drawing.Image]::FromFile($path); $g.DrawImage($img, $lw + $k * ($cw + 3), $y, $cw, $ch); $img.Dispose() }
		}
	}
	$out = Join-Path $review 'Spiral_Reference.png'
	$sheet.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
	$g.Dispose(); $sheet.Dispose()
	Write-Output "reference comparison $out"
}
