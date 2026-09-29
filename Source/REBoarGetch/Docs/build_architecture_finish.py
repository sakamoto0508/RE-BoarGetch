import unreal,math,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'facility_equipment_structure.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
out={'assets':[],'levels':{},'lighting':[]}
def protected():
 result={}
 for a in EA.get_all_level_actors():
  p=a.get_actor_location();r=a.get_actor_rotation();s=a.get_actor_scale3d()
  result[a.get_path_name()]=[p.x,p.y,p.z,r.pitch,r.yaw,r.roll,s.x,s.y,s.z,a.get_editor_property('hidden')]
 return result
def export_arch(level):
 made=[]
 for m,tris in G.items():
  path='/Game/Lobby/Meshes/SM_'+level+'_Architecture_'+m
  assert not AS.does_asset_exist(path),'Existing: '+path
  vertices=[];indices=[]
  for t in tris:
   i=len(vertices);vertices.extend(unreal.Vector(*p) for p in t);indices.append(unreal.IntVector(i,i+1,i+2))
  dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=vertices,triangles=indices,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in vertices]));dm.set_per_face_normals()
  opt=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opt.set_editor_property('enable_collision',False);opt.set_editor_property('enable_nanite',False)
  sm,status=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opt);assert sm;sm.set_material(0,mats[m]);assert AS.save_loaded_asset(sm)
  a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(),unreal.Rotator());a.set_actor_label('Architecture_'+m);a.set_folder_path('Art/Architecture')
  c=a.static_mesh_component;c.set_static_mesh(sm);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('can_ever_affect_navigation',False)
  # Stylized room keeps existing neutral daylight; architectural light fixtures supply local fill.
  c.set_cast_shadow(False)
  made.append(a.get_actor_label());out['assets'].append(path)
 return made
def light(name,p,power,width,height,radius):
 a=EA.spawn_actor_from_class(unreal.RectLight,unreal.Vector(*p),unreal.Rotator(pitch=-90));a.set_actor_label(name);a.set_folder_path('Art/Architecture/Lights')
 c=a.get_component_by_class(unreal.RectLightComponent);c.set_editor_property('intensity_units',unreal.LightUnits.LUMENS);c.set_editor_property('intensity',power);c.set_editor_property('source_width',width);c.set_editor_property('source_height',height);c.set_editor_property('attenuation_radius',radius);c.set_editor_property('use_temperature',True);c.set_editor_property('temperature',5800);c.set_cast_shadows(False)
 out['lighting'].append(name)
def lab():
 G.clear()
 # Broad floor zones; all decoration is non-colliding and only a few cm above existing floor.
 for x in [-760,760]:
  for y in [-500,450]:
   box('Ice',(x,y,2.65),(670,560,.3));box('Ivory',(x,y,2.9),(648,538,.2))
 ring('Navy',0,-80,225,360,3.1,n=12);ring('Cyan',0,-80,349,356,3.3,n=12)
 for i in [0,3,6,9]:
  a=math.tau*i/12;box('Yellow',(330*math.cos(a),-80+330*math.sin(a),3.6),(32,10,.3))
 for side in [-1,1]:
  box('Navy',(side*1170,0,3),(35,1740,.6));box('Navy',(0,side*870,3),(2320,35,.6))
  box('Navy',(side*1180,0,24),(30,1760,48));box('Navy',(0,side*880,24),(2340,30,48))
  for y in [-126,126]:box('Cyan',(side*660,y,3.4),(650,7,.4))
  box('Yellow',(side*380,0,3.5),(12,80,.4))
 for x in [-148,148]:box('Cyan',(x,380,3.4),(7,350,.4))
 box('Navy',(-280,650,3),(1070,350,.4))
 box('Ice',(-280,650,3.3),(1035,320,.3))
 # Extend existing enclosure upward without moving the original walls, pillars or equipment.
 for side in [-1,1]:
  box('Ivory',(side*1210,0,510),(38,1840,510));box('Ivory',(0,side*910,510),(2400,38,510))
  box('Navy',(side*1184,0,707),(24,1790,54));box('Navy',(0,side*884,707),(2360,24,54))
 # Consistent display bays behind the two existing wall-side pod locations.
 for x in [-590,0]:
  box('Navy',(x,842,260),(460,20,440))
  for dx in [-244,244]:box('Ivory',(x+dx,816,278),(34,72,482))
  box('Ivory',(x,816,518),(520,72,35));box('Ivory',(x,816,45),(520,72,35))
  box('Cyan',(x-220,804,287),(6,5,330));box('Yellow',(x+224,798,470),(13,8,45))
 # Large roof panels, with a continuous backing preventing sky gaps.
 box('Navy',(0,0,785),(2460,1860,28))
 for x in [-800,0,800]:
  for y in [-450,450]:box('Ivory',(x,y,763),(780,875,25))
 for x in [-400,400]:box('Navy',(x,0,731),(44,1810,55))
 box('Navy',(0,0,732),(2430,38,54))
 for x in [-780,780]:
  for y in [-425,425]:
   box('Navy',(x,y,737),(430,145,25));box('SoftWhite',(x,y,722),(400,116,5))
   light('Architecture_LabFill_'+str(x)+'_'+str(y),(x,y,706),4500,400,116,1800)
 # Station ceiling centerpiece is a simple architectural coffer.
 ring('Navy',0,-80,260,340,712,n=12);ring('SoftWhite',0,-80,272,305,710,n=12);ring('Cyan',0,-80,326,332,709,n=12)
 for x in [-970,970]:
  box('Navy',(x,650,744),(240,125,10))
  for y in [610,635,660,685]:box('Ice',(x,y,737),(205,9,5))
def lobby():
 G.clear()
 # High atrium roof and continuous perimeter fascia, above all portal signs.
 box('Navy',(0,0,1640),(3660,3060,35))
 for x in [-1350,-450,450,1350]:
  for y in [-1000,0,1000]:box('Ivory',(x,y,1615),(880,975,24))
 # Central bright skylight panel, faceted circular framing and radial beams.
 drum('SoftWhite',0,0,1575,720,12,n=24)
 ring('Navy',0,0,715,800,1572,n=24);ring('Cyan',0,0,742,751,1568,n=24)
 for i in range(8):
  ang=math.tau*i/8;dx=math.cos(ang);dy=math.sin(ang)
  reach=min(1760/max(abs(dx),.001),1460/max(abs(dy),.001))
  beam('Ivory',(dx*790,dy*790,1540),(dx*reach,dy*reach,1500),95,80)
  beam('Navy',(dx*790,dy*790,1500),(dx*reach,dy*reach,1460),28,20)
  if i in [0,2,6]:beam('Cyan',(dx*820,dy*820,1484),(dx*(reach-100),dy*(reach-100),1447),8,6)
  box('Yellow',(dx*(reach-45),dy*(reach-45),1482),(60,60,18))
 # Tall perimeter clerestory enclosure; ground-level garden and portal composition stays intact.
 for side in [-1,1]:
  box('Ivory',(side*1810,0,880),(40,3040,1470));box('Ivory',(0,side*1510,880),(3600,40,1470))
  box('Navy',(side*1778,0,1510),(42,3000,90));box('Navy',(0,side*1478,1510),(3560,42,90))
  for y in [-1180,1180]:box('Ivory',(side*1750,y,920),(85,90,1200))
  for x in [-1150,0,1150]:
   box('Ice',(x,side*1485,1175),(1020,10,410));box('Navy',(x,side*1474,955),(1040,25,35))
   box('Cyan',(x,side*1460,1340),(750,6,7))
 for x in [-900,900]:
  for y in [-650,650]:
   box('SoftWhite',(x,y,1540),(450,135,6));light('Architecture_AtriumFill_'+str(x)+'_'+str(y),(x,y,1490),9000,600,220,2800)
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 # Existing palette reused; add only a neutral architectural luminous diffuser.
 path='/Game/Lobby/Materials/M_Architecture_SoftWhite'
 mat=unreal.load_asset(path)
 if not mat:
  mat=AT.create_asset('M_Architecture_SoftWhite','/Game/Lobby/Materials',unreal.Material,unreal.MaterialFactoryNew())
  mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
  c=unreal.MaterialEditingLibrary.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
  c.set_editor_property('constant',unreal.LinearColor(1.5,1.48,1.4,1));unreal.MaterialEditingLibrary.connect_material_property(c,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
  unreal.MaterialEditingLibrary.recompile_material(mat);assert AS.save_loaded_asset(mat)
 mats['SoftWhite']=mat;out['assets'].append(path)
 for level,builder in [('L_GadgetLab',lab),('L_Lobby',lobby)]:
  assert LS.load_level('/Game/Level/'+level);before=protected()
  assert not any(a.get_actor_label().startswith('Architecture_') for a in EA.get_all_level_actors()),'Architecture already exists'
  builder();made=export_arch(level)
  after=protected();assert all(after.get(k)==v for k,v in before.items()),'Original actor changed'
  assert LS.save_current_level();out['levels'][level]={'added':made,'saved':True,'original_actor_transforms_visibility_unchanged':True}
 assert LS.load_level('/Game/Level/L_GadgetLab')
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
(ROOT/'build_architecture_finish.json').write_text(json.dumps(out,indent=2))
