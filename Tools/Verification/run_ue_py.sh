#!/bin/sh
# Usage: run_ue_py.sh <script.py> [extra args]  -- headless NullRHI python commandlet
# Engine: UE_ENGINE_DIR, otherwise resolved by FindEngine.ps1. Log: Saved/Verification/<script>.log
# Exit status: UE's exit code; 1 when the log is missing or shows a Python exception, crash or assertion.
# Exit code 1 becomes 0 only when ue_log_checks.sh proves it comes from the known GameFeatureData errors alone.
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
. "$HERE/ue_log_checks.sh"
OUT=$ROOT/Saved/Verification
mkdir -p "$OUT"
ENGINE=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/FindEngine.ps1")") || exit 1
ENGINE=$(cygpath -u "$(printf '%s' "$ENGINE" | tr -d '\r')")
S=$1; shift
N=$(basename "$S" .py)
LOG=$OUT/$N.log
# 前回のログを今回の判定に使わないよう、このスクリプトのログだけを消しておく。
rm -f "$LOG"
"$ENGINE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$(cygpath -m "$ROOT/REBoarGetch.uproject")" -run=pythonscript -script="$(cygpath -m "$S")" -unattended -nosplash -NullRHI -abslog="$(cygpath -m "$LOG")" "$@" > /dev/null 2>&1
STATUS=$?
echo "exit $STATUS"
if [ ! -f "$LOG" ]; then
    echo "log not found: $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
    exit "$STATUS"
fi
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro" "$LOG" | grep -v "Deprecation\|LogInit: Display" | head -30
if log_has_fatal "$LOG"; then
    echo "Python exception, crash or assertion found in $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
elif [ "$STATUS" -eq 1 ] && only_known_gamefeature_errors "$LOG"; then
    echo "exit 1 comes only from the known GameFeatureData configuration errors; treated as success"
    STATUS=0
fi
exit "$STATUS"
