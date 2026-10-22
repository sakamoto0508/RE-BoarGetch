import unreal
B = '/Game/InportAssets/LPRiverForest/Meshes/'
for n in ['Rocks/SM_LPRock0%d' % i for i in range(1, 9)] + ['Rocks/SM_LPRockPebble0%d' % i for i in (1, 2, 3)] + ['Rocks/SM_LPRockSlab01', 'Trees/SM_LPTreeLog01', 'Cliffs/SM_LPCliffRock01', 'Cliffs/SM_LPCliffRock02', 'Cliffs/SM_LPCliffRock03', 'Trees/SM_LPTree01', 'Trees/SM_LPPineTree01', 'Plants/SM_LPBush01', 'HeroProps/SM_LPFence01']:
    m = unreal.load_asset(B + n)
    b = m.get_bounding_box()
    unreal.log('[S01] %-28s size=%s col=%s' % (n, [round(b.max.x - b.min.x), round(b.max.y - b.min.y), round(b.max.z - b.min.z)], m.get_editor_property('body_setup').get_editor_property('collision_trace_flag')))
unreal.log('[S01] COMPLETE')
