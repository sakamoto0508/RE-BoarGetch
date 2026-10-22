"""Give blocking props real (simple) collision. Full editor only: StaticMeshEditorSubsystem is None in the commandlet.
- Own props: one box each.
- Pack meshes without simple collision: duplicated to /Game/InportAssets/Stage01Redesign/Meshes/Reused (originals untouched);
  rocks/cliffs/logs use their low-poly shape (complex as simple), barrel/bucket/crate get a box.
s01r_dressing.REUSED maps the originals to these copies."""
import os, sys, unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s01r_dressing as D

sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
eal = unreal.EditorAssetLibrary
assert sms, 'run in the full editor'
OWN_BOXES = ['SM_S01R_ResearchCrate', 'SM_S01R_Bollard', 'SM_S01R_GuideBeacon', 'SM_S01R_Signpost', 'SM_S01R_ResearchFencePost']


def box(mesh):
    sms.remove_collisions(mesh)
    mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_DEFAULT)
    sms.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)


def complex_as_simple(mesh):
    mesh.get_editor_property('body_setup').set_editor_property('collision_trace_flag', unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)


for n in OWN_BOXES:
    m = unreal.load_asset(D.RD + n)
    box(m)
    eal.save_loaded_asset(m)
    unreal.log('[S01] %s box simple=%d' % (n, sms.get_simple_collision_count(m)))

for src, dst in D.REUSED.items():
    if not eal.does_asset_exist(dst):
        assert eal.duplicate_asset(src, dst), dst
    m = unreal.load_asset(dst)
    if any(k in src for k in ('Barrel', 'Bucket', 'WoodenCrate')):
        box(m)
    else:
        sms.remove_collisions(m)
        complex_as_simple(m)
    eal.save_loaded_asset(m)
    unreal.log('[S01] %s simple=%d flag=%s' % (dst.split('/')[-1], sms.get_simple_collision_count(m),
               m.get_editor_property('body_setup').get_editor_property('collision_trace_flag')))
unreal.log('[S01] COMPLETE')
unreal.SystemLibrary.quit_editor()
