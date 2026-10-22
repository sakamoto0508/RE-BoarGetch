#!/bin/sh
# Usage: ue_cmd.sh <script.py> <logname> [extra args]  -- headless NullRHI python commandlet
export MSYS_NO_PATHCONV=1
P=C:/UnrealGames/REBoarGetch
S=$1; N=$2; shift 2
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$P/REBoarGetch.uproject" -run=pythonscript -script="$S" -unattended -nosplash -NullRHI -abslog="$P/SourceArt/Stage01Redesign/Logs/$N.log" "$@" > /dev/null 2>&1
echo "exit $?"
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[S01|Error: " "$P/SourceArt/Stage01Redesign/Logs/$N.log" | grep -v "Deprecation\|GameFeatureData" | cut -c30-400 | head -60
