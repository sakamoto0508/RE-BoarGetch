from pathlib import Path
p=Path('Public/Boar/BoarBase.h');s=p.read_text(encoding='utf-8-sig');s=s.replace('void Capture();','void Capture();\n\t/** Net passes one shared-feedback flag per swing; success state is still owned by CaptureComponent. */\n\tbool CaptureWithFeedback(bool bSharedFeedback);');p.write_text(s,encoding='utf-8')
p=Path('Private/Boar/BoarBase.cpp');s=p.read_text(encoding='utf-8-sig');s=s.replace('#include "Component/CaptureComponent.h"','#include "Component/CaptureComponent.h"\n#include "Component/CapturePresentationComponent.h"\n#include "GameFramework/PlayerController.h"');start=s.index('void ABoarBase::Capture()');end=s.index('// 捕獲状態を返す。',start)
s=s[:start]+'''void ABoarBase::Capture()
{
 CaptureWithFeedback(true);
}

bool ABoarBase::CaptureWithFeedback(bool bSharedFeedback)
{
 if (const ABoarGameMode* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>())
  if (!Mode->CanAdvanceStage()) return false;
 if (!CaptureComponent || !CaptureComponent->Capture(nullptr)) return false;
 SetActorTickEnabled(false);
 // Keep the successful capture at its original position until presentation completes.
 if (auto* PC = GetWorld()->GetFirstPlayerController())
  if (auto* Presentation = PC->FindComponentByClass<UCapturePresentationComponent>())
   if (Presentation->PresentCapturedBoar(this,bSharedFeedback)) return true;
 if (auto* Mode = GetWorld()->GetAuthGameMode<ABoarGameMode>()) Mode->HandleBoarCaptured(this);
 return true;
}

'''+s[end:];p.write_text(s,encoding='utf-8')
p=Path('Public/Gadget/NetGadget.h');s=p.read_text(encoding='utf-8-sig').replace('TSet<TWeakObjectPtr<class ABoarBase>> AttemptedBoarsThisUse;','TSet<TWeakObjectPtr<class ABoarBase>> AttemptedBoarsThisUse;\n\tbool bSuccessFeedbackPlayedThisUse = false;');p.write_text(s,encoding='utf-8')
p=Path('Private/Gadget/NetGadget.cpp');s=p.read_text(encoding='utf-8-sig').replace('AttemptedBoarsThisUse.Reset();','AttemptedBoarsThisUse.Reset();\n\tbSuccessFeedbackPlayedThisUse = false;').replace('Boar->Capture();','if (!Boar->CaptureWithFeedback(!bSuccessFeedbackPlayedThisUse)) return false;\n\tbSuccessFeedbackPlayedThisUse = true;');p.write_text(s,encoding='utf-8')
p=Path('Public/Player/BoarPlayerController.h');s=p.read_text(encoding='utf-8-sig').replace('public:\n','public:\n\tABoarPlayerController();\n\tUPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Presentation")\n\tTObjectPtr<class UCapturePresentationComponent> CapturePresentation;\n',1);p.write_text(s,encoding='utf-8')
p=Path('Private/Player/BoarPlayerController.cpp');s=p.read_text(encoding='utf-8-sig').replace('#include "Component/HealthComponent.h"','#include "Component/HealthComponent.h"\n#include "Component/CapturePresentationComponent.h"');s=s.replace('void ABoarPlayerController::BeginPlay()','ABoarPlayerController::ABoarPlayerController()\n{\n\tCapturePresentation = CreateDefaultSubobject<UCapturePresentationComponent>(TEXT("CapturePresentation"));\n}\n\nvoid ABoarPlayerController::BeginPlay()',1);p.write_text(s,encoding='utf-8')
p=Path('Private/Core/BoarGameMode.cpp');s=p.read_text(encoding='utf-8-sig').replace('#include "Component/GadgetComponent.h"','#include "Component/GadgetComponent.h"\n#include "Component/CapturePresentationComponent.h"');s=s.replace('StageState = Next;','StageState = Next;\n\tif (auto* PC = GetWorld()->GetFirstPlayerController())\n\t\tif (auto* Presentation = PC->FindComponentByClass<UCapturePresentationComponent>()) Presentation->CancelPresentation();',1);p.write_text(s,encoding='utf-8')
p=Path('REBoarGetch.Build.cs');s=p.read_text(encoding='utf-8-sig').replace('"SlateCore"','"SlateCore",\n\t\t\t"Niagara",\n\t\t\t"EngineCameras"');p.write_text(s,encoding='utf-8')
p=Path('Private/Component/CapturePresentationComponent.cpp');s=p.read_text().replace('Finished.Add(T.Boar);','Finished.Insert(T.Boar,0);');p.write_text(s)
