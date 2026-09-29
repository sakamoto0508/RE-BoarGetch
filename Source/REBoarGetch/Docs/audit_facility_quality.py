import unreal,json
from pathlib import Path
EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
out={}
for level in ['L_GadgetLab','L_Archive']:
    assert LS.load_level('/Game/Level/'+level)
    rows=[]
    for a in EA.get_all_level_actors():
        p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d()
        row={'label':a.get_actor_label(),'class':a.get_class().get_name(),'p':[p.x,p.y,p.z],'r':[r.pitch,r.yaw,r.roll],'s':[s.x,s.y,s.z]}
        row['components']=[]
        for c in a.get_components_by_class(unreal.PrimitiveComponent):
            d={'class':c.get_class().get_name(),'collision':str(c.get_collision_enabled()),'profile':str(c.get_collision_profile_name())}
            if isinstance(c,unreal.StaticMeshComponent):
                m=c.get_editor_property('static_mesh');d['mesh']=m.get_path_name() if m else None;d['materials']=[m.get_path_name() if m else None for m in c.get_materials()]
            row['components'].append(d)
        if isinstance(a,unreal.HubPortal):row['action']=str(a.get_editor_property('action'));row['destination']=str(a.get_editor_property('destination'))
        rows.append(row)
    out[level]=rows
out['reusable']=unreal.EditorAssetLibrary.list_assets('/Game/Lobby/Meshes',True,False)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2,default=str),encoding='utf-8')
