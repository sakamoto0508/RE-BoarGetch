import unreal,json
from pathlib import Path
asub=unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
out={'assets':{},'entrances':[],'widgets':[]}
for folder in ['/Game/UI','/Game/BP/Widget','/Game/Stage01']:
    out['assets'][folder]=[p for p in asub.list_assets(folder,recursive=True,include_folder=False) if any(t in p.lower() for t in ['preview','diorama','rendertarget','stageconfig'])]
for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if a.get_class().get_name().startswith('BP_StageEntrance'):
        out['entrances'].append({'name':a.get_actor_label(),'stage':str(a.get_editor_property('stage_config')),'catalog':[str(v) for v in a.get_editor_property('stage_catalog')]})
bp=unreal.load_asset('/Game/BP/Widget/WBP_LobbyStageSelect')
tree=unreal.find_object(bp,'WidgetTree')
for name in ["SafeArea","ScreenCanvas","StagePanel","HoloPanel","ScreenCaption","StageName","StageDescription","ObjectiveCaption","TargetCount","TargetUnit","PreviewCaption","StageThumbnail","InputGuide","StartStageButtonFrame","Button_Start","StartLabel","CancelActionFrame","Button_Cancel","CancelLabel","EncyclopediaFrame","Button_Encyclopedia","EncyclopediaLabel","CatalogPanel","CatalogHoloPanel","CatalogCaption","Scroll_Stages","Text_StageProgress","Text_StageStatus","Text_StageNumber","Button_PreviousFrame","Button_Previous","Button_PreviousLabel","Button_NextFrame","Button_Next","Button_NextLabel","CarouselPrevious","CarouselPreviousFrame","CarouselPreviousImage","CarouselPreviousEmpty","CarouselPreviousLabel","CarouselPreviousBadge","CarouselPreviousLock","CarouselPreviousLockBody","CarouselPreviousLockLeft","CarouselPreviousLockTop","CarouselPreviousLockRight","CarouselCurrent","CarouselCurrentFrame","CarouselCurrentImage","CarouselCurrentEmpty","CarouselCurrentLabel","CarouselCurrentBadge","CarouselCurrentLock","CarouselCurrentLockBody","CarouselCurrentLockLeft","CarouselCurrentLockTop","CarouselCurrentLockRight","CarouselNext","CarouselNextFrame","CarouselNextImage","CarouselNextEmpty","CarouselNextLabel","CarouselNextBadge","CarouselNextLock","CarouselNextLockBody","CarouselNextLockLeft","CarouselNextLockTop","CarouselNextLockRight"]:
    w=unreal.find_object(tree,name)
    if not isinstance(w,unreal.Widget):continue
    d={'name':w.get_name(),'class':w.get_class().get_name(),'visibility':str(w.get_visibility())}
    slot=w.get_editor_property('slot')
    if isinstance(slot,unreal.CanvasPanelSlot):d['layout']=str(slot.get_editor_property('layout_data'))
    if isinstance(w,unreal.Image):d['brush']=str(w.get_editor_property('brush'))
    if isinstance(w,unreal.TextBlock):d['text']=str(w.get_text())
    out['widgets'].append(d)
Path(__file__).with_suffix('.json').write_text(json.dumps(out,indent=2),encoding='utf-8')
