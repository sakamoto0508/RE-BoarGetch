#!/bin/sh
# Usage: run_ue_py.sh <script.py> [extra args]  -- headless NullRHI python commandlet
# Engine: UE_ENGINE_DIR, otherwise resolved by FindEngine.ps1. Log: Saved/Verification/<script>.log
# Exit status: UE's exit code, or 1 when the log shows a Python error. An exit code of 1 caused only by
# the known GameFeatureData configuration errors counts as success (same rule as VerifyPhase.ps1).
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/Saved/Verification
mkdir -p "$OUT"
ENGINE=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/FindEngine.ps1")") || exit 1
ENGINE=$(cygpath -u "$(printf '%s' "$ENGINE" | tr -d '\r')")
S=$1; shift
N=$(basename "$S" .py)
LOG=$OUT/$N.log
"$ENGINE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$(cygpath -m "$ROOT/REBoarGetch.uproject")" -run=pythonscript -script="$(cygpath -m "$S")" -unattended -nosplash -NullRHI -abslog="$(cygpath -m "$LOG")" "$@" > /dev/null 2>&1
STATUS=$?
echo "exit $STATUS"
if [ ! -f "$LOG" ]; then
    echo "log not found: $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
    exit "$STATUS"
fi
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro" "$LOG" | grep -v "Deprecation\|LogInit: Display" | head -30
if grep -q -E "Traceback \(most recent call last\)|LogPython: Error" "$LOG"; then
    echo "Python error found in $LOG"
    [ "$STATUS" -eq 0 ] && STATUS=1
elif [ "$STATUS" -eq 1 ] && grep -q "Warning/Error Summary (Unique only)" "$LOG" &&
    ! sed -n '/Warning\/Error Summary (Unique only)/,$p' "$LOG" | grep ": Error:" | grep -v -q "GameFeatureData"; then
    echo "exit 1 comes only from the known GameFeatureData configuration errors; treated as success"
    STATUS=0
fi
exit "$STATUS"
