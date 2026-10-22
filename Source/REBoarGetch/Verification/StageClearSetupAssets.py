"""Stage clear presentation assets (idempotent): star texture/material, Victory anim import + montage.
Run: run_ue_py.sh StageClearSetupAssets.py   -> Saved/StageClearSetupAssets.json"""
import unreal, json, traceback
OUT = {}
E = unreal.EditorAssetLibrary
M = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
SRC = 'C:/UnrealGames/REBoarGetch/SourceArt/StageClear/'
VFX = '/Game/REBoarGetch/Art/VFX/StageClear'
ANIM = '/Game/Animation/Player'  # tracked folder (Content/InportAssets is git-ignored)
SKEL = '/Game/InportAssets/Player/GreatSwordSlash_UE_Skeleton'


def import_texture():
    path = VFX + '/T_StageClearStar'
    t = unreal.AssetImportTask()
    t.filename = SRC + 'Textures/T_StageClearStar.png'
    t.destination_path = VFX
    t.destination_name = 'T_StageClearStar'
    t.automated = True
    t.replace_existing = True
    t.save = False
    tools.import_asset_tasks([t])
    tex = unreal.load_asset(path)
    tex.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('srgb', True)
    tex.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_EFFECTS)
    E.save_loaded_asset(tex)
    return tex


def make_star_material(tex):
    path = VFX + '/M_StageClearStar'
    mat = unreal.load_asset(path)
    if mat:
        M.delete_all_material_expressions(mat)
    else:
        mat = tools.create_asset('M_StageClearStar', VFX, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    mat.set_editor_property('two_sided', True)
    M.set_material_usage(mat, unreal.MaterialUsage.MATUSAGE_NIAGARA_SPRITES)
    sample = M.create_material_expression(mat, unreal.MaterialExpressionTextureSample, -700, 0)
    sample.set_editor_property('texture', tex)
    pc = M.create_material_expression(mat, unreal.MaterialExpressionParticleColor, -700, 300)
    strength = M.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 500)
    strength.set_editor_property('parameter_name', 'EmissiveStrength')
    strength.set_editor_property('default_value', 4.0)
    col = M.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 50)
    M.connect_material_expressions(sample, 'RGB', col, 'A')
    M.connect_material_expressions(pc, 'RGB', col, 'B')
    emi = M.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 50)
    M.connect_material_expressions(col, '', emi, 'A')
    M.connect_material_expressions(strength, '', emi, 'B')
    M.connect_material_property(emi, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    op = M.create_material_expression(mat, unreal.MaterialExpressionMultiply, -400, 300)
    M.connect_material_expressions(sample, 'A', op, 'A')
    M.connect_material_expressions(pc, 'A', op, 'B')
    M.connect_material_property(op, '', unreal.MaterialProperty.MP_OPACITY)
    M.recompile_material(mat)
    E.save_loaded_asset(mat)
    return mat


def import_anim():
    # UE5.8 routes FBX through Interchange by default; the legacy path honours FbxImportUI.skeleton (existing skeleton).
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 0')
    path = ANIM + '/A_Player_Victory_NetRaise'
    t = unreal.AssetImportTask()
    t.filename = SRC + 'Player_Victory_NetRaise.fbx'
    t.destination_path = ANIM
    t.destination_name = 'A_Player_Victory_NetRaise'
    t.automated = True
    t.replace_existing = True
    t.save = False
    ui = unreal.FbxImportUI()
    ui.import_mesh = False
    ui.import_animations = True
    ui.import_materials = False
    ui.import_textures = False
    ui.import_as_skeletal = True
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_ANIMATION
    ui.skeleton = unreal.load_asset(SKEL)
    ad = ui.anim_sequence_import_data
    ad.set_editor_property('animation_length', unreal.FBXAnimationLengthImportType.FBXALIT_EXPORTED_TIME)
    ad.set_editor_property('import_bone_tracks', True)
    ad.set_editor_property('remove_redundant_keys', False)
    ad.set_editor_property('use_default_sample_rate', False)
    ad.set_editor_property('custom_sample_rate', 30)
    t.options = ui
    tools.import_asset_tasks([t])
    OUT['anim_import_objects'] = [str(p) for p in t.imported_object_paths]
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.FBX 1')
    anim = unreal.load_asset(path)
    if anim:
        anim.set_editor_property('enable_root_motion', False)
        E.save_loaded_asset(anim)
    return anim


def make_montage(anim):
    path = ANIM + '/AM_Player_Victory_NetRaise'
    mont = unreal.load_asset(path)
    if not mont:
        f = unreal.AnimMontageFactory()
        f.set_editor_property('source_animation', anim)
        mont = tools.create_asset('AM_Player_Victory_NetRaise', ANIM, unreal.AnimMontage, f)
    tracks = mont.get_editor_property('slot_anim_tracks')
    track = tracks[0]
    track.set_editor_property('slot_name', 'FullBody')
    tracks[0] = track
    mont.set_editor_property('slot_anim_tracks', tracks)
    bi = mont.get_editor_property('blend_in'); bi.set_editor_property('blend_time', .15); mont.set_editor_property('blend_in', bi)
    bo = mont.get_editor_property('blend_out'); bo.set_editor_property('blend_time', .2); mont.set_editor_property('blend_out', bo)
    mont.set_editor_property('enable_auto_blend_out', False)
    mont.modify()
    E.save_asset(path, only_if_is_dirty=False)
    return mont


def pose(anim, bone, t):
    tr = unreal.AnimationLibrary.get_bone_pose_for_time(anim, bone, t, False)
    return [round(tr.translation.x, 2), round(tr.translation.y, 2), round(tr.translation.z, 2)]


try:
    tex = import_texture(); OUT['texture'] = tex.get_path_name()
    OUT['material'] = make_star_material(tex).get_path_name()
    anim = import_anim()
    OUT['anim'] = anim.get_path_name() if anim else None
    if anim:
        OUT['anim_skeleton'] = anim.get_editor_property('skeleton').get_path_name()
        OUT['anim_length'] = anim.get_play_length()
        idle = unreal.load_asset('/Game/InportAssets/Animation/A_Player_Idle')
        OUT['pelvis_idle_t0'] = pose(idle, 'pelvis', 0.0)
        OUT['pelvis_victory'] = {str(t): pose(anim, 'pelvis', t) for t in [0.0, .2, .54, .8, .92, 1.5, 1.86]}
        OUT['hand_r_idle_t0'] = pose(idle, 'hand_r', 0.0)
        OUT['hand_r_victory_t0'] = pose(anim, 'hand_r', 0.0)
        mont = make_montage(anim)
        OUT['montage'] = mont.get_path_name()
        OUT['montage_slot'] = str(mont.get_editor_property('slot_anim_tracks')[0].slot_name)
        OUT['montage_length'] = mont.get_play_length()
except Exception:
    OUT['error'] = traceback.format_exc()
open(unreal.Paths.project_saved_dir() + 'StageClearSetupAssets.json', 'w').write(json.dumps(OUT, indent=1))
unreal.log('[SCS] COMPLETE')
