#!/bin/sh
# Usage: run_ue_gui.sh <script.py> [extra args] -- full editor (real GPU, offscreen)
# The script must quit the editor itself when it sees -RetroQuit or -VerificationQuit.
# Engine: UE_ENGINE_DIR, otherwise resolved by FindEngine.ps1. Log: Saved/Verification/<script>_gui.log
# Exit status: UE's exit code; 1 when the log is missing or shows a Python exception, crash or assertion.
# Exit code 1 becomes 0 only when ue_log_checks.sh proves it comes from the known GameFeatureData errors alone.
# PieSmoke / StageSelectPieSmoke must also write Saved/Verification/<script>Results.json for this run
# (matching -VerificationRunId) that reports a complete, error-free run with unchanged user saves.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
. "$HERE/ue_log_checks.sh"
OUT=$ROOT/Saved/Verification
mkdir -p "$OUT"
ENGINE=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/FindEngine.ps1")") || exit 1
ENGINE=$(cygpath -u "$(printf '%s' "$ENGINE" | tr -d '\r')")
S=$1; shift
N=$(basename "$S" .py)
LOG=$OUT/${N}_gui.log
RESULT=$OUT/${N}Results.json
case "$N" in
    PieSmoke|StageSelectPieSmoke) REQUIRE_RESULT=1 ;;
    *) REQUIRE_RESULT=0 ;;
esac
RUN_ID=$(date +%Y%m%d%H%M%S)-$$
# 前回のログ・結果JSONを今回の判定に使わないよう、このスクリプトの分だけを消しておく。
rm -f "$LOG" "$RESULT"
MSYS_NO_PATHCONV=1 "$ENGINE/Engine/Binaries/Win64/UnrealEditor.exe" "$(cygpath -m "$ROOT/REBoarGetch.uproject")" /Engine/Maps/Entry -ExecutePythonScript="$(cygpath -m "$S")" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound -RetroQuit -VerificationQuit -VerificationRunId="$RUN_ID" -abslog="$(cygpath -m "$LOG")" "$@" > /dev/null 2>&1
STATUS=$?
echo "exit $STATUS"
if [ ! -f "$LOG" ]; then
    echo "log not found: $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
    exit "$STATUS"
fi
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro|Critical error|Assertion failed" "$LOG" | grep -v "Deprecation\|LogInit: Display" | cut -c40-300 | head -40
if log_has_fatal "$LOG"; then
    echo "Python exception, crash or assertion found in $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
elif [ "$STATUS" -eq 1 ] && only_known_gamefeature_errors "$LOG"; then
    echo "exit 1 comes only from the known GameFeatureData configuration errors; treated as success"
    STATUS=0
fi
if [ "$REQUIRE_RESULT" -eq 1 ]; then
    if ! "$ENGINE/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -I "$(cygpath -w "$HERE/check_smoke_result.py")" "$(cygpath -w "$RESULT")" "$RUN_ID"; then
        [ "$STATUS" -eq 0 ] && STATUS=1
    fi
fi
exit "$STATUS"
