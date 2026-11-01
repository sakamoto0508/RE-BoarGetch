#!/bin/sh
# Usage: run_ue_gui.sh <script.py> [extra args] -- full editor (real GPU, offscreen)
# The script must quit the editor itself when it sees -RetroQuit or -VerificationQuit.
# Engine: UE_ENGINE_DIR, otherwise resolved by FindEngine.ps1. Log: Saved/Verification/<script>_gui.log
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$ROOT/Saved/Verification
mkdir -p "$OUT"
ENGINE=$(powershell -NoProfile -ExecutionPolicy Bypass -File "$(cygpath -w "$HERE/FindEngine.ps1")") || exit 1
ENGINE=$(cygpath -u "$(printf '%s' "$ENGINE" | tr -d '\r')")
S=$1; shift
N=$(basename "$S" .py)
MSYS_NO_PATHCONV=1 "$ENGINE/Engine/Binaries/Win64/UnrealEditor.exe" "$(cygpath -m "$ROOT/REBoarGetch.uproject")" /Engine/Maps/Entry -ExecutePythonScript="$(cygpath -m "$S")" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound -RetroQuit -VerificationQuit -abslog="$(cygpath -m "$OUT/${N}_gui.log")" "$@" > /dev/null 2>&1
echo "exit $?"
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro|Critical error|Assertion failed" "$OUT/${N}_gui.log" | grep -v "Deprecation\|LogInit: Display" | cut -c40-300 | head -40
