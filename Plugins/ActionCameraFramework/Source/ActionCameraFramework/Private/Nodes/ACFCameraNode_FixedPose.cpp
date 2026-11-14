#include "Nodes/ACFCameraNode_FixedPose.h"

void UACFCameraNode_FixedPose::Evaluate(const FACFCameraNodeContext& Context, FACFCameraPose& InOutPose, FInstancedStruct* RuntimeState) const
{
	if (bOverrideLocation)
	{
		InOutPose.Location = Location;
	}
	if (bOverrideRotation)
	{
		InOutPose.Rotation = Rotation;
	}
	if (bOverrideFieldOfView)
	{
		InOutPose.FieldOfView = FieldOfView;
	}
}
