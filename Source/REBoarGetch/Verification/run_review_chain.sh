#!/bin/sh
# PR #2 review verification chain. Every step is a separate editor process (= editor restart between steps).
P=C:/UnrealGames/REBoarGetch
V=$P/Source/REBoarGetch/Verification
S=$P/Plugins/RetroRender/Scripts
E="/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe"
gui() { "$E" "$P/REBoarGetch.uproject" -ExecutePythonScript="$1" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound -RetroQuit -abslog="$V/$2.log" > /dev/null 2>&1; echo "== $2 exit $?"; }
check() { $V/run_ue_py.sh $S/CheckRetroMaterials.py | grep "COMPLETE fails\|FAIL" | cut -c40-; }

echo "### setup + check"
$V/run_ue_gui.sh $S/SetupRetroAssets.py 2>&1 | grep -E "RetroSetup\] (COMPLETE|LowPoly|MPC)|Error" ; check
echo "### patch -> unpatch -> patch"
$V/run_ue_gui.sh $S/PatchRetroMaterials.py 2>&1 | grep -E "COMPLETE|Error|missing"; check
$V/run_ue_gui.sh $S/PatchRetroMaterials.py -RetroUnpatch 2>&1 | grep -E "COMPLETE|Error"
$V/run_ue_py.sh $S/CheckRetroMaterials.py | grep -c "FAIL"
$V/run_ue_gui.sh $S/PatchRetroMaterials.py 2>&1 | grep -E "COMPLETE|Error|patched /" | wc -l; check
echo "### review checks"
gui $V/RetroReviewChecks.py RetroReviewChecks; grep -E "RetroReview\] (PASS|FAIL|COMPLETE|ERROR)" $V/RetroReviewChecks.log | cut -c40-400
echo "### stage1 gameplay"
gui $V/RetroStage1Gameplay.py RetroStage1Gameplay; grep -E "RetroStage1\] (PASS|FAIL|COMPLETE|ERROR)" $V/RetroStage1Gameplay.log | cut -c40-400
grep -c "Failed to compile" $V/RetroStage1Gameplay.log
echo "### existing PIE smoke"
gui $V/PieSmoke.py PieSmoke_review; "/c/Program Files/Epic Games/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" -I -c "import json;d=json.load(open('$V/PieSmokeResults.json',encoding='utf-8'));print('passed',len(d.get('passed',[])),'error',d.get('error'))"
echo "### restart: panel + presets"
gui $V/RetroPanelCheck.py RetroPanelCheck_restart; grep -E "RetroPanel\]" $V/RetroPanelCheck_restart.log | cut -c40-600
"$E" "$P/REBoarGetch.uproject" -ExecutePythonScript="$V/RetroLookCapture.py" -unattended -nosplash -RenderOffscreen -Windowed -ResX=1920 -ResY=1080 -NoSound -RetroQuit -RetroCaptureSet=presets -abslog="$V/RetroLookCapture_restart.log" > /dev/null 2>&1
grep -E "RetroCapture\] (COMPLETE|ERROR)" $V/RetroLookCapture_restart.log | cut -c40-200; grep -h '"stamp"' $V/RetroLookCapture.json; grep -c "Failed to compile" $V/RetroLookCapture_restart.log
echo "### inventory"
$V/run_ue_py.sh $S/InventoryInportAssets.py | grep RetroInventory | cut -c40-
echo "### done"
