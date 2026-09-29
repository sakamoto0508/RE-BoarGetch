import unreal
from pathlib import Path
exec(Path(__file__).with_name('build_lobby_visual.py').read_text(encoding='utf-8').split('try:build()')[0])
path='/Game/Lobby/Materials/M_Facility_Sky'
m=unreal.load_asset(path)
if not m:
    m=create(path,unreal.Material,unreal.MaterialFactoryNew());m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);m.set_editor_property('two_sided',True)
    output(node(m,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(.14,.38,.68,1)),unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    unreal.MaterialEditingLibrary.recompile_material(m);save(m)
for level in ['/Game/Level/L_GadgetLab','/Game/Level/L_Archive']:
    LS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);LS.load_level(level)
    a=next((a for a in EA.get_all_level_actors() if a.get_actor_label()=='FacilitySkyDome'),None)
    if not a:
        a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(0,0,0));a.set_actor_label('FacilitySkyDome');a.set_actor_scale3d(unreal.Vector(300,300,300))
        c=a.static_mesh_component;c.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Sphere'));c.set_material(0,m);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('cast_shadow',False);c.set_editor_property('can_ever_affect_navigation',False)
    assert LS.save_current_level()
