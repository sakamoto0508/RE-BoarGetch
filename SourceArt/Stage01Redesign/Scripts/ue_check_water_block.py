"""Headless: sweep a character capsule (Pawn profile) into the pond / stream and over the bridge on the saved L_Stage01.
Writes Logs/water_block_check.json."""
import json, os, sys, unreal
HERE = os.path.dirname(os.path.abspath(__file__)); sys.path.insert(0, HERE)
import s01r_layout as L
w = unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Stage01')
# Right after load_map traces miss everything (collision not ready); validating the level first makes them hit.
vs = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
st = unreal.ValidateAssetsSettings()
st.set_editor_property('skip_excluded_directories', False)
vs.validate_assets_with_settings([unreal.EditorAssetLibrary.find_asset_data('/Game/Level/L_Stage01')], st)


def sweep(a, b, lift=0.0):
    za, zb = L.height(*a) + 100 + lift, L.height(*b) + 100 + lift
    hit = unreal.SystemLibrary.capsule_trace_single_by_profile(w, unreal.Vector(a[0], a[1], za), unreal.Vector(b[0], b[1], zb),
                                                             42, 90, 'Pawn', False, [], unreal.DrawDebugTrace.NONE, True)
    if hit is None:
        return {'hit': None}
    t = hit.to_tuple()
    act = t[9]
    return {'hit': act.get_actor_label() if act else None, 'at': [round(t[4].x), round(t[4].y)]}


bx, by = L.BRIDGE
tests = {
    'pond east shore -> centre': ((-1300.0, -500.0), L.POND[:2]),
    'pond south shore -> centre': ((-2100.0, -1350.0), L.POND[:2]),
    'pond north shore -> centre': ((-2300.0, 60.0), L.POND[:2]),
    'stream east bank -> west bank (y=-1500)': ((-2200.0, -1500.0), (-2950.0, -1500.0)),
    'stream east bank -> west bank (y=-2650)': ((-2450.0, -2600.0), (-3050.0, -2600.0)),
    'bridge east -> west (deck)': ((bx + 420, by), (bx - 420, by)),
}
r = {k: sweep(a, b, 130.0 if 'bridge' in k else 30.0) for k, (a, b) in tests.items()}
json.dump(r, open(os.path.join(os.path.dirname(HERE), 'Logs', 'water_block_check.json'), 'w'), indent=1)
unreal.log_warning('[S01Water] %s' % json.dumps(r))
