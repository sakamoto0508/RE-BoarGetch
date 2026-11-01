"""Creates/refreshes the RetroRender shared assets under /Game/Art/Rendering/Retro.

UnrealEditor-Cmd REBoarGetch.uproject -run=pythonscript -script=<this> -unattended -nosplash -NullRHI
  - M_RetroPost        : post process material generated from Shaders/RetroPost.hlsl (always regenerated)
  - MPC_Retro          : Material Parameter Collection for geometry features (defaults 0 = no visual change)
  - MF_RetroVertexSnap : WPO -> WPO with screen-space vertex snapping (shadow pass keeps the original)
  - MF_RetroAffine3 / MF_RetroAffine2 : true screen-affine interpolation of a vertex value
  - MF_RetroTexel      : virtual-texel snapping of UVs + gradients for SampleGrad
  - DA_RetroPresets    : preset values (reset to C++ defaults only with -RetroResetPresets or when missing)
Existing project materials/levels are NOT touched here (see PatchRetroMaterials.py).
"""
from pathlib import Path
import unreal

ROOT = '/Game/Art/Rendering/Retro'
LP_MPC = '/Game/InportAssets/LPRiverForest/Materials/Common/MPC_UnlitStrength'
HERE = Path(__file__).resolve().parent
SHADERS = HERE.parent / 'Shaders'
tools = unreal.AssetToolsHelpers.get_asset_tools()
lib = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
args = unreal.SystemLibrary.get_command_line()
lib.make_directory(ROOT)


def _backup_package(path):
    import shutil
    content = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir()))
    saved = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))
    rel = path[len('/Game/'):] + '.uasset'
    src, dst = content / rel, saved / 'RetroRender' / 'MaterialBackup' / rel
    lp = unreal.load_asset(path)
    names = {str(p.get_editor_property('parameter_name')) for p in lp.get_editor_property('scalar_parameters')} if lp else set()
    if src.exists() and not dst.exists() and 'VertexSnapStrength' not in names:
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)


def main():
    global mpc


    def log(msg):
        unreal.log('[RetroSetup] ' + msg)


    def enum_value(enum_cls, *names):
        for n in names:
            if hasattr(enum_cls, n):
                return getattr(enum_cls, n)
        raise AttributeError('%s has none of %s' % (enum_cls, names))


    def load_or_create(name, cls, factory):
        path = ROOT + '/' + name
        if lib.does_asset_exist(path):
            return unreal.load_asset(path)
        return tools.create_asset(name, ROOT, cls, factory)


    def custom_node(create, code, inputs, output_type, x, y, desc):
        node = create(unreal.MaterialExpressionCustom, x, y)
        node.set_editor_property('code', code)
        node.set_editor_property('output_type', output_type)
        node.set_editor_property('description', desc)
        items = []
        for name in inputs:
            item = unreal.CustomInput()
            item.set_editor_property('input_name', name)
            items.append(item)
        node.set_editor_property('inputs', items)
        return node


    # ---------------------------------------------------------------- MPC
    SCALARS = {'VertexSnapStrength': 0.0, 'SnapResolutionX': 160.0, 'SnapResolutionY': 120.0, 'AffineAmount': 0.0,
               'TexelSnapStrength': 0.0, 'TextureResolution': 64.0, 'TextureMipBias': 0.0,
               'SnapMaxObjectRadius': 1500.0, 'SnapFlatRatio': 0.0}
    def ensure_scalars(collection_asset, names):
        existing = {str(p.get_editor_property('parameter_name')) for p in collection_asset.get_editor_property('scalar_parameters')}
        params = list(collection_asset.get_editor_property('scalar_parameters'))
        added = []
        for name in names:
            if name not in existing:
                p = unreal.CollectionScalarParameter()
                p.set_editor_property('parameter_name', name)
                p.set_editor_property('default_value', SCALARS[name])
                params.append(p)
                added.append(name)
        if added:
            collection_asset.set_editor_property('scalar_parameters', params)
            lib.save_loaded_asset(collection_asset)
        log('%s: +%s' % (collection_asset.get_name(), added))

    mpc = load_or_create('MPC_Retro', unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew())
    ensure_scalars(mpc, list(SCALARS))
    # The LowPoly pack's materials already use 2 collections (UE limit). Its MPC_UnlitStrength (used by all of
    # them) also carries the vertex-snap scalars so MF_RetroVertexSnap_LP stays within the limit. Defaults: no effect.
    snap_scalars = ['VertexSnapStrength', 'SnapResolutionX', 'SnapResolutionY', 'SnapMaxObjectRadius', 'SnapFlatRatio']
    lp_mpc = unreal.load_asset(LP_MPC) if lib.does_asset_exist(LP_MPC) else None
    if lp_mpc:
        _backup_package(LP_MPC)  # git-ignored pack: keep the untouched file once (Saved/RetroRender/MaterialBackup)
        ensure_scalars(lp_mpc, snap_scalars)
    else:
        log('LowPoly pack not installed: MPC_UnlitStrength / MF_RetroVertexSnap_LP skipped')

    FLOAT1 = unreal.CustomMaterialOutputType.CMOT_FLOAT1
    FLOAT2 = unreal.CustomMaterialOutputType.CMOT_FLOAT2
    FLOAT3 = unreal.CustomMaterialOutputType.CMOT_FLOAT3
    FLOAT4 = unreal.CustomMaterialOutputType.CMOT_FLOAT4
    IN_V3 = enum_value(unreal.FunctionInputType, 'FUNCTION_INPUT_VECTOR3')
    IN_V2 = enum_value(unreal.FunctionInputType, 'FUNCTION_INPUT_VECTOR2')
    IN_S = enum_value(unreal.FunctionInputType, 'FUNCTION_INPUT_SCALAR')
    CAMERA_RELATIVE = enum_value(unreal.WorldPositionIncludedOffsets, 'WPT_CAMERA_RELATIVE')


    current = {'mf': None}

    def new_function(name, description):
        # Function Input/Output nodes are kept: materials calling this function link to them by GUID, and
        # recreating them would silently disconnect every caller. Everything else is rebuilt.
        mf = load_or_create(name, unreal.MaterialFunction, unreal.MaterialFunctionFactoryNew())
        current['mf'] = mf
        rebuild = [e for e in mel.get_material_function_expressions(mf)
                   if not isinstance(e, (unreal.MaterialExpressionFunctionInput, unreal.MaterialExpressionFunctionOutput))]
        if rebuild:
            unreal.RetroRenderEditorLibrary.remove_retro_expressions(mf, rebuild)
        mf.set_editor_property('description', description)
        mf.set_editor_property('expose_to_library', True)
        mf.set_editor_property('library_categories_text', [unreal.Text('RetroRender')])
        return mf, (lambda cls, x, y: mel.create_material_expression_in_function(mf, cls, x, y))


    def existing_pin(cls, prop, name):
        for e in mel.get_material_function_expressions(current['mf']):
            if isinstance(e, cls) and str(e.get_editor_property(prop)) == name:
                return e
        return None

    def fn_input(create, name, kind, x, y, preview=(0, 0, 0, 0), sort=0):
        node = existing_pin(unreal.MaterialExpressionFunctionInput, 'input_name', name) or create(unreal.MaterialExpressionFunctionInput, x, y)
        node.set_editor_property('input_name', name)
        node.set_editor_property('input_type', kind)
        node.set_editor_property('use_preview_value_as_default', True)
        value = node.get_editor_property('preview_value')
        for axis, v in zip('xyzw', preview):
            value.set_editor_property(axis, float(v))
        node.set_editor_property('preview_value', value)
        node.set_editor_property('sort_priority', sort)
        return node


    def fn_output(create, name, x, y, sort=0):
        node = existing_pin(unreal.MaterialExpressionFunctionOutput, 'output_name', name) or create(unreal.MaterialExpressionFunctionOutput, x, y)
        node.set_editor_property('output_name', name)
        node.set_editor_property('sort_priority', sort)
        return node


    def collection(create, name, x, y, source=None):
        node = create(unreal.MaterialExpressionCollectionParameter, x, y)
        node.set_editor_property('collection', source or mpc)
        node.set_editor_property('parameter_name', name)
        return node


    def connect(a, a_out, b, b_in):
        assert mel.connect_material_expressions(a, a_out, b, b_in), 'connect %s.%s -> %s.%s' % (a.get_name(), a_out, b.get_name(), b_in)


    def camera_relative_position(create, x, y):
        node = create(unreal.MaterialExpressionWorldPosition, x, y)
        node.set_editor_property('world_position_shader_offset', CAMERA_RELATIVE)
        return node


    # ---------------------------------------------------------------- MF_RetroVertexSnap
    SNAP_CODE = r'''
    float s = saturate(Strength) * (MaxRadius > 0.0 ? 1.0 - smoothstep(MaxRadius * 0.75, MaxRadius, Radius) : 1.0);
    // Flat floor-like objects (plazas, paths, water) keep their shape: height/width of the object bounds.
    float flatness = Bounds.z / max(max(Bounds.x, Bounds.y), 1.0);
    s *= FlatRatio > 0.0 ? smoothstep(FlatRatio * 0.75, FlatRatio, flatness) : 1.0;
    if (s <= 0.0) return WPO;
    float3 p = TWP + WPO;
    float4 c = mul(float4(p, 1.0), ResolvedView.TranslatedWorldToClip);
    if (!(c.w > 1.0)) return WPO;  // behind / at the near plane: leave untouched
    float2 vs = max(ResolvedView.ViewSizeAndInvSize.xy, 1.0);
    float ry = max(ResY, 1.0);
    float rx = ResX > 0.5 ? ResX : ry * vs.x / vs.y;
    float2 half_res = float2(rx, ry) * 0.5;
    float2 ndc = c.xy / c.w;
    c.xy = lerp(ndc, round(ndc * half_res) / half_res, s) * c.w;
    float4 q = mul(c, ResolvedView.ClipToTranslatedWorld);
    float3 off = q.xyz / q.w - p;
    if (any(isnan(off)) || any(isinf(off)) || dot(off, off) > 1.0e8) return WPO;
    return WPO + off;
    '''
    def build_snap(name, source):
        mf, create = new_function(name, 'PS1 vertex snapping: quantises clip-space XY of every vertex to a SnapResolution grid (%s). Chain the existing WPO through WPO. Shadow depth uses the unsnapped WPO.' % source.get_name())
        wpo_in = fn_input(create, 'WPO', IN_V3, -900, 0)
        twp = camera_relative_position(create, -900, 150)
        snap = custom_node(create, SNAP_CODE, ['WPO', 'TWP', 'Strength', 'ResX', 'ResY', 'Radius', 'MaxRadius', 'Bounds', 'FlatRatio'], FLOAT3, -400, 0, 'RetroRender vertex snap')
        connect(wpo_in, '', snap, 'WPO')
        connect(twp, '', snap, 'TWP')
        connect(create(unreal.MaterialExpressionObjectRadius, -900, 700), '', snap, 'Radius')
        connect(collection(create, 'SnapMaxObjectRadius', -900, 800, source), '', snap, 'MaxRadius')
        connect(create(unreal.MaterialExpressionObjectBounds, -900, 900), '', snap, 'Bounds')
        connect(collection(create, 'SnapFlatRatio', -900, 1000, source), '', snap, 'FlatRatio')
        for i, (pin, param) in enumerate([('Strength', 'VertexSnapStrength'), ('ResX', 'SnapResolutionX'), ('ResY', 'SnapResolutionY')]):
            connect(collection(create, param, -900, 300 + i * 120, source), '', snap, pin)
        shadow = create(unreal.MaterialExpressionShadowReplace, -100, 0)
        connect(snap, '', shadow, 'Default')
        connect(wpo_in, '', shadow, 'Shadow')
        connect(shadow, '', fn_output(create, 'WPO', 150, 0), '')
        mel.update_material_function(mf)
        lib.save_loaded_asset(mf)
        log(name + ' ok')

    build_snap('MF_RetroVertexSnap', mpc)
    if lp_mpc:
        build_snap('MF_RetroVertexSnap_LP', lp_mpc)

    # ---------------------------------------------------------------- MF_RetroAffine3 / 2
    PACK_CODE = r'''
    // Raw clip w (may be negative behind the camera): clipping interpolates in clip space, so stay linear.
    float w = mul(float4(TWP, 1.0), ResolvedView.TranslatedWorldToClip).w;
    return float4(Value * w, w);
    '''
    UNPACK_CODE = r'''
    return Packed.xyz / max(Packed.w, 1.0e-5);
    '''


    def build_affine(name, kind):
        mf, create = new_function(name, 'PS1 affine texture mapping: interpolates Value*w and w perspective-correctly and divides per pixel, '
                                        'which yields screen-linear interpolation per triangle. Blend with MPC_Retro.AffineAmount. Value must be vertex-evaluable (TexCoord / WorldPosition).')
        value = fn_input(create, 'Value', kind, -1000, 0)
        as3 = value
        if kind == IN_V2:
            as3 = create(unreal.MaterialExpressionAppendVector, -800, 100)
            zero = create(unreal.MaterialExpressionConstant, -1000, 200)
            connect(value, '', as3, 'A')
            connect(zero, '', as3, 'B')
        twp = camera_relative_position(create, -1000, 300)
        pack = custom_node(create, PACK_CODE, ['Value', 'TWP'], FLOAT4, -600, 100, 'RetroRender affine pack (VS)')
        connect(as3, '', pack, 'Value')
        connect(twp, '', pack, 'TWP')
        interp = create(unreal.MaterialExpressionVertexInterpolator, -350, 100)
        connect(pack, '', interp, mel.get_material_expression_input_names(interp)[0])
        unpack = custom_node(create, UNPACK_CODE, ['Packed'], FLOAT3, -150, 100, 'RetroRender affine unpack (PS)')
        connect(interp, mel.get_material_expression_output_names(interp)[0] if mel.get_material_expression_output_names(interp) else '', unpack, 'Packed')
        result = unpack
        if kind == IN_V2:
            mask = create(unreal.MaterialExpressionComponentMask, 50, 100)
            mask.set_editor_property('r', True); mask.set_editor_property('g', True)
            mask.set_editor_property('b', False); mask.set_editor_property('a', False)
            connect(unpack, '', mask, '')
            result = mask
        lerp = create(unreal.MaterialExpressionLinearInterpolate, 250, 0)
        connect(value, '', lerp, 'A')
        connect(result, '', lerp, 'B')
        amount = custom_node(create, 'return saturate(Amount) * (MaxRadius > 0.0 ? 1.0 - smoothstep(MaxRadius * 0.75, MaxRadius, Radius) : 1.0);',
                             ['Amount', 'Radius', 'MaxRadius'], FLOAT1, 0, 300, 'RetroRender affine amount')
        connect(collection(create, 'AffineAmount', -150, 300), '', amount, 'Amount')
        connect(create(unreal.MaterialExpressionObjectRadius, -150, 400), '', amount, 'Radius')
        connect(collection(create, 'SnapMaxObjectRadius', -150, 500), '', amount, 'MaxRadius')
        connect(amount, '', lerp, 'Alpha')
        connect(lerp, '', fn_output(create, 'Value', 450, 0), '')
        mel.update_material_function(mf)
        lib.save_loaded_asset(mf)
        log(name + ' ok')


    build_affine('MF_RetroAffine3', IN_V3)
    build_affine('MF_RetroAffine2', IN_V2)

    # ---------------------------------------------------------------- MF_RetroTexel
    TEXEL_CODE = r'''
    float r = max(Resolution * max(Scale, 0.001), 1.0);
    float2 snapped = (floor(UV * r) + 0.5) / r;
    return lerp(UV, snapped, saturate(Strength));
    '''
    BIAS_CODE = r'''
    return exp2(ResolvedView.MaterialTextureMipBias + ExtraBias);
    '''
    mf, create = new_function('MF_RetroTexel', 'Nearest-texel look on a virtual grid of TextureResolution texels per UV unit (MPC_Retro). '
                                               'Use UV for the sample coordinates and DDX/DDY with MipValueMode=Derivative so mips follow the unsnapped UV.')
    uv = fn_input(create, 'UV', IN_V2, -900, 0, sort=0)
    scale = fn_input(create, 'Scale', IN_S, -900, 150, preview=(1, 0, 0, 0), sort=1)
    texel = custom_node(create, TEXEL_CODE, ['UV', 'Resolution', 'Scale', 'Strength'], FLOAT2, -400, 0, 'RetroRender texel snap')
    connect(uv, '', texel, 'UV')
    connect(scale, '', texel, 'Scale')
    connect(collection(create, 'TextureResolution', -900, 300), '', texel, 'Resolution')
    connect(collection(create, 'TexelSnapStrength', -900, 420), '', texel, 'Strength')
    bias = custom_node(create, BIAS_CODE, ['ExtraBias'], FLOAT1, -600, 600, 'RetroRender mip bias')
    connect(collection(create, 'TextureMipBias', -900, 600), '', bias, 'ExtraBias')
    outs = []
    for i, cls in enumerate([unreal.MaterialExpressionDDX, unreal.MaterialExpressionDDY]):
        d = create(cls, -400, 250 + i * 150)
        connect(uv, '', d, '')
        m = create(unreal.MaterialExpressionMultiply, -200, 250 + i * 150)
        connect(d, '', m, 'A')
        connect(bias, '', m, 'B')
        outs.append(m)
    connect(texel, '', fn_output(create, 'UV', 0, 0, sort=0), '')
    connect(outs[0], '', fn_output(create, 'DDX', 0, 250, sort=1), '')
    connect(outs[1], '', fn_output(create, 'DDY', 0, 400, sort=2), '')
    mel.update_material_function(mf)
    lib.save_loaded_asset(mf)
    log('MF_RetroTexel ok')

    # ---------------------------------------------------------------- M_RetroPost
    material = load_or_create('M_RetroPost', unreal.Material, unreal.MaterialFactoryNew())
    unreal.RetroRenderEditorLibrary.remove_retro_expressions(material, [])
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property('blendable_priority', 100)
    create = lambda cls, x, y: mel.create_material_expression(material, cls, x, y)
    defaults = unreal.RetroRenderParameters()
    SCALAR_FIELDS = ['Pixelation', 'PixelResolutionX', 'PixelResolutionY', 'IntegerScale', 'ColorLevels', 'ColorQuantization',
                     'DitheringStrength', 'DitherMatrixSize', 'Saturation', 'Contrast', 'Brightness', 'PixelFilter', 'SoftSampling',
                     'FogStrength', 'FogStart', 'FogEnd', 'ScanlineStrength', 'PhosphorMask', 'CRTCurvature',
                     'ChromaticAberration', 'ColorBleed', 'VignetteStrength']
    code = (SHADERS / 'RetroPost.hlsl').read_text(encoding='utf-8')
    post = custom_node(create, code, ['SceneDependency', 'DepthDependency', 'FogColor'] + SCALAR_FIELDS, FLOAT3, 100, 0, 'RetroRender post')
    # Explicit SceneTexture nodes make the compiler track PostProcessInput0 / SceneDepth usage.
    for i, (sid, pin) in enumerate([('PPI_POST_PROCESS_INPUT0', 'SceneDependency'), ('PPI_SCENE_DEPTH', 'DepthDependency')]):
        scene = create(unreal.MaterialExpressionSceneTexture, -500, -300 + i * 120)
        scene.set_editor_property('scene_texture_id', getattr(unreal.SceneTextureId, sid))
        connect(scene, 'Color', post, pin)
    fog = create(unreal.MaterialExpressionVectorParameter, -500, -60)
    fog.set_editor_property('parameter_name', 'FogColor')
    fog.set_editor_property('default_value', defaults.get_editor_property('fog_color'))
    connect(fog, '', post, 'FogColor')


    def snake(name):
        out = ''
        for i, ch in enumerate(name):
            if ch.isupper() and i and (not name[i - 1].isupper() or (i + 1 < len(name) and name[i + 1].islower())):
                out += '_'
            out += ch.lower()
        return out


    for i, name in enumerate(SCALAR_FIELDS):
        param = create(unreal.MaterialExpressionScalarParameter, -500, 60 + i * 90)
        param.set_editor_property('parameter_name', name)
        param.set_editor_property('default_value', defaults.get_editor_property(snake(name)))
        connect(param, '', post, name)
    assert mel.connect_material_property(post, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    errors = mel.recompile_material(material)
    if errors:
        raise RuntimeError('M_RetroPost compile failed: ' + str(errors))
    lib.save_loaded_asset(material)
    log('M_RetroPost ok (%d scalars)' % len(SCALAR_FIELDS))

    # ---------------------------------------------------------------- DA_RetroPresets
    path = ROOT + '/DA_RetroPresets'
    if not lib.does_asset_exist(path) or '-RetroResetPresets' in args:
        data = unreal.load_asset(path) if lib.does_asset_exist(path) else None
        if data is None:
            factory = unreal.DataAssetFactory()
            factory.set_editor_property('data_asset_class', unreal.RetroRenderPresetData)
            data = tools.create_asset('DA_RetroPresets', ROOT, unreal.RetroRenderPresetData, factory)
        cdo = unreal.get_default_object(unreal.RetroRenderPresetData)
        data.set_editor_property('presets', cdo.get_editor_property('presets'))
        lib.save_loaded_asset(data)
        log('DA_RetroPresets reset to C++ defaults')

    # Old single-pass material replaced by M_RetroPost.
    if lib.does_asset_exist(ROOT + '/M_RetroCompare') and '-RetroDeleteLegacy' in args:
        lib.delete_asset(ROOT + '/M_RetroCompare')
        log('Deleted legacy M_RetroCompare')
    log('COMPLETE')


# Run after editor start-up has finished: objects loaded while the engine is still initialising are
# permanent (rooted), and rebuilding a graph deletes its old expressions.
_state = {'ticks': 0, 'handle': None}


def _tick(delta):
    _state['ticks'] += 1
    if _state['ticks'] < 30:
        return
    unreal.unregister_slate_post_tick_callback(_state['handle'])
    try:
        main()
    except Exception:
        import traceback
        unreal.log_error('[RetroSetup] ' + traceback.format_exc())
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    if '-RetroQuit' in args:
        unreal.SystemLibrary.quit_editor()


if unreal.SystemLibrary.is_unattended() and '-run=pythonscript' in args.lower():
    main()  # commandlet: first-time creation only (rebuilding existing graphs needs the editor)
else:
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    _state['handle'] = unreal.register_slate_post_tick_callback(_tick)
