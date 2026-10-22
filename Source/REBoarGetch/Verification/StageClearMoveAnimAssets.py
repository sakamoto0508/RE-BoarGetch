"""Moves the authored victory animation out of the git-ignored Content/InportAssets into tracked /Game/Animation/Player,
then fixes up redirectors so BP_BoarPlayerController references the new paths. Writes Saved/StageClearMoveAnimAssets.txt"""
import unreal
E = unreal.EditorAssetLibrary
out = []
moves = [('/Game/InportAssets/Animation/A_Player_Victory_NetRaise', '/Game/Animation/Player/A_Player_Victory_NetRaise'),
         ('/Game/InportAssets/Animation/Montage/AM_Player_Victory_NetRaise', '/Game/Animation/Player/AM_Player_Victory_NetRaise')]
for src, dst in moves:
    if E.does_asset_exist(src) and not E.does_asset_exist(dst):
        out.append('rename %s -> %s %s' % (src, dst, E.rename_asset(src, dst)))
    else:
        out.append('skip %s (exists dst=%s)' % (src, E.does_asset_exist(dst)))
# Resolve redirectors left in the old folders and re-save referencers.
ar = unreal.AssetRegistryHelpers.get_asset_registry()
reds = [a.get_asset() for p in ['/Game/InportAssets/Animation', '/Game/InportAssets/Animation/Montage']
        for a in ar.get_assets_by_path(p, recursive=False) if str(a.asset_class_path.asset_name) == 'ObjectRedirector']
reds = [r for r in reds if r and 'Victory' in r.get_name()]
if reds:
    unreal.AssetToolsHelpers.get_asset_tools().fixup_referencers(reds)
out.append('redirectors fixed: %d' % len(reds))
bp = unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController')
c = unreal.get_default_object(bp.generated_class()).get_component_by_class(unreal.StageClearPresentationComponent)
out.append('controller montage: %s' % c.get_editor_property('victory_montage').get_path_name())
m = unreal.load_asset('/Game/Animation/Player/AM_Player_Victory_NetRaise')
out.append('montage slot: %s anim: %s' % ([str(t.slot_name) for t in m.get_editor_property('slot_anim_tracks')],
                                          m.get_editor_property('slot_anim_tracks')[0].anim_track.anim_segments[0].get_editor_property('anim_reference').get_path_name()))
out.append('old exist: %s' % [E.does_asset_exist(s) for s, _ in moves])
open(unreal.Paths.project_saved_dir() + 'StageClearMoveAnimAssets.txt', 'w').write('\n'.join(out))
