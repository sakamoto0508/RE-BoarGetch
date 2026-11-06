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

	void DrawWorld(UWorld* World, const FACFCameraRigDebug& Debug, const FACFCameraEvaluationResult& Result)
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

	void DrawCanvas(UCanvas* Canvas, const FACFCameraRigDebug& Debug, const FACFCameraEvaluationResult& Result)
	{
		if (!Canvas || !GEngine)
		{
			return;
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
		if (Debug.bHasComposition)
		{
			Line(FString::Printf(TEXT("Screen target (%.3f, %.3f)  desired (%.3f, %.3f)"), Debug.TargetOnScreen.X, Debug.TargetOnScreen.Y, Debug.ScreenPosition.X, Debug.ScreenPosition.Y));
		}
		if (Debug.bHasCollision)
		{
			Line(FString::Printf(TEXT("Collision %s  pulled in %.0f cm"), Debug.bCollisionHit ? TEXT("HIT") : TEXT("clear"),
				FVector::Dist(Debug.CollisionDesired, Result.Pose.Location)));
		}
	}
}

#endif // !UE_BUILD_SHIPPING
