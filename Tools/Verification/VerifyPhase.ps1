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
# 前回のログを今回の判定に使わないよう、このPhaseのログだけを消しておく。
Remove-Item -LiteralPath $testLog, $blueprintLog -ErrorAction SilentlyContinue

$fatalPattern = 'Traceback \(most recent call last\)|LogPython: Error|Critical error|Assertion failed|Fatal error|Unhandled Exception'
$knownGameFeatureErrors = @(
    'LogGameFeatures: Error: Asset manager settings do not include a rule for assets of type GameFeatureData',
    'LoadErrors: Error: Asset Manager settings do not include an entry for assets of type GameFeatureData')

# 終了コード1が既知のGameFeatureData設定エラーだけによるものと、ログから証明できる場合だけ$trueを返します。
# 条件: ログが閉じられ終了時サマリー（"Warning/Error Summary" … "Failure - N error(s)"）があること、
# 既知エラーが記録され、ログ中の": Error:"行がすべて既知エラーであること、サマリー前のError行数がNと一致すること。
function Test-OnlyKnownGameFeatureErrors([string[]]$Lines) {
    if (-not ($Lines | Where-Object { $_.Contains('Log file closed') })) { return $false }
    $header = -1; $footer = -1; $count = -1
    for ($i = 0; $i -lt $Lines.Count; $i++) {
        if ($Lines[$i].Contains('Warning/Error Summary (Unique only)')) { $header = $i }
        if ($Lines[$i] -match 'LogInit: Display: Failure - (\d+) error\(s\)') { $footer = $i; $count = [int]$Matches[1] }
    }
    if ($header -lt 1 -or $footer -le $header) { return $false }
    $errors = @($Lines | Where-Object { $_.Contains(': Error:') })
    $unknown = @($errors | Where-Object { $line = $_; -not ($knownGameFeatureErrors | Where-Object { $line.Contains($_) }) })
    $body = @($Lines[0..($header - 1)] | Where-Object { $_.Contains(': Error:') })
    return ($body.Count -gt 0 -and $unknown.Count -eq 0 -and $body.Count -eq $count)
}

# 期待するテストは、ソースに定義されたREBoarGetch.Specテストの名前から求めます（0件実行や実行漏れを成功扱いしない）。
$expectedNames = @()
if ($ExpectedTests -le 0) {
    $testSources = Get-ChildItem -LiteralPath (Join-Path $projectRoot 'Source\REBoarGetch\Private\Tests') -Filter *.cpp
    $sourceText = ($testSources | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n"
    if ($sourceText -match 'IMPLEMENT_(COMPLEX|CUSTOM)_[A-Z_]*AUTOMATION_TEST|BEGIN_DEFINE_SPEC') {
        throw 'Complex tests or specs found; their case count cannot be derived from source. Pass -ExpectedTests explicitly.'
    }
    $expectedNames = @([regex]::Matches($sourceText, 'IMPLEMENT_SIMPLE_AUTOMATION_TEST\(\s*\w+\s*,\s*"(REBoarGetch\.Spec\.[^"]+)"') |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    $ExpectedTests = $expectedNames.Count
}
if ($ExpectedTests -le 0) { throw 'No REBoarGetch.Spec tests are defined; nothing to verify.' }

# Hidden engine processes; actual evidence comes from their absolute log paths.
$testProcess = Start-Process -FilePath $engineExecutable -ArgumentList @(
    "`"$projectFile`"", '-unattended', '-NullRHI', '-nosplash',
    '"-ExecCmds=Automation RunTests REBoarGetch.Spec"',
    '"-TestExit=Automation Test Queue Empty"', "`"-abslog=$testLog`""
) -WindowStyle Hidden -PassThru -Wait
if (-not (Test-Path -LiteralPath $testLog)) { throw "Automation log was not written: $testLog" }
$testContent = Get-Content -LiteralPath $testLog -Raw
# ログ形式: "Test Completed. Result={Success} Name={…} Path={REBoarGetch.Spec.…}"、最後に "Automation Test Queue Empty N tests performed."
$completed = ([regex]::Matches($testContent, 'Test Completed\. Result=\{')).Count
$passedNames = @([regex]::Matches($testContent, 'Test Completed\. Result=\{Success\} Name=\{[^}]*\} Path=\{([^}]+)\}') |
    ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$performed = if ($testContent -match 'Automation Test Queue Empty (\d+) tests performed') { [int]$Matches[1] } else { -1 }
$failed = $completed - $passedNames.Count
Write-Output "Phase $Phase Automation: exit=$($testProcess.ExitCode), expected=$ExpectedTests, performed=$performed, completed=$completed, passed=$($passedNames.Count), failed=$failed"
$testProblems = @()
# テスト実行はEditorの終了コードを例外扱いせず、0だけを成功とします（終了時サマリーが無く、既知エラーだけとは証明できないため）。
if ($testProcess.ExitCode -ne 0) { $testProblems += "exit code $($testProcess.ExitCode)" }
if ($performed -ne $ExpectedTests) { $testProblems += "queue reported $performed tests (log incomplete or tests missing)" }
if ($completed -ne $ExpectedTests -or $passedNames.Count -ne $ExpectedTests) { $testProblems += 'not every expected test completed with Success' }
if ($expectedNames.Count -gt 0) {
    $notPassed = @($expectedNames | Where-Object { $passedNames -notcontains $_ })
    if ($notPassed.Count -gt 0) { $testProblems += "not passed: $($notPassed -join ', ')" }
}
if ($testContent -match $fatalPattern) { $testProblems += 'crash, assertion or Python exception in log' }
if ($testProblems.Count -gt 0) { throw "Automation verification failed ($($testProblems -join '; ')); inspect $testLog" }

# AllowListの全アセットが存在することを確認します。
# AllowListにはMaterial・DataAsset・StateTreeも含まれ、CompileAllBlueprintsはそれらをコンパイルしません。
# そこで生成クラス（<アセット名>_C）を持つアセットだけをBlueprintとして数え、ログと照合します。
$allowList = Join-Path $PSScriptRoot 'BlueprintAllowList.txt'
$assets = @(Get-Content -LiteralPath $allowList | ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith('#') })
if ($assets.Count -eq 0) { throw "BlueprintAllowList.txt is empty: $allowList" }
$missing = @()
$blueprints = @()
foreach ($asset in $assets) {
    $relative = ($asset -replace '^/Game/', '' -replace '\.[^./]+$', '') -replace '/', '\'
    $file = Join-Path $projectRoot "Content\$relative.uasset"
    if (-not (Test-Path -LiteralPath $file)) { $missing += $asset; continue }
    # バイト列を1文字ずつ対応させて読み、パッケージ内の名前を文字列として探します。
    $bytes = [System.Text.Encoding]::GetEncoding(28591).GetString([System.IO.File]::ReadAllBytes($file))
    if ($bytes.StartsWith('version https://git-lfs')) { throw "Git LFS pointer, not the asset: $file (run git lfs pull)" }
    if ($bytes.Contains(($asset -replace '^.*\.', '') + '_C')) { $blueprints += $asset }
}
if ($missing.Count -gt 0) { throw "Allow-listed assets not found: $($missing -join ', ')" }
if ($blueprints.Count -eq 0) { throw "BlueprintAllowList.txt contains no Blueprints: $allowList" }

$bpProcess = Start-Process -FilePath $engineExecutable -ArgumentList @(
    "`"$projectFile`"", '-run=CompileAllBlueprints',
    "`"-AllowListFile=$allowList`"",
    '-unattended', '-NullRHI', '-nosplash', "`"-abslog=$blueprintLog`""
) -WindowStyle Hidden -PassThru -Wait
if (-not (Test-Path -LiteralPath $blueprintLog)) { throw "Blueprint log was not written: $blueprintLog" }
$bpContent = Get-Content -LiteralPath $blueprintLog -Raw
# ログ形式: "LogCompileAllBlueprintsCommandlet: Display: Loading and Compiling: '/Game/...Name.Name'..."
$compiled = @([regex]::Matches($bpContent, "Loading and Compiling: '([^']+)'") | ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
$notCompiled = @($blueprints | Where-Object { $compiled -notcontains $_ })
$unexpected = @($compiled | Where-Object { $blueprints -notcontains $_ })
$cleanCompilation = $bpContent.Contains('Compiling Completed with 0 errors and 0 warnings and 0 blueprints that failed to load.')
Write-Output "Phase $Phase Blueprint: exit=$($bpProcess.ExitCode), listed=$($assets.Count), blueprints=$($blueprints.Count), compiled=$($compiled.Count), cleanCompilation=$cleanCompilation"
if ($notCompiled.Count -gt 0) { Write-Output "Not compiled: $($notCompiled -join ', ')" }
if ($unexpected.Count -gt 0) { Write-Output "Compiled but not expected: $($unexpected -join ', ')" }
if (-not $cleanCompilation -or $compiled.Count -eq 0 -or $notCompiled.Count -gt 0 -or $unexpected.Count -gt 0) {
    throw "Blueprint compilation failed; inspect $blueprintLog"
}
if ($bpContent -match $fatalPattern) { throw "Crash, assertion or Python exception in log; inspect $blueprintLog" }
if ($bpProcess.ExitCode -ne 0) {
    # 既知のGameFeatureData設定エラー（REBoarGetch-backup-2026.10.02-08.05.52.logで記録）だけで終了コード1になる場合に限り許容します。
    if ($bpProcess.ExitCode -ne 1 -or -not (Test-OnlyKnownGameFeatureErrors (Get-Content -LiteralPath $blueprintLog))) {
        throw "Commandlet exited with $($bpProcess.ExitCode) and the log does not prove it is only the known GameFeatureData errors; inspect $blueprintLog"
    }
    Write-Output 'Exit code 1 comes only from the known GameFeatureData configuration errors; BP compiler itself reports zero errors/warnings.'
}
