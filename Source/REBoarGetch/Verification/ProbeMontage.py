import unreal
m = unreal.load_asset('/Game/Animation/Player/AM_Player_Victory_NetRaise')
bp = unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController')
c = unreal.get_default_object(bp.generated_class()).get_component_by_class(unreal.StageClearPresentationComponent)
open('C:/UnrealGames/REBoarGetch/Saved/ProbeMontage.txt','w').write('slots=%s ctrl=%s star=%s' % ([str(x.slot_name) for x in m.get_editor_property('slot_anim_tracks')], c.get_editor_property('victory_montage'), c.get_editor_property('star_vfx')))
