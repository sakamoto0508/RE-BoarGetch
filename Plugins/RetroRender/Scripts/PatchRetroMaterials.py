"""Adds (or removes) RetroRender geometry processing to project parent materials.

Patch : UnrealEditor-Cmd REBoarGetch.uproject -run=pythonscript -script=<this> -unattended -nosplash -NullRHI
Revert: same with -RetroUnpatch

Every inserted node is tagged with Description "RetroRender..." and driven by MPC_Retro whose defaults are 0,
so without an active URetroRenderComponent the materials output the same values as before:
  - WPO        : MF_RetroVertexSnap(existing WPO)        -> returns the input unchanged at strength 0
  - affine     : WorldPosition/TexCoord -> MF_RetroAffine -> lerp(original, affine, 0) == original
  - textures   : UVs -> MF_RetroTexel, MipValueMode=Derivative with gradients of the unsnapped UV
                 (same mip selection as before, including the view mip bias used by TSR)
Engine-content materials (Interchange FBX Phong used by the Player/Boar) are duplicated into
/Game/Art/Rendering/Retro/Materials and only the project's material instances are reparented (recorded for revert).
Writes Plugins/RetroRender/Scripts/PatchRetroMaterials.json.
"""
from pathlib import Path
import json
import unreal

HERE = Path(__file__).resolve().parent
RECORD = HERE / 'PatchRetroMaterials.json'
ROOT = '/Game/Art/Rendering/Retro'
TAG = 'RetroRender'
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
args = unreal.SystemLibrary.get_command_line()
UNPATCH = '-RetroUnpatch' in args
CAN_DELETE = True  # RemoveRetroExpressions works in commandlets too
# Removing "unused" tagged nodes is opt-in: if a function's outputs were ever re-created, live nodes look unused.
PURGE = '-RetroPurge' in args
WPO = unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET

FBX_PHONG = '/InterchangeAssets/Materials/FBXLegacyPhongSurfaceMaterial'
FBX_COPY = ROOT + '/Materials/M_RetroFBXPhong'
# path -> features. 'world': affine on WorldPosition (world-aligned ground textures), 'uv': affine on TexCoord.
TARGETS = {
    '/Game/InportAssets/LevelPrototyping/Materials/M_FlatCol': {'snap'},
    # LowPoly pack: its materials already use MPC_GlobalWind + MPC_UnlitStrength (UE allows 2 per material), so they
    # use MF_RetroVertexSnap_LP, which reads the same scalars from MPC_UnlitStrength ('snap_lp').
    '/Game/InportAssets/LPRiverForest/Materials/M_LowPoly': {'snap_lp'},
    '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyGrass': {'snap_lp'},
    # Water samples its texture in the vertex shader (waves): no affine/texel (interpolators and DDX are pixel-only).
    '/Game/InportAssets/LPRiverForest/Materials/M_LowPolyWater': {'snap_lp'},
    '/Game/Stage01/Ground/Materials/M_Grass_LowPoly_World': {'snap', 'texel', 'world'},
    '/Game/Stage01/Ground/Materials/M_Ground_LowPoly_World': {'snap', 'texel', 'world'},
    '/Game/BP/Cage/Materials/M_Cage_White': {'snap'},
    '/Game/BP/Cage/Materials/M_Cage_Yellow': {'snap'},
    '/Game/BP/Cage/Materials/M_Cage_Glow': {'snap'},
    '/Game/BP/Cage/Materials/M_Cage_Navy': {'snap'},
    FBX_COPY: {'snap', 'texel', 'uv'},
}
MF = {name: unreal.load_asset(ROOT + '/' + name) for name in
      ['MF_RetroVertexSnap', 'MF_RetroVertexSnap_LP', 'MF_RetroAffine3', 'MF_RetroAffine2', 'MF_RetroTexel']}
assert all(MF.values()), 'Run SetupRetroAssets.py first'
report = {'patched': {}, 'reparented': [], 'errors': {}}


def log(msg):
    unreal.log('[RetroPatch] ' + msg)


def tagged(e):
    return str(e.get_editor_property('desc')).startswith(TAG)


def tag(e, what):
    e.set_editor_property('desc', '%s %s' % (TAG, what))
    return e


def call(m, name, x, y, what):
    node = mel.create_material_expression(m, unreal.MaterialExpressionMaterialFunctionCall, x, y)
    node.set_editor_property('material_function', MF[name])
    return tag(node, what)


def connect(a, a_out, b, b_in):
    if not mel.connect_material_expressions(a, a_out, b, b_in):
        raise RuntimeError('connect %s.%s -> %s.%s (inputs %s)' % (a.get_name(), a_out, b.get_name(), b_in,
                                                                   mel.get_material_expression_input_names(b)))


def pins(m, e):
    """[(input name, source expression, source output name)] for one expression."""
    names = mel.get_material_expression_input_names(e)
    sources = mel.get_inputs_for_material_expression(m, e)
    result = []
    for name, src in zip(names, sources):
        out = ''
        if src:
            r = mel.get_input_node_output_name_for_material_expression(e, src)
            out = r[-1] if isinstance(r, tuple) else r
        result.append((name, src, out if src else None))
    return result


def same(a, b):
    # Python wrappers of the same UObject are not guaranteed to compare equal: compare object paths.
    return a is not None and b is not None and a.get_path_name() == b.get_path_name()


def consumers(m, node):
    """[(expression, input name, output name of node)] reading from node."""
    found = []
    for e in mel.get_material_expressions(m):
        for name, src, out in pins(m, e):
            if same(src, node):
                found.append((e, name, out))
    return found


def pos(e):
    return mel.get_material_expression_node_position(e)


def is_patched(m):
    # Orphaned tagged nodes left by a commandlet revert do not count.
    if (mel.get_material_property_input_node(m, WPO) or None) and tagged(mel.get_material_property_input_node(m, WPO)):
        return True
    for e in mel.get_material_expressions(m):
        if any(src is not None and tagged(src) and not tagged(e) for _, src, _ in pins(m, e)):
            return True
    return False


def purge_orphans(m):
    """Removes tagged nodes nothing reads from (left by an older revert), repeating until stable."""
    removed = 0
    while True:
        wpo = mel.get_material_property_input_node(m, WPO)
        orphans = [e for e in mel.get_material_expressions(m) if tagged(e) and not same(e, wpo) and not consumers(m, e)]
        if not orphans:
            return removed
        removed += unreal.RetroRenderEditorLibrary.remove_retro_expressions(m, orphans)


BACKUP = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()) + 'RetroRender/MaterialBackup'


def backup(path):
    """File copy of the untouched package (first time only); InportAssets is not in git."""
    import shutil
    if not path.startswith('/Game/'):
        return
    src = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())) / (path[len('/Game/'):] + '.uasset')
    dst = Path(BACKUP) / (path[len('/Game/'):] + '.uasset')
    if src.exists() and not dst.exists():
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def snap_function(features):
    return 'MF_RetroVertexSnap_LP' if 'snap_lp' in features else 'MF_RetroVertexSnap'


def patch(path, features):
    m = unreal.load_asset(path)
    wanted = MF[snap_function(features)]
    for e in mel.get_material_expressions(m):
        if tagged(e) and isinstance(e, unreal.MaterialExpressionMaterialFunctionCall):
            f = e.get_editor_property('material_function')
            if f and f.get_name().startswith('MF_RetroVertexSnap') and f.get_name() != wanted.get_name():
                e.set_editor_property('material_function', wanted)
                errors = mel.recompile_material(m)
                if errors:
                    raise RuntimeError('compile: %s' % errors)
                lib.save_loaded_asset(m)
                log('switched %s -> %s' % (path, wanted.get_name()))
    if is_patched(m):
        log('already patched: ' + path)
        return
    backup(path)
    if m.get_editor_property('use_material_attributes'):
        raise RuntimeError('material attributes graph not supported')
    done = []
    if 'snap' in features or 'snap_lp' in features:
        src = mel.get_material_property_input_node(m, WPO)
        out = mel.get_material_property_input_node_output_name(m, WPO) if src else ''
        x, y = pos(src) if src else (-400, 600)
        snap = call(m, snap_function(features), x + 250, y + 150, 'vertex snap')
        if src:
            connect(src, out, snap, 'WPO')
        assert mel.connect_material_property(snap, 'WPO', WPO)
        done.append('vertex snap' + (' (chained existing WPO)' if src else ''))
    if 'world' in features:
        for wp in [e for e in mel.get_material_expressions(m) if isinstance(e, unreal.MaterialExpressionWorldPosition) and not tagged(e)]:
            users = consumers(m, wp)
            x, y = pos(wp)
            aff = call(m, 'MF_RetroAffine3', x + 200, y + 120, 'affine world')
            connect(wp, '', aff, 'Value')
            for e, name, out in users:
                connect(aff, 'Value', e, name)
            done.append('affine world position (%d users)' % len(users))
    if 'uv' in features:
        groups = {}
        for tc in [e for e in mel.get_material_expressions(m) if isinstance(e, unreal.MaterialExpressionTextureCoordinate)]:
            key = (tc.get_editor_property('coordinate_index'), tc.get_editor_property('u_tiling'), tc.get_editor_property('v_tiling'),
                   tc.get_editor_property('un_mirror_u'), tc.get_editor_property('un_mirror_v'))
            groups.setdefault(key, []).append(tc)
        for key, nodes in groups.items():
            # One interpolator per distinct UV set: identical TexCoord nodes share the affine node.
            x, y = pos(nodes[0])
            aff = call(m, 'MF_RetroAffine2', x + 150, y - 80, 'affine uv')
            connect(nodes[0], '', aff, 'Value')
            count = 0
            for tc in nodes:
                for e, name, out in consumers(m, tc):
                    if not same(e, aff):
                        connect(aff, 'Value', e, name)
                        count += 1
            done.append('affine uv%d (%d users)' % (key[0], count))
    if 'texel' in features:
        scale = None
        samples = [e for e in mel.get_material_expressions(m)
                   if isinstance(e, unreal.MaterialExpressionTextureSample) and 'TextureObject' not in e.get_class().get_name() and not tagged(e)]
        for s in samples:
            if s.get_editor_property('mip_value_mode') != unreal.TextureMipValueMode.TMVM_NONE:
                continue
            uv_pin = [p for p in pins(m, s) if p[0] in ('UVs', 'Coordinates')]
            if not uv_pin:
                continue
            name, src, out = uv_pin[0]
            x, y = pos(s)
            if src is None:
                src = tag(mel.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, x - 500, y + 60), 'texcoord')
                out = ''
            if scale is None:
                scale = tag(mel.create_material_expression(m, unreal.MaterialExpressionScalarParameter, x - 500, y + 160), 'texel scale')
                scale.set_editor_property('parameter_name', 'RetroTexelScale')
                scale.set_editor_property('default_value', 1.0)
                scale.set_editor_property('group', 'Retro')
            texel = call(m, 'MF_RetroTexel', x - 250, y + 60, 'texel')
            connect(src, out, texel, 'UV')
            connect(scale, '', texel, 'Scale')
            s.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_DERIVATIVE)
            names = mel.get_material_expression_input_names(s)
            ddx = [n for n in names if n.upper().startswith('DDX')]
            ddy = [n for n in names if n.upper().startswith('DDY')]
            connect(texel, 'UV', s, name)
            connect(texel, 'DDX', s, ddx[0])
            connect(texel, 'DDY', s, ddy[0])
        done.append('texel snap (%d samples)' % len(samples))
    errors = mel.recompile_material(m)
    if errors:
        raise RuntimeError('compile: %s' % errors)
    lib.save_loaded_asset(m)
    report['patched'][path] = done
    log('patched %s: %s' % (path, '; '.join(done)))


def unpatch(path):
    if not lib.does_asset_exist(path):
        return
    m = unreal.load_asset(path)
    nodes = [e for e in mel.get_material_expressions(m) if tagged(e)]
    if not nodes or not is_patched(m):
        return
    for node in nodes:
        if not isinstance(node, unreal.MaterialExpressionMaterialFunctionCall):
            continue
        fn = node.get_editor_property('material_function').get_name()
        source = [p for p in pins(m, node) if p[0] in ('WPO', 'Value', 'UV')]
        src, out = (source[0][1], source[0][2]) if source else (None, None)
        if src is not None and tagged(src):
            src = None  # TexCoord created by the patch: the sample falls back to its default UVs
        if fn.startswith('MF_RetroVertexSnap'):
            if src:
                assert mel.connect_material_property(src, out, WPO)
            else:
                mel.disconnect_material_property(m, WPO)
        for e, name, my_out in consumers(m, node):
            if fn == 'MF_RetroTexel':
                if isinstance(e, unreal.MaterialExpressionTextureSample):
                    e.set_editor_property('mip_value_mode', unreal.TextureMipValueMode.TMVM_NONE)
                for pin in ('UVs', 'Coordinates'):
                    if pin in mel.get_material_expression_input_names(e):
                        if src:
                            connect(src, out, e, pin)
                        else:
                            mel.disconnect_material_expressions(e, pin)
            elif src:
                connect(src, out, e, name)
            else:
                mel.disconnect_material_expressions(e, name)
    # Deleting expressions asserts inside the pythonscript commandlet; there the disconnected nodes stay as
    # unused (never compiled) tagged nodes and are purged by the next editor-mode run.
    purge_orphans(m)
    errors = mel.recompile_material(m)
    if errors:
        raise RuntimeError('compile: %s' % errors)
    lib.save_loaded_asset(m)
    report['patched'][path] = ['reverted']
    log('reverted ' + path)


def project_instances_of(parent_path):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    flt = unreal.ARFilter(class_paths=[unreal.TopLevelAssetPath('/Script/Engine', 'MaterialInstanceConstant')],
                          package_paths=['/Game'], recursive_paths=True)
    found = []
    for data in registry.get_assets(flt):
        mi = data.get_asset()
        parent = mi.get_editor_property('parent')
        if parent and parent.get_path_name().split('.')[0] == parent_path:
            found.append(mi)
    return found


if UNPATCH:
    previous = json.loads(RECORD.read_text(encoding='utf-8')) if RECORD.exists() else {}
    original = unreal.load_asset(FBX_PHONG)
    for mi_path in previous.get('reparented', []):
        mi = unreal.load_asset(mi_path)
        if mi:
            mel.set_material_instance_parent(mi, original)
            lib.save_loaded_asset(mi)
            report['reparented'].append(mi_path)
    for path in TARGETS:
        if not lib.does_asset_exist(path):
            continue
        try:
            unpatch(path)
        except Exception as e:
            report['errors'][path] = str(e)
            unreal.log_error('[RetroPatch] %s: %s' % (path, e))
    report['mode'] = 'unpatch'
else:
    if not lib.does_asset_exist(FBX_COPY):
        lib.make_directory(ROOT + '/Materials')
        tools = unreal.AssetToolsHelpers.get_asset_tools()
        assert tools.duplicate_asset('M_RetroFBXPhong', ROOT + '/Materials', unreal.load_asset(FBX_PHONG)), 'duplicate FBX phong'
        lib.save_asset(FBX_COPY)
        log('duplicated %s -> %s' % (FBX_PHONG, FBX_COPY))
    for path, features in TARGETS.items():
        if not lib.does_asset_exist(path):
            # InportAssets is git-ignored: a checkout without that pack simply skips its materials.
            report.setdefault('missing', []).append(path)
            log('missing (pack not installed), skipped: ' + path)
            continue
        try:
            if PURGE and lib.does_asset_exist(path):
                m = unreal.load_asset(path)
                purged = purge_orphans(m)
                if purged:
                    mel.recompile_material(m)
                    lib.save_loaded_asset(m)
                    log('purged %d orphan nodes in %s' % (purged, path))
            patch(path, features)
        except Exception as e:
            report['errors'][path] = str(e)
            unreal.log_error('[RetroPatch] %s: %s' % (path, e))
    copy = unreal.load_asset(FBX_COPY)
    previous = json.loads(RECORD.read_text(encoding='utf-8')) if RECORD.exists() else {}
    candidates = {mi.get_path_name(): mi for mi in project_instances_of(FBX_PHONG)}
    for mi_path in previous.get('reparented', []):  # also MIs whose parent copy was re-created
        mi = unreal.load_asset(mi_path)
        if mi:
            candidates[mi.get_path_name()] = mi
    for mi in candidates.values():
        parent = mi.get_editor_property('parent')
        if parent and parent.get_path_name().split('.')[0] == FBX_PHONG:
            backup(mi.get_path_name().split('.')[0])  # pristine copy before the first reparent
        mel.set_material_instance_parent(mi, copy)
        lib.save_loaded_asset(mi)
        report['reparented'].append(mi.get_path_name().split('.')[0])
    log('reparented %d FBX material instances' % len(report['reparented']))
    report['mode'] = 'patch'
    previous = json.loads(RECORD.read_text(encoding='utf-8')) if RECORD.exists() else {}
    report['reparented'] = sorted(set(report['reparented']) | set(previous.get('reparented', [])))
RECORD.write_text(json.dumps(report, ensure_ascii=False, indent=1), encoding='utf-8')
log('COMPLETE errors=%d' % len(report['errors']))

if '-RetroQuit' in unreal.SystemLibrary.get_command_line():
    unreal.SystemLibrary.quit_editor()
