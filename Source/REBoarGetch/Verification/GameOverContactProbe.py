"""Read-only: collision setup relevant to Boar -> Player contact damage. No saves."""
import unreal, json
out = {}
def comps(path):
    bp = unreal.load_asset(path)
    cd = unreal.get_default_object(bp.generated_class())
    sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    res = {}
    for h in sds.k2_gather_subobject_data_for_blueprint(bp):
        o = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h))
        if isinstance(o, unreal.PrimitiveComponent):
            res[o.get_name()] = {'profile': str(o.get_collision_profile_name()), 'type': str(o.get_collision_object_type()).split('.')[-1],
                                 'enabled': str(o.get_collision_enabled()).split('.')[-1], 'overlap_events': o.get_editor_property('generate_overlap_events'),
                                 'vs': {ch: str(o.get_collision_response_to_channel(getattr(unreal.CollisionChannel, ch))).split('.')[-1].split(':')[0]
                                        for ch in ['ECC_PAWN', 'ECC_CAMERA', 'ECC_VISIBILITY', 'ECC_WORLD_DYNAMIC', 'ECC_PHYSICS_BODY']}}
    return res
out['player'] = comps('/Game/BP/Player/BP_PlayerCharacter')
out['boar'] = comps('/Game/BP/Boar/BP_NormalBoar')
open(unreal.Paths.project_saved_dir() + 'GameOverContactProbe.json', 'w').write(json.dumps(out, indent=1))
unreal.log('[GOC] COMPLETE')
