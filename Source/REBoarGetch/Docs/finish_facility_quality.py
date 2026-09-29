import unreal,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'build_facility_levels.py').read_text(encoding='utf-8').split('def run():')[0])
out={}
try:
    path='/Game/Lobby/Materials/M_Facility_ExhibitHologram'
    assert not AS.does_asset_exist(path)
    m=AT.create_asset('M_Facility_ExhibitHologram','/Game/Lobby/Materials',unreal.Material,unreal.MaterialFactoryNew())
    m.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT);m.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT);m.set_editor_property('two_sided',True)
    ml=unreal.MaterialEditingLibrary
    c=ml.create_material_expression(m,unreal.MaterialExpressionConstant3Vector);c.set_editor_property('constant',unreal.LinearColor(.02,.72,1.1,1));ml.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    a=ml.create_material_expression(m,unreal.MaterialExpressionScalarParameter);a.set_editor_property('parameter_name','Opacity');a.set_editor_property('default_value',.62);ml.connect_material_property(a,'',unreal.MaterialProperty.MP_OPACITY)
    ml.recompile_material(m);assert AS.save_loaded_asset(m)
    for level in ['L_GadgetLab','L_Archive']:
        assert LS.load_level('/Game/Level/'+level)
        aa=actors()
        aa['Quality_Holo'].static_mesh_component.set_material(0,m)
        for i,yaw in enumerate([140,-80]):
            light=EA.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,650),unreal.Rotator(pitch=-48,yaw=yaw));light.set_actor_label('Quality_SoftFill'+str(i));light.set_folder_path('Facility/Quality')
            c=light.light_component;c.set_editor_property('intensity',1.6);c.set_editor_property('cast_shadows',False);c.set_editor_property('atmosphere_sun_light',False)
        if level=='L_Archive':mesh('Quality_ResearchScreenFace',(225,635,345),(790,3,180),'Panel')
        assert LS.save_current_level()
        out[level]={'saved':True,'actors':len(actors())}
    out['material']=path
except Exception:out['error']=traceback.format_exc()
(ROOT/'finish_facility_quality.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
