#include "Boar/BoarMovementComponent.h"

// 演出中は移動の経過時間だけを縮める。速度の設定値（ApplyMovementSpeed・攻撃Taskが書くMaxWalkSpeed）とVelocityは変えないので、
// ABPの速度判定や突進終了判定、速度の保存・復元を行う処理と競合しない。
void UBoarMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType,
                                           FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime * PresentationTimeScale, TickType, ThisTickFunction);
}
