#include "ACFCameraDebugDraw.h"

#if !UE_BUILD_SHIPPING

#include "DrawDebugHelpers.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace ACFCameraDebug
{
	static TAutoConsoleVariable<int32> CVarDraw(
		TEXT("ACF.Debug.Draw"), 0,
		TEXT("Action Camera Framework debug drawing. 0 = off, 1 = world, 2 = world + screen zones / text."),
		ECVF_Cheat);

	bool IsWorldDrawEnabled() { return CVarDraw.GetValueOnGameThread() >= 1; }
	bool IsScreenDrawEnabled() { return CVarDraw.GetValueOnGameThread() >= 2; }

	void DrawWorld(UWorld* World, const FACFCameraRigDebug& Debug, const FACFCameraEvaluationResult& Result, const FACFCameraSolverReport& Solver)
	{
#if ENABLE_DRAW_DEBUG
		if (!World || !Result.HasInfluence())
		{
			return;
		}
		// Lifetime -1 / not persistent: one frame, redrawn every camera update.
		if (Debug.bHasFollowTarget)
		{
			DrawDebugSphere(World, Debug.FollowTarget, 18.f, 12, FColor::Green);
		}
		if (Debug.bHasPrediction)
		{
			DrawDebugSphere(World, Debug.PredictedTarget, 12.f, 8, FColor::Cyan);
			if (Debug.bHasFollowTarget)
			{
				DrawDebugLine(World, Debug.FollowTarget, Debug.PredictedTarget, FColor::Cyan);
			}
		}
		if (Debug.bHasLookAtTarget)
		{
			DrawDebugCrosshairs(World, Debug.LookAtTarget, FRotator::ZeroRotator, 40.f, FColor::Yellow);
		}
		for (int32 Index = 0; Index < Solver.NumDebugLines; ++Index)
		{
			DrawDebugLine(World, Solver.DebugFrom[Index], Solver.DebugTo[Index], Solver.DebugBlocked[Index] ? FColor::Red : FColor(0, 200, 255));
		}
		if (Debug.bHasGroup)
		{
			for (int32 Index = 0; Index < Debug.NumGroupSubjects; ++Index)
			{
				const FVector4& Subject = Debug.GroupSubjects[Index];
				DrawDebugSphere(World, FVector(Subject), FMath::Max(static_cast<float>(Subject.W), 5.f), 12, FColor::Magenta);
				DrawDebugLine(World, Debug.GroupCenter, FVector(Subject), FColor(160, 60, 160));
			}
			DrawDebugPoint(World, Debug.GroupCenter, 12.f, FColor::Magenta);
		}
		if (Debug.bHasLockOn)
		{
			const FColor LockColor = Debug.bLockOnTargetLost ? FColor(128, 128, 128) : FColor::Red;
			DrawDebugSphere(World, Debug.LockOnPoint, 30.f, 12, LockColor);
			DrawDebugLine(World, Debug.FollowTarget, Debug.LockOnPoint, LockColor);
			DrawDebugCrosshairs(World, Debug.LockOnFocus, FRotator::ZeroRotator, 30.f, FColor::Orange);
		}
		if (Debug.bHasCollision)
		{
			DrawDebugLine(World, Debug.CollisionOrigin, Debug.CollisionDesired, Debug.bCollisionHit ? FColor::Red : FColor(80, 160, 80));
			if (Debug.bCollisionHit)
			{
				DrawDebugSphere(World, Debug.CollisionHit, 10.f, 8, FColor::Red);
			}
		}
		// Camera itself (visible with a detached debug camera / from another view).
		DrawDebugCamera(World, Result.Pose.Location, Result.Pose.Rotation, Result.Pose.FieldOfView, 0.4f, FColor::Orange);
#endif
	}

	static void DrawRect(UCanvas* Canvas, const FVector2D& Center, const FVector2D& Size, const FLinearColor& Color)
	{
		const FVector2D Scale(Canvas->ClipX, Canvas->ClipY);
		const FVector2D Min((0.5 + Center.X - Size.X * 0.5) * Scale.X, (0.5 - Center.Y - Size.Y * 0.5) * Scale.Y);
		const FVector2D Max((0.5 + Center.X + Size.X * 0.5) * Scale.X, (0.5 - Center.Y + Size.Y * 0.5) * Scale.Y);
		DrawDebugCanvas2DBox(Canvas, FBox2D(Min, Max), Color, 1.5f);
	}

	void DrawCanvas(UCanvas* Canvas, const FACFCameraRigDebug& Debug, const FACFCameraEvaluationResult& Result, const FACFCameraSolverReport& Solver, const TArray<FACFCameraEffectInfo>& Effects, const TArray<FString>& ExtraLines)
	{
		if (!Canvas || !GEngine)
		{
			return;
		}
		if (Debug.bHasGroup)
		{
			DrawRect(Canvas, FVector2D::ZeroVector, Debug.GroupFramingSize, FLinearColor(1.f, 0.2f, 1.f, 1.f));
		}
		if (Debug.bHasComposition)
		{
			const FVector2D Soft(FMath::Max(Debug.SoftZone.X, Debug.DeadZone.X), FMath::Max(Debug.SoftZone.Y, Debug.DeadZone.Y));
			DrawRect(Canvas, Debug.ScreenPosition, Soft, FLinearColor(1.f, 0.2f, 0.2f, 1.f));
			DrawRect(Canvas, Debug.ScreenPosition, Debug.DeadZone, FLinearColor(0.2f, 1.f, 0.2f, 1.f));
			DrawRect(Canvas, Debug.TargetOnScreen, FVector2D(0.01, 0.01 * Canvas->ClipX / FMath::Max(Canvas->ClipY, 1.f)), FLinearColor::Yellow);
		}

		UFont* Font = GEngine->GetSmallFont();
		float Y = 60.f;
		auto Line = [&](const FString& Text)
		{
			Canvas->SetDrawColor(FColor::Cyan);
			Canvas->DrawText(Font, Text, 20.f, Y);
			Y += 14.f;
		};
		Line(FString::Printf(TEXT("ACF %s  weight %.2f (request %.2f%s)  blend %.2f  requests %d"),
			*Result.ActiveRequest.ToString(), Result.Weight, Result.RequestWeight, Result.bOutputSuppressed ? TEXT(", SUPPRESSED") : TEXT(""),
			Result.BlendAlpha, Result.NumRequests));
		Line(FString::Printf(TEXT("Camera %s  %s  FOV %.1f"), *Result.Pose.Location.ToCompactString(), *Result.Pose.Rotation.ToCompactString(), Result.Pose.FieldOfView));
		for (const FString& Extra : ExtraLines)
		{
			Line(Extra);
		}
		if (Debug.bHasFollowTarget)
		{
			Line(FString::Printf(TEXT("Follow %s"), *Debug.FollowTarget.ToCompactString()));
		}
		if (Debug.bHasLookAtTarget)
		{
			Line(FString::Printf(TEXT("LookAt %s"), *Debug.LookAtTarget.ToCompactString()));
		}
		if (Debug.bHasPrediction)
		{
			Line(FString::Printf(TEXT("Lookahead %s"), *Debug.PredictedTarget.ToCompactString()));
		}
		if (Debug.bHasOrbit)
		{
			Line(FString::Printf(TEXT("Orbit yaw %.1f pitch %.1f distance %.0f"), Debug.OrbitYaw, Debug.OrbitPitch, Debug.OrbitDistance));
		}
		if (Debug.bHasGroup)
		{
			Line(FString::Printf(TEXT("Group subjects %d  dolly %.0f cm  FOV %+.1f"), Debug.NumGroupSubjects, Debug.GroupDolly, Debug.GroupFovDelta));
		}
		if (Debug.bHasCombat)
		{
			Line(FString::Printf(TEXT("Combat %s  weight %.2f  distance x%.2f  FOV %+.1f  enemy range %.0f"), Debug.bInCombat ? TEXT("ON") : TEXT("off"),
				Debug.CombatWeight, Debug.CombatDistanceScale, Debug.CombatFovOffset, Debug.CombatRange));
		}
		if (Solver.NumCheckedSubjects > 0 || Solver.FovRecovery != 0.f)
		{
			Line(FString::Printf(TEXT("Solver  checked %d  traces %d%s  correction yaw %.1f pitch %.1f pull %.2f  FOV recovery %+.1f%s%s  occluded %d%s"),
				Solver.NumCheckedSubjects, Solver.NumTraces, Solver.bSearched ? TEXT(" (search)") : TEXT(""), Solver.Correction.X, Solver.Correction.Y, Solver.Correction.Z,
				Solver.FovRecovery, Solver.bFramingLimited ? TEXT(" LIMIT") : TEXT(""), Solver.bPlayerRotationCorrected ? TEXT(" +turn") : TEXT(""),
				Solver.NumOccluded, Solver.bPlayerOccluded ? TEXT(" PLAYER HIDDEN") : TEXT("")));
		}
		if (Debug.bHasLockOn)
		{
			Line(FString::Printf(TEXT("Lock-On %s  range %.0f cm%s"), *Debug.LockOnPoint.ToCompactString(), Debug.LockOnRange, Debug.bLockOnTargetLost ? TEXT("  (TARGET LOST: holding)") : TEXT("")));
		}
		if (Debug.bHasComposition)
		{
			Line(FString::Printf(TEXT("Screen target (%.3f, %.3f)  desired (%.3f, %.3f)"), Debug.TargetOnScreen.X, Debug.TargetOnScreen.Y, Debug.ScreenPosition.X, Debug.ScreenPosition.Y));
		}
		if (Debug.bHasCollision)
		{
			Line(FString::Printf(TEXT("Collision %s  pulled in %.0f cm"), Debug.bCollisionHit ? TEXT("HIT") : TEXT("clear"),
				FVector::Dist(Debug.CollisionDesired, Result.Pose.Location)));
		}
		for (const FACFCameraEffectInfo& Effect : Effects)
		{
			Line(FString::Printf(TEXT("Effect %s %s  t %.2f  weight %.2f  layers %d%s"), *Effect.Handle.ToString(), *Effect.PresetName.ToString(),
				Effect.Time, Effect.Weight, Effect.ActiveLayers, Effect.bStopping ? TEXT("  (stopping)") : TEXT("")));
		}
	}
}

#endif // !UE_BUILD_SHIPPING
