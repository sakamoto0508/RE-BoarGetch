#!/bin/sh
# Usage: run_ue_gui.sh <script.py> [extra args] -- full editor (real GPU, offscreen)
# The script must quit the editor itself when it sees -RetroQuit or -VerificationQuit.
# Engine: UE_ENGINE_DIR, otherwise resolved by FindEngine.ps1. Log: Saved/Verification/<script>_gui.log
# Exit status: UE's exit code, or 1 when the log shows a Python error or Saved/Verification/<script>Results.json
# reports an error. An exit code of 1 caused only by the known GameFeatureData configuration errors counts as success.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/Saved/Verification
mkdir -p "$OUT"
ENGINE=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/FindEngine.ps1")") || exit 1
ENGINE=$(cygpath -u "$(printf '%s' "$ENGINE" | tr -d '\r')")
S=$1; shift
N=$(basename "$S" .py)
LOG=$OUT/${N}_gui.log
RESULT=$OUT/${N}Results.json
# 前回の結果JSONを今回の結果と取り違えないよう、このスクリプトの結果ファイルだけを消しておく。
rm -f "$RESULT"
MSYS_NO_PATHCONV=1 "$ENGINE/Engine/Binaries/Win64/UnrealEditor.exe" "$(cygpath -m "$ROOT/REBoarGetch.uproject")" /Engine/Maps/Entry -ExecutePythonScript="$(cygpath -m "$S")" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound -RetroQuit -VerificationQuit -abslog="$(cygpath -m "$LOG")" "$@" > /dev/null 2>&1
STATUS=$?
echo "exit $STATUS"
if [ ! -f "$LOG" ]; then
    echo "log not found: $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
    exit "$STATUS"
fi
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro|Critical error|Assertion failed" "$LOG" | grep -v "Deprecation\|LogInit: Display" | cut -c40-300 | head -40
FAILED=0
if grep -q -E "Traceback \(most recent call last\)|LogPython: Error|Critical error|Assertion failed" "$LOG"; then
    echo "Python error or crash found in $LOG"
    FAILED=1
fi
if [ -f "$RESULT" ] && ! grep -q '"error": null' "$RESULT"; then
    echo "Result reports an error: $RESULT"
    FAILED=1
fi
if [ "$FAILED" -eq 1 ]; then
    [ "$STATUS" -eq 0 ] && STATUS=1
elif [ "$STATUS" -eq 1 ] && grep -q "Warning/Error Summary (Unique only)" "$LOG" &&
    ! sed -n '/Warning\/Error Summary (Unique only)/,$p' "$LOG" | grep ": Error:" | grep -v -q "GameFeatureData"; then
    echo "exit 1 comes only from the known GameFeatureData configuration errors; treated as success"
    STATUS=0
fi
exit "$STATUS"
