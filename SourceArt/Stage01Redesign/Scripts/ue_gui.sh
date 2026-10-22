#!/bin/sh
# Usage: ue_gui.sh <script.py> <logname> [extra args]  -- full editor, real GPU, offscreen
export MSYS_NO_PATHCONV=1
P=C:/UnrealGames/REBoarGetch
S=$1; N=$2; shift 2
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe" "$P/REBoarGetch.uproject" /Engine/Maps/Entry -ExecutePythonScript="$S" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1600 -ResY=900 -NoSound -abslog="$P/SourceArt/Stage01Redesign/Logs/$N.log" "$@" > /dev/null 2>&1
echo "exit $?"
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[S01|Critical error|Assertion failed" "$P/SourceArt/Stage01Redesign/Logs/$N.log" | grep -v Deprecation | cut -c30-400 | head -60
