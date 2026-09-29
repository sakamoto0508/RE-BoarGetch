import unreal,math,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'brush_facility_quality.py').read_text(encoding='utf-8').split('\ntry:\n')[0])
out={'assets':[],'levels':[]}
def beam(m,a,b,w,d=None):
    a=unreal.Vector(*a);b=unreal.Vector(*b);v=b-a;length=v.length();v=v/length
    ref=unreal.Vector(0,0,1) if abs(v.z)<.95 else unreal.Vector(0,1,0)
    u=v.cross(ref);u=u/u.length()*(w/2);t=v.cross(u);t=t/t.length()*((d or w)/2)
    q=[p+u*i+t*j for p in [a,b] for j in [-1,1] for i in [-1,1]]
    for f in [(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)]:quad(m,*[(q[i].x,q[i].y,q[i].z) for i in f])
def arch(m,x,y,z,r,width,depth,plane='xz',n=12):
    for i in range(n):
        a=math.pi*i/n;b=math.pi*(i+1)/n
        def p(t):return (x+r*math.cos(t),y,z+r*math.sin(t)) if plane=='xz' else (x,y+r*math.cos(t),z+r*math.sin(t))
        beam(m,p(a),p(b),width,depth)
def diskface(x,y,z,r,m):
    # Vertical target facing the existing firing lane, toward -X.
    for i in range(24):
        a=math.tau*i/24;b=math.tau*(i+1)/24
        tri(m,(x,y,z),(x,y+r*math.cos(a),z+r*math.sin(a)),(x,y+r*math.cos(b),z+r*math.sin(b)))
        tri(m,(x,y,z),(x,y+r*math.cos(b),z+r*math.sin(b)),(x,y+r*math.cos(a),z+r*math.sin(a)))
def wall_support(hx,hy):
    for side in [-1,1]:
        for x in [-hx*.5,hx*.5]:
            beam('Ivory',(x,side*(hy-90),15),(x,side*(hy-22),245),46,65)
            box('Navy',(x,side*(hy-80),15),(130,135,28))
    # Broad service channels connect architecture to work zones, flush to floor.
    for side in [-1,1]:
        box('Navy',(0,side*(hy-160),1.9),(hx*1.5,24,.4))
        box('Cyan',(0,side*(hy-160),2.2),(hx*1.5,5,.3))
def save_groups(level):
    for m,tris in G.items():
        path='/Game/Lobby/Meshes/SM_'+level+'_Equipment_'+m
        assert not AS.does_asset_exist(path),path
        verts=[];ids=[]
        for t in tris:
            i=len(verts);verts.extend(unreal.Vector(*p) for p in t);ids.append(unreal.IntVector(i,i+1,i+2))
        dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=ids,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts]));dm.set_per_face_normals()
        opt=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opt.set_editor_property('enable_collision',False);opt.set_editor_property('enable_nanite',False)
        sm,status=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opt);assert sm,str(status)
        sm.set_material(0,mats[m]);assert AS.save_loaded_asset(sm)
        a=mesh('Equipment_'+m,(0,0,0),(100,100,100),m,shape=sm);a.set_folder_path('Facility/EquipmentStructure');out['assets'].append(path)
    assert LS.save_current_level();out['levels'].append(level)
def lab():
    assert LS.load_level('/Game/Level/L_GadgetLab');G.clear();wall_support(1200,900)
    # Main service cradle: rear support spine, two cantilever arms, broad tool head.
    for x in [-145,145]:
        box('Navy',(x,-137,80),(62,80,130));box('Ivory',(x,-137,211),(46,60,230))
        beam('Ivory',(x,-137,298),(x*.6,-80,355),44,55)
        box('Yellow',(x,-137,304),(59,70,28))
        beam('Cyan',(x-8,-103,130),(x-8,-103,270),8,5)
    box('Navy',(0,-112,339),(190,58,28));box('Ivory',(0,-110,357),(180,76,25))
    # Paired mechanical jaws make the floating gadget's mounting purpose explicit.
    for s in [-1,1]:
        beam('Navy',(s*120,-60,152),(s*100,-60,208),22)
        beam('Yellow',(s*100,-60,208),(s*67,-60,208),28)
    # Capture test: open-front containment scanner, with field emitters and overhead yoke.
    for x in [-910,-550]:
        box('Navy',(x,-600,42),(78,95,84));box('Ivory',(x,-600,190),(45,60,275));box('Cyan',(x,-565,195),(17,6,185))
    arch('Ivory',-730,-600,282,180,38,50);arch('Cyan',-730,-574,282,157,9,10)
    for x in [-910,-550]:box('Yellow',(x,-600,304),(64,80,30))
    # Existing target actors stay interactive and unmoved; backing bullseyes are visual only.
    for y in [-220,130]:
        diskface(793,y,180,83,'Navy');diskface(791,y,180,69,'Ivory');diskface(789,y,180,48,'Cyan');diskface(787,y,180,24,'Yellow')
        beam('Navy',(820,y-80,16),(820,y,95),27);beam('Navy',(820,y+80,16),(820,y,95),27)
    # Motion apparatus side structures describe the rising platform sequence without obstructing it.
    for x,h in [(-200,70),(170,120),(520,170)]:
        box('Navy',(x,620,h*.5),(170,26,h));beam('Ivory',(x-70,628,10),(x+70,628,h-5),25,24)
        box('Cyan',(x,632,h*.55),(90,4,10))
    # Obstacle fixtures acquire a mechanical hinge silhouette, staying outside the straight lane.
    for x in [120,360,580]:
        box('Navy',(x,-760,122),(80,45,35));beam('Ivory',(x-40,-760,137),(x+40,-760,190),35,45)
        box('Yellow',(x+40,-760,190),(38,60,32))
    save_groups('GadgetLab')
def archive2():
    assert LS.load_level('/Game/Level/L_Archive');G.clear();wall_support(900,700)
    # Landmark scan arch is structural, tall and open, rather than another screen on a plinth.
    for x in [-123,163]:
        box('Navy',(x,70,43),(56,92,86));box('Ivory',(x,70,174),(34,54,205));box('Yellow',(x,70,272),(48,68,24))
    arch('Ivory',20,70,265,143,35,48);arch('Cyan',20,42,265,125,9,9)
    box('Navy',(20,70,415),(85,66,32));box('Yellow',(20,70,437),(70,56,13));box('Glow',(20,31,414),(52,5,10))
    # East-south: museum specimen case; chamfer-like corner posts and overhead canopy.
    x,y=480,-350
    for dx in [-112,112]:
        for dy in [-78,78]:
            box('Ivory',(x+dx,y+dy,207),(15,15,190));box('Navy',(x+dx,y+dy,115),(27,27,25))
    box('Navy',(x,y,307),(258,188,26));box('Ivory',(x,y,328),(242,176,16));box('Glow',(x,y-96,306),(200,5,8))
    # East-north: two-arm biometric scanner with vertical emitter head.
    x,y=500,300
    for dx in [-100,100]:
        box('Navy',(x+dx,y,138),(35,95,58));beam('Ivory',(x+dx,y,154),(x+dx*.7,y,280),25,38)
        box('Yellow',(x+dx*.7,y,281),(38,50,20));box('Cyan',(x+dx*.7,y-30,253),(18,7,40))
    box('Ivory',(x+82,y+62,225),(40,42,220));box('Navy',(x+82,y+62,343),(65,60,28));box('Cyan',(x+82,y+27,326),(32,6,26))
    # North-west: angled folio / archive-reader desk, intentionally low and open.
    x,y=-180,410
    for dx in [-96,96]:beam('Ivory',(x+dx,y+65,112),(x+dx,y-54,160),24,24)
    beam('Navy',(x-109,y-60,163),(x+109,y-60,163),26,22)
    box('Yellow',(x,y-78,143),(60,12,9))
    # Functional encyclopedia console: sloped instrument hood and rear support.
    for x in [-155,155]:
        beam('Ivory',(x,-493,55),(x,-526,267),30,42)
        beam('Navy',(x,-510,188),(x,-420,143),25,30)
    box('Ivory',(0,-526,340),(348,52,23));box('Cyan',(0,-497,340),(240,6,8))
    # Wall display's cantilever brackets visibly attach it to the facility.
    for x in [-95,540]:
        beam('Navy',(x,680,170),(x,635,250),35,35);box('Yellow',(x,642,247),(44,30,28))
    save_groups('Archive')
try:
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    lab();archive2()
except Exception:out['error']=traceback.format_exc();unreal.log_error(out['error'])
finally:(ROOT/'facility_equipment_structure.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
