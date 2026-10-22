"""Read-only: component-space ref-pose transforms of key bones and the net socket (spawned temp actor, nothing saved)."""
import unreal, json
out = {}
sk = unreal.load_asset('/Game/InportAssets/Player/GreatSwordSlash_UE')
actor = unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SkeletalMeshActor, unreal.Vector(0, 0, 0))
comp = actor.skeletal_mesh_component
comp.set_skeletal_mesh_asset(sk)
def t(x):
    return {'loc': [x.translation.x, x.translation.y, x.translation.z],
            'quat': [x.rotation.x, x.rotation.y, x.rotation.z, x.rotation.w],
            'scale': [x.scale3d.x, x.scale3d.y, x.scale3d.z]}
for n in ['pelvis', 'spine_03', 'head', 'hand_r', 'hand_l', 'lowerarm_r', 'foot_l', 'ball_l', 'thigh_r', 'hand_r_net_socket']:
    out[n] = t(comp.get_socket_transform(n, unreal.RelativeTransformSpace.RTS_COMPONENT))
net = unreal.load_asset('/Game/InportAssets/mushitoriami/mushitoriami')
b = net.get_bounds()
out['net_bounds'] = [b.origin.x, b.origin.y, b.origin.z, b.box_extent.x, b.box_extent.y, b.box_extent.z]
actor.destroy_actor()
open('C:/UnrealGames/REBoarGetch/SourceArt/StageClear/UEExport/socket_probe.json', 'w').write(json.dumps(out, indent=1))
unreal.log('[SCP] COMPLETE')
if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
