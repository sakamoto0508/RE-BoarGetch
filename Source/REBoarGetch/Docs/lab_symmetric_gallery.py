import unreal,json,traceback,math
from pathlib import Path
R=Path(__file__).parent
exec((R/'build_facility_levels.py').read_text().split('def run():')[0])
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert w.get_name()=='L_GadgetLab' and not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
a=actors();out={}
protected={n:str(o.get_actor_transform()) for n,o in a.items() if o.get_class().get_name()=='HubPortal' or n in ['LabResearchStation','GadgetTestClosedBarrier'] or 'DataPortal' in n}
try:
 pm=unreal.load_asset('/Game/InportAssets/GadgetPod');assert pm
 # Keep old architecture instances recoverable, remove their visual contribution only.
 hidden=[]
 for n,o in a.items():
  if n.startswith(('CleanLab_Bay','CleanLab_CollectionRail','CleanLab_CollectionHeader','CleanLab_CollectionFloor')):
   o.set_actor_hidden_in_game(True);o.set_is_temporarily_hidden_in_editor(True)
   for c in o.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
   hidden.append(n)
 name='CollectionPod_Reserve_5'
 if name not in a:
  o=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(),unreal.Rotator());o.set_actor_label(name);o.set_folder_path('Facility/CollectionReserve')
  c=o.static_mesh_component;c.set_static_mesh(pm);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('can_ever_affect_navigation',False);a[name]=o
 names=['CollectionPod_Net']+['CollectionPod_Reserve_'+str(i) for i in range(1,6)]
 rows=[]
 for i,n in enumerate(names):
  side=-1 if i<3 else 1; y=[300,520,740][i%3];yaw=90 if side<0 else -90
  o=a[n];o.set_actor_location(unreal.Vector(side*990,y-side*26.8,0),False,False);o.set_actor_rotation(unreal.Rotator(yaw=yaw),False);o.set_actor_scale3d(unreal.Vector(.8,.8,.8))
  rows.append({'actor':n,'side':side,'center_y':y,'yaw':yaw})
  if n=='CollectionPod_Net':
   # Apply the same rigid visual transform to its existing display and label.
   d=a['DisplayNet_Collection'];d.set_actor_location(unreal.Vector(-1010,300.8,80),False,False);d.set_actor_rotation(unreal.Rotator(yaw=90),False)
   sign('HubCollectionNet','NET / UNLOCKED',(-850,300,62),0,18)
 def p(n,loc,size,mat):
  o=mesh('Gallery_'+n,loc,size,mat);o.static_mesh_component.set_cast_shadow(False);o.set_folder_path('Art/CollectionGallery')
 for side in [-1,1]:
  p('Floor'+str(side),(side*1000,520,2),(310,680,1),'Ice')
  p('Guide'+str(side),(side*833,520,2.6),(5,675,.4),'Cyan')
  p('Header'+str(side),(side*1170,520,426),(22,680,22),'Navy')
  for j,y in enumerate([300,520,740]):
   suffix=str(side)+'_'+str(j)
   p('Bay'+suffix,(side*1178,y,205),(10,206,332),'Navy')
   p('Line'+suffix,(side*1170,y,388),(5,180,4),'Cyan')
   p('Accent'+suffix,(side*1168,y-88,365),(5,9,20),'Yellow')
  sign('GalleryTitle'+str(side),'GADGET COLLECTION',(side*1150,520,475),0 if side<0 else 180,23)
 # Remove obsolete rear-wall title, not an additional interaction station.
 o=a['HubCollectionTitle'];o.set_actor_hidden_in_game(True);o.set_is_temporarily_hidden_in_editor(True)
 for c in o.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
 assert all(str(a[n].get_actor_transform())==t for n,t in protected.items())
 assert LS.save_current_level()
 out={'saved':True,'layout':rows,'protected_transforms_unchanged':True,'hidden_old_architecture':hidden,'PIE':False}
except Exception:out['error']=traceback.format_exc()
(R/'lab_symmetric_gallery.json').write_text(json.dumps(out,indent=2))
