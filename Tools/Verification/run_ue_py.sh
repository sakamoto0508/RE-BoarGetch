#!/bin/sh
# Usage: run_ue_py.sh <script.py> [extra args]  -- headless NullRHI python commandlet
# Engine: UE_ENGINE_DIR, otherwise resolved by FindEngine.ps1. Log: Saved/Verification/<script>.log
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/Saved/Verification
mkdir -p "$OUT"
ENGINE=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/FindEngine.ps1")") || exit 1
ENGINE=$(cygpath -u "$(printf '%s' "$ENGINE" | tr -d '\r')")
S=$1; shift
N=$(basename "$S" .py)
"$ENGINE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$(cygpath -m "$ROOT/REBoarGetch.uproject")" -run=pythonscript -script="$(cygpath -m "$S")" -unattended -nosplash -NullRHI -abslog="$(cygpath -m "$OUT/$N.log")" "$@" > /dev/null 2>&1
echo "exit $?"
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro" "$OUT/$N.log" | grep -v "Deprecation\|LogInit: Display" | head -30
