#include "ACFCameraPresetValidation.h"

#if WITH_EDITOR

#include "ACFCameraHealth.h"
#include "ACFCameraNode.h"
#include "ACFCameraPreset.h"
#include "ACFCameraRigInstance.h"
#include "Algo/StableSort.h"
#include "Logging/TokenizedMessage.h"
#include "Misc/DataValidation.h"
#include "Nodes/ACFCameraNode_Collision.h"
#include "Nodes/ACFCameraNode_Combat.h"
#include "Nodes/ACFCameraNode_Composers.h"
#include "Nodes/ACFCameraNode_FixedPose.h"
#include "Nodes/ACFCameraNode_Follow.h"
#include "Nodes/ACFCameraNode_GroupFraming.h"
#include "Nodes/ACFCameraNode_LockOn.h"
#include "Nodes/ACFCameraNode_LookAt.h"
#include "Nodes/ACFCameraNode_OrbitalFollow.h"
#include "Nodes/ACFCameraNode_ShotAnchor.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "ACFCameraPresetValidation"

FText FACFCameraPresetIssue::ToText() const
{
	FText Where = LOCTEXT("WherePreset", "Preset");
	if (NodeIndex != INDEX_NONE)
	{
		Where = FText::Format(LOCTEXT("WhereNode", "Node {0} ({1})"), NodeIndex, Node ? Node->GetClass()->GetDisplayNameText() : LOCTEXT("EmptySlot", "empty"));
	}
	if (!Property.IsEmpty())
	{
		Where = FText::Format(LOCTEXT("WhereProperty", "{0} {1}"), Where, FText::FromString(Property));
	}
	if (Fix.IsEmpty())
	{
		return FText::Format(LOCTEXT("IssueNoFix", "[{0}] {1}: {2}"), FText::FromName(Id), Where, Message);
	}
	return FText::Format(LOCTEXT("Issue", "[{0}] {1}: {2} Fix: {3}"), FText::FromName(Id), Where, Message, Fix);
}

namespace ACFPresetValidationPrivate
{
	using ESeverity = EACFCameraPresetIssueSeverity;

	struct FCollector
	{
		const UACFCameraPreset* Preset = nullptr;
		TArray<FACFCameraPresetIssue>& Out;

		void Add(ESeverity Severity, const TCHAR* Id, const UACFCameraNode* Node, int32 NodeIndex, FText Message, FText Fix, const FString& Property = FString()) const
		{
			FACFCameraPresetIssue& Issue = Out.AddDefaulted_GetRef();
			Issue.Severity = Severity;
			Issue.Id = FName(Id);
			Issue.Preset = Preset;
			Issue.Node = Node;
			Issue.NodeIndex = NodeIndex;
			Issue.Property = Property;
			Issue.Message = MoveTemp(Message);
			Issue.Fix = MoveTemp(Fix);
		}
	};

	FText NodeLabel(const UACFCameraNode* Node, int32 Index)
	{
		return FText::Format(LOCTEXT("NodeLabel", "{0} (node {1})"), Node ? Node->GetClass()->GetDisplayNameText() : LOCTEXT("EmptyNode", "empty"), Index);
	}

	FText PresetLabel(const UACFCameraPreset* Preset)
	{
		return FText::FromString(GetNameSafe(Preset));
	}

	// --- Parameters -------------------------------------------------------------------------------------------------------

	/**
	 * EditCondition of a property, evaluated on its container: "bFlag", "!bFlag", "Enum == E::Value", "Enum != E::Value",
	 * joined with || / &&. Anything else (parentheses, other operators, unknown names) counts as met, so a parameter is never
	 * skipped because of an expression this parser does not understand.
	 */
	TOptional<bool> EvaluateConditionTerm(FString Term, const UStruct* Owner, const void* Container)
	{
		Term.TrimStartAndEndInline();
		if (Term.Contains(TEXT("(")) || Term.Contains(TEXT(")")))
		{
			return {};
		}
		FString Left;
		FString Right;
		bool bEquals = true;
		if (!Term.Split(TEXT("=="), &Left, &Right))
		{
			if (!Term.Split(TEXT("!="), &Left, &Right))
			{
				const bool bNegate = Term.StartsWith(TEXT("!"));
				const FString Name = bNegate ? Term.Mid(1).TrimStart() : Term;
				const FBoolProperty* Bool = FindFProperty<FBoolProperty>(Owner, *Name);
				if (!Bool)
				{
					return {};
				}
				return Bool->GetPropertyValue_InContainer(Container) != bNegate;
			}
			bEquals = false;
		}
		Left.TrimStartAndEndInline();
		Right.TrimStartAndEndInline();
		const FProperty* Property = FindFProperty<FProperty>(Owner, *Left);
		const UEnum* Enum = nullptr;
		int64 Value = 0;
		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			Enum = EnumProperty->GetEnum();
			Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Container));
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property); ByteProperty && ByteProperty->Enum)
		{
			Enum = ByteProperty->Enum;
			Value = ByteProperty->GetPropertyValue_InContainer(Container);
		}
		const int64 Expected = Enum ? Enum->GetValueByNameString(Right) : INDEX_NONE;
		if (Expected == INDEX_NONE)
		{
			return {};
		}
		return (Value == Expected) == bEquals;
	}

	bool IsEditConditionMet(const FProperty& Property, const UStruct* Owner, const void* Container)
	{
		const FString* Condition = Property.FindMetaData(TEXT("EditCondition"));
		if (!Condition || Condition->IsEmpty())
		{
			return true;
		}
		TArray<FString> AnyOf;
		Condition->ParseIntoArray(AnyOf, TEXT("||"));
		for (const FString& Clause : AnyOf)
		{
			TArray<FString> AllOf;
			Clause.ParseIntoArray(AllOf, TEXT("&&"));
			bool bAll = true;
			for (const FString& Term : AllOf)
			{
				const TOptional<bool> Met = EvaluateConditionTerm(Term, Owner, Container);
				if (!Met.IsSet())
				{
					return true;
				}
				bAll &= Met.GetValue();
			}
			if (bAll)
			{
				return true;
			}
		}
		return false;
	}

	/** Structs whose components share the metadata (ClampMin, ...) of the property that holds them. */
	bool IsMathStruct(const UScriptStruct* Struct)
	{
		return Struct == TBaseStructure<FVector>::Get() || Struct == TBaseStructure<FVector2D>::Get() || Struct == TBaseStructure<FRotator>::Get()
			|| Struct == TBaseStructure<FVector4>::Get() || Struct == TBaseStructure<FQuat>::Get();
	}

	/** A pose field of view (not an offset / increase): it must be a valid FOV for UE's projection. */
	bool IsFieldOfView(const FProperty& Property)
	{
		return Property.GetName().EndsWith(TEXT("FieldOfView"));
	}

	struct FParameterWalker
	{
		const FCollector& Collector;
		const UACFCameraNode* Node = nullptr;
		int32 NodeIndex = INDEX_NONE;

		void VisitContainer(const UStruct* Struct, const void* Container, const FString& Prefix, bool bActive) const
		{
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				const FProperty* Property = *It;
				if (!Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient))
				{
					continue;
				}
				const bool bPropertyActive = bActive && IsEditConditionMet(*Property, Struct, Container);
				for (int32 ArrayIndex = 0; ArrayIndex < Property->ArrayDim; ++ArrayIndex)
				{
					FString Path = Prefix.IsEmpty() ? Property->GetName() : Prefix + TEXT(".") + Property->GetName();
					if (Property->ArrayDim > 1)
					{
						Path += FString::Printf(TEXT("[%d]"), ArrayIndex);
					}
					VisitValue(*Property, *Property, Property->ContainerPtrToValuePtr<void>(Container, ArrayIndex), Path, bPropertyActive);
				}
			}
		}

		/** MetaOwner is the property whose metadata applies (the FVector property for its X / Y / Z, the array for its elements). */
		void VisitValue(const FProperty& Property, const FProperty& MetaOwner, const void* Value, const FString& Path, bool bActive) const
		{
			if (const FFloatProperty* Float = CastField<FFloatProperty>(&Property))
			{
				CheckScalar(MetaOwner, Float->GetPropertyValue(Value), Path, bActive);
			}
			else if (const FDoubleProperty* Double = CastField<FDoubleProperty>(&Property))
			{
				CheckScalar(MetaOwner, Double->GetPropertyValue(Value), Path, bActive);
			}
			else if (const FStructProperty* Struct = CastField<FStructProperty>(&Property))
			{
				if (IsMathStruct(Struct->Struct))
				{
					for (TFieldIterator<FProperty> It(Struct->Struct); It; ++It)
					{
						VisitValue(**It, MetaOwner, It->ContainerPtrToValuePtr<void>(Value), Path + TEXT(".") + It->GetName(), bActive);
					}
				}
				else
				{
					VisitContainer(Struct->Struct, Value, Path, bActive);
				}
			}
			else if (const FArrayProperty* Array = CastField<FArrayProperty>(&Property))
			{
				FScriptArrayHelper Helper(Array, Value);
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					VisitValue(*Array->Inner, MetaOwner, Helper.GetRawPtr(Index), FString::Printf(TEXT("%s[%d]"), *Path, Index), bActive);
				}
			}
			// Object references (curves, presets) are validated by their own assets.
		}

		static FText OutOfRangeEffect(const FString& Path, bool bBelow)
		{
			if (!bBelow)
			{
				return FText::GetEmpty();
			}
			if (Path.Contains(TEXT("Damping")))
			{
				return LOCTEXT("NegativeDamping", " A negative damping time behaves as 0 (no smoothing).");
			}
			if (Path.EndsWith(TEXT("Radius")))
			{
				return LOCTEXT("NegativeRadius", " A sphere radius at or below 0 makes UE fall back to a line trace.");
			}
			if (Path.EndsWith(TEXT("Duration")))
			{
				return LOCTEXT("NegativeDuration", " A blend of 0 seconds or less is a cut.");
			}
			return FText::GetEmpty();
		}

		void CheckScalar(const FProperty& Meta, double Value, const FString& Path, bool bActive) const
		{
			if (!FMath::IsFinite(Value))
			{
				if (bActive)
				{
					Collector.Add(ESeverity::Error, TEXT("ACF.Param.NonFinite"), Node, NodeIndex,
						FText::Format(LOCTEXT("NonFinite", "{0} is {1}. The camera health checks reject the node output every frame (or the blend becomes a cut)."), FText::FromString(Path), FText::FromString(LexToString(Value))),
						LOCTEXT("NonFiniteFix", "Enter a finite number."), Path);
				}
				else
				{
					Collector.Add(ESeverity::Warning, TEXT("ACF.Param.NonFinite"), Node, NodeIndex,
						FText::Format(LOCTEXT("NonFiniteUnused", "{0} is {1}. It is not used while its node / option is off, but breaks the camera once enabled."), FText::FromString(Path), FText::FromString(LexToString(Value))),
						LOCTEXT("NonFiniteFix", "Enter a finite number."), Path);
				}
				return;
			}
			if (!bActive)
			{
				return; // ranges of unused parameters do not matter
			}
			if (IsFieldOfView(Meta) && !ACFCameraValidation::IsValidFov(static_cast<float>(Value)))
			{
				Collector.Add(ESeverity::Error, TEXT("ACF.Param.InvalidFov"), Node, NodeIndex,
					FText::Format(LOCTEXT("InvalidFov", "{0} is {1} degrees; UE's perspective projection needs a field of view above 0 and below 180."), FText::FromString(Path), FText::AsNumber(Value)),
					LOCTEXT("InvalidFovFix", "Use a field of view between 5 and 170 degrees."), Path);
				return;
			}
			// Ranges come from the property metadata (what the Details panel enforces); values outside them were set from C++,
			// Blueprint, Python or an older asset version.
			const FString* MinText = Meta.FindMetaData(TEXT("ClampMin"));
			const FString* MaxText = Meta.FindMetaData(TEXT("ClampMax"));
			const double Min = MinText ? FCString::Atod(**MinText) : 0.0;
			const double Max = MaxText ? FCString::Atod(**MaxText) : 0.0;
			const bool bBelow = MinText && Value < Min - 1e-4 * FMath::Max(1.0, FMath::Abs(Min));
			const bool bAbove = MaxText && Value > Max + 1e-4 * FMath::Max(1.0, FMath::Abs(Max));
			if (bBelow || bAbove)
			{
				Collector.Add(ESeverity::Warning, TEXT("ACF.Param.OutOfRange"), Node, NodeIndex,
					FText::Format(LOCTEXT("OutOfRange", "{0} is {1}, outside the supported range [{2}, {3}] (the Details panel would clamp it).{4}"),
						FText::FromString(Path), FText::AsNumber(Value), MinText ? FText::FromString(*MinText) : LOCTEXT("NoMin", "-"), MaxText ? FText::FromString(*MaxText) : LOCTEXT("NoMax", "-"),
						OutOfRangeEffect(Path, bBelow)),
					FText::Format(LOCTEXT("OutOfRangeFix", "Set it to {0}."), FText::FromString(bBelow ? *MinText : *MaxText)), Path);
			}
		}
	};

	/** Min > Max pairs the node itself swaps at runtime (it works, but the author probably mixed the fields up). */
	void CheckMinMax(const FCollector& C, const UACFCameraNode& Node, int32 Index, const TCHAR* MinName, float Min, const TCHAR* MaxName, float Max)
	{
		if (Min > Max)
		{
			C.Add(ESeverity::Warning, TEXT("ACF.Param.MinMaxInverted"), &Node, Index,
				FText::Format(LOCTEXT("MinMaxInverted", "{0} ({1}) is greater than {2} ({3}); the node swaps them at runtime."), FText::FromString(MinName), FText::AsNumber(Min), FText::FromString(MaxName), FText::AsNumber(Max)),
				FText::Format(LOCTEXT("MinMaxInvertedFix", "Swap {0} and {1}."), FText::FromString(MinName), FText::FromString(MaxName)), FString::Printf(TEXT("%s / %s"), MinName, MaxName));
		}
	}

	/** Near >= Far: the distance-dependent values collapse into a step (Lock-On) or always use the far values (Combat). */
	void CheckRange(const FCollector& C, const UACFCameraNode& Node, int32 Index, const TCHAR* Prefix, float Near, float Far, FText Effect)
	{
		if (Near >= Far)
		{
			C.Add(ESeverity::Warning, TEXT("ACF.Param.RangeInverted"), &Node, Index,
				FText::Format(LOCTEXT("RangeInverted", "NearRange ({0}) is not below FarRange ({1}): {2}"), FText::AsNumber(Near), FText::AsNumber(Far), Effect),
				LOCTEXT("RangeInvertedFix", "Make NearRange smaller than FarRange."), FString::Printf(TEXT("%sNearRange / %sFarRange"), Prefix, Prefix));
		}
	}

	void CheckClampedInto(const FCollector& C, const UACFCameraNode& Node, int32 Index, const TCHAR* Name, float Value, float Min, float Max)
	{
		const float Low = FMath::Min(Min, Max);
		const float High = FMath::Max(Min, Max);
		if (Value < Low || Value > High)
		{
			C.Add(ESeverity::Info, TEXT("ACF.Param.Normalized"), &Node, Index,
				FText::Format(LOCTEXT("ClampedInto", "{0} ({1}) is outside the pitch limits [{2}, {3}]; the node clamps it."), FText::FromString(Name), FText::AsNumber(Value), FText::AsNumber(Low), FText::AsNumber(High)),
				LOCTEXT("ClampedIntoFix", "Move it inside the pitch limits if the clamped value is not what you want."), Name);
		}
	}

	void CheckComposition(const FCollector& C, const UACFCameraNode& Node, int32 Index, const FACFCameraScreenComposition& Composition)
	{
		if (Composition.SoftZoneSize.X < Composition.DeadZoneSize.X || Composition.SoftZoneSize.Y < Composition.DeadZoneSize.Y)
		{
			C.Add(ESeverity::Info, TEXT("ACF.Param.Normalized"), &Node, Index,
				LOCTEXT("SoftZone", "Soft Zone Size is smaller than Dead Zone Size on an axis; the soft zone is widened to the dead zone at runtime."),
				LOCTEXT("SoftZoneFix", "Make Soft Zone Size at least Dead Zone Size."), TEXT("Composition.SoftZoneSize"));
		}
	}

	void CheckZeroDistance(const FCollector& C, const UACFCameraNode& Node, int32 Index, const TCHAR* Property, FText Message)
	{
		C.Add(ESeverity::Warning, TEXT("ACF.Param.ZeroDistance"), &Node, Index, MoveTemp(Message),
			LOCTEXT("ZeroDistanceFix", "Give the camera a distance from what it frames."), Property);
	}

	/** Relations between parameters of one enabled node (single values are checked by FParameterWalker). */
	void ValidateNodeSemantics(const FCollector& C, const UACFCameraNode& Node, int32 Index)
	{
		if (const UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(&Node))
		{
			CheckMinMax(C, Node, Index, TEXT("PitchMin"), Orbit->PitchMin, TEXT("PitchMax"), Orbit->PitchMax);
			if (Orbit->InitialRotation == EACFCameraOrbitInitialRotation::BehindTarget || Orbit->InitialRotation == EACFCameraOrbitInitialRotation::Fixed)
			{
				CheckClampedInto(C, Node, Index, TEXT("InitialPitch"), Orbit->InitialPitch, Orbit->PitchMin, Orbit->PitchMax);
			}
			const bool bNoRings = !Orbit->bUseRings || (Orbit->TopDistance <= UE_KINDA_SMALL_NUMBER && Orbit->BottomDistance <= UE_KINDA_SMALL_NUMBER);
			if (Orbit->Distance <= UE_KINDA_SMALL_NUMBER && bNoRings)
			{
				CheckZeroDistance(C, Node, Index, TEXT("Distance"), LOCTEXT("OrbitZero", "Distance is 0: the camera sits on the pivot, so the orbit only rotates it (and Collision has no arm to sweep)."));
			}
		}
		else if (const UACFCameraNode_LockOn* LockOn = Cast<UACFCameraNode_LockOn>(&Node))
		{
			CheckMinMax(C, Node, Index, TEXT("PitchMin"), LockOn->PitchMin, TEXT("PitchMax"), LockOn->PitchMax);
			CheckClampedInto(C, Node, Index, TEXT("BasePitch"), LockOn->BasePitch, LockOn->PitchMin, LockOn->PitchMax);
			CheckRange(C, Node, Index, TEXT(""), LockOn->NearRange, LockOn->FarRange, LOCTEXT("LockOnStep", "the arm length (and FOV) jump from the near to the far values at FarRange instead of blending."));
			if (LockOn->NearArmLength <= UE_KINDA_SMALL_NUMBER && LockOn->FarArmLength <= UE_KINDA_SMALL_NUMBER)
			{
				CheckZeroDistance(C, Node, Index, TEXT("NearArmLength / FarArmLength"), LOCTEXT("LockOnZero", "Both arm lengths are 0: the camera sits on the player's pivot."));
			}
		}
		else if (const UACFCameraNode_GroupFraming* Framing = Cast<UACFCameraNode_GroupFraming>(&Node))
		{
			CheckMinMax(C, Node, Index, TEXT("MinFieldOfView"), Framing->MinFieldOfView, TEXT("MaxFieldOfView"), Framing->MaxFieldOfView);
			if (!Framing->bFrameTargetGroup && !Framing->FollowTargetSubject.bInclude && !Framing->LookAtTargetSubject.bInclude)
			{
				C.Add(ESeverity::Warning, TEXT("ACF.Preset.FramingNoSubjects"), &Node, Index,
					LOCTEXT("FramingNoSubjects", "Group Framing has no subjects (Frame Target Group, Follow Target Subject and Look At Target Subject are all off); it does nothing."),
					LOCTEXT("FramingNoSubjectsFix", "Turn on at least one subject source, or remove the node."));
			}
		}
		else if (const UACFCameraNode_Combat* Combat = Cast<UACFCameraNode_Combat>(&Node))
		{
			CheckRange(C, Node, Index, TEXT("Profile."), Combat->Profile.NearRange, Combat->Profile.FarRange, LOCTEXT("CombatFar", "the enemy distance has no effect; the far values are always used."));
		}
		else if (const UACFCameraNode_RotationComposer* RotationComposer = Cast<UACFCameraNode_RotationComposer>(&Node))
		{
			CheckComposition(C, Node, Index, RotationComposer->Composition);
		}
		else if (const UACFCameraNode_PositionComposer* PositionComposer = Cast<UACFCameraNode_PositionComposer>(&Node))
		{
			CheckComposition(C, Node, Index, PositionComposer->Composition);
		}
		else if (const UACFCameraNode_ShotAnchor* Shot = Cast<UACFCameraNode_ShotAnchor>(&Node))
		{
			if (Shot->bAimAtAnchor && Shot->CameraOffset.IsNearlyZero(1.0))
			{
				CheckZeroDistance(C, Node, Index, TEXT("CameraOffset"), LOCTEXT("ShotZero", "Camera Offset is 0 with Aim At Anchor: the camera sits on the anchor and has no direction to look at it."));
			}
		}
	}

	// --- Structure --------------------------------------------------------------------------------------------------------

	enum EPoseComponent : uint8
	{
		PoseLocation,
		PoseRotation,
		PoseFieldOfView,
		PoseComponentCount
	};

	/** Pose components a built-in Body node overwrites (it ignores what earlier Body nodes wrote there). 0 for other nodes. */
	uint8 GetBodyWrites(const UACFCameraNode& Node)
	{
		constexpr uint8 L = 1 << PoseLocation;
		constexpr uint8 R = 1 << PoseRotation;
		constexpr uint8 F = 1 << PoseFieldOfView;
		if (Node.GetStage() != EACFCameraNodeStage::Body)
		{
			return 0;
		}
		if (const UACFCameraNode_FixedPose* Fixed = Cast<UACFCameraNode_FixedPose>(&Node))
		{
			return (Fixed->bOverrideLocation ? L : 0) | (Fixed->bOverrideRotation ? R : 0) | (Fixed->bOverrideFieldOfView ? F : 0);
		}
		if (const UACFCameraNode_ShotAnchor* Shot = Cast<UACFCameraNode_ShotAnchor>(&Node))
		{
			return L | (Shot->bAimAtAnchor ? R : 0) | (Shot->bOverrideFieldOfView ? F : 0);
		}
		if (const UACFCameraNode_OrbitalFollow* Orbit = Cast<UACFCameraNode_OrbitalFollow>(&Node))
		{
			return L | (Orbit->bControlRotation ? R : 0);
		}
		if (const UACFCameraNode_LockOn* LockOn = Cast<UACFCameraNode_LockOn>(&Node))
		{
			return L | R | (LockOn->bAdjustFieldOfView ? F : 0);
		}
		if (Node.IsA<UACFCameraNode_Follow>() || Node.IsA<UACFCameraNode_PositionComposer>())
		{
			return L;
		}
		return 0;
	}

	bool IsAimNode(const UACFCameraNode& Node)
	{
		return Node.IsA<UACFCameraNode_LookAt>() || Node.IsA<UACFCameraNode_RotationComposer>();
	}

	/** Nodes that place the camera away from a target: without Collision such a camera can end up inside geometry. */
	bool PlacesCameraAroundTarget(const UACFCameraNode& Node)
	{
		return Node.IsA<UACFCameraNode_Follow>() || Node.IsA<UACFCameraNode_OrbitalFollow>() || Node.IsA<UACFCameraNode_LockOn>()
			|| Node.IsA<UACFCameraNode_PositionComposer>() || Node.IsA<UACFCameraNode_ShotAnchor>();
	}

	struct FIndexedNode
	{
		const UACFCameraNode* Node = nullptr;
		int32 Index = INDEX_NONE;
	};

	FText JoinLabels(TConstArrayView<FIndexedNode> Nodes)
	{
		TArray<FText> Labels;
		for (const FIndexedNode& Entry : Nodes)
		{
			Labels.Add(NodeLabel(Entry.Node, Entry.Index));
		}
		return FText::Join(LOCTEXT("ListSeparator", ", "), Labels);
	}

	void ValidateStructure(const FCollector& C, const UACFCameraPreset& Preset)
	{
		const TArray<TObjectPtr<UACFCameraNode>>& Nodes = Preset.Nodes;
		if (Nodes.IsEmpty())
		{
			C.Add(ESeverity::Warning, TEXT("ACF.Preset.NoNodes"), nullptr, INDEX_NONE,
				LOCTEXT("NoNodes", "Preset has no nodes; it will reproduce the upstream UE camera unchanged."),
				LOCTEXT("NoNodesFix", "Add nodes (e.g. Orbital Follow + Collision), or do not request this preset."));
			return;
		}

		TMap<const UACFCameraNode*, int32> FirstSlot;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const UACFCameraNode* Node = Nodes[Index];
			if (!Node)
			{
				C.Add(ESeverity::Warning, TEXT("ACF.Preset.EmptyNodeSlot"), nullptr, Index,
					LOCTEXT("EmptyNodeSlot", "Node slot is empty and will be skipped."),
					LOCTEXT("EmptyNodeSlotFix", "Pick a node class or remove the slot."));
				continue;
			}
			if (const int32* First = FirstSlot.Find(Node))
			{
				C.Add(ESeverity::Warning, TEXT("ACF.Preset.DuplicateNodeReference"), Node, Index,
					FText::Format(LOCTEXT("DuplicateNodeReference", "The same node object is also in slot {0}; it is evaluated twice per frame."), *First),
					LOCTEXT("DuplicateNodeReferenceFix", "Remove the slot, or duplicate the node in the Details panel so each slot has its own object."));
				continue;
			}
			FirstSlot.Add(Node, Index);
			const int32 StageOrder = Node->GetStageOrder();
			if (StageOrder < -FACFCameraRigInstance::MaxStageOrder || StageOrder > FACFCameraRigInstance::MaxStageOrder)
			{
				C.Add(ESeverity::Warning, TEXT("ACF.Preset.StageOrderClamped"), Node, Index,
					FText::Format(LOCTEXT("StageOrderClamped", "GetStageOrder() returns {0}, outside +-{1}; it is clamped, so nodes beyond the limit tie and keep their list order."), StageOrder, FACFCameraRigInstance::MaxStageOrder),
					FText::Format(LOCTEXT("StageOrderClampedFix", "Return a stage order within +-{0}."), FACFCameraRigInstance::MaxStageOrder));
			}
		}

		// The rig evaluates by stage, then stage order; the list order only breaks ties (FACFCameraRigInstance).
		TArray<int32> Order;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (Nodes[Index] && Nodes[Index]->bEnabled && FirstSlot.FindRef(Nodes[Index]) == Index)
			{
				Order.Add(Index);
			}
		}
		const TArray<int32> ListOrder = Order;
		Algo::StableSortBy(Order, [&Nodes](int32 Index) { return FACFCameraRigInstance::GetEvaluationSortKey(Nodes[Index]); });
		if (Order != ListOrder)
		{
			TArray<FIndexedNode> Evaluated;
			for (const int32 Index : Order)
			{
				Evaluated.Add({ Nodes[Index], Index });
			}
			C.Add(ESeverity::Info, TEXT("ACF.Preset.EvaluationOrder"), nullptr, INDEX_NONE,
				FText::Format(LOCTEXT("EvaluationOrder", "The list order differs from the evaluation order (stage first, then the list): {0}."), JoinLabels(Evaluated)),
				LOCTEXT("EvaluationOrderFix", "Optional: reorder the list to match, so the preset reads the way it runs."));
		}

		TArray<FIndexedNode> Writers[PoseComponentCount];
		TArray<FIndexedNode> AimNodes;
		TArray<FIndexedNode> Combats;
		TArray<FIndexedNode> GroupFramings;
		TArray<FIndexedNode> Collisions;
		const UACFCameraNode* PositionComposer = nullptr;
		int32 PositionComposerIndex = INDEX_NONE;
		for (const int32 Index : Order)
		{
			const UACFCameraNode& Node = *Nodes[Index];
			const uint8 Writes = GetBodyWrites(Node);
			for (int32 Component = 0; Component < PoseComponentCount; ++Component)
			{
				if (Writes & (1 << Component))
				{
					Writers[Component].Add({ &Node, Index });
				}
			}
			if (IsAimNode(Node))
			{
				AimNodes.Add({ &Node, Index });
			}
			if (Node.IsA<UACFCameraNode_Combat>())
			{
				Combats.Add({ &Node, Index });
			}
			if (Node.IsA<UACFCameraNode_GroupFraming>())
			{
				GroupFramings.Add({ &Node, Index });
			}
			if (Node.IsA<UACFCameraNode_Collision>())
			{
				Collisions.Add({ &Node, Index });
			}
			if (Node.IsA<UACFCameraNode_PositionComposer>() && !PositionComposer)
			{
				PositionComposer = &Node;
				PositionComposerIndex = Index;
			}
		}

		// Body: two nodes that both overwrite the same pose component. Nodes writing different components (Fixed Pose with
		// FOV only + Orbital Follow, two Fixed Poses for location and FOV) are a valid split and are not reported.
		const FText ComponentNames[PoseComponentCount] = { LOCTEXT("Location", "location"), LOCTEXT("Rotation", "rotation"), LOCTEXT("FieldOfView", "field of view") };
		for (int32 Component = 0; Component < PoseComponentCount; ++Component)
		{
			if (Writers[Component].Num() > 1)
			{
				const FIndexedNode& Winner = Writers[Component].Last();
				C.Add(ESeverity::Warning, TEXT("ACF.Preset.BodyConflict"), Winner.Node, Winner.Index,
					FText::Format(LOCTEXT("BodyConflict", "Body nodes {0} all set the camera {1}; they run in that order and only the last one, {2}, is visible (the others only cost time and state)."),
						JoinLabels(Writers[Component]), ComponentNames[Component], NodeLabel(Winner.Node, Winner.Index)),
					LOCTEXT("BodyConflictFix", "Keep one node for this part of the pose (or turn the override off on the others, e.g. Fixed Pose Override Location)."));
			}
		}
		if (AimNodes.Num() > 1)
		{
			const FIndexedNode& Winner = AimNodes.Last();
			C.Add(ESeverity::Warning, TEXT("ACF.Preset.AimConflict"), Winner.Node, Winner.Index,
				FText::Format(LOCTEXT("AimConflict", "Aim nodes {0} all set the rotation; the last one, {1}, decides it."), JoinLabels(AimNodes), NodeLabel(Winner.Node, Winner.Index)),
				LOCTEXT("AimConflictFix", "Keep one Aim node (Look At or Rotation Composer)."));
		}
		if (PositionComposer && AimNodes.Num() > 0)
		{
			C.Add(ESeverity::Warning, TEXT("ACF.Preset.ComposerAim"), PositionComposer, PositionComposerIndex,
				LOCTEXT("ComposerAim", "Position Composer frames with the incoming rotation; an Aim node re-rotates afterwards and breaks that framing."),
				LOCTEXT("ComposerAimFix", "Remove the Aim node, or use Follow / Orbital Follow with the Aim node instead of Position Composer."));
		}
		else if (const FIndexedNode* LookAt = AimNodes.FindByPredicate([](const FIndexedNode& Entry) { return Entry.Node->IsA<UACFCameraNode_LookAt>(); });
			LookAt && Writers[PoseRotation].Num() > 0)
		{
			// Only Look At: it replaces the rotation. Rotation Composer starts from the incoming (Body) rotation, so Orbital
			// Follow + Rotation Composer is the intended "orbit, then compose" stack and is not reported.
			C.Add(ESeverity::Info, TEXT("ACF.Preset.RotationOverriddenByAim"), LookAt->Node, LookAt->Index,
				FText::Format(LOCTEXT("RotationOverriddenByAim", "{0} also sets the rotation, but Look At runs later (Aim stage) and replaces it."), JoinLabels(Writers[PoseRotation])),
				LOCTEXT("RotationOverriddenByAimFix", "Optional: turn the Body node's rotation off (Orbital Follow Control Rotation, Shot Anchor Aim At Anchor, Fixed Pose Override Rotation)."));
		}
		auto WarnDuplicates = [&C](const TArray<FIndexedNode>& Found, const TCHAR* Id, FText Message)
		{
			if (Found.Num() > 1)
			{
				C.Add(ESeverity::Warning, Id, Found.Last().Node, Found.Last().Index, FText::Format(LOCTEXT("Duplicates", "{0}: {1}."), Message, JoinLabels(Found)),
					LOCTEXT("DuplicatesFix", "Keep one of them (or disable the others while comparing settings)."));
			}
		};
		WarnDuplicates(Combats, TEXT("ACF.Preset.DuplicateCombat"), LOCTEXT("Combats", "More than one Combat Camera node; their distance / FOV / shoulder changes add up"));
		WarnDuplicates(GroupFramings, TEXT("ACF.Preset.DuplicateGroupFraming"), LOCTEXT("GroupFramings", "More than one Group Framing node; each one moves / zooms the camera again"));
		WarnDuplicates(Collisions, TEXT("ACF.Preset.DuplicateCollision"), LOCTEXT("Collisions", "More than one Collision node; each one sweeps again and the last one sets the output guard constraint"));

		// Collision is optional (fixed shots, zoom presets, cameras that never come near walls). Only noted.
		bool bAroundTarget = false;
		for (const int32 Index : Order)
		{
			bAroundTarget |= PlacesCameraAroundTarget(*Nodes[Index]);
		}
		if (bAroundTarget && !ACFCameraPresetValidation::HasCollision(&Preset))
		{
			C.Add(ESeverity::Info, TEXT("ACF.Collision.None"), nullptr, INDEX_NONE,
				LOCTEXT("NoCollision", "No Collision node: the camera is placed around a target but nothing keeps it out of walls, and the output guard has no constraint for it. Fine when that is intended."),
				LOCTEXT("NoCollisionFix", "Add a Collision node if the camera can come near geometry."));
		}
	}

	void ValidateBlend(const FCollector& C, const FACFCameraBlendSettings& Blend, const TCHAR* Name)
	{
		if (Blend.Curve == EACFCameraBlendCurve::Custom && !Blend.CustomCurve)
		{
			C.Add(ESeverity::Warning, TEXT("ACF.Blend.MissingCurve"), nullptr, INDEX_NONE,
				FText::Format(LOCTEXT("MissingCurve", "{0} uses Custom but has no curve; Linear will be used."), FText::FromString(Name)),
				LOCTEXT("MissingCurveFix", "Assign a Custom Curve or pick a built-in curve."), FString::Printf(TEXT("%s.CustomCurve"), Name));
		}
	}

	const UACFCameraNode_Collision* FindCollision(const UACFCameraPreset* Preset)
	{
		const UACFCameraNode_Collision* Found = nullptr;
		for (const UACFCameraNode* Node : Preset->Nodes)
		{
			if (const UACFCameraNode_Collision* Collision = Cast<UACFCameraNode_Collision>(Node); Collision && Collision->bEnabled)
			{
				Found = Collision; // the last one sets the constraint
			}
		}
		return Found;
	}
}

namespace ACFCameraPresetValidation
{
	using namespace ACFPresetValidationPrivate;

	void ValidatePreset(const UACFCameraPreset& Preset, TArray<FACFCameraPresetIssue>& OutIssues)
	{
		const FCollector C{ &Preset, OutIssues };
		ValidateStructure(C, Preset);

		// Parameters of every node object once (disabled nodes too: their values are checked as "unused").
		TSet<const UACFCameraNode*> Visited;
		for (int32 Index = 0; Index < Preset.Nodes.Num(); ++Index)
		{
			const UACFCameraNode* Node = Preset.Nodes[Index];
			if (!Node || Visited.Contains(Node))
			{
				continue;
			}
			Visited.Add(Node);
			const FParameterWalker Walker{ C, Node, Index };
			Walker.VisitContainer(Node->GetClass(), Node, FString(), Node->bEnabled);
			if (Node->bEnabled)
			{
				ValidateNodeSemantics(C, *Node, Index);
			}
		}

		// Preset-level parameters (default blends).
		const FParameterWalker PresetWalker{ C, nullptr, INDEX_NONE };
		PresetWalker.VisitContainer(UACFCameraPreset::StaticClass(), &Preset, FString(), true);
		ValidateBlend(C, Preset.DefaultBlendIn, TEXT("DefaultBlendIn"));
		ValidateBlend(C, Preset.DefaultBlendOut, TEXT("DefaultBlendOut"));
	}

	void ValidateTransition(const UACFCameraPreset* From, const UACFCameraPreset* To, TArray<FACFCameraPresetIssue>& OutIssues)
	{
		if (!From || !To || From == To)
		{
			return;
		}
		const FCollector C{ To, OutIssues };
		const bool bFrom = HasCollision(From);
		const bool bTo = HasCollision(To);
		const UACFCameraNode_Collision* ToCollision = FindCollision(To);
		const int32 ToIndex = ToCollision ? To->Nodes.IndexOfByPredicate([ToCollision](const UACFCameraNode* Node) { return Node == ToCollision; }) : INDEX_NONE;
		if (bFrom && !bTo)
		{
			C.Add(ESeverity::Warning, TEXT("ACF.Transition.CollisionDropped"), nullptr, INDEX_NONE,
				FText::Format(LOCTEXT("CollisionDropped", "{0} has Collision but {1} does not. A blend runs the PostBlend nodes of the incoming camera, so from the first frame of the blend into {1} nothing keeps the blended camera (or the output guard) out of walls."),
					PresetLabel(From), PresetLabel(To)),
				FText::Format(LOCTEXT("CollisionDroppedFix", "Add a Collision node to {0}, or cut / shorten the blend where the path can cross geometry."), PresetLabel(To)));
		}
		else if (!bFrom && bTo)
		{
			C.Add(ESeverity::Info, TEXT("ACF.Transition.CollisionAdded"), ToCollision, ToIndex,
				FText::Format(LOCTEXT("CollisionAdded", "{0} has no Collision but {1} does: the collision of {1} applies to the blended pose from the first frame of the blend."), PresetLabel(From), PresetLabel(To)),
				FText::GetEmpty());
		}
		else if (bFrom && bTo)
		{
			const UACFCameraNode_Collision* FromCollision = FindCollision(From);
			if (FromCollision && ToCollision && (FromCollision->ProbeChannel != ToCollision->ProbeChannel || !FMath::IsNearlyEqual(FromCollision->ProbeRadius, ToCollision->ProbeRadius)))
			{
				C.Add(ESeverity::Info, TEXT("ACF.Transition.CollisionSettingsDiffer"), ToCollision, ToIndex,
					FText::Format(LOCTEXT("CollisionSettingsDiffer", "The Collision probes of {0} and {1} differ (channel / radius); the probe of {1} is used for the whole blend."), PresetLabel(From), PresetLabel(To)),
					FText::GetEmpty());
			}
		}
	}

	bool HasCollision(const UACFCameraPreset* Preset)
	{
		if (!Preset)
		{
			return false;
		}
		for (const UACFCameraNode* Node : Preset->Nodes)
		{
			if (Node && Node->bEnabled && Node->GetStage() == EACFCameraNodeStage::PostBlend)
			{
				return true;
			}
		}
		return false;
	}

	EDataValidationResult Report(TConstArrayView<FACFCameraPresetIssue> Issues, FDataValidationContext& Context)
	{
		bool bHasError = false;
		for (const FACFCameraPresetIssue& Issue : Issues)
		{
			switch (Issue.Severity)
			{
			case EACFCameraPresetIssueSeverity::Error:
				Context.AddError(Issue.ToText());
				bHasError = true;
				break;
			case EACFCameraPresetIssueSeverity::Warning:
				Context.AddWarning(Issue.ToText());
				break;
			default:
				Context.AddMessage(EMessageSeverity::Info, Issue.ToText());
				break;
			}
		}
		return bHasError ? EDataValidationResult::Invalid : EDataValidationResult::Valid;
	}
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
