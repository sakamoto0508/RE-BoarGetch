$sys=@{refPath='/Game/REBoarGetch/Art/VFX/Capture/NS_CaptureSuccess.NS_CaptureSuccess'}
function Ref($e,$s='',$m='',$i=@()){return @{system=$sys;emitterName=$e;scriptName=$s;moduleName=$m;rendererIndex=-1;inputNameStack=$i}}
function Call($name,$callArgs){$j=@{toolset_name='NiagaraToolsets.NiagaraToolset_System';tool_name=$name;arguments=$callArgs}|ConvertTo-Json -Depth 20 -Compress; ./Docs/invoke_unreal_mcp.ps1 -Name call_tool -ArgsJson $j}
Call SetEmitterData @{emitter=(Ref 'Glow_Base');emitterData=@{propertyValues='{"bIsEnabled":false}'}}
Call SetEmitterData @{emitter=(Ref 'Spores');emitterData=@{propertyValues='{"SimTarget":"CPUSim"}'}}
foreach($e in @('Bands','Spores')) {
 Call SetStackInputData @{stackInputRef=(Ref $e 'ParticleSpawnScript' 'InitializeParticle' @('AdjustHue'));inputData=@{struct=@{refPath='/Script/Niagara.NiagaraBool'};value=@{value=0}}}
}
Call SetStackInputData @{stackInputRef=(Ref 'Spores' 'EmitterUpdateScript' 'SpawnRate' @('SpawnRate'));inputData=@{struct=@{refPath='/Script/Niagara.NiagaraFloat'};value=@{value=24}}}
Call SetStackInputData @{stackInputRef=(Ref 'Bands' 'ParticleSpawnScript' 'InitializeParticle' @('Lifetime'));inputData=@{struct=@{refPath='/Script/Niagara.NiagaraFloat'};value=@{value=.28}}}
Call SetStackInputData @{stackInputRef=(Ref 'Bands' 'EmitterUpdateScript' 'SpawnRate' @('SpawnRate'));inputData=@{struct=@{refPath='/Script/Niagara.NiagaraFloat'};value=@{value=8}}}
Call GetSystemCompileState @{system=$sys}

