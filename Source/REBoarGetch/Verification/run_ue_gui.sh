#!/bin/sh
# Usage: run_ue_gui.sh <script.py> [extra args] -- full editor (real GPU, offscreen); script must honour -RetroQuit
P=C:/UnrealGames/REBoarGetch
V=$P/Source/REBoarGetch/Verification
S=$1; shift
N=$(basename "$S" .py)
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "$P/REBoarGetch.uproject" /Engine/Maps/Entry -ExecutePythonScript="$S" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound -RetroQuit -abslog="$V/${N}_gui.log" "$@" > /dev/null 2>&1
echo "exit $?"
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro|Critical error|Assertion failed" "$V/${N}_gui.log" | grep -v "Deprecation\|LogInit: Display" | cut -c40-300 | head -40
