param([Parameter(Mandatory=$true)][int]$Phase, [int]$ExpectedTests = 6)
$ErrorActionPreference = 'Stop'
$engineExecutable = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
$sourceRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent (Split-Path -Parent $sourceRoot)
$projectFile = Join-Path $projectRoot 'REBoarGetch.uproject'
$testLog = Join-Path $PSScriptRoot "Phase$Phase-Tests.log"
$blueprintLog = Join-Path $PSScriptRoot "Phase$Phase-Blueprint.log"

# Hidden engine processes; actual evidence comes from their absolute log paths.
$testProcess = Start-Process -FilePath $engineExecutable -ArgumentList @(
    $projectFile, '-unattended', '-NullRHI', '-nosplash',
    '"-ExecCmds=Automation RunTests REBoarGetch.Spec"',
    '"-TestExit=Automation Test Queue Empty"', "-abslog=$testLog"
) -WindowStyle Hidden -PassThru -Wait
$testContent = Get-Content -LiteralPath $testLog -Raw
$passed = ([regex]::Matches($testContent, 'Test Completed\. Result=\{Success\}')).Count
$failed = ([regex]::Matches($testContent, 'Test Completed\. Result=\{Fail')).Count
Write-Output "Phase $Phase Automation: exit=$($testProcess.ExitCode), passed=$passed, failed=$failed"
if ($testProcess.ExitCode -ne 0 -or $passed -ne $ExpectedTests -or $failed -ne 0) { throw 'Automation verification failed; inspect log.' }

$bpProcess = Start-Process -FilePath $engineExecutable -ArgumentList @(
    $projectFile, '-run=CompileAllBlueprints',
    '-AllowListFile=Source/REBoarGetch/Verification/BlueprintAllowList.txt',
    '-unattended', '-NullRHI', '-nosplash', "-abslog=$blueprintLog"
) -WindowStyle Hidden -PassThru -Wait
$bpContent = Get-Content -LiteralPath $blueprintLog -Raw
$compiled = ([regex]::Matches($bpContent, 'Loading and Compiling:')).Count
$cleanCompilation = $bpContent.Contains('Compiling Completed with 0 errors and 0 warnings and 0 blueprints that failed to load.')
Write-Output "Phase $Phase Blueprint: exit=$($bpProcess.ExitCode), compiled=$compiled, cleanCompilation=$cleanCompilation"
if (-not $cleanCompilation -or $compiled -ne 41) { throw 'Blueprint compilation failed; inspect log.' }
if ($bpProcess.ExitCode -ne 0) {
    # Recorded before edits in REBoarGetch-backup-2026.10.02-08.05.52.log.
    if (-not $bpContent.Contains('Asset Manager settings do not include an entry for assets of type GameFeatureData')) {
        throw 'Unexpected commandlet error; inspect log.'
    }
    Write-Output 'Existing GameFeatureData configuration error makes commandlet exit 1; BP compiler itself reports zero errors/warnings.'
}
