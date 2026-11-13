#!/usr/bin/env bash
# Action Camera Framework: runs an editor Python script headless (pythonscript commandlet, -NullRHI). Used to (re)create
# the test / sample assets (README 12 章, 22 章):
#
#   Scripts/RunEditorPython.sh Scripts/CreateVerificationAssets.py
#   Scripts/RunEditorPython.sh Scripts/CreateManualTestAssets.py
#
# --project FILE / --engine DIR as in RunTests.sh (defaults: the first .uproject above this plugin, $UE_ROOT or
# "C:/Program Files/Epic Games/UE_5.8"). The log goes to <project>/Saved/Logs/ACFPython_<script>.log; the script exits
# with 1 if the commandlet failed or the log has a Python error / traceback.

set -u
export MSYS_NO_PATHCONV=1
PROJECT=""
ENGINE="${UE_ROOT:-C:/Program Files/Epic Games/UE_5.8}"
SCRIPT=""
while [ $# -gt 0 ]; do
	case "$1" in
		--project) PROJECT="$2"; shift 2 ;;
		--engine) ENGINE="$2"; shift 2 ;;
		*) SCRIPT="$1"; shift ;;
	esac
done
if [ -z "$SCRIPT" ] || [ ! -f "$SCRIPT" ]; then
	echo "Usage: $0 [--project FILE] [--engine DIR] <script.py>" >&2
	exit 2
fi
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
if [ -z "$PROJECT" ]; then
	DIR="$SCRIPT_DIR"
	while [ "$DIR" != "/" ] && [ -z "$PROJECT" ]; do
		DIR="$(dirname "$DIR")"
		PROJECT="$(ls "$DIR"/*.uproject 2>/dev/null | head -1)"
	done
fi
[ -f "$PROJECT" ] || { echo "No .uproject found (use --project)." >&2; exit 2; }
SCRIPT="$(cd "$(dirname "$SCRIPT")" && pwd)/$(basename "$SCRIPT")"
LOG="$(dirname "$PROJECT")/Saved/Logs/ACFPython_$(basename "$SCRIPT" .py).log"
if command -v cygpath > /dev/null 2>&1; then
	PROJECT="$(cygpath -m "$PROJECT")"
	SCRIPT="$(cygpath -m "$SCRIPT")"
	LOG="$(cygpath -m "$LOG")"
fi
"$ENGINE/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$PROJECT" -run=pythonscript "-script=$SCRIPT" -unattended -nosplash -NullRHI "-abslog=$LOG" > /dev/null 2>&1
CODE=$?
ERRORS=$(grep -cE "LogPython: Error|Traceback" "$LOG" 2>/dev/null)
echo "exit $CODE, python errors ${ERRORS:-0}: $LOG"
[ "$CODE" -eq 0 ] && [ "${ERRORS:-0}" -eq 0 ]
