import unreal,json,traceback
from pathlib import Path
R=Path(__file__).parent;EA=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
baseline=json.loads((R/'audit_facility_quality.json').read_text(encoding='utf-8'));out={}
moved=set(json.loads((R/'brush_facility_quality.json').read_text())['moved'])
try:
    for level in ['L_Archive','L_GadgetLab']:
        assert LS.load_level('/Game/Level/'+level)
        aa={a.get_actor_label():a for a in EA.get_all_level_actors()};issues=[]
        aa['FacilitySun'].light_component.set_editor_property('forward_shading_priority',1)
        for row in baseline[level]:
            n=row['label'];assert n in aa,'Missing existing actor '+n
            a=aa[n];p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d();expected=list(row['p'])
            if level=='L_GadgetLab' and n in moved:expected[1]+=590
            if any(abs(x-y)>.01 for x,y in zip([p.x,p.y,p.z],expected)):issues.append(n+' location')
            if any(abs(x-y)>.01 for x,y in zip([s.x,s.y,s.z],row['s'])):issues.append(n+' scale')
            for c,d in zip(a.get_components_by_class(unreal.PrimitiveComponent),row['components']):
                if str(c.get_collision_enabled())!=d['collision'] or str(c.get_collision_profile_name())!=d['profile']:issues.append(n+' collision')
            if isinstance(a,unreal.HubPortal):
                assert str(a.get_editor_property('action'))==row['action']
                dest=a.get_editor_property('destination')
                if row['destination']=='None':assert dest is None
                else:assert dest.get_path_name()==row['destination'].split("'")[1]
        assert not issues,str(issues)
        assert LS.save_current_level()
        out[level]={'existing_actors_preserved':len(baseline[level]),'unexpected_transform_or_collision_changes':issues,'saved':True}
    out['dirty_maps']=[str(p) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    out['PIE']='Not run'
except Exception:out['error']=traceback.format_exc()
(R/'verify_facility_quality.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
