import unreal,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'facility_equipment_structure.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
out={'assets':[],'hidden':[]}
def hide(a):
 a.set_actor_hidden_in_game(True)
 for c in a.get_components_by_class(unreal.PrimitiveComponent):
  c.set_visibility(False);c.set_collision_profile_name('NoCollision')
def placed(n,asset,p,scale=1,yaw=0):
 a=actors().get(n)
 if not a:
  a=EA.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*p),unreal.Rotator(yaw=yaw));a.set_actor_label(n)
 c=a.static_mesh_component;c.set_static_mesh(asset);c.set_editor_property('use_default_collision',False);c.set_collision_profile_name('NoCollision');c.set_editor_property('generate_overlap_events',False);c.set_editor_property('can_ever_affect_navigation',False)
 a.set_actor_scale3d(unreal.Vector(scale,scale,scale));a.set_folder_path('Facility/FinishedAssets');return a
try:
 assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
 assert LS.load_level('/Game/Level/L_GadgetLab')
 aa=actors();station=unreal.load_asset('/Game/InportAssets/GadgetResearchStation');podmesh=unreal.load_asset('/Game/InportAssets/GadgetPod');assert station and podmesh
 # Replace only the old procedural station/case visuals; retain actor and asset identities.
 for n,a in aa.items():
  if n.startswith('DisplayStructure_') or n in ['TerminalBase','TerminalBody','TerminalPad','DisplayStationBackdrop','DisplayCollectionBackdrop','HubCollectionFace','DisplayNet_Station']:
   hide(a);out['hidden'].append(n)
 # Preserve the same architectural and floor composition, without primitive-made equipment.
 G.clear();architecture(1200,900);wall_support(1200,900)
 ring('Navy',0,-80,190,320,1.8);ring('Cyan',0,-80,255,300,2);ring('Yellow',0,-80,309,318,2.2,n=16,gap=.06)
 floorzone('Collection',0,640,1050,250);floorzone('StationApproach',0,-450,850,295)
 for x in [-550,550]:box('Ice',(x,0,1),(500,220,1));box('Cyan',(x,118,2),(500,9,1));box('Yellow',(x,-118,2),(500,9,1))
 box('Ice',(0,380,1),(245,270,1));box('Cyan',(-130,380,2),(9,270,1));box('Cyan',(130,380,2),(9,270,1))
 for m,tris in G.items():
  path='/Game/Lobby/Meshes/SM_GadgetLabHubShell_'+m
  sm=unreal.load_asset(path)
  if not sm:
   verts=[];ids=[]
   for t in tris:
    i=len(verts);verts.extend(unreal.Vector(*p) for p in t);ids.append(unreal.IntVector(i,i+1,i+2))
   dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=ids,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts]));dm.set_per_face_normals()
   opt=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opt.set_editor_property('enable_collision',False);opt.set_editor_property('enable_nanite',False)
   sm,status=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opt);assert sm;sm.set_material(0,mats[m]);assert AS.save_loaded_asset(sm)
  placed('LabHubShell_'+m,sm,(0,0,0));out['assets'].append(path)
 placed('LabResearchStation',station,(0,-80,0))
 entry=aa['OpenFacilityUI'];entry.set_editor_property('requires_interaction',True)
 entry.set_actor_location(unreal.Vector(0,-80,150),False,False);entry.get_editor_property('trigger').set_box_extent(unreal.Vector(285,285,160))
 sign('HubStationTitle','LOADOUT / UNLOCK / TEST',(0,-295,350),-90,26)
 sign('HubStationReverse','GADGET RESEARCH STATION',(0,135,350),90,26)
 sign('LabStationInteraction','E / D-PAD UP : INTERACT',(0,-295,318),-90,20)
 # Explicit fixed Net pod; do not invent Sword or placeholder gadget definitions.
 pod=aa.get('CollectionPod_Net')
 if not pod:pod=EA.spawn_actor_from_class(unreal.GadgetCollectionPod,unreal.Vector(26,620,0),unreal.Rotator());pod.set_actor_label('CollectionPod_Net')
 pod.set_editor_property('gadget_id','Net');pod.get_editor_property('pod_mesh').set_static_mesh(podmesh);pod.set_actor_scale3d(unreal.Vector(.8,.8,.8))
 display=aa['DisplayNet_Collection'];display.set_actor_location(unreal.Vector(0,640,80),False,False);display.set_actor_scale3d(unreal.Vector(.45,.45,.45));display.set_actor_enable_collision(False)
 pod.set_editor_property('display_actors',[display]);pod.set_editor_property('status_label',aa['HubCollectionNet'])
 sign('HubCollectionNet','NET / UNLOCKED',(0,505,62),-90,22)
 sign('HubCollectionTitle','GADGET COLLECTION',(0,740,400),-90,36)
 portal=aa['HubTestTrigger'];portal.set_actor_hidden_in_game(False)
 aa['DataPortal_Test_Frame'].set_actor_hidden_in_game(False)
 aa['DataPortal_Test_Surface'].set_actor_hidden_in_game(True)
 aa['HubTestPad'].set_actor_hidden_in_game(True)
 portal.set_editor_property('test_visual_actors',[aa['DataPortal_Test_Surface'],aa['HubTestPad']])
 portal.set_editor_property('collection_labels',[])
 barrier=mesh('GadgetTestClosedBarrier',(1010,0,220),(8,266,370),'Navy',solid=True)
 barrier.static_mesh_component.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE)
 barrier.static_mesh_component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN,unreal.CollisionResponseType.ECR_BLOCK)
 portal.set_editor_property('test_barrier_actors',[barrier])
 label=sign('HubTestTitle','SELECT GADGET TO TEST',(900,0,510),180,27);label.set_actor_hidden_in_game(False)
 portal.set_editor_property('test_label',label)
 aa['ReturnToLobby'].set_editor_property('clear_test_target_on_travel',True)
 assert LS.save_current_level();out['level_saved']=True
 # Dedicated mapping context applies only to GadgetLab; existing mappings stay intact.
 action=unreal.load_asset('/Game/Input/Actions/IA_FacilityInteract')
 if not action:action=AT.create_asset('IA_FacilityInteract','/Game/Input/Actions',unreal.InputAction,unreal.InputAction_Factory())
 context=unreal.load_asset('/Game/Input/IMC_Facility')
 if not context:
  context=AT.create_asset('IMC_Facility','/Game/Input',unreal.InputMappingContext,unreal.InputMappingContext_Factory())
  context.map_key(action,unreal.Key('E'));context.map_key(action,unreal.Key('Gamepad_DPad_Up'))
 assert AS.save_loaded_asset(action);assert AS.save_loaded_asset(context)
 bp=unreal.load_asset('/Game/BP/Player/BP_BoarPlayerController');cd=unreal.get_default_object(bp.generated_class())
 cd.set_editor_property('interact_action',action);cd.set_editor_property('facility_mapping_context',context)
 for p in ['/Game/BP/Player/BP_BoarPlayerController','/Game/BP/Widget/WBP_GadgetLoadout','/Game/BP/Core/BP_BoarGameInstance','/Game/BP/Lobby/BP_GM_GadgetLab']:
  b=unreal.load_asset(p);unreal.BlueprintEditorLibrary.compile_blueprint(b);assert AS.save_loaded_asset(b);out['assets'].append(p)
 out['assets']+=['/Game/Input/Actions/IA_FacilityInteract','/Game/Input/IMC_Facility']
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
(ROOT/'update_lab_spec.json').write_text(json.dumps(out,indent=2))

