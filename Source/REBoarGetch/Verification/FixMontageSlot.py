"""Sets AM_Player_Victory_NetRaise to the FullBody slot (the only Slot node in ABP_Player) and force-saves it."""
import unreal
path = '/Game/Animation/Player/AM_Player_Victory_NetRaise'
m = unreal.load_asset(path)
m.modify()
tracks = m.get_editor_property('slot_anim_tracks')
track = tracks[0]
track.set_editor_property('slot_name', 'FullBody')
tracks[0] = track
m.set_editor_property('slot_anim_tracks', tracks)
ok = unreal.EditorAssetLibrary.save_asset(path, only_if_is_dirty=False)
open('C:/UnrealGames/REBoarGetch/Saved/ProbeMontage.txt', 'w').write('saved=%s slots=%s' % (ok, [str(x.slot_name) for x in m.get_editor_property('slot_anim_tracks')]))
