"""Additive facility art pass; run in editor, never PIE."""
import unreal,math,json,traceback
from pathlib import Path
ROOT=Path(__file__).parent
exec((ROOT/'build_facility_levels.py').read_text(encoding='utf-8').split('def run():')[0])
result={'assets':[],'levels':[],'preserved':{},'moved':[]}
mats['Holo']=unreal.load_asset('/Game/Lobby/Materials/M_Lobby_ResearchHologram')
G={}
def tri(m,a,b,c):G.setdefault(m,[]).append((a,b,c))
def quad(m,a,b,c,d):tri(m,a,b,c);tri(m,a,c,d)
def box(m,p,s):
    x,y,z=p;u,v,w=[t/2 for t in s]
    a=[(x+i*u,y+j*v,z+k*w) for k in [-1,1] for j in [-1,1] for i in [-1,1]]
    for f in [(0,2,3,1),(4,5,7,6),(0,1,5,4),(2,6,7,3),(0,4,6,2),(1,3,7,5)]:quad(m,*[a[i] for i in f])
def ring(m,x,y,ri,ro,z,n=32,gap=0):
    for i in range(n):
        a=math.tau*i/n+gap;b=math.tau*(i+1)/n-gap
        def p(r,t):return(x+r*math.cos(t),y+r*math.sin(t),z)
        quad(m,p(ri,a),p(ro,a),p(ro,b),p(ri,b))
def drum(m,x,y,z,r,h,n=12):
    for i in range(n):
        a=math.tau*i/n;b=math.tau*(i+1)/n
        p=(x+r*math.cos(a),y+r*math.sin(a),z);q=(x+r*math.cos(b),y+r*math.sin(b),z)
        pp=(p[0],p[1],z+h);qq=(q[0],q[1],z+h)
        quad(m,p,q,qq,pp);tri(m,(x,y,z+h),pp,qq);tri(m,(x,y,z),q,p)
def gem(m,p,s,n=8):
    x,y,z=p;rx,ry,rz=[v/2 for v in s]
    for i in range(n):
        a=math.tau*i/n;b=math.tau*(i+1)/n
        p1=(x+rx*math.cos(a),y+ry*math.sin(a),z);p2=(x+rx*math.cos(b),y+ry*math.sin(b),z)
        tri(m,(x,y,z+rz),p1,p2);tri(m,(x,y,z-rz),p2,p1)
def boar(m,x,y,z,k=1):
    gem(m,(x,y,z+66*k),(200*k,95*k,115*k))
    gem(m,(x-90*k,y,z+56*k),(85*k,78*k,85*k))
    box(m,(x-135*k,y,z+45*k),(26*k,48*k,30*k))
    for dx in [-62,62]:
        for dy in [-28,28]:box(m,(x+dx*k,y+dy*k,z+22*k),(24*k,25*k,48*k))
    for dy in [-26,26]:gem(m,(x-70*k,y+dy*k,z+107*k),(30*k,22*k,55*k))
    for dy in [-36,36]:gem(m,(x-118*k,y+dy*k,z+58*k),(20*k,15*k,40*k))
def panel(n,p,s):
    x,y,z=p;w,d,h=s
    box('Navy',p,s);box('Panel',(x,y+5+d/2,z),(w-28,4,h-28))
    for xx in [x-w/2,x+w/2]:box('Ivory',(xx,y,z),(18,d+12,h+30))
    for zz in [z-h/2,z+h/2]:box('Glow',(x,y+d/2+5,zz),(w,5,7))
def floorzone(n,x,y,w,h):
    box('Navy',(x,y,.4),(w,h,.6));box('Ice',(x,y,.9),(w-22,h-22,.5))
    for xx in [x-w/2+18,x+w/2-18]:box('Cyan',(xx,y,1.4),(7,h-40,.5))
    for yy in [y-h/2+20,y+h/2-20]:box('Yellow',(x,yy,1.5),(w*.38,8,.5))
def architecture(hx,hy):
    # Existing collision walls stay untouched; add large wall bays outside circulation.
    for side in [-1,1]:
        for i,x in enumerate([-hx+80, -hx*.5,0,hx*.5,hx-80]):
            y=side*(hy-12)
            box('Navy',(x,y,46),(108,106,92));box('Ivory',(x,y,270),(78,78,420))
            box('Yellow',(x,y,467),(104,96,28));box('Navy',(x,y,494),(94,90,26))
            box('Navy',(x,y-side*43,270),(42,8,275));box('Glow',(x,y-side*49,270),(12,5,230))
        for i in range(4):
            x=-hx+(i+.5)*hx/2;y=side*(hy-17);w=hx/2-110
            box('Ice',(x,y,305),(w,40,180));box('Navy',(x,y,402),(w+20,64,24))
            box('Cyan',(x,y-side*25,302),(w-32,7,105));box('Glow',(x,y-side*31,258),(w-42,5,8))
        box('Navy',(side*hx,0,242),(70,hy*2,22))
        for yy in [-hy+110,hy-110]:
            box('Ivory',(side*hx,yy,300),(85,90,300));box('Yellow',(side*hx,yy,457),(100,108,24))
    # Broad inset lanes lead to unchanged return gate; no raised trip edges.
    box('Ice',(-hx*.53,0,1.2),(hx*.78,230,.6))
    for yy in [-122,122]:box('Cyan',(-hx*.53,yy,1.7),(hx*.78,8,.5))
    for x in [-hx+210,-hx+310]:box('Yellow',(x,0,2),(12,95,.5))
def export(level):
    for m,tris in G.items():
        path='/Game/Lobby/Meshes/SM_'+level+'_Quality_'+m
        assert not AS.does_asset_exist(path),'Existing art batch '+path
        verts=[];idx=[]
        for t in tris:
            i=len(verts);verts += [unreal.Vector(*p) for p in t];idx.append(unreal.IntVector(i,i+1,i+2))
        dm=unreal.DynamicMesh();dm.append_buffers_to_mesh(unreal.GeometryScriptSimpleMeshBuffers(vertices=verts,triangles=idx,uv0=[unreal.Vector2D(v.x/100,v.y/100) for v in verts]));dm.set_per_face_normals()
        opt=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();opt.set_editor_property('enable_collision',False);opt.set_editor_property('enable_nanite',False)
        sm,status=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dm,path,opt);assert sm,str(status)
        sm.set_material(0,mats[m]);assert AS.save_loaded_asset(sm);result['assets'].append(path)
        a=mesh('Quality_'+m,(0,0,0),(100,100,100),m,shape=sm);a.set_folder_path('Facility/Quality')
def protected():
    return {a.get_actor_label():str(a.get_actor_transform())+'|'+str(a.get_editor_property('action'))+'|'+str(a.get_editor_property('destination')) for a in EA.get_all_level_actors() if isinstance(a,unreal.HubPortal) and a.get_actor_label()!='OpenFacilityUI'}
def gadget():
    assert LS.load_level('/Game/Level/L_GadgetLab');G.clear();before=protected();aa=actors()
    # Preserve original actor identities and UI code; move whole interaction assembly together.
    for n in ['TerminalBase','TerminalBody','TerminalMonitor','MonitorEdge-175','MonitorEdge175','TerminalTitle','TerminalPad','OpenFacilityUI','TerminalInstruction']:
        a=aa[n];p=a.get_actor_location();a.set_actor_location(unreal.Vector(p.x,p.y+590,p.z),False,False);result['moved'].append(n)
    architecture(1200,900)
    floorzone('Movement',40,490,1330,410);floorzone('Target',730,-30,500,690)
    floorzone('Capture',-730,-490,480,440);floorzone('Obstacle',350,-650,770,280)
    ring('Cyan',0,-80,215,240,2.1);ring('Yellow',0,-80,251,262,2.1,n=12,gap=.065)
    drum('Navy',0,-80,2,180,18);drum('Ivory',0,-80,20,164,22);ring('Glow',0,-80,160,169,43)
    # Console housing surrounds the retained functional console footprint.
    for x in [-172,172]:
        box('Ivory',(x,-80,76),(32,180,110));box('Yellow',(x,-80,139),(38,190,16));box('Glow',(x,-80,148),(14,130,4))
    drum('Navy',0,-95,145,65,20);drum('Cyan',0,-95,165,59,8)
    gem('Holo',(0,-80,250),(120,110,135));ring('Glow',0,-80,85,90,185);ring('Holo',0,-80,75,78,310)
    panel('Loadout',(0,-138,410),(540,18,95));sign('Quality_LoadoutTitle','GADGET LOADOUT',(0,-123,385),90,47)
    # Four clear destinations around the central station.
    for name,p,t in [('Movement',(30,770,280),'MOVEMENT TEST'),('Capture',(-730,-740,260),'CAPTURE TEST'),('Obstacle',(320,-820,260),'OBSTACLE TEST')]:
        panel(name,p,(410,20,100));sign('Quality_'+name,t,(p[0],p[1]+17,p[2]-18),90,30)
    for x in [-230,140,490]:box('Navy',(x,625,50),(200,18,96))
    for y in [-220,130]:
        box('Ivory',(820,y,160),(35,180,290));box('Cyan',(799,y,175),(5,140,210));box('Yellow',(820,y,310),(50,200,20))
    for x in [250,420,590]:box('Cyan',(x,-30,2),(7,510,1))
    # Capture equipment is a visual test fixture, not a new capture actor.
    for x in [-935,-535]:
        for y in [-670,-320]:
            box('Ivory',(x,y,100),(34,34,200));box('Yellow',(x,y,204),(46,46,20))
    for x in [-935,-535]:box('Panel',(x,-495,100),(4,325,110))
    box('Navy',(-730,-530,30),(175,120,60));boar('Ice',-730,-530,60,.55)
    for i,x in enumerate([120,360,580]):
        # Decorative frames off the straight traversal strip; existing movement tests are retained.
        box('Navy',(x,-760,35),(115,80,70));box('Ivory',(x,-760,85),(108,76,30));box('Yellow',(x,-760,104),(100,70,8))
    for x,y in [(-310,-220),(315,120)]:box('Cyan',(x,y,2),(180,14,1))
    export('GadgetLab');assert before==protected();result['preserved']['GadgetReturn']=True
    assert LS.save_current_level();result['levels'].append('L_GadgetLab')
def archive():
    assert LS.load_level('/Game/Level/L_Archive');G.clear();before=protected()
    architecture(900,700)
    for x,y,w,h in [(480,-350,310,270),(500,300,310,280),(-180,410,330,250),(0,-470,420,250)]:floorzone('',x,y,w,h)
    ring('Navy',20,20,207,280,1.5);ring('Cyan',20,20,218,258,2);ring('Yellow',20,20,266,278,2.2,n=16,gap=.06)
    drum('Navy',20,20,1,165,28);drum('Ivory',20,20,29,153,35);drum('Cyan',20,20,64,141,12)
    ring('Glow',20,20,130,143,77);boar('Holo',20,20,110,1.35)
    for z,r in [(92,135),(310,125)]:ring('Holo',20,20,r,r+4,z)
    mesh('Quality_CentralBaseCollision',(20,20,31),(300,300,62),'Ivory',shape=cyl,solid=True)
    for a in [0,math.pi/2,math.pi,3*math.pi/2]:
        x=20+157*math.cos(a);y=20+157*math.sin(a)
        box('Yellow',(x,y,67),(38,38,30));box('Glow',(x,y,86),(20,20,6))
    for i,(x,y) in enumerate([(480,-350),(500,300),(-180,410)]):
        # Retain existing book-display meshes, add a small boar projection above them.
        boar('Holo',x,y,180,.6)
        for xx in [x-128,x+128]:box('Yellow',(xx,y,97),(20,215,24))
        box('Glow',(x,y-104,66),(185,6,12))
        box('Panel',(x,y+88,215),(245,4,180))
    # Research wall is readable as an exhibit, with schematic geometry instead of fake save data.
    panel('Research',(225,658,345),(820,24,220))
    sign('Quality_ResearchTitle','BOAR RESEARCH ARCHIVE',(225,679,475),90,42)
    # North wall face must look into room (toward -Y).
    sign('Quality_ResearchTitle','BOAR RESEARCH ARCHIVE',(225,636,440),-90,42)
    for x in [-40,210,460]:
        boar('Cyan',x,620,278,.48)
        for z,w in [(260,135),(242,100)]:box('Glow',(x,633,z),(w,3,4))
    # Lower shelving at west corners, leaving return gate and spawn corridor clear.
    for y in [-460,460]:
        box('Navy',(-705,y,100),(180,180,200));box('Ivory',(-705,y,209),(200,200,18))
        for z in [55,120,185]:box('Ivory',(-705,y,z),(180,185,10))
        for j in range(4):box('Cyan' if j%2 else 'Ice',(-730+j*22,y-94,90),(15,8,48))
    # Terminal remains at its established entry pad; a broad guide connects exhibition to it.
    for x in [-160,160]:box('Cyan',(x,-292,1.8),(7,150,.6))
    box('Yellow',(0,-405,143),(270,12,12))
    # Compact water feature within unused edge space.
    box('Ivory',(620,0,22),(280,210,44));box('Cyan',(620,0,47),(240,170,8))
    for xx in [520,720]:box('Navy',(xx,65,105),(40,55,125));box('Glow',(xx,34,110),(15,5,75))
    export('Archive');assert before==protected();result['preserved']['ArchiveReturn']=True
    assert LS.save_current_level();result['levels'].append('L_Archive')
try:
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    gadget();archive()
    for p in ['/Game/BP/Lobby/BP_GM_GadgetLab','/Game/BP/Lobby/BP_GM_Archive']:
        b=unreal.load_asset(p);unreal.BlueprintEditorLibrary.compile_blueprint(b);assert AS.save_loaded_asset(b)
    result['compile']='Facility GameMode Blueprints compiled; no source or logic changes'
except Exception:result['error']=traceback.format_exc();unreal.log_error(result['error'])
finally:(ROOT/'brush_facility_quality.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
