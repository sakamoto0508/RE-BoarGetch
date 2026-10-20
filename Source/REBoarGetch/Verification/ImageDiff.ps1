# Mean absolute RGB difference and share of pixels differing by more than -Threshold (0-255, max channel).
# powershell -File ImageDiff.ps1 -A a.png -B b.png [-Threshold 8] [-DiffOut diff.png]
param([Parameter(Mandatory)] [string] $A, [Parameter(Mandatory)] [string] $B, [int] $Threshold = 8, [string] $DiffOut = "")
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @"
using System; using System.Drawing; using System.Drawing.Imaging; using System.Runtime.InteropServices;
public static class RetroDiff {
  public static double[] Run(Bitmap a, Bitmap b, int threshold, Bitmap diff) {
    var r = new Rectangle(0, 0, a.Width, a.Height);
    var da = a.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    var db = b.LockBits(r, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
    int n = da.Stride * a.Height; var pa = new byte[n]; var pb = new byte[n];
    Marshal.Copy(da.Scan0, pa, 0, n); Marshal.Copy(db.Scan0, pb, 0, n);
    a.UnlockBits(da); b.UnlockBits(db);
    byte[] pd = diff != null ? new byte[n] : null;
    double sum = 0; long over = 0; long px = (long)a.Width * a.Height;
    for (int i = 0; i < n; i += 4) {
      int d0 = Math.Abs(pa[i] - pb[i]), d1 = Math.Abs(pa[i+1] - pb[i+1]), d2 = Math.Abs(pa[i+2] - pb[i+2]);
      sum += d0 + d1 + d2; int m = Math.Max(d0, Math.Max(d1, d2)); if (m > threshold) over++;
      if (pd != null) { byte v = (byte)Math.Min(255, m * 4); pd[i] = v; pd[i+1] = v; pd[i+2] = v; pd[i+3] = 255; }
    }
    if (diff != null) { var dd = diff.LockBits(r, ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb); Marshal.Copy(pd, 0, dd.Scan0, n); diff.UnlockBits(dd); }
    return new double[] { sum / (px * 3.0), 100.0 * over / px };
  }
}
"@ -ReferencedAssemblies System.Drawing
$ba = New-Object System.Drawing.Bitmap $A
$bb = New-Object System.Drawing.Bitmap $B
$bd = if ($DiffOut) { New-Object System.Drawing.Bitmap $ba.Width, $ba.Height } else { $null }
$res = [RetroDiff]::Run($ba, $bb, $Threshold, $bd)
if ($bd) { $bd.Save($DiffOut, [System.Drawing.Imaging.ImageFormat]::Png); $bd.Dispose() }
$ba.Dispose(); $bb.Dispose()
"{0:N3} mean_abs  {1:N3} pct_over_{2}" -f $res[0], $res[1], $Threshold
