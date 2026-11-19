"""Dump every actor of L_Stage02 (headless, saves nothing) to Logs/level_dump.json."""
import json, os, unreal
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage02')
eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
out = []
for a in eas.get_all_level_actors():
    l, r, s = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
    d = {'label': a.get_actor_label(), 'cls': a.get_class().get_name(), 'name': a.get_name(),
         'loc': [round(l.x, 1), round(l.y, 1), round(l.z, 1)], 'rot': [round(r.roll, 1), round(r.pitch, 1), round(r.yaw, 1)],
         'scale': [round(s.x, 3), round(s.y, 3), round(s.z, 3)], 'folder': str(a.get_folder_path()), 'tags': [str(t) for t in a.tags]}
    if isinstance(a, unreal.StaticMeshActor) and a.static_mesh_component.static_mesh:
        d['mesh'] = a.static_mesh_component.static_mesh.get_path_name().split('.')[0]
    out.append(d)
json.dump(out, open(os.path.join(ROOT, 'Logs', 'level_dump.json'), 'w'), indent=0)
unreal.log_warning('[S02Dump] %d actors' % len(out))
