# REBoarGetch.SpecのAutomation Testと、BlueprintAllowList.txtのBlueprintコンパイルを実行します。
# アセット・セーブは変更しません。ログはSaved/Verificationへ出力します。
param([Parameter(Mandatory=$true)][int]$Phase, [int]$ExpectedTests = 0)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$projectFile = Join-Path $projectRoot 'REBoarGetch.uproject'
$engineExecutable = Join-Path (& (Join-Path $PSScriptRoot 'FindEngine.ps1')) 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
if (-not (Test-Path -LiteralPath $engineExecutable)) { throw "UnrealEditor-Cmd.exe not found: $engineExecutable" }
$outputDir = Join-Path $projectRoot 'Saved\Verification'
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
$testLog = Join-Path $outputDir "Phase$Phase-Tests.log"
$blueprintLog = Join-Path $outputDir "Phase$Phase-Blueprint.log"

# 期待件数は、ソースに定義されたREBoarGetch.Specテストの数から求めます（0件実行や実行漏れを成功扱いしない）。
if ($ExpectedTests -le 0) {
    $testSources = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source\REBoarGetch\Private\Tests') -Filter *.cpp
    $sourceText = ($testSources | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    if ($sourceText -match 'IMPLEMENT_(COMPLEX|CUSTOM)_[A-Z_]*AUTOMATION_TEST|BEGIN_DEFINE_SPEC') {
        throw 'Complex tests or specs found; their case count cannot be derived from source. Pass -ExpectedTests explicitly.'
    }
    $ExpectedTests = ([regex]::Matches($sourceText, 'IMPLEMENT_SIMPLE_AUTOMATION_TEST\(\s*\w+\s*,\s*"REBoarGetch\.Spec\.')).Count
}
if ($ExpectedTests -le 0) { throw 'No REBoarGetch.Spec tests are defined; nothing to verify.' }

# Hidden engine processes; actual evidence comes from their absolute log paths.
$testProcess = Start-Process -FilePath $engineExecutable -ArgumentList @(
    "`"$projectFile`"", '-unattended', '-NullRHI', '-nosplash',
    '"-ExecCmds=Automation RunTests REBoarGetch.Spec"',
    '"-TestExit=Automation Test Queue Empty"', "`"-abslog=$testLog`""
) -WindowStyle Hidden -PassThru -Wait
$testContent = Get-Content -LiteralPath $testLog -Raw
$completed = ([regex]::Matches($testContent, 'Test Completed\. Result=\{')).Count
$passed = ([regex]::Matches($testContent, 'Test Completed\. Result=\{Success\}')).Count
$failed = $completed - $passed
Write-Output "Phase $Phase Automation: exit=$($testProcess.ExitCode), expected=$ExpectedTests, completed=$completed, passed=$passed, failed=$failed"
if ($testProcess.ExitCode -ne 0 -or $completed -ne $ExpectedTests -or $passed -ne $ExpectedTests) {
    throw "Automation verification failed; inspect $testLog"
}

# AllowListの全アセットが存在し、全件がコンパイルされたことを確認します。
$allowList = Join-Path $PSScriptRoot 'BlueprintAllowList.txt'
$assets = @(Get-Content -LiteralPath $allowList | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
if ($assets.Count -eq 0) { throw "BlueprintAllowList.txt is empty: $allowList" }
$missing = @($assets | Where-Object {
    $relative = ($_ -replace '^/Game/', '' -replace '\.[^./]+$', '') -replace '/', '\'
    -not (Test-Path -LiteralPath (Join-Path $projectRoot "Content\$relative.uasset"))
})
if ($missing.Count -gt 0) { throw "Allow-listed Blueprints not found: $($missing -join ', ')" }

$bpProcess = Start-Process -FilePath $engineExecutable -ArgumentList @(
    "`"$projectFile`"", '-run=CompileAllBlueprints',
    "`"-AllowListFile=$allowList`"",
    '-unattended', '-NullRHI', '-nosplash', "`"-abslog=$blueprintLog`""
) -WindowStyle Hidden -PassThru -Wait
$bpContent = Get-Content -LiteralPath $blueprintLog -Raw
$compiled = ([regex]::Matches($bpContent, 'Loading and Compiling:')).Count
$cleanCompilation = $bpContent.Contains('Compiling Completed with 0 errors and 0 warnings and 0 blueprints that failed to load.')
Write-Output "Phase $Phase Blueprint: exit=$($bpProcess.ExitCode), listed=$($assets.Count), compiled=$compiled, cleanCompilation=$cleanCompilation"
if (-not $cleanCompilation -or $compiled -ne $assets.Count) { throw "Blueprint compilation failed; inspect $blueprintLog" }
if ($bpProcess.ExitCode -ne 0) {
    # Recorded before edits in REBoarGetch-backup-2026.10.02-08.05.52.log.
    if (-not $bpContent.Contains('Asset Manager settings do not include an entry for assets of type GameFeatureData')) {
        throw "Unexpected commandlet error; inspect $blueprintLog"
    }
    Write-Output 'Existing GameFeatureData configuration error makes commandlet exit 1; BP compiler itself reports zero errors/warnings.'
}
