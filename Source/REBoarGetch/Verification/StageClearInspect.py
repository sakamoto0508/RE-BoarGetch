"""Read-only dump of assets used by the stage clear presentation (no saves)."""
import unreal

out = []
def log(*a):
    s = ' '.join(str(x) for x in a)
    out.append(s)
    unreal.log('[SCI] ' + s)

def cdo(path):
    bp = unreal.load_asset(path)
    gen = unreal.load_object(None, path + '_C') if bp else None
    if gen is None and bp:
        gen = bp.generated_class()
    return bp, unreal.get_default_object(gen) if gen else None

def try_prop(o, name):
    try:
        return o.get_editor_property(name)
    except Exception as e:
        return '<err %s>' % e

sds = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
def dump_components(bp_path):
    bp = unreal.load_asset(bp_path)
    handles = sds.k2_gather_subobject_data_for_blueprint(bp)
    for h in handles:
        d = unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h)
        obj = unreal.SubobjectDataBlueprintFunctionLibrary.get_object(d)
        if obj is None:
            continue
        info = [obj.get_name(), obj.get_class().get_name()]
        if isinstance(obj, unreal.SceneComponent):
            info += ['parent=%s' % try_prop(obj, 'attach_parent'), 'socket=%s' % try_prop(obj, 'attach_socket_name'),
                     'loc=%s' % obj.get_editor_property('relative_location'), 'rot=%s' % obj.get_editor_property('relative_rotation'),
                     'scale=%s' % obj.get_editor_property('relative_scale3d'), 'visible=%s' % obj.get_editor_property('visible'),
                     'hiddenInGame=%s' % obj.get_editor_property('hidden_in_game')]
        if isinstance(obj, unreal.StaticMeshComponent):
            info += ['mesh=%s' % obj.static_mesh.get_path_name() if obj.static_mesh else 'mesh=None',
                     'mats=%s' % [m.get_path_name() if m else None for m in obj.get_materials()]]
        if isinstance(obj, unreal.SkeletalMeshComponent):
            sk = obj.get_skeletal_mesh_asset()
            info += ['skmesh=%s' % (sk.get_path_name() if sk else None), 'anim=%s' % try_prop(obj, 'anim_class'),
                     'mats=%s' % [m.get_path_name() if m else None for m in obj.get_materials()]]
        if isinstance(obj, unreal.SpringArmComponent):
            info += ['arm=%s' % obj.target_arm_length, 'offset=%s' % obj.socket_offset]
        if isinstance(obj, unreal.CameraComponent):
            info += ['fov=%s' % obj.field_of_view]
        log('  comp', *info)

log('=== BP_PlayerCharacter')
dump_components('/Game/BP/Player/BP_PlayerCharacter')
bp, d = cdo('/Game/BP/Player/BP_PlayerCharacter')
mesh = d.get_editor_property('mesh')
sk = mesh.get_skeletal_mesh_asset()
log('mesh', sk.get_path_name() if sk else None, 'anim', mesh.get_editor_property('anim_class'), 'rel', mesh.get_editor_property('relative_location'), mesh.get_editor_property('relative_rotation'))
if sk:
    skel = sk.get_editor_property('skeleton')
    log('skeleton', skel.get_path_name())
    log('mats', [m.get_path_name() if m else None for m in mesh.get_materials()])
    for i in range(sk.num_sockets()):
        sock = sk.get_socket_by_index(i)
        log('  socket', sock.socket_name, sock.bone_name, sock.relative_location, sock.relative_rotation, sock.relative_scale, sock.get_outer().get_name())
    n = sk.get_editor_property('skeleton')
    try:
        bones = unreal.SkeletalMeshEditorSubsystem
    except Exception:
        pass

for p in ['/Game/BP/Gadget/BP_NetGadget']:
    log('=== ', p)
    dump_components(p)
    bp, d = cdo(p)
    log('socket', try_prop(d, 'equip_socket_name'))

log('=== BP_BoarGameMode')
bp, d = cdo('/Game/BP/GameMode/BP_BoarGameMode')
for n in ['wait_for_stage_end_presentation', 'wait_for_stage_introduction', 'stage_config', 'special_coin_class', 'default_pawn_class', 'player_controller_class', 'hud_class']:
    log(n, try_prop(d, n))
log('=== BP_BoarPlayerController')
bp, d = cdo('/Game/BP/Player/BP_BoarPlayerController')
for n in ['result_widget_class', 'lobby_level', 'lobby_level_name', 'game_over_widget_class']:
    log(n, try_prop(d, n))
cp = try_prop(d, 'capture_presentation')
if not isinstance(cp, str):
    for n in ['data_material', 'glow_material', 'capture_vfx', 'cage_reception_vfx', 'impact_sound', 'transfer_start_sound', 'capture_success_sound']:
        log('  cp', n, try_prop(cp, n))

log('=== WBP_Result')
wbp = unreal.load_asset('/Game/BP/Widget/WBP_Result')
log('parent', try_prop(wbp, 'parent_class'))
wt = try_prop(wbp, 'widget_tree')
log('tree', wt)
try:
    anims = wbp.get_editor_property('animations')
    log('anims', [a.get_name() for a in anims])
except Exception as e:
    log('anims err', e)
bp, d = cdo('/Game/BP/Widget/WBP_Result')
for n in ['captured_count_text_widget_name', 'target_count_text_widget_name', 'clear_time_text_widget_name', 'new_boar_count_text_widget_name', 'special_coin_count_text_widget_name']:
    log(n, try_prop(d, n))

log('=== Montages')
for p in ['/Game/InportAssets/Animation/Montage/AM_Player_NetSwing']:
    m = unreal.load_asset(p)
    if m:
        log(p, 'skel', m.get_editor_property('skeleton').get_path_name(), 'len', m.get_play_length())
        for t in m.get_editor_property('slot_anim_tracks'):
            log('  slot', t.slot_name)

ar = unreal.AssetRegistryHelpers.get_asset_registry()
opt = unreal.AssetRegistryDependencyOptions()
for pkg in ['/Game/BP/Widget/WBP_Result', '/Game/BP/GameMode/BP_BoarGameMode', '/Game/Animation/Player/ABP_Player']:
    log('referencers', pkg, ar.get_referencers(pkg, opt))
for pkg in ['/Game/BP/GameMode/BP_BoarGameMode', '/Game/Level/Stage1', '/Game/Animation/Player/ABP_Player', '/Game/BP/Widget/WBP_Result']:
    log('deps', pkg, ar.get_dependencies(pkg, opt))

log('=== Sound assets')
for a in ar.get_assets_by_path('/Game', recursive=True):
    c = str(a.asset_class_path.asset_name)
    if c in ('SoundWave', 'SoundCue', 'MetaSoundSource'):
        log('  snd', a.package_name, c)
for a in ar.get_assets_by_path('/Game', recursive=True):
    c = str(a.asset_class_path.asset_name)
    if c in ('NiagaraSystem',) and '/Game/Fab' not in str(a.package_name):
        log('  nia', a.package_name)
log('=== Levels')
for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/Engine', 'World')):
    if not str(a.package_name).startswith('/Game/Fab'):
        log('  map', a.package_name)
for a in ar.get_assets_by_class(unreal.TopLevelAssetPath('/Script/REBoarGetch', 'StageConfig')):
    sc = a.get_asset()
    log('  stagecfg', a.package_name, sc.stage_id, sc.target_capture_count, sc.level, len(sc.special_coin_definitions))
import os
p = unreal.Paths.project_saved_dir() + 'StageClearInspect.txt'
open(p, 'w', encoding='utf-8').write('\n'.join(out))
unreal.log('[SCI] COMPLETE ' + p)
