import unreal,json,traceback
from pathlib import Path
R=Path(__file__).parent
exec((R/'build_facility_levels.py').read_text().split('def run():')[0])
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_name()=='L_GadgetLab' and not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
a=actors();out={}
keep={n:str(o.get_actor_transform()) for n,o in a.items() if o.get_class().get_name() in ['HubPortal','PlayerStart'] or n in ['LabResearchStation','GadgetTestClosedBarrier'] or 'DataPortal' in n}
try:
 for n,o in a.items():
  if n.startswith('Gallery_') or n.startswith('GalleryTitle'):
   o.set_actor_hidden_in_game(True);o.set_is_temporarily_hidden_in_editor(True)
   for c in o.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
 pm=unreal.load_asset('/Game/InportAssets/GadgetPod');assert pm
 names=['CollectionPod_Net']+['CollectionPod_Reserve_'+str(i) for i in range(1,8)]
 rows=[]
 for i,n in enumerate(names):
  wall=1 if i<4 else -1;x=[-750,-450,450,750][i%4];yaw=0 if wall>0 else 180
  o=a.get(n)
  if not o:
   o=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(),unreal.Rotator());o.set_actor_label(n);o.set_folder_path('Facility/CollectionReserve');c=o.static_mesh_component;c.set_static_mesh(pm);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('can_ever_affect_navigation',False)
  o.set_actor_location(unreal.Vector(x+wall*26.8,wall*680,0),False,False);o.set_actor_rotation(unreal.Rotator(yaw=yaw),False);o.set_actor_scale3d(unreal.Vector(.8,.8,.8))
  rows.append({'name':n,'wall':wall,'x':x})
 a['DisplayNet_Collection'].set_actor_location(unreal.Vector(-749.2,700,80),False,False);a['DisplayNet_Collection'].set_actor_rotation(unreal.Rotator(),False)
 sign('HubCollectionNet','NET / UNLOCKED',(-750,565,62),-90,18)
 def p(n,loc,size,mat):
  o=mesh('EndWallGallery_'+n,loc,size,mat);o.static_mesh_component.set_cast_shadow(False);o.set_folder_path('Art/CollectionGallery')
 for wall in [-1,1]:
  for half in [-1,1]:
   suffix=str(wall)+'_'+str(half)
   p('Floor'+suffix,(half*600,wall*695,2),(570,320,1),'Ice')
   p('Guide'+suffix,(half*600,wall*530,2.6),(570,5,.4),'Cyan')
   p('Header'+suffix,(half*600,wall*855,426),(570,22,22),'Navy')
  for x in [-750,-450,450,750]:
   suffix=str(wall)+'_'+str(x)
   p('Bay'+suffix,(x,wall*865,205),(250,10,332),'Navy')
   p('Line'+suffix,(x,wall*856,388),(224,5,4),'Cyan')
   p('Accent'+suffix,(x-108,wall*852,365),(9,5,20),'Yellow')
  sign('EndWallGalleryTitle'+str(wall),'GADGET COLLECTION',(0,wall*840,475),-90 if wall>0 else 90,26)
 assert all(str(a[n].get_actor_transform())==t for n,t in keep.items())
 assert LS.save_current_level()
 out={'saved':True,'layout':rows,'protected_transforms_unchanged':True,'PIE':False,'dirty_maps':len(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())}
except Exception:out['error']=traceback.format_exc()
(R/'lab_endwall_gallery.json').write_text(json.dumps(out,indent=2))
