"""Compile startup-related Blueprints in memory; never save assets or progress."""
import hashlib
import json
from pathlib import Path
import unreal

here = Path(__file__).resolve().parent
project = Path(unreal.Paths.project_dir()).resolve()
save_dir = project / "Saved" / "SaveGames"


def save_hashes():
    return {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
            for p in save_dir.glob("*.sav")} if save_dir.exists() else {}


before = save_hashes()
report = {"blueprints": [], "errors": []}
paths = [
    "/Game/BP/Core/BP_BoarGameInstance",
    "/Game/BP/Player/BP_PlayerCharacter",
    "/Game/BP/Player/BP_BoarPlayerController",
    "/Game/BP/Lobby/BP_PC_Lobby",
    "/Game/BP/Lobby/BP_GM_GadgetLab",
    "/Game/BP/Lobby/BP_GM_Archive",
    "/Game/BP/Lobby/BP_GM_GadgetTest_Net",
    "/Game/BP/Boar/BP_NormalBoar",
    "/Game/BP/Gadget/BP_NetGadget",
    "/Game/BP/Cage/BP_Cage",
]
for path in paths:
    try:
        bp = unreal.load_asset(path)
        if not bp:
            raise RuntimeError("Missing Blueprint: " + path)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        status = str(bp.get_editor_property("status"))
        report["blueprints"].append({"path": path, "status": status})
        if "ERROR" in status.upper():
            raise RuntimeError("Blueprint compilation failed: " + path)
    except Exception as exc:
        report["errors"].append(str(exc))
report["save_files_unchanged"] = before == save_hashes()
if not report["save_files_unchanged"]:
    report["errors"].append("Save files changed")
(here / "InitializationCompatibility.json").write_text(
    json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
if report["errors"]:
    raise RuntimeError("; ".join(report["errors"]))
unreal.log("Initialization compatibility: all startup Blueprints compiled; saves unchanged")
