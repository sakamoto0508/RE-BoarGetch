"""Navmesh diagnostics for the redesign level (headless): projects probe points onto the saved navmesh."""
import sys, os, unreal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import s01r_layout as L

les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
les.load_level('/Game/Level/L_Stage01')
world = unreal.EditorLevelLibrary.get_editor_world()
nav = unreal.NavigationSystemV1.get_navigation_system(world)
unreal.log('[S01] navsys=%s %s' % (nav, [m for m in dir(unreal.NavigationSystemV1) if 'project' in m or 'random' in m]))
for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh):
    for p in ['agent_radius', 'agent_height', 'agent_max_slope', 'agent_max_step_height', 'cell_size', 'cell_height', 'runtime_generation']:
        try:
            unreal.log('[S01] recast %s=%s' % (p, a.get_editor_property(p)))
        except Exception as e:
            unreal.log('[S01] recast %s ? %s' % (p, e))
tz = L.height(*L.TOWER)
probes = [('bridge x=%d' % x, x, L.BRIDGE[1], 60) for x in range(-2300, -3001, -100)]
probes += [('ramp x=%d' % x, x, L.TOWER[1], tz + 150 * (1 - (2960 - x) / 480) + 20) for x in range(2500, 2961, 80)]
probes += [('deck', L.TOWER[0] + 60, L.TOWER[1], tz + 170), ('pool bank', -2520, -2950, 20), ('pool bank2', -2400, -2850, 20)]
for name, x, y, z in probes:
    p = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(x, y, z), None, None, unreal.Vector(40, 40, 120)); ok = p is not None and (abs(p.x) + abs(p.y) + abs(p.z)) > 0
    unreal.log('[S01] %-12s target z=%.0f -> %s %s' % (name, z, ok, [round(p.x), round(p.y), round(p.z)] if ok else None))
unreal.log('[S01] COMPLETE')
rows = []
for y in range(1600, -3700, -200):
    row = ''
    for x in range(-3700, 3701, 150):
        z = L.height(x, y)
        p = unreal.NavigationSystemV1.project_point_to_navigation(world, unreal.Vector(x, y, z + 60), None, None, unreal.Vector(60, 60, 150))
        ok = p is not None and (abs(p.x) + abs(p.y) + abs(p.z)) > 0
        row += ('.' if abs(p.z - z) < 40 else '^') if ok else '#'
    rows.append('%6d %s' % (y, row))
unreal.log('[S01] navmap (x -3700..3700 step 150, y top=+1600)\n' + '\n'.join(rows))
