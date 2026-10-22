import unreal
c=unreal.load_asset('/Game/DataAssets/StageConfig/DA_TestStageConfig')
unreal.log('[S01] readback level=%s' % c.get_editor_property('level').get_path_name())
unreal.log('[S01] COMPLETE')
