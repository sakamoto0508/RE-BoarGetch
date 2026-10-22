"""Read-only export: WBP_Result tree text, player mesh / net mesh / idle+netswing anims as FBX for Blender authoring."""
import unreal, os
root = 'C:/UnrealGames/REBoarGetch/SourceArt/StageClear/UEExport/'
os.makedirs(root, exist_ok=True)
lines = [unreal.BoarUIAuthoringLibrary.describe_widget_blueprint(unreal.load_asset('/Game/BP/Widget/WBP_Result'))]

def export(asset_path, filename, anim=False):
    task = unreal.AssetExportTask()
    task.object = unreal.load_asset(asset_path)
    task.filename = root + filename
    task.automated = True
    task.prompt = False
    task.replace_identical = True
    opt = unreal.FbxExportOption()
    opt.export_morph_targets = False
    opt.level_of_detail = False
    opt.collision = False
    opt.export_preview_mesh = False if anim else False
    task.options = opt
    ok = unreal.Exporter.run_asset_export_task(task)
    lines.append('export %s -> %s %s' % (asset_path, filename, ok))

export('/Game/InportAssets/Player/GreatSwordSlash_UE', 'Player_Mesh.fbx')
export('/Game/InportAssets/mushitoriami/mushitoriami', 'Net_Mesh.fbx')
export('/Game/InportAssets/Animation/A_Player_Idle', 'A_Player_Idle.fbx', True)
export('/Game/InportAssets/Animation/A_Player_JumpStart', 'A_Player_JumpStart.fbx', True)
export('/Game/InportAssets/Animation/A_Player_Land', 'A_Player_Land.fbx', True)
export('/Game/InportAssets/Animation/A_Player_NetSwing', 'A_Player_NetSwing.fbx', True)
sk = unreal.load_asset('/Game/InportAssets/Player/GreatSwordSlash_UE')
s = sk.find_socket('hand_r_net_socket')
lines.append('socket hand_r_net_socket bone=%s loc=%s rot=%s scale=%s' % (s.bone_name, s.relative_location, s.relative_rotation, s.relative_scale))
net = unreal.load_asset('/Game/InportAssets/mushitoriami/mushitoriami')
lines.append('net bounds %s' % net.get_bounds())
lines.append('player bounds %s' % sk.get_bounds())
open(root + 'export_info.txt', 'w', encoding='utf-8').write('\n'.join(str(l) for l in lines))
unreal.log('[SCE] COMPLETE')
if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
