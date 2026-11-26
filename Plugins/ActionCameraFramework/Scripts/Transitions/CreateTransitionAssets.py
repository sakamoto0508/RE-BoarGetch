"""Action Camera Framework, Phase 7D (README 33 章): creates the plugin content of the transition system.

    Scripts/RunEditorPython.sh Scripts/Transitions/CreateTransitionAssets.py [--only material|material-code|presets|maps] [--min-id N]

  /ActionCameraFramework/Transitions/M_ACFTransition          UI material; its Custom node is ACFTransitionEffects.hlsl
  /ActionCameraFramework/Transitions/Presets/DA_ACFTransition_<ID>_<Name>   standard presets (catalog defaults)
  /ActionCameraFramework/Transitions/Samples/L_ACFTransitionA, _B            two test maps + MI_ACFTransitionSample_*

Re-running replaces the assets. Results: <Project>/Saved/ACFTransitionAssets.json.
NOTE: Python unreal.Rotator positional order is (roll, pitch, yaw) - always use keywords.
"""
import json
import os
import sys
import traceback

import unreal

ROOT = '/ActionCameraFramework/Transitions'
MATERIAL = ROOT + '/M_ACFTransition'
PRESETS = ROOT + '/Presets'
SAMPLES = ROOT + '/Samples'
HLSL = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ACFTransitionEffects.hlsl')
OUT = {}
E = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()

SCALARS = [  # (parameter, custom input, default)
    ('Effect', 'Effect', 1.0), ('Progress', 'T', 0.0), ('Opening', 'Opening', 0.0), ('Intensity', 'Intensity', 1.0),
    ('Direction', 'Direction', 0.0), ('Seed', 'Seed', 0.0), ('Aspect', 'Aspect', 1.7778), ('Time', 'Time', 0.0),
    ('Softness', 'Softness', 0.0), ('Count', 'Count', 1.0), ('Reverse', 'Reverse', 1.0), ('UseSnapshot', 'UseSnap', 0.0),
    ('UseOverlay', 'UseOver', 0.0), ('Origin', 'Origin', 0.0), ('SnapshotEncoded', 'SnapEnc', 0.0),
]
VECTORS = [('Color', 'Col', (0, 0, 0, 1)), ('Color2', 'Col2', (1, 1, 1, 1)), ('Center', 'Center', (0.5, 0.5, 0, 0))]
TEXTURES = [('Snapshot', 'SnapTex', '/Engine/EngineResources/Black.Black'),
            ('OverlayTexture', 'OverTex', '/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture')]


def ensure(name, folder, cls, factory):
    """The existing asset (re-runs edit it in place: delete + create fails while it is referenced) or a new one."""
    path = folder + '/' + name
    if E.does_asset_exist(path):
        return unreal.load_asset(path)
    return TOOLS.create_asset(name, folder, cls, factory)


def build_material():
    with open(HLSL, 'r', encoding='utf-8') as f:
        code = f.read()
    mat = ensure('M_ACFTransition', ROOT, unreal.Material, unreal.MaterialFactoryNew())
    MEL.delete_all_material_expressions(mat)
    mat.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)

    custom = MEL.create_material_expression(mat, unreal.MaterialExpressionCustom, -300, 0)
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    custom.set_editor_property('description', 'ACF Transition Effects (Scripts/Transitions/ACFTransitionEffects.hlsl)')
    names = ['UV'] + [s[1] for s in SCALARS] + [v[1] for v in VECTORS] + [t[1] for t in TEXTURES]
    inputs = []
    for n in names:
        ci = unreal.CustomInput()
        ci.set_editor_property('input_name', n)
        inputs.append(ci)
    custom.set_editor_property('inputs', inputs)

    y = -800
    uv = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureCoordinate, -700, y)
    MEL.connect_material_expressions(uv, '', custom, 'UV')
    for param, inp, default in SCALARS:
        y += 70
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, y)
        e.set_editor_property('parameter_name', param)
        e.set_editor_property('default_value', default)
        MEL.connect_material_expressions(e, '', custom, inp)
    for param, inp, default in VECTORS:
        y += 120
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, y)
        e.set_editor_property('parameter_name', param)
        e.set_editor_property('default_value', unreal.LinearColor(*default))
        MEL.connect_material_expressions(e, '', custom, inp)
    for param, inp, default in TEXTURES:
        y += 150
        e = MEL.create_material_expression(mat, unreal.MaterialExpressionTextureObjectParameter, -700, y)
        e.set_editor_property('parameter_name', param)
        e.set_editor_property('texture', unreal.load_asset(default))
        e.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        MEL.connect_material_expressions(e, '', custom, inp)

    rgb = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 0, -60)
    rgb.set_editor_property('r', True); rgb.set_editor_property('g', True); rgb.set_editor_property('b', True); rgb.set_editor_property('a', False)
    alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionComponentMask, 0, 60)
    alpha.set_editor_property('r', False); alpha.set_editor_property('g', False); alpha.set_editor_property('b', False); alpha.set_editor_property('a', True)
    MEL.connect_material_expressions(custom, '', rgb, '')
    MEL.connect_material_expressions(custom, '', alpha, '')
    MEL.connect_material_property(rgb, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(alpha, '', unreal.MaterialProperty.MP_OPACITY)
    MEL.recompile_material(mat)
    OUT['material_saved'] = E.save_loaded_asset(mat, False)
    OUT['material_code_chars'] = len(code)


def update_material_code():
    """Refresh the supplied shader in place, keeping the material graph and its connections."""
    mat = unreal.load_asset(MATERIAL)
    if mat is None:
        raise RuntimeError('Transition material not found. Use --only material to create it.')
    rgb = MEL.get_material_property_input_node(mat, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    inputs = MEL.get_inputs_for_material_expression(mat, rgb) if rgb is not None else []
    custom_nodes = [node for node in inputs if isinstance(node, unreal.MaterialExpressionCustom)]
    if len(custom_nodes) != 1:
        raise RuntimeError('Expected one transition Custom node upstream of Emissive Color.')
    with open(HLSL, 'r', encoding='utf-8') as shader:
        code = shader.read()
    custom_nodes[0].set_editor_property('code', code)
    MEL.recompile_material(mat)
    OUT['material_saved'] = E.save_loaded_asset(mat, False)
    if not OUT['material_saved']:
        raise RuntimeError('Failed to save the transition material.')
    OUT['material_code_chars'] = len(code)


def build_presets(min_id):
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.ACFTransitionPreset)
    made = []
    for info in unreal.ACFTransitionLibrary.get_all_transition_effects():
        effect = info.get_editor_property('effect')
        ident = info.get_editor_property('id')
        if ident < min_id:
            continue
        clean = ''.join(ch for ch in info.get_editor_property('name') if ch.isalnum())
        name = 'DA_ACFTransition_%02d_%s' % (ident, clean)
        preset = ensure(name, PRESETS, unreal.ACFTransitionPreset, factory)
        preset.set_editor_property('close', unreal.ACFTransitionLibrary.make_default_transition_settings(effect, unreal.ACFTransitionPhase.CLOSE))
        preset.set_editor_property('open', unreal.ACFTransitionLibrary.make_default_transition_settings(effect, unreal.ACFTransitionPhase.OPEN))
        if not E.save_loaded_asset(preset, False):
            raise RuntimeError('Failed to save ' + name)
        made.append(name)
    OUT['presets'] = len(made)


def color_mi(name, color):
    path = SAMPLES + '/' + name
    mi = ensure(name, SAMPLES, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mi.set_editor_property('parent', unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial'))
    MEL.set_material_instance_vector_parameter_value(mi, 'Color', unreal.LinearColor(*color))
    E.save_loaded_asset(mi, False)
    return mi


def spawn_mesh(sub, label, mesh, location, scale, material, yaw=0.0):
    a = sub.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location), unreal.Rotator(roll=0.0, pitch=0.0, yaw=yaw))
    a.static_mesh_component.set_static_mesh(unreal.load_asset(mesh))
    a.static_mesh_component.set_material(0, material)
    a.set_actor_scale3d(unreal.Vector(*scale))
    a.set_actor_label(label)
    return a


def build_map(letter, other, floor_color, colors, text_color):
    path = SAMPLES + '/L_ACFTransition' + letter
    disk = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_plugins_dir() + 'ActionCameraFramework/Content/Transitions/Samples/L_ACFTransition%s.umap' % letter)
    if os.path.exists(disk):
        os.remove(disk)  # EditorAssetLibrary.delete_asset on maps fails silently in commandlets
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    OUT['new_level_' + letter] = level.new_level(path)
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    floor = color_mi('MI_ACFTransitionSample_Floor%s' % letter, floor_color)
    mis = [color_mi('MI_ACFTransitionSample_%s%d' % (letter, i), c) for i, c in enumerate(colors)]
    spawn_mesh(sub, 'Floor', '/Engine/BasicShapes/Cube.Cube', (0, 0, -10), (60, 60, 0.2), floor)
    ps = sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-600, 0, 120), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    ps.set_actor_label('PlayerStart')
    shapes = ['/Engine/BasicShapes/Cube.Cube', '/Engine/BasicShapes/Cylinder.Cylinder', '/Engine/BasicShapes/Sphere.Sphere', '/Engine/BasicShapes/Cone.Cone']
    # A ring of coloured shapes in front of the player start (A and B differ in shape order, colours and layout).
    for i in range(12):
        x = 500 + (i % 4) * 260 + (60 if letter == 'B' else 0)
        y = -480 + (i // 4) * 480 + ((i % 2) * 120 if letter == 'B' else 0)
        h = 1.0 + ((i * 7) % 5) * 0.6
        shape = shapes[(i + (1 if letter == 'B' else 0)) % 4]
        spawn_mesh(sub, 'Shape_%02d' % i, shape, (x, y, h * 50), (1.4, 1.4, h), mis[i % len(mis)], yaw=i * 15.0)
    t = sub.spawn_actor_from_class(unreal.TextRenderActor, unreal.Vector(1400, 0, 520), unreal.Rotator(roll=0.0, pitch=0.0, yaw=180.0))
    comp = t.text_render
    comp.set_editor_property('text', unreal.Text('LEVEL ' + letter))
    comp.set_editor_property('world_size', 220.0)
    comp.set_editor_property('horizontal_alignment', unreal.HorizTextAligment.EHTA_CENTER)
    comp.set_editor_property('text_render_color', unreal.Color(*text_color))
    t.set_actor_label('Label_Level' + letter)

    director = sub.spawn_actor_from_class(unreal.ACFTransitionSampleDirector, unreal.Vector(-600, 0, 300), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    director.set_actor_label('TransitionSampleDirector')
    director.set_editor_property('next_level', SAMPLES + '/L_ACFTransition' + other)

    light = sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000), unreal.Rotator(roll=0.0, pitch=-45.0, yaw=(30.0 if letter == 'A' else 140.0)))
    light.set_actor_label('Sun')
    lc = light.get_component_by_class(unreal.DirectionalLightComponent)
    lc.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    lc.set_editor_property('atmosphere_sun_light', True)
    sky = sub.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 800), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    sky.set_actor_label('SkyLight')
    sky.light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    sky.light_component.set_editor_property('real_time_capture', True)
    atmo = sub.spawn_actor_from_class(unreal.SkyAtmosphere, unreal.Vector(0, 0, 0), unreal.Rotator(roll=0.0, pitch=0.0, yaw=0.0))
    atmo.set_actor_label('SkyAtmosphere')

    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    ws = world.get_world_settings()
    ws.set_editor_property('default_game_mode', unreal.GameModeBase)
    OUT['saved_' + letter] = level.save_current_level()
    OUT['actors_' + letter] = len(sub.get_all_level_actors())


def main():
    # Plugin content is not in the asset registry of a pythonscript commandlet until it is scanned.
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(['/ActionCameraFramework'], True)
    only = set()
    min_id = 1
    for i, a in enumerate(sys.argv):
        if a == '--only' and i + 1 < len(sys.argv):
            only.add(sys.argv[i + 1])
        elif a == '--min-id' and i + 1 < len(sys.argv):
            min_id = int(sys.argv[i + 1])
    if 'material-code' in only:
        update_material_code()
    if not only or 'material' in only:
        build_material()
    if not only or 'presets' in only:
        build_presets(min_id)
    if not only or 'maps' in only:
        build_map('A', 'B', (0.32, 0.30, 0.27, 1), [(0.9, 0.35, 0.05, 1), (0.05, 0.3, 0.9, 1), (0.95, 0.85, 0.1, 1)], (255, 140, 40, 255))
        build_map('B', 'A', (0.18, 0.24, 0.2, 1), [(0.1, 0.8, 0.3, 1), (0.6, 0.1, 0.8, 1), (0.9, 0.9, 0.9, 1)], (90, 255, 140, 255))


try:
    main()
    OUT['ok'] = True
except Exception:
    OUT['error'] = traceback.format_exc()
    unreal.log_error(OUT['error'])
with open(os.path.join(unreal.Paths.project_saved_dir(), 'ACFTransitionAssets.json'), 'w') as f:
    json.dump(OUT, f, indent=1, default=str)
