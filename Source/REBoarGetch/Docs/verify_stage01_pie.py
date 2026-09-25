"""One-shot editor PIE observation. Does not save or edit gameplay assets."""
import json
import time
from pathlib import Path
import unreal

report_path = Path(__file__).with_suffix(".json")

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
if not world:
    raise RuntimeError("PIE must be running")
player = unreal.GameplayStatics.get_player_character(world, 0)
if not player:
    raise RuntimeError("No possessed player")
boars = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Character)
         if a.get_name().startswith("BP_NormalBoar_")]

def xyz(actor):
    p = actor.get_actor_location()
    return [p.x, p.y, p.z]

def command(value):
    unreal.SystemLibrary.execute_console_command(world, value)

report = {"player_start": xyz(player), "boars_start": {b.get_name(): xyz(b) for b in boars},
          "player_samples": [], "errors": []}
started = time.monotonic()
last_sample = -1.0
phase = 0
handle = None
command("Input.+key W 0.35")

def finish():
    command("Input.-key W")
    command("Input.-key SpaceBar")
    report["player_end"] = xyz(player)
    report["boars_end"] = {b.get_name(): xyz(b) for b in boars}
    report["jump_rise_cm"] = max([p[3] for p in report["player_samples"]] or [report["player_start"][2]]) - report["player_start"][2]
    report["nav_paths"] = {}
    for name, end in {"cage_approach": (120, -1080, 10), "bridge_east": (-1800, -1510, 35),
                      "bridge_west": (-3360, -1510, 35), "plateau": (2410, -1250, 270)}.items():
        try:
            nav = unreal.NavigationSystemV1.find_path_to_location_synchronously(
                world, unreal.Vector(0, 0, 10), unreal.Vector(*end), player)
            report["nav_paths"][name] = {"valid": bool(nav and nav.is_valid()),
                                         "partial": bool(nav and nav.is_partial()),
                                         "points": len(nav.path_points) if nav else 0}
        except Exception as exc:
            report["nav_paths"][name] = {"error": str(exc)}
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.log("[Stage01QualityPIE] " + json.dumps({k: v for k, v in report.items() if k != "player_samples"}))

def tick(delta):
    global phase, last_sample
    try:
        elapsed = time.monotonic() - started
        if elapsed - last_sample >= 0.04:
            report["player_samples"].append([elapsed] + xyz(player))
            last_sample = elapsed
        if phase == 0 and elapsed >= 0.8:
            command("Input.+key SpaceBar")
            phase = 1
        elif phase == 1 and elapsed >= 1.8:
            command("Input.-key SpaceBar")
            phase = 2
        elif phase == 2 and elapsed >= 2.8:
            command("Input.-key W")
            phase = 3
        if elapsed >= 10.0:
            unreal.unregister_slate_post_tick_callback(handle)
            finish()
    except Exception as exc:
        report["errors"].append(str(exc))
        unreal.unregister_slate_post_tick_callback(handle)
        command("Input.-key W")
        command("Input.-key SpaceBar")
        report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
        unreal.log_error("[Stage01QualityPIE] " + str(exc))

handle = unreal.register_slate_post_tick_callback(tick)
