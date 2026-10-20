# Backup / restore of the git-ignored InportAssets packages that RetroRender modifies.
#   Backup : powershell -File InportAssetsBackup.ps1 -Backup  -Archive D:\Share\RetroRender_InportAssets.zip
#   Restore: powershell -File InportAssetsBackup.ps1 -Restore -Archive D:\Share\RetroRender_InportAssets.zip
#   Pristine (pre-patch copies kept by PatchRetroMaterials.py): -Restore -Pristine
# The package list comes from Plugins/RetroRender/Docs/InportAssetsDependencies.json (InventoryInportAssets.py).
# Restore overwrites .uasset files, so it refuses to run while the Unreal Editor is open.
param(
    [switch] $Backup,
    [switch] $Restore,
    [switch] $Pristine,
    [string] $Archive = ""
)
$ErrorActionPreference = "Stop"
$Project = (Resolve-Path "$PSScriptRoot\..\..\..").Path
$Content = Join-Path $Project "Content"
$Manifest = Join-Path $PSScriptRoot "..\Docs\InportAssetsDependencies.json"
$PristineDir = Join-Path $Project "Saved\RetroRender\MaterialBackup"

function PackageFile([string] $Package) { Join-Path $Content (($Package -replace '^/Game/', '') + ".uasset") }

if ($Backup -eq $Restore) { throw "Specify exactly one of -Backup or -Restore." }
if ($Restore -and (Get-Process UnrealEditor -ErrorAction SilentlyContinue)) { throw "Close the Unreal Editor before restoring packages." }

if ($Backup) {
    if (-not $Archive) { throw "-Archive <file.zip> is required." }
    $packages = (Get-Content $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json).modified | ForEach-Object { $_.package }
    $stage = Join-Path ([IO.Path]::GetTempPath()) ("RetroInport_" + [guid]::NewGuid().ToString("N"))
    $lines = @()
    foreach ($p in $packages) {
        $src = PackageFile $p
        if (-not (Test-Path -LiteralPath $src)) { Write-Warning "missing: $p"; continue }
        $rel = $src.Substring($Content.Length + 1)
        $dst = Join-Path $stage $rel
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath $src $dst
        $lines += "{0}`t{1}" -f (Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash, $rel
    }
    Set-Content -Path (Join-Path $stage "manifest.tsv") -Value $lines -Encoding UTF8
    if (Test-Path $Archive) { Remove-Item $Archive }
    Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $Archive
    Remove-Item -Recurse -Force $stage
    Write-Output ("backed up {0} packages -> {1}" -f $lines.Count, $Archive)
    exit 0
}

if ($Pristine) {
    if (-not (Test-Path $PristineDir)) { throw "No pre-patch copies in $PristineDir" }
    $files = Get-ChildItem -Recurse -File $PristineDir
    foreach ($f in $files) {
        $rel = $f.FullName.Substring($PristineDir.Length + 1)
        Copy-Item -LiteralPath $f.FullName (Join-Path $Content $rel) -Force
        Write-Output "restored pristine $rel"
    }
    exit 0
}

if (-not $Archive) { throw "-Archive <file.zip> is required (or -Pristine)." }
$stage = Join-Path ([IO.Path]::GetTempPath()) ("RetroInport_" + [guid]::NewGuid().ToString("N"))
Expand-Archive -Path $Archive -DestinationPath $stage
$ok = 0
foreach ($line in Get-Content (Join-Path $stage "manifest.tsv") -Encoding UTF8) {
    $hash, $rel = $line -split "`t", 2
    $src = Join-Path $stage $rel
    if ((Get-FileHash -LiteralPath $src -Algorithm SHA256).Hash -ne $hash) { throw "hash mismatch: $rel" }
    $dst = Join-Path $Content $rel
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    Copy-Item -LiteralPath $src $dst -Force
    $ok++
}
Remove-Item -Recurse -Force $stage
Write-Output "restored $ok packages from $Archive"
