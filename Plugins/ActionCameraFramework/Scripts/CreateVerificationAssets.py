"""Creates / edits the ACF verification assets through the editor APIs (idempotent). Headless:
  run_ue_py.sh C:/UnrealGames/REBoarGetch/Plugins/ActionCameraFramework/Scripts/CreateVerificationAssets.py
- DA_ACFVerify_Fixed        : ACFCameraPreset created with the Data Asset factory, FixedPose node; then EDITED (FOV 55 -> 65)
- DA_ACFVerify_FollowLookAt : preset with Follow + LookAt instanced nodes (checks new node types are editable)
- BP_ACFBlueprintApiTest    : Actor Blueprint. BeginPlay -> Request Camera For Player(DA_ACFVerify_Fixed) -> Delay 1s -> Release Camera For Player(same handle)
- DA_ACFVerify_Orbit / BP_ACFOrbitInputTest : Orbital Follow + Collision; BP adds camera input (90, 0) from Blueprint
- BP_ACFEffectTest : Recoil + Impulse + ACF Shake from Blueprint, stop the shake by handle, Stop All
Used by ActionCameraFramework.Integration.BlueprintRequestRelease / BlueprintOrbitInput / BlueprintEffects. Writes Saved/ACFVerification/CreateVerificationAssets.json."""
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


def ensure_bp(name):
    """Load the Actor Blueprint if it exists (headless delete is refused while loaded), else create it."""
    path = FOLDER + '/' + name
    existing = unreal.load_asset(path + '.' + name)  # the asset registry may not have scanned plugin content yet
    if existing:
        return existing
    f = unreal.BlueprintFactory()
    f.set_editor_property('parent_class', unreal.Actor)
    return unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, FOLDER, unreal.Blueprint, f)


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
    damping = unreal.ACFCameraPositionDamping()
    damping.set_editor_property('time', unreal.Vector(0.2, 0.2, 0.1))
    damping.set_editor_property('space', unreal.ACFCameraDampingSpace.TARGET_LOCAL)
    follow.set_editor_property('damping', damping)
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
    bp = ensure_bp('BP_ACFBlueprintApiTest')
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
    OUT['bp_status'] = str(bp.get_editor_property('status'))
    OUT['bp_saved'] = E.save_loaded_asset(bp, False)

    # ---------- Phase 2: Orbital Follow (FreeLook) + Rotation Composer + Collision ----------
    orbit_da = ensure_preset('DA_ACFVerify_Orbit')
    orbit = unreal.new_object(unreal.ACFCameraNode_OrbitalFollow, outer=orbit_da)
    orbit.set_editor_property('distance', 300.0)
    orbit.set_editor_property('input_source', unreal.ACFCameraOrbitInputSource.CAMERA_INPUT)
    orbit.set_editor_property('initial_rotation', unreal.ACFCameraOrbitInitialRotation.BEHIND_TARGET)
    orbit.set_editor_property('initial_pitch', -15.0)
    orbit.set_editor_property('sensitivity', unreal.Vector2D(1.0, 1.0))
    collision = unreal.new_object(unreal.ACFCameraNode_Collision, outer=orbit_da)
    collision.set_editor_property('probe_radius', 15.0)
    orbit_da.set_editor_property('nodes', [orbit, collision])
    orbit_da.set_editor_property('default_blend_in', cut())
    orbit_da.set_editor_property('default_blend_out', cut())
    OUT['orbit_saved'] = E.save_loaded_asset(orbit_da, False)
    OUT['orbit_reread'] = [n.get_class().get_name() for n in unreal.load_asset(FOLDER + '/DA_ACFVerify_Orbit').get_editor_property('nodes')]

    obp_path = FOLDER + '/BP_ACFOrbitInputTest'
    obp = ensure_bp('BP_ACFOrbitInputTest')
    ograph = {'refPath': obp_path + '.BP_ACFOrbitInputTest:EventGraph'}

    def find_one(flt, must):
        ids = call('find_node_types', {'graph': ograph, 'type_id_filter': flt, 'context_pins': []})['returnValue']
        hits = [i for i in ids if must in i]
        OUT.setdefault('node_ids', {})[flt] = ids[:20]
        if not hits:
            raise RuntimeError('node type not found: %s (%s)' % (flt, ids[:20]))
        return hits[0]

    ids = {
        'pawn': find_one('GetPlayerPawn', 'Game|GetPlayerPawn'),
        'params': find_one('MakeTargetRequestParams', 'ActionCamera|'),
        'sub': find_one('GetACFCameraSubsystem', 'ActionCamera|'),
        'input': find_one('AddCameraInput', 'ActionCamera|'),
        'vec2': find_one('MakeVector2D', 'MakeVector2D'),
    }
    OUT['orbit_bp_ids'] = ids
    OUT['pins_input'] = call('get_node_type_pins', {'graph': ograph, 'type_id': ids['input']})
    odsl = '''
(event EventBeginPlay
  (bind pc (Game|GetPlayerController :PlayerIndex 0))
  (bind pawn (%(pawn)s :PlayerIndex 0))
  (bind params (%(params)s :Priority 0 :FollowTarget pawn))
  (bind handle (ActionCamera|RequestCameraForPlayer :PlayerController pc :Preset "%(folder)s/DA_ACFVerify_Orbit.DA_ACFVerify_Orbit" :Params params))
  (bind sub (%(sub)s :PlayerController pc))
  (%(input)s :self sub :YawPitchDelta (%(vec2)s :X 90.0 :Y 0.0))
  (Utilities|FlowControl|Delay :Duration 1.0)
  (ActionCamera|ReleaseCameraForPlayer :PlayerController pc :Handle handle))
''' % dict(ids, folder=FOLDER)
    call('write_graph_dsl', {'graph': ograph, 'code': odsl})
    OUT['orbit_graph'] = call('read_graph_dsl', {'graph': ograph})
    unreal.BlueprintEditorLibrary.compile_blueprint(obp)
    OUT['orbit_bp_status'] = str(obp.get_editor_property('status'))
    OUT['orbit_bp_saved'] = E.save_loaded_asset(obp, False)

    # ---------- Phase 3: Blueprint using the camera effect API ----------
    fbp = ensure_bp('BP_ACFEffectTest')
    fgraph = {'refPath': FOLDER + '/BP_ACFEffectTest.BP_ACFEffectTest:EventGraph'}
    fx = {
        'recoil': find_one('PlayACFCameraRecoil', 'ActionCamera|'),
        'impulse': find_one('PlayACFCameraImpulse', 'ActionCamera|'),
        'shake': find_one('PlayACFCameraShake', 'ActionCamera|'),
        'stop': find_one('StopACFCameraEffect', 'ActionCamera|'),
        'stopall': find_one('StopAllACFCameraEffects', 'ActionCamera|'),
        'loc': find_one('GetActorLocation', 'GetActorLocation'),
    }
    OUT['effect_bp_ids'] = fx
    fdsl = '''
(event EventBeginPlay
  (bind pc (Game|GetPlayerController :PlayerIndex 0))
  (bind pawn (%(pawn)s :PlayerIndex 0))
  (bind params (%(params)s :Priority 0 :FollowTarget pawn))
  (bind handle (ActionCamera|RequestCameraForPlayer :PlayerController pc :Preset "%(folder)s/DA_ACFVerify_Orbit.DA_ACFVerify_Orbit" :Params params))
  (bind sub (%(sub)s :PlayerController pc))
  (%(recoil)s :self sub :Scale 1.0)
  (bind where (%(loc)s :self pawn))
  (%(impulse)s :self sub :SourceLocation where :Scale 1.0)
  (bind shake (%(shake)s :self sub :ShakeClass "/Script/ActionCameraFramework.ACFDebugCameraShake" :Scale 1.0))
  (Utilities|FlowControl|Delay :Duration 0.5)
  (%(stop)s :self sub :Handle shake :bImmediate false)
  (Utilities|FlowControl|Delay :Duration 0.5)
  (%(stopall)s :self sub :bImmediate true)
  (ActionCamera|ReleaseCameraForPlayer :PlayerController pc :Handle handle))
''' % dict(fx, pawn=ids['pawn'], params=ids['params'], sub=ids['sub'], folder=FOLDER)
    call('write_graph_dsl', {'graph': fgraph, 'code': fdsl})
    OUT['effect_graph'] = call('read_graph_dsl', {'graph': fgraph})
    unreal.BlueprintEditorLibrary.compile_blueprint(fbp)
    OUT['effect_bp_status'] = str(fbp.get_editor_property('status'))
    OUT['effect_bp_saved'] = E.save_loaded_asset(fbp, False)
except Exception:
    OUT['error'] = traceback.format_exc()

open(unreal.Paths.project_saved_dir() + 'ACFVerification/CreateVerificationAssets.json', 'w', encoding='utf-8').write(json.dumps(OUT, indent=1, ensure_ascii=False, default=str))
unreal.log('[ACF] COMPLETE' if 'error' not in OUT else '[ACF] FAILED')
