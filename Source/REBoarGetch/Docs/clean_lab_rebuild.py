import unreal,json,re,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'build_facility_levels.py').read_text().split('def run():')[0])
out={'hidden':[],'retained':[],'created':[]}
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_name()=='L_GadgetLab' and not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
text=(ROOT/'lab_cleanup_before.t3d').read_text()
blocks=re.findall(r'      Begin Actor .*?      End Actor',text,re.S)
aa=actors()
protected={a.get_path_name():str(a.get_actor_transform()) for a in aa.values() if a.get_class().get_name() in ['HubPortal','GadgetTestTarget','PlayerStart'] or a.get_actor_label() in ['LabResearchStation','GadgetTestClosedBarrier'] or 'DataPortal' in a.get_actor_label() or 'Neon_Portal_Frame' in a.get_actor_label()}
def candidate(n,a):
 return a.get_class().get_name() in ['StaticMeshActor','TextRenderActor'] and (n.startswith(('Equipment_','DisplayStructure_','HubStructure_','LabHubShell_','Quality_','Terminal','MonitorEdge','TestPlatform','TestEdge','TestRamp','TargetBase')) or n in ['DisplayStationBackdrop','DisplayCollectionBackdrop','HubCollectionFace','TestTitle','WallCapY-1','WallCapY1','WallLightY-1','WallLightY1'] or (n.startswith('Architecture_') and isinstance(a,unreal.StaticMeshActor)))
try:
 for n,a in aa.items():
  if not candidate(n,a):continue
  # All serialized actor references, including Blueprint instances, attachments and editor groups.
  own=[b for b in blocks if re.search(r' Name='+re.escape(a.get_name())+r' ',b.splitlines()[0])]
  refs=[b.splitlines()[0] for b in blocks if b not in own and re.search(r"[\.'\"]"+re.escape(a.get_name())+r"[\.'\"]",b)]
  collision=any(c.get_collision_enabled()!=unreal.CollisionEnabled.NO_COLLISION for c in a.get_components_by_class(unreal.PrimitiveComponent))
  if refs or collision or list(a.tags):out['retained'].append({'actor':n,'refs':refs,'collision':collision});continue
  a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True);out['hidden'].append(n)
 # Robust built-in cube architecture instead of overlapping procedural wall generations.
 def part(n,p,s,m):
  a=mesh('CleanLab_'+n,p,s,m);a.static_mesh_component.set_cast_shadow(False);a.set_folder_path('Art/CleanLab');out['created'].append(a.get_actor_label());return a
 mats['Ceiling']=unreal.load_asset('/Game/Lobby/Materials/M_Architecture_CeilingWhite')
 mats['Light']=unreal.load_asset('/Game/Lobby/Materials/M_Architecture_SoftWhite')
 for side in [-1,1]:
  part('WallX'+str(side),(side*1204,0,490),(24,1810,540),'Ivory')
  part('WallY'+str(side),(0,side*904,490),(2400,24,540),'Ivory')
  part('SkirtX'+str(side),(side*1180,0,24),(20,1780,48),'Navy')
  part('SkirtY'+str(side),(0,side*880,24),(2380,20,48),'Navy')
  part('CorniceX'+str(side),(side*1180,0,725),(35,1800,45),'Navy')
  part('CorniceY'+str(side),(0,side*880,725),(2380,35,45),'Navy')
 # restrained continuous backing; no towering black boxes
 part('CollectionRail',(0,873,225),(1830,12,360),'Ice')
 part('CollectionHeader',(0,858,454),(1840,30,24),'Navy')
 for i,x in enumerate([-720,-360,0,360,720]):
  part('Bay'+str(i),(x,858,219),(285,12,322),'Navy')
  part('BayFoot'+str(i),(x,835,10),(302,64,18),'Ivory')
  part('BayLine'+str(i),(x,849,394),(250,5,5),'Cyan')
  part('BayIndex'+str(i),(x-130,846,370),(10,7,24),'Yellow')
 # Broad floor panels and flat, non-colliding guides.
 part('StationInset',(0,-80,1.2),(650,610,1),'Navy')
 part('StationFloor',(0,-80,1.8),(622,582,.4),'Ice')
 part('CollectionFloor',(0,650,1.2),(1880,330,1),'Ice')
 for x in [-760,760]:
  for y in [-520,340]:part('Floor'+str(x)+'_'+str(y),(x,y,1.1),(620,510,.5),'Ivory')
 for side in [-1,1]:
  for y in [-138,138]:part('Guide'+str(side)+'_'+str(y),(side*655,y,2.1),(650,6,.4),'Cyan')
  part('Junction'+str(side),(side*365,0,2.2),(10,70,.4),'Yellow')
 # Six sealed ceiling panels, two cross beams, restrained lights.
 for x in [-800,0,800]:
  for y in [-450,450]:
   part('Ceiling'+str(x)+'_'+str(y),(x,y,760),(800,900,30),'Ceiling')
   if x:
    part('LightFrame'+str(x)+'_'+str(y),(x,y,736),(350,125,16),'Navy')
    part('Light'+str(x)+'_'+str(y),(x,y,726),(310,85,5),'Light')
 for x in [-400,400]:part('Beam'+str(x),(x,0,728),(34,1800,40),'Navy')
 for y in [-385,225]:part('StationCeilingFrame'+str(y),(0,y,721),(650,24,22),'Navy');part('StationCeilingLight'+str(y),(0,y,708),(580,7,4),'Cyan')
 # Preserve live Net pod; add only unassigned display-only copies of the official mesh.
 pm=unreal.load_asset('/Game/InportAssets/GadgetPod');assert pm
 pod=aa['CollectionPod_Net'];pod.set_actor_hidden_in_game(False)
 for c in pod.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(True)
 for i,x in enumerate([-720,-360,360,720]):
  n='CollectionPod_Reserve_'+str(i+1);assert n not in actors()
  a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(x+26,620,0),unreal.Rotator());a.set_actor_label(n);a.set_actor_scale3d(unreal.Vector(.8,.8,.8));a.set_folder_path('Facility/CollectionReserve')
  c=a.static_mesh_component;c.set_static_mesh(pm);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('can_ever_affect_navigation',False)
  out['created'].append(n)
 # Keep text legible above equipment without changing any UI or binding.
 sign('HubCollectionTitle','GADGET COLLECTION',(0,790,490),-90,30)
 assert all(str(a.get_actor_transform())==protected[a.get_path_name()] for a in EA.get_all_level_actors() if a.get_path_name() in protected)
 out['protected_transforms_unchanged']=True
 assert LS.save_current_level();out['saved']=True
except Exception:out['error']=traceback.format_exc()
(ROOT/'clean_lab_rebuild.json').write_text(json.dumps(out,indent=2))

