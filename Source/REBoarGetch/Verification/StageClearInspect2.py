"""Read-only: WBP_Result widget tree, player skeleton bones, ABP slots."""
import unreal
out = []
def log(*a):
    out.append(' '.join(str(x) for x in a))

def walk(w, depth=0):
    extra = ''
    if isinstance(w, unreal.TextBlock):
        extra = 'text=%r size=%s' % (str(w.get_text()), w.get_editor_property('font').size)
    slot = w.get_editor_property('slot')
    log('  ' * depth + '%s [%s] vis=%s %s slot=%s' % (w.get_name(), w.get_class().get_name(), w.get_editor_property('visibility'), extra, slot.get_class().get_name() if slot else None))
    if isinstance(w, unreal.PanelWidget):
        for i in range(w.get_children_count()):
            walk(w.get_child_at(i), depth + 1)
    elif isinstance(w, unreal.ContentWidget) and w.get_content():
        walk(w.get_content(), depth + 1)

for path in ['/Game/BP/Widget/WBP_Result']:
    wt = unreal.load_object(None, path + '.WBP_Result:WidgetTree')
    log('tree', wt)
    root = None
    if root:
        walk(root)
sk = unreal.load_asset('/Game/InportAssets/Player/GreatSwordSlash_UE')
skel = sk.get_editor_property('skeleton')
ref = skel.get_reference_pose() if hasattr(skel, 'get_reference_pose') else None
try:
    names = unreal.SkeletalMeshEditorSubsystem
except Exception:
    pass
try:
    lib = unreal.AnimationLibrary
    for i, n in enumerate(lib.get_bone_names(sk) if hasattr(lib, 'get_bone_names') else []):
        log('bone', i, n)
except Exception as e:
    log('bones err', e)
for p in ['/Game/InportAssets/Animation/A_Player_Idle', '/Game/InportAssets/Animation/A_Player_JumpStart', '/Game/InportAssets/Animation/A_Player_NetSwing']:
    a = unreal.load_asset(p)
    if a:
        log(p, a.get_play_length(), 'frames', unreal.AnimationLibrary.get_num_frames(a), 'root motion', a.get_editor_property('enable_root_motion'))
        try:
            log('  bones', unreal.AnimationLibrary.get_animation_track_names(a)[:80])
        except Exception as e:
            log('  err', e)
ar = unreal.AssetRegistryHelpers.get_asset_registry()
for a in ar.get_assets_by_path('/Game/InportAssets/Player', recursive=False):
    log('playerasset', a.package_name, a.asset_class_path.asset_name)
for a in ar.get_assets_by_path('/Game/InportAssets/mushitoriami', recursive=True):
    log('netasset', a.package_name, a.asset_class_path.asset_name)
for pkg in ['/Game/InportAssets/Audio/' + n for n in ['button31','cyber','Cyber15-4__mp3cut_net_','Cyber21-4__mp3cut_net_','drumn','isoge','robot01','save','soundlogo12','soundlogo40','spaceship','spaceship01','sumia','ufo02','yodomi','ショット','パワーアップ','回避__2_','建物が少し崩れる2','選択5','電源オン']]:
    s = unreal.load_asset(pkg)
    log('snd', pkg, round(s.get_editor_property('duration'), 2), 'loop', s.get_editor_property('looping'), 'refs', ar.get_referencers(pkg, unreal.AssetRegistryDependencyOptions()))
open(unreal.Paths.project_saved_dir() + 'StageClearInspect2.txt', 'w', encoding='utf-8').write('\n'.join(out))
unreal.log('[SCI] COMPLETE')
