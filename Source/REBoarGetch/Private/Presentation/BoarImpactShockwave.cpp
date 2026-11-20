#include "Presentation/BoarImpactShockwave.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

namespace BoarImpactShockwave
{
	UNiagaraComponent* Spawn(UWorld* World, UNiagaraSystem* System, const FVector& Location, const FVector& InDirection,
		const APlayerCameraManager* Camera, const FBoarImpactShockwaveParams& Params)
	{
		if (!World || !System)
			return nullptr;

		FVector Direction = InDirection.GetSafeNormal2D();
		if (Direction.IsNearlyZero())
			Direction = FVector::ForwardVector;

		const FRotator Rotation = FRotationMatrix::MakeFromX(Direction).Rotator();

		// リング面は押し込む向きに垂直（法線＝突進方向）。真横から見て線になりすぎないよう、法線を少しだけカメラへ傾ける。
		// 両面Materialなので、カメラと突進方向の前後関係に関わらず「カメラ側へ開く」向きに傾ける。
		FVector RingNormal = Direction;
		if (Camera && Params.RingCameraBias > 0.0f)
		{
			const FVector ToCamera = (Camera->GetCameraLocation() - Location).GetSafeNormal();
			if (!ToCamera.IsNearlyZero())
			{
				const double Side = FVector::DotProduct(Direction, ToCamera) >= 0.0 ? 1.0 : -1.0;
				const FVector Blended = (Direction + ToCamera * (Side * Params.RingCameraBias)).GetSafeNormal();
				if (!Blended.IsNearlyZero())
					RingNormal = Blended;
			}
		}

		// World Space Emitterなので、大きさはComponentのScaleではなくUser Parameter「Scale」で渡す。
		UNiagaraComponent* VFX = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World, System, Location, Rotation, FVector::OneVector, true, false, ENCPoolMethod::AutoRelease);
		if (!VFX)
			return nullptr;

		VFX->SetVariableLinearColor(TEXT("Color"), Params.Color);
		VFX->SetVariableFloat(TEXT("Scale"), Params.Scale);
		VFX->SetVariableVec3(TEXT("ImpactDirection"), Direction);
		VFX->SetVariableVec3(TEXT("RingNormal"), RingNormal);

		if (Params.bOverrideSparkColor)
			VFX->SetVariableLinearColor(TEXT("SparkColor"), Params.SparkColor);
		if (Params.StreakCount >= 0)
			VFX->SetVariableInt(TEXT("StreakCount"), Params.StreakCount);
		if (Params.ParticleCount >= 0)
			VFX->SetVariableInt(TEXT("ParticleCount"), Params.ParticleCount);
		if (Params.MaxRadius >= 0.0f)
			VFX->SetVariableFloat(TEXT("MaxRadius"), Params.MaxRadius);
		if (Params.Lifetime > 0.0f)
			VFX->SetVariableFloat(TEXT("Lifetime"), Params.Lifetime);

		VFX->Activate(true);
		return VFX;
	}
}
