"""Shared Blender helpers for Player_Victory_NetRaise authoring (Blender 5.x, run with -b --python).
Master rig = armature from the UE animation FBX export (object 'root', scale 0.01, cm bones), so the authored
action round-trips onto /Game/InportAssets/Player/GreatSwordSlash_UE_Skeleton. The player mesh FBX is re-bound to it."""
import bpy, json, math
from mathutils import Matrix, Quaternion, Vector

ROOT = 'C:/UnrealGames/REBoarGetch/SourceArt/StageClear/'
EXP = ROOT + 'UEExport/'
S = Matrix.Diagonal((1.0, -1.0, 1.0, 1.0))
# Rig world (m) = 0.01 * (S * UE component (cm) + RIG_OFFSET); measured from ref-pose bone heads.
RIG_OFFSET = Vector((-0.53, -1.78, 0.0))
UE_TO_WORLD = Matrix.Translation(RIG_OFFSET * 0.01) @ Matrix.Scale(0.01, 4) @ S


def ue_matrix(loc, quat, scale):
    q = Quaternion((quat[3], quat[0], quat[1], quat[2]))
    return Matrix.Translation(Vector(loc)) @ q.to_matrix().to_4x4() @ Matrix.Diagonal((scale[0], scale[1], scale[2], 1.0))


def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.render.fps = 30


def import_fbx(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path)
    return [o for o in bpy.data.objects if o not in before]


def assign_action(obj, act):
    obj.animation_data_create()
    obj.animation_data.action = act
    if hasattr(act, 'slots') and len(act.slots) and obj.animation_data.action_slot is None:
        obj.animation_data.action_slot = act.slots[0]


def load_action(path, name):
    objs = import_fbx(path)
    tmp = next(o for o in objs if o.type == 'ARMATURE')
    act = tmp.animation_data.action
    act.name = name
    act.use_fake_user = True
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
    return act


def load_rig():
    """Returns (rig, body_mesh, idle_action)."""
    objs = import_fbx(EXP + 'A_Player_Idle.fbx')
    rig = next(o for o in objs if o.type == 'ARMATURE')
    rig.name = 'root'
    idle = rig.animation_data.action
    idle.name = 'A_Player_Idle'
    idle.use_fake_user = True
    rig.animation_data.action = None
    for pb in rig.pose.bones:
        pb.matrix_basis.identity()
    mobjs = import_fbx(EXP + 'Player_Mesh.fbx')
    body = next(o for o in mobjs if o.type == 'MESH')
    bpy.context.view_layer.update()
    mw = body.matrix_world.copy()
    body.parent = None
    body.matrix_world = Matrix.Translation(RIG_OFFSET * 0.01) @ mw
    for mod in body.modifiers:
        if mod.type == 'ARMATURE':
            mod.object = rig
    body.parent = rig
    body.matrix_parent_inverse = rig.matrix_world.inverted()
    for o in mobjs:
        if o is not body:
            bpy.data.objects.remove(o, do_unlink=True)
    body.name = 'PlayerBody'
    return rig, body, idle


def attach_net(rig):
    """Import the real net mesh and parent it to hand_r with the in-game socket transform (hand_r_net_socket, scale 0.63)."""
    probe = json.load(open(EXP + 'socket_probe.json'))
    objs = import_fbx(EXP + 'Net_Mesh.fbx')
    net = next(o for o in objs if o.type == 'MESH')
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    me = bpy.data.meshes.new_from_object(net.evaluated_get(dg), preserve_all_data_layers=True, depsgraph=dg)
    me.transform(net.matrix_world)          # world metres, Blender axes (= 0.01 * S * p_ue)
    me.transform(Matrix.Scale(100.0, 4))    # cm, Blender axes (= S * p_ue)
    mats = list(net.data.materials)
    for o in objs:
        bpy.data.objects.remove(o, do_unlink=True)
    obj = bpy.data.objects.new('Net', me)
    bpy.context.scene.collection.objects.link(obj)
    for m in mats:
        if m:
            me.materials.append(m)
    sock = probe['hand_r_net_socket']
    T = ue_matrix(sock['loc'], sock['quat'], sock['scale'])
    # world = UE_TO_WORLD * T * p_ue = UE_TO_WORLD * T * S * p_mesh   (rig at ref pose)
    obj.matrix_world = UE_TO_WORLD @ T @ S
    bpy.context.view_layer.update()
    mw = obj.matrix_world.copy()
    obj.parent = rig
    obj.parent_type = 'BONE'
    obj.parent_bone = 'hand_r'
    bpy.context.view_layer.update()
    obj.matrix_world = mw
    return obj


def net_points_world(net):
    """Grip (local origin) and head centre in world space."""
    zs = [v.co.z for v in net.data.vertices]
    top = max(zs)
    head = [v.co for v in net.data.vertices if v.co.z > top - 55.0]
    c = sum(head, Vector()) / max(1, len(head))
    return net.matrix_world @ Vector((0, 0, 0)), net.matrix_world @ c


def setup_preview(floor_z, res=(720, 720)):
    scn = bpy.context.scene
    scn.render.engine = 'BLENDER_WORKBENCH'
    scn.display.shading.light = 'STUDIO'
    scn.display.shading.color_type = 'MATERIAL'
    scn.render.resolution_x, scn.render.resolution_y = res
    cam_data = bpy.data.cameras.new('PreviewCam')
    cam = bpy.data.objects.new('PreviewCam', cam_data)
    scn.collection.objects.link(cam)
    cam_data.lens = 35
    scn.camera = cam
    bpy.ops.mesh.primitive_plane_add(size=8, location=(0, 0, floor_z))
    return cam


def aim_camera(cam, loc, target):
    cam.location = Vector(loc)
    cam.rotation_euler = (Vector(target) - cam.location).to_track_quat('-Z', 'Y').to_euler()


def render(path):
    bpy.context.scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
