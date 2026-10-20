#!/bin/sh
# Usage: run_ue_py.sh <script.py> [extra args]  -- headless NullRHI python commandlet
P=C:/UnrealGames/REBoarGetch
V=$P/Source/REBoarGetch/Verification
S=$1; shift
N=$(basename "$S" .py)
"/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" "$P/REBoarGetch.uproject" -run=pythonscript -script="$S" -unattended -nosplash -NullRHI -abslog="$V/$N.log" "$@" > /dev/null 2>&1
echo "exit $?"
grep -E "LogPython: (Error|Warning)|Traceback|COMPLETE|\[Retro" "$V/$N.log" | grep -v "Deprecation\|LogInit: Display" | head -30
