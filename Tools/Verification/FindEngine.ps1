# このプロジェクトが使うUnreal Engineのルート（Engine\Binariesを含むフォルダ）を1行で出力します。
# 優先順: 環境変数UE_ENGINE_DIR → Epic Launcherのインストール情報 → レジストリ（ランチャー版・ソースビルド）。
param([string]$ProjectFile = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'REBoarGetch.uproject'))
$ErrorActionPreference = 'Stop'

if ($env:UE_ENGINE_DIR) { return $env:UE_ENGINE_DIR }
$version = (Get-Content -LiteralPath $ProjectFile -Raw | ConvertFrom-Json).EngineAssociation

$launcher = Join-Path $env:ProgramData 'Epic\UnrealEngineLauncher\LauncherInstalled.dat'
if (Test-Path -LiteralPath $launcher) {
    $entry = (Get-Content -LiteralPath $launcher -Raw | ConvertFrom-Json).InstallationList |
        Where-Object { $_.AppName -eq "UE_$version" } | Select-Object -First 1
    if ($entry) { return $entry.InstallLocation }
}
$installed = "HKLM:\SOFTWARE\EpicGames\Unreal Engine\$version"
if (Test-Path $installed) { return (Get-ItemProperty $installed).InstalledDirectory }
# EngineAssociationがGUIDのソースビルド。
$builds = Get-ItemProperty 'HKCU:\Software\Epic Games\Unreal Engine\Builds' -ErrorAction SilentlyContinue
if ($builds -and $builds.$version) { return $builds.$version }
throw "Unreal Engine '$version' was not found. Set UE_ENGINE_DIR to the engine root (the folder that contains Engine\Binaries)."
