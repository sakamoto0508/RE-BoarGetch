import unreal,json
from pathlib import Path
b=unreal.load_asset('/Game/BP/Lobby/BP_PC_Lobby');d=unreal.get_default_object(b.generated_class())
keys=['default_mapping_context','global_mapping_context','ui_mapping_context','move_action','look_action','jump_action','gadget_action','dash_action','gadget_modifier_action','gadget_slot1_action','gadget_slot2_action','gadget_slot3_action','gadget_slot4_action','ui_back_action','player_hud_widget_class']
Path(__file__).with_suffix('.json').write_text(json.dumps({k:str(d.get_editor_property(k)) for k in keys},indent=2))
for p in ['/Game/BP/Lobby/BP_GM_GadgetLab','/Game/BP/Lobby/BP_GM_Archive']:assert unreal.EditorAssetLibrary.save_asset(p)
unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level('/Game/Level/L_Lobby')
