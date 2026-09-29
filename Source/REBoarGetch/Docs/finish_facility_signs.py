import unreal
from pathlib import Path
exec((Path(__file__).parent/'build_facility_levels.py').read_text(encoding='utf-8').split('def run():')[0])
assert LS.load_level('/Game/Level/L_GadgetLab')
sign('Quality_Movement','MOVEMENT TEST',(30,753,262),-90,30)
mesh('Quality_MovementScreenFace',(30,756,280),(380,4,72),'Panel')
sign('Quality_LoadoutReverse','GADGET LOADOUT',(0,-153,385),-90,47)
mesh('Quality_LoadoutReversePanel',(0,-150,410),(512,4,68),'Panel')
assert LS.save_current_level()
unreal.log('FACILITY_QUALITY_SIGN_SAVE_OK')
