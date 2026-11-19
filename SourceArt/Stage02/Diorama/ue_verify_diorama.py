"""Read-only verification of the Stage 2 Stage Select registration and diorama (headless, fresh process):
  sh Tools/Verification/run_ue_py.sh C:/UnrealGames/REBoarGetch/SourceArt/Stage02/Diorama/ue_verify_diorama.py
Checks references, vertex colours, BP compile status, Data Validation and the L_Lobby entrance instance.
Writes SourceArt/Stage02/Logs/ue_verify_diorama.json. Saves nothing."""
import json, os, traceback
import unreal

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), 'Logs', 'ue_verify_diorama.json')
R = {'checks': {}}
C = R['checks']
ASSETS = ['/Game/UI/StagePreview/BP_Stage02_Diorama', '/Game/UI/StagePreview/BP_Stage01_Diorama',
          '/Game/UI/StagePreview/M_Preview_VertexColor', '/Game/UI/StagePreview/Meshes/SM_S02_Diorama_Island',
          '/Game/UI/StagePreview/Meshes/SM_S02_Diorama_Props', '/Game/UI/StagePreview/Meshes/SM_S01_Diorama_Island', '/Game/UI/StagePreview/Meshes/SM_S01_Diorama_Props',
          '/Game/DataAssets/StageConfig/DA_Stage02StageConfig', '/Game/DataAssets/StageConfig/DA_TestStageConfig',
          '/Game/BP/Lobby/BP_StageEntrance_Stage01', '/Game/BP/Widget/WBP_LobbyStageSelect']


def parts_of(path):
    cls = unreal.load_object(None, path + '.' + path.rsplit('/', 1)[1] + '_C')
    cdo = unreal.get_default_object(cls)
    out = []
    for p in cdo.get_editor_property('parts'):
        t = p.get_editor_property('transform')
        out.append({'mesh': p.get_editor_property('mesh').get_name() if p.get_editor_property('mesh') else None,
                    'mat': p.get_editor_property('material').get_name() if p.get_editor_property('material') else None,
                    'loc': [round(t.translation.x, 1), round(t.translation.y, 1), round(t.translation.z, 1)],
                    'yaw': round(t.rotation.rotator().yaw, 1)})
    return out, cdo.get_editor_property('ortho_width')


try:
    reg = unreal.AssetRegistryHelpers.get_asset_registry()
    # plugin content is not in the commandlet's registry until scanned (else soft refs into it look missing)
    reg.scan_paths_synchronous(['/ActionCameraFramework', '/RetroRender'], True)
    s2 = unreal.load_asset('/Game/DataAssets/StageConfig/DA_Stage02StageConfig')
    s1 = unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
    C['stage02_id'] = str(s2.get_editor_property('stage_id'))
    C['stage02_name'] = str(s2.get_editor_property('display_name'))
    C['stage02_level'] = s2.get_editor_property('level').get_path_name() if s2.get_editor_property('level') else None
    C['stage02_target'] = s2.get_editor_property('target_capture_count')
    C['stage02_unlock'] = str(s2.get_editor_property('unlock_condition').get_editor_property('required_cleared_stage_id'))
    C['stage02_preview'] = s2.get_editor_property('preview_actor_class').get_path_name()
    C['stage01_preview'] = s1.get_editor_property('preview_actor_class').get_path_name()
    C['stage01_level'] = s1.get_editor_property('level').get_path_name()
    gi = unreal.get_default_object(unreal.load_asset('/Game/BP/Core/BP_BoarGameInstance').generated_class())
    C['gi_catalog'] = [x.get_path_name() for x in gi.get_editor_property('stage_catalog') if x]
    ent = unreal.get_default_object(unreal.load_asset('/Game/BP/Lobby/BP_StageEntrance_Stage01').generated_class())
    C['entrance_bp_catalog'] = [x.get_path_name() for x in ent.get_editor_property('stage_catalog') if x]
    C['entrance_bp_config'] = ent.get_editor_property('stage_config').get_path_name()
    C['s2_parts'], C['s2_ortho'] = parts_of('/Game/UI/StagePreview/BP_Stage02_Diorama')
    s1parts, C['s1_ortho'] = parts_of('/Game/UI/StagePreview/BP_Stage01_Diorama')
    C['s1_part_count'] = len(s1parts)
    C['s1_last_part'] = s1parts[-1]
    sms = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.EditorStaticMeshLibrary
    for n in ('SM_S02_Diorama_Island', 'SM_S02_Diorama_Props', 'SM_S01_Diorama_Island', 'SM_S01_Diorama_Props'):
        sm = unreal.load_asset('/Game/UI/StagePreview/Meshes/' + n)
        C['nanite_' + n] = sm.get_editor_property('nanite_settings').get_editor_property('enabled')
        C['mat_' + n] = sm.get_material(0).get_name()
    # BP compile status
    for p in ('/Game/UI/StagePreview/BP_Stage02_Diorama', '/Game/UI/StagePreview/BP_Stage01_Diorama',
              '/Game/BP/Lobby/BP_StageEntrance_Stage01', '/Game/BP/Widget/WBP_LobbyStageSelect'):
        bp = unreal.load_asset(p)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
        C['compile_' + p.rsplit('/', 1)[1]] = str(bp.get_editor_property('status'))
    # Data Validation
    datas = [reg.get_asset_by_object_path(p + '.' + p.rsplit('/', 1)[1]) for p in ASSETS]
    settings = unreal.ValidateAssetsSettings()
    settings.set_editor_property('show_if_no_failures', True)
    settings.set_editor_property('validation_usecase', unreal.DataValidationUsecase.MANUAL)
    res = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem).validate_assets_with_settings(datas, settings)
    C['validation'] = str(res)
    # referencers / dependencies of the new assets
    opt = unreal.AssetRegistryDependencyOptions()
    C['deps_BP_Stage02_Diorama'] = [str(d) for d in reg.get_dependencies('/Game/UI/StagePreview/BP_Stage02_Diorama', opt) or []]
    C['refs_BP_Stage02_Diorama'] = [str(d) for d in reg.get_referencers('/Game/UI/StagePreview/BP_Stage02_Diorama', opt) or []]
    C['refs_DA_Stage02'] = [str(d) for d in reg.get_referencers('/Game/DataAssets/StageConfig/DA_Stage02StageConfig', opt) or []]
    C['pedestal_asset_exists'] = unreal.EditorAssetLibrary.does_asset_exist('/Game/UI/StagePreview/Meshes/SM_StageDiorama_Pedestal')
    # L_Lobby entrance instance (inherits the BP catalog?)
    unreal.EditorLoadingAndSavingUtils.load_map('/Game/Level/L_Lobby')
    world = unreal.EditorLevelLibrary.get_editor_world()
    assert world.get_path_name().startswith('/Game/Level/L_Lobby')
    C['lobby_entrances'] = [{'name': a.get_name(), 'catalog': [x.get_path_name() for x in a.get_editor_property('stage_catalog') if x]}
                            for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.StageEntrance)]
    R['status'] = 'COMPLETE'
except Exception:
    R['error'] = traceback.format_exc()
    unreal.log_error(R['error'])
with open(OUT, 'w', encoding='utf-8') as f:
    json.dump(R, f, indent=1, ensure_ascii=False, default=str)
unreal.log_warning('[S02DioramaVerify] ' + R.get('status', 'ERROR'))
