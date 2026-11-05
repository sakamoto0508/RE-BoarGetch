"""Creates / edits the ACF verification assets through the editor APIs (idempotent). Headless:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Plugins/ActionCameraFramework/Scripts/CreateVerificationAssets.py
- DA_ACFVerify_Fixed        : ACFCameraPreset created with the Data Asset factory, FixedPose node; then EDITED (FOV 55 -> 65)
- DA_ACFVerify_FollowLookAt : preset with Follow + LookAt instanced nodes (checks new node types are editable)
- BP_ACFBlueprintApiTest    : Actor Blueprint. BeginPlay -> Request Camera For Player(DA_ACFVerify_Fixed) -> Delay 1s -> Release Camera For Player(same handle)
Used by ActionCameraFramework.Integration.BlueprintRequestRelease. Writes Saved/ACFVerification/CreateVerificationAssets.json."""
import unreal, json, traceback

TS = 'editor_toolset.toolsets.blueprint.BlueprintTools'
FOLDER = '/ActionCameraFramework/Verification'
E = unreal.EditorAssetLibrary
OUT = {}


def call(tool, args):
    r = unreal.ToolsetRegistry.execute_tool(TS, tool, json.dumps(args))
    err = r.get_editor_property('error')
    val = r.get_value_as_json_string() or ''
    if err:
        raise RuntimeError('%s: %s' % (tool, err[:3000]))
    d = json.loads(val) if val else None
    return json.loads(d) if isinstance(d, str) else d


def ensure_preset(name):
    path = FOLDER + '/' + name
    if not E.does_asset_exist(path):
        f = unreal.DataAssetFactory()
        f.set_editor_property('data_asset_class', unreal.ACFCameraPreset)
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.ACFCameraPreset, f)
    return unreal.load_asset(path)


def cut():
    b = unreal.ACFCameraBlendSettings()
    b.set_editor_property('curve', unreal.ACFCameraBlendCurve.CUT)
    b.set_editor_property('duration', 0.0)
    return b


try:
    # ---------- preset: create ----------
    fixed = ensure_preset('DA_ACFVerify_Fixed')
    node = unreal.new_object(unreal.ACFCameraNode_FixedPose, outer=fixed)
    node.set_editor_property('location', unreal.Vector(111, 222, 333))
    node.set_editor_property('rotation', unreal.Rotator(roll=0, pitch=-15, yaw=60))
    node.set_editor_property('field_of_view', 55.0)
    fixed.set_editor_property('nodes', [node])
    fixed.set_editor_property('default_blend_in', cut())
    fixed.set_editor_property('default_blend_out', cut())
    OUT['fixed_saved_create'] = E.save_loaded_asset(fixed, False)

    # ---------- preset: edit (as a designer would in the details panel) ----------
    fixed = unreal.load_asset(FOLDER + '/DA_ACFVerify_Fixed')
    n0 = fixed.get_editor_property('nodes')[0]
    n0.set_editor_property('field_of_view', 65.0)
    fixed.modify()
    OUT['fixed_saved_edit'] = E.save_loaded_asset(fixed, False)
    n0 = unreal.load_asset(FOLDER + '/DA_ACFVerify_Fixed').get_editor_property('nodes')[0]
    OUT['fixed_reread'] = {'class': n0.get_class().get_name(), 'outer': n0.get_outer().get_name(),
                           'location': str(n0.get_editor_property('location')), 'rotation': str(n0.get_editor_property('rotation')),
                           'fov': n0.get_editor_property('field_of_view')}

    # ---------- preset with Follow + LookAt ----------
    fl = ensure_preset('DA_ACFVerify_FollowLookAt')
    follow = unreal.new_object(unreal.ACFCameraNode_Follow, outer=fl)
    follow.set_editor_property('offset', unreal.Vector(-350, 0, 180))
    follow.set_editor_property('offset_space', unreal.ACFCameraOffsetSpace.TARGET_LOCAL)
    follow.set_editor_property('damping_time', 0.2)
    look = unreal.new_object(unreal.ACFCameraNode_LookAt, outer=fl)
    look.set_editor_property('target_source', unreal.ACFCameraLookAtSource.FOLLOW_TARGET)
    look.set_editor_property('target_offset', unreal.Vector(0, 0, 60))
    fl.set_editor_property('nodes', [follow, look])
    blend = unreal.ACFCameraBlendSettings()
    blend.set_editor_property('curve', unreal.ACFCameraBlendCurve.EASE_IN_OUT)
    blend.set_editor_property('duration', 0.6)
    blend.set_editor_property('source', unreal.ACFCameraBlendSource.LIVE)
    fl.set_editor_property('default_blend_in', blend)
    OUT['follow_saved'] = E.save_loaded_asset(fl, False)
    OUT['follow_reread'] = [n.get_class().get_name() for n in unreal.load_asset(FOLDER + '/DA_ACFVerify_FollowLookAt').get_editor_property('nodes')]

    # ---------- Blueprint using the public BP API ----------
    bp_path = FOLDER + '/BP_ACFBlueprintApiTest'
    if E.does_asset_exist(bp_path):
        E.delete_asset(bp_path)
    f = unreal.BlueprintFactory()
    f.set_editor_property('parent_class', unreal.Actor)
    bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_ACFBlueprintApiTest', FOLDER, unreal.Blueprint, f)
    ref = {'refPath': bp_path + '.BP_ACFBlueprintApiTest'}
    graph = {'refPath': bp_path + '.BP_ACFBlueprintApiTest:EventGraph'}
    OUT['pins_request'] = call('get_node_type_pins', {'graph': graph, 'type_id': 'ActionCamera|RequestCameraForPlayer'})
    OUT['pins_release'] = call('get_node_type_pins', {'graph': graph, 'type_id': 'ActionCamera|ReleaseCameraForPlayer'})
    dsl = '''
(event EventBeginPlay
  (bind pc (Game|GetPlayerController :PlayerIndex 0))
  (bind handle (ActionCamera|RequestCameraForPlayer :PlayerController pc :Preset "%s/DA_ACFVerify_Fixed.DA_ACFVerify_Fixed"))
  (Utilities|FlowControl|Delay :Duration 1.0)
  (ActionCamera|ReleaseCameraForPlayer :PlayerController pc :Handle handle))
''' % FOLDER
    call('write_graph_dsl', {'graph': graph, 'code': dsl})
    OUT['graph'] = call('read_graph_dsl', {'graph': graph})
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    OUT['bp_status'] = str(bp.get_editor_property('status')) if hasattr(bp, 'status') else 'n/a'
    OUT['bp_saved'] = E.save_loaded_asset(bp, False)
except Exception:
    OUT['error'] = traceback.format_exc()

open(unreal.Paths.project_saved_dir() + 'ACFVerification/CreateVerificationAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, ensure_ascii=False, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
