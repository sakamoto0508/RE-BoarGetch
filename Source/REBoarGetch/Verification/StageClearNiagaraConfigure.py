# exec'd by StageClearNiagara.py configure  (uses call/ref/SYS from there)
# Star burst for the victory pose: star sprites + star-headed ribbon trails, colour from User.Color (C++ spawns yellow/white/cyan).
STAR_MAT = {'refPath': '/Game/REBoarGetch/Art/VFX/StageClear/M_StageClearStar.M_StageClearStar'}
LC = '/Script/CoreUObject.LinearColor'


def f(v):
    return {'struct': {'refPath': '/Script/Niagara.NiagaraFloat'}, 'value': {'value': v}}


def i(v):
    return {'struct': {'refPath': '/Script/Niagara.NiagaraInt32'}, 'value': {'value': v}}


def vec3(x, y, z):
    return {'struct': {'refPath': '/Script/CoreUObject.Vector3f'}, 'value': {'x': x, 'y': y, 'z': z}}


def linked(name, typ):
    return {'struct': {'refPath': '/Script/NiagaraEditor.NiagaraExt_StackInputData_Linked'},
            'value': {'linkedVariable': {'name': name, 'type': {'classStructOrEnum': {'refPath': typ}}}}}


def setin(emitter, script, module, name, data):
    call('SetStackInputData', {'stackInputRef': ref(emitter, script, module, [name]), 'inputData': data})


call('AddUserVariables', {'system': SYS, 'variablesToAdd': [
    {'name': 'User.Color', 'description': 'Star colour (set per spawned instance by UStageClearPresentationComponent).',
     'type': {'classStructOrEnum': {'refPath': LC}},
     'defaultValue': {'struct': {'refPath': LC}, 'value': {'r': 1, 'g': .85, 'b': .1, 'a': 1}}}]})

for em, count, smin, smax, lmin, lmax in (('OmnidirectionalBurst', 16, 14., 30., .9, 1.6), ('Ribbon_Trail_Leader', 7, 20., 30., 1.0, 1.5)):
    setin(em, 'EmitterSpawnScript' if False else 'EmitterUpdateScript', 'SpawnBurst_Instantaneous', 'Spawn Count', i(count))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Color', linked('User.Color', LC))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Min', f(smin))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Uniform Sprite Size Max', f(smax))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Min', f(lmin))
    setin(em, 'ParticleSpawnScript', 'InitializeParticle', 'Lifetime Max', f(lmax))
    setin(em, 'ParticleSpawnScript', 'ShapeLocation', 'Sphere Radius', f(12.))
    setin(em, 'ParticleUpdateScript', 'GravityForce', 'Gravity', vec3(0, 0, -260))
    call('SetRendererData', {'renderer': ref(em, renderer=0), 'rendererData': {'propertyValues': json.dumps({'Material': STAR_MAT, 'bCastShadows': False})}})
# Ribbon trails inherit the leader colour through the location event; also tint the follower's own init colour.
setin('RibbonTrailFollower', 'ParticleSpawnScript', 'InitializeParticle', 'Color', linked('User.Color', LC))
call('GetSystemCompileState', {'system': SYS})
call('GetStackIssues', {'system': SYS})
