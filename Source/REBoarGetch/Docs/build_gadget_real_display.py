"""Real Net display and structural art pass. Editor only; preserves gameplay actors."""
import unreal,json,math,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'facility_equipment_structure.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
out={'assets':[],'hidden':[],'displays':[]}
def hide_visual(a):
 a.set_actor_hidden_in_game(True);a.set_is_temporarily_hidden_in_editor(True)
 for c in a.get_components_by_class(unreal.PrimitiveComponent):c.set_visibility(False)
 out['hidden'].append(a.get_actor_label())
def protected():
 return {a.get_actor_label():([a.get_actor_location().x,a.get_actor_location().y,a.get_actor_location().z],str(a.get_editor_property('action')),str(a.get_editor_property('destination'))) for a in EA.get_all_level_actors() if isinstance(a,unreal.HubPortal)}
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
 assert LS.load_level('/Game/Level/L_GadgetLab');before=protected()
 bp_path='/Game/BP/Lobby/BP_Display_Net'
 source=unreal.load_asset('/Game/BP/Gadget/BP_NetGadget');ss=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);source_mesh=None
 for h in ss.k2_gather_subobject_data_for_blueprint(source):
  data=unreal.SubobjectDataBlueprintFunctionLibrary.get_data(h);o=unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data,source)
  if isinstance(o,unreal.SkeletalMeshComponent) and o.get_skinned_asset():source_mesh=o;break
 assert source_mesh
 f=unreal.BlueprintFactory();f.set_editor_property('parent_class',unreal.SkeletalMeshActor)
 bp=unreal.load_asset(bp_path) or AT.create_asset('BP_Display_Net','/Game/BP/Lobby',unreal.Blueprint,f);assert bp
 cd=unreal.get_default_object(bp.generated_class());c=cd.get_component_by_class(unreal.SkeletalMeshComponent);assert c
 c.set_skeletal_mesh_asset(source_mesh.get_skinned_asset())
 for i,m in enumerate(source_mesh.get_materials()):c.set_material(i,m)
 c.set_collision_profile_name('NoCollision');c.set_editor_property('generate_overlap_events',False);c.set_simulate_physics(False);c.set_editor_property('can_ever_affect_navigation',False)
 c.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);c.set_component_tick_enabled(False);cd.set_actor_enable_collision(False);cd.set_actor_tick_enabled(False)
 unreal.BlueprintEditorLibrary.compile_blueprint(bp);assert AS.save_loaded_asset(bp);out['assets'].append(bp_path)
 # Keep old batch assets and actors intact, hidden. Re-author the same architecture minus substitute gadget geometry.
 for a in list(EA.get_all_level_actors()):
  n=a.get_actor_label()
  if isinstance(a,unreal.StaticMeshActor) and a.static_mesh_component.static_mesh and '/SM_GadgetHub_Equipment_' in a.static_mesh_component.static_mesh.get_path_name():hide_visual(a)
  if n in ['TerminalMonitor','MonitorEdge-175','MonitorEdge175','TerminalTitle','TerminalInstruction']:hide_visual(a)
 src=(ROOT/'build_gadget_lab_hub.py').read_text(encoding='utf-8')
 body=src.split(' G.clear();architecture(1200,900);wall_support(1200,900)',1)[1].split(" save_groups('GadgetHub')",1)[0]
 body=body.replace(";gem('Holo',(0,-80,225),(110,105,100))",'')
 start=body.index(" beam('Navy',(0,650,130)");end=body.index(' # Floor routes',start);body=body[:start]+body[end:]
 G.clear();architecture(1200,900);wall_support(1200,900)
 exec('\n'.join(line[1:] if line.startswith(' ') else line for line in body.splitlines()))
 # Station: broad mechanical cradle, articulated supports, raised function housing and console wings.
 for x in [-140,140]:
  beam('Ivory',(x,-120,55),(x,-150,285),44,75)
  beam('Ivory',(x,-150,285),(x*.55,-100,310),32,50)
  box('Yellow',(x,-150,280),(58,85,28));box('Glow',(x,-106,185),(10,6,120))
 box('Navy',(0,-175,231),(242,24,176));box('Ice',(0,-158,231),(213,8,155))
 drum('Navy',0,-80,115,78,18);drum('Ivory',0,-80,133,72,10);ring('Glow',0,-80,66,72,144)
 for x in [-265,265]:
  beam('Ivory',(x,-170,96),(x,-125,170),48,64)
  box('Navy',(x,-112,185),(145,115,24));box('Cyan',(x,-110,200),(118,85,6));box('Yellow',(x,-60,202),(90,9,8))
 # Collection: a single real gadget in a framed research case, not invented locked entries.
 for x in [-190,190]:
  box('Navy',(x,690,48),(92,195,96));beam('Ivory',(x,705,75),(x,743,385),42,60)
  box('Yellow',(x,735,375),(66,76,28));box('Glow',(x,703,260),(10,6,165))
 box('Ivory',(0,736,407),(440,86,45));box('Navy',(0,708,350),(312,22,85));box('Ice',(0,693,260),(310,10,235))
 drum('Navy',0,625,119,74,23);drum('Ivory',0,625,142,65,14);ring('Glow',0,625,57,65,157)
 # Large inset working zone behind the interaction lane. No new gameplay collision.
 floorzone('StationApproach',0,-450,850,295)
 for x in [-390,390]:box('Yellow',(x,-450,2),(10,200,2))
 for m,tris in G.items():
  path='/Game/Lobby/Meshes/SM_GadgetLabDisplay_'+m;assert not AS.does_asset_exist(path),path
  verts=[];idx=[]
  for t in tris:
   i=len(verts);verts.extend(unreal.Vector(*p) for p in t);idx.append(unreal.IntVector(i,i+1,i+2))
  dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=idx,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts]));dm.set_per_face_normals()
  opt=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opt.set_editor_property('enable_collision',False);opt.set_editor_property('enable_nanite',False)
  sm,status=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opt);assert sm;sm.set_material(0,mats[m]);assert AS.save_loaded_asset(sm)
  a=mesh('DisplayStructure_'+m,(0,0,0),(100,100,100),m,shape=sm);a.set_folder_path('Facility/DisplayStructure');out['assets'].append(path)
 for name,p,k in [('DisplayNet_Station',(0,-55,145),.49),('DisplayNet_Collection',(0,650,158),.64)]:
  assert name not in actors();a=EA.spawn_actor_from_class(bp.generated_class(),unreal.Vector(*p),unreal.Rotator());a.set_actor_label(name);a.set_folder_path('Facility/GadgetDisplay');a.set_actor_scale3d(unreal.Vector(k,k,k));a.set_actor_enable_collision(False);a.set_actor_tick_enabled(False)
  c=a.get_component_by_class(unreal.SkeletalMeshComponent);c.set_component_tick_enabled(False);c.set_editor_property('generate_overlap_events',False);out['displays'].append(name)
 sign('HubCollectionNet','READY / NET',(0,526,115),-90,31)
 sign('ReturnTitle','RETURN TO LOBBY',(-1000,0,500),0,33)
 assert before==protected(),'Gameplay portal modified'
 assert LS.save_current_level();out['saved_level']='/Game/Level/L_GadgetLab'
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
finally:(ROOT/'build_gadget_real_display.json').write_text(json.dumps(out,indent=2),encoding='utf-8')


