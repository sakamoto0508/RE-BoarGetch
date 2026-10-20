"""Read-only audit for the retro rendering rework (Phase 1).
UnrealEditor-Cmd REBoarGetch.uproject -run=pythonscript -script=<this> -unattended -nosplash -NullRHI
Writes Verification/RetroAudit.json. Saves nothing.
"""
from collections import Counter, defaultdict
from pathlib import Path
import json
import unreal

HERE = Path(__file__).resolve().parent
LEVELS = ['/Game/Art/Rendering/Retro/L_RetroCompare', '/Game/Level/Stage1']
mel = unreal.MaterialEditingLibrary
out = {'cvars': {}, 'levels': {}, 'base_materials': {}, 'textures': {}, 'meshes': {}}

for name in ['r.AntiAliasingMethod', 'r.ScreenPercentage', 'r.DynamicGlobalIlluminationMethod',
             'r.ReflectionMethod', 'r.Shadow.Virtual.Enable', 'r.Nanite', 'r.MotionBlurQuality',
             'r.BloomQuality', 'r.DefaultFeature.AutoExposure', 'r.DefaultFeature.MotionBlur',
             'r.DefaultFeature.Bloom', 'r.Tonemapper.Sharpen', 'r.Upscale.Quality']:
    try:
        out['cvars'][name] = unreal.SystemLibrary.get_console_variable_float_value(name)
    except Exception as e:
        out['cvars'][name] = str(e)

base_users = Counter()
texture_users = Counter()

def base_of(mi):
    try:
        return mi.get_base_material()
    except Exception:
        return None

def describe_base(base):
    path = base.get_path_name()
    if path in out['base_materials']:
        return
    info = {}
    for prop in ['blend_mode', 'material_domain', 'two_sided', 'used_with_skeletal_mesh',
                 'used_with_nanite', 'used_with_instanced_static_meshes']:
        try:
            info[prop] = str(base.get_editor_property(prop))
        except Exception:
            pass
    try:
        node = mel.get_material_property_input_node(base, unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        info['wpo_connected'] = node is not None
    except Exception as e:
        info['wpo_connected'] = str(e)
    try:
        info['expression_count'] = mel.get_num_material_expressions(base)
    except Exception:
        pass
    try:
        info['texture_params'] = [str(n) for n in mel.get_texture_parameter_names(base)]
    except Exception:
        pass
    try:
        textures = mel.get_used_textures(base)
    except Exception:
        textures = []
    info['textures'] = [t.get_path_name() for t in textures]
    info['package_on_disk'] = base.get_path_name().split('.')[0]
    for t in textures:
        texture_users[t.get_path_name()] += 1
        describe_texture(t)
    out['base_materials'][path] = info

def describe_texture(t):
    path = t.get_path_name()
    if path in out['textures']:
        return
    info = {}
    for prop in ['filter', 'lod_group', 'max_texture_size', 'mip_gen_settings', 'compression_settings', 's_rgb', 'never_stream']:
        try:
            info[prop] = str(t.get_editor_property(prop))
        except Exception:
            pass
    try:
        info['size'] = [t.blueprint_get_size_x(), t.blueprint_get_size_y()]
    except Exception:
        pass
    out['textures'][path] = info

def describe_mesh(mesh):
    path = mesh.get_path_name()
    if path in out['meshes']:
        return
    info = {'class': mesh.get_class().get_name()}
    try:
        info['nanite'] = bool(mesh.get_editor_property('nanite_settings').get_editor_property('enabled'))
    except Exception:
        pass
    try:
        info['tris_lod0'] = mesh.get_num_triangles(0)
    except Exception:
        pass
    try:
        info['lods'] = mesh.get_num_lods()
    except Exception:
        pass
    out['meshes'][path] = info

level_editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actor_sys = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for level in LEVELS:
    if not level_editor.load_level(level):
        out['levels'][level] = 'load failed'
        continue
    actors = actor_sys.get_all_level_actors()
    linfo = {'actor_classes': Counter(), 'post_process_volumes': [], 'sky': [], 'static_mesh_components': 0,
             'skeletal': [], 'mesh_usage': Counter(), 'base_material_usage': Counter()}
    for a in actors:
        cls = a.get_class().get_name()
        linfo['actor_classes'][cls] += 1
        if isinstance(a, unreal.PostProcessVolume):
            s = a.get_editor_property('settings')
            ov = {}
            for p in ['override_bloom_intensity', 'bloom_intensity', 'override_auto_exposure_method', 'auto_exposure_method',
                      'override_motion_blur_amount', 'motion_blur_amount', 'override_auto_exposure_bias', 'auto_exposure_bias',
                      'override_color_saturation', 'override_scene_fringe_intensity', 'scene_fringe_intensity',
                      'override_vignette_intensity', 'vignette_intensity', 'override_dynamic_global_illumination_method',
                      'override_film_grain_intensity', 'film_grain_intensity', 'override_local_exposure_method']:
                try:
                    ov[p] = str(s.get_editor_property(p))
                except Exception:
                    pass
            try:
                wb = s.get_editor_property('weighted_blendables').get_editor_property('array')
                ov['blendables'] = [str(b.get_editor_property('object')) for b in wb]
            except Exception:
                pass
            linfo['post_process_volumes'].append({'name': a.get_actor_label(),
                'unbound': a.get_editor_property('unbound'), 'blend_weight': a.get_editor_property('blend_weight'),
                'priority': a.get_editor_property('priority'), 'settings': ov})
        if cls in ('SkyAtmosphere', 'VolumetricCloud', 'ExponentialHeightFog', 'DirectionalLight', 'SkyLight', 'Landscape', 'BP_Sky_Sphere_C', 'SkyDome'):
            linfo['sky'].append(cls + ':' + a.get_actor_label())
        for comp in a.get_components_by_class(unreal.StaticMeshComponent):
            mesh = comp.get_editor_property('static_mesh')
            if not mesh:
                continue
            linfo['static_mesh_components'] += 1
            linfo['mesh_usage'][mesh.get_path_name()] += 1
            describe_mesh(mesh)
            for m in comp.get_materials():
                b = base_of(m) if m else None
                if b:
                    linfo['base_material_usage'][b.get_path_name()] += 1
                    describe_base(b)
        for comp in a.get_components_by_class(unreal.SkeletalMeshComponent):
            mesh = comp.get_editor_property('skeletal_mesh_asset') if hasattr(comp, 'skeletal_mesh_asset') else None
            try:
                mesh = comp.get_skeletal_mesh_asset()
            except Exception:
                pass
            mats = []
            for m in comp.get_materials():
                b = base_of(m) if m else None
                if b:
                    mats.append(b.get_path_name())
                    linfo['base_material_usage'][b.get_path_name()] += 1
                    describe_base(b)
            linfo['skeletal'].append({'actor': a.get_actor_label(), 'class': cls,
                                      'mesh': mesh.get_path_name() if mesh else None, 'bases': mats})
    for k in ['actor_classes', 'mesh_usage', 'base_material_usage']:
        linfo[k] = dict(linfo[k].most_common())
    out['levels'][level] = linfo

out['texture_users'] = dict(texture_users.most_common())
(HERE / 'RetroAudit.json').write_text(json.dumps(out, ensure_ascii=False, indent=1, default=str), encoding='utf-8')
unreal.log('[RetroAudit] COMPLETE')
