"""Run in UE5.8 Editor: py import runpy; runpy.run_path(r'.../Rendering/SetupRetroAssets.py').
Creates new assets only. Existing assets are never overwritten. No existing level saves.
"""
from pathlib import Path
import unreal

ROOT = '/Game/Art/Rendering/Retro'
HERE = Path(__file__).resolve().parent
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
lib.make_directory(ROOT)

material_path = ROOT + '/M_RetroCompare'
rebuild = '-RetroRebuildMaterial' in unreal.SystemLibrary.get_command_line()
if not lib.does_asset_exist(material_path) or rebuild:
    material = unreal.load_asset(material_path) if lib.does_asset_exist(material_path) else tools.create_asset('M_RetroCompare', ROOT, unreal.Material, unreal.MaterialFactoryNew())
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property('blendable_priority', 100)
    edit = unreal.MaterialEditingLibrary
    custom = edit.create_material_expression(material, unreal.MaterialExpressionCustom, 100, 0)
    custom.set_editor_property('code', (HERE / 'RetroCompare.hlsl').read_text(encoding='utf-8'))
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    # Explicit SceneTexture expression makes the compiler track PostProcessInput0 usage.
    scene = edit.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -500, -200)
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    defaults = unreal.RetroRenderParameters()
    fields = [
        ('Pixelation', 'pixelation'), ('TargetVerticalResolution', 'target_vertical_resolution'),
        ('ColorQuantization', 'color_quantization'), ('DitheringStrength', 'dithering_strength'),
        ('Saturation', 'saturation'), ('Contrast', 'contrast'), ('Brightness', 'brightness'),
        ('ScanlineStrength', 'scanline_strength'), ('VignetteStrength', 'vignette_strength'),
        ('ChromaticAberration', 'chromatic_aberration'), ('CRTCurvature', 'crt_curvature'),
        ('SoftSampling', 'soft_sampling'), ('PhosphorMask', 'phosphor_mask')]
    names = ['SceneDependency'] + [x[0] for x in fields]
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    assert edit.connect_material_expressions(scene, 'Color', custom, 'SceneDependency')
    for i, (name, attr) in enumerate(fields):
        param = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, i*100)
        param.set_editor_property('parameter_name', name)
        param.set_editor_property('default_value', defaults.get_editor_property(attr))
        assert edit.connect_material_expressions(param, '', custom, name)
    assert edit.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = edit.recompile_material(material)
    if errors:
        raise RuntimeError('Retro material compilation failed: ' + str(errors))
    lib.save_loaded_asset(material)
else:
    unreal.log('[Retro] Existing material preserved: ' + material_path)

if not lib.does_asset_exist(ROOT+'/DA_RetroPresets'):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.RetroRenderPresetData)
    data = tools.create_asset('DA_RetroPresets',ROOT,unreal.RetroRenderPresetData,factory)
    assert data and len(data.get_editor_property('presets')) == 5
    lib.save_loaded_asset(data)

# New isolated map copied from Stage1; never save the source level.
test_map = ROOT+'/L_RetroCompare'
if not lib.does_asset_exist(test_map):
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).new_level_from_template(test_map, '/Game/Level/Stage1')
    actor = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).spawn_actor_from_class(
        unreal.RetroCompareActor, unreal.Vector(0, 0, 0))
    actor.set_actor_label('Retro Compare - Ctrl Alt F7')
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
unreal.log('[Retro] Assets generated. Open '+test_map+' and Play. Ctrl+Alt+F7 opens controls.')
