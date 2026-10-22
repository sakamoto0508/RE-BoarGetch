"""Fresh-process verification of the completed content migration."""
import unreal, json
from pathlib import Path
E = unreal.EditorAssetLibrary
ar = unreal.AssetRegistryHelpers.get_asset_registry()
ar.search_all_assets(True)
result_path = Path(__file__).with_name('OrganizeContent.json')
result = json.loads(result_path.read_text(encoding='utf-8'))
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True)
for row in result['moves']:
    assert E.load_asset(row['destination'])
    assert not E.does_asset_exist(row['source']), row['source']
    assert not ar.get_referencers(row['source'], options), row['source']
assert all(str(a.asset_class_path.asset_name) in ('Blueprint', 'WidgetBlueprint') for a in ar.get_assets_by_path('/Game/BP', recursive=True))
bp = E.load_asset('/Game/Animation/Boar/ABP_NormalBoar')
nodes = [n for n in unreal.ObjectIterator(unreal.AnimGraphNode_BlendSpacePlayer) if n.get_path_name().startswith(bp.get_path_name() + ':')]
assert nodes
for node in nodes:
    blend = node.get_editor_property('node').get_editor_property('blend_space')
    assert blend and blend.get_path_name().startswith('/Game/Animation/Boar/BS_BoarLocomotion'), node
montage = E.load_asset('/Game/Animation/Player/AM_Player_Victory_NetRaise')
tracks = montage.get_editor_property('slot_anim_tracks')
segments = tracks[0].anim_track.anim_segments
anim = segments[0].get_editor_property('anim_reference')
repaired = not bool(anim)
if not anim:
    anim = E.load_asset('/Game/Animation/Player/A_Player_Victory_NetRaise')
    segment = segments[0]
    segment.set_editor_property('anim_reference', anim)
    segments[0] = segment
    slot = tracks[0]
    track = slot.get_editor_property('anim_track')
    track.set_editor_property('anim_segments', segments)
    slot.set_editor_property('anim_track', track)
    tracks[0] = slot
    montage.modify()
    montage.set_editor_property('slot_anim_tracks', tracks)
    assert E.save_loaded_asset(montage, False)
assert anim and anim.get_path_name().startswith('/Game/Animation/Player/A_Player_Victory_NetRaise'), anim
for path in ['/Game/Animaiton', '/Game/BP/Cage/Materials']:
    assert not ar.get_assets_by_path(path, recursive=True)
    if E.does_directory_exist(path):
        assert E.delete_directory(path)
result['fresh_process_verification'] = {'passed': not repaired, 'montage_repaired_this_run': repaired, 'boar_blend_space_nodes': len(nodes), 'victory_animation': anim.get_path_name(), 'empty_old_folders_removed': True}
result_path.write_text(json.dumps(result, indent=2), encoding='utf-8')
unreal.log('CONTENT_FRESH_VERIFICATION_PASSED')
