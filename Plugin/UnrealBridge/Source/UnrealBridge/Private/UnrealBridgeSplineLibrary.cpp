#include "UnrealBridgeSplineLibrary.h"

#include "Components/SplineComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeSplineLibrary"

namespace BridgeSplineImpl
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AActor* FindActor(const FString& ActorLabel)
	{
		UWorld* World = GetEditorWorld();
		if (!World || ActorLabel.IsEmpty())
		{
			return nullptr;
		}
		const FName ActorName(*ActorLabel);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorLabel() == ActorLabel || Actor->GetFName() == ActorName || Actor->GetPathName() == ActorLabel))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	USplineComponent* FindSpline(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}
		TArray<USplineComponent*> Splines;
		Actor->GetComponents(Splines);
		if (ComponentName.IsEmpty())
		{
			return Splines.Num() > 0 ? Splines[0] : nullptr;
		}
		for (USplineComponent* Spline : Splines)
		{
			if (Spline && Spline->GetName() == ComponentName)
			{
				return Spline;
			}
		}
		return nullptr;
	}

	ESplineCoordinateSpace::Type CoordSpace(bool bWorldSpace)
	{
		return bWorldSpace ? ESplineCoordinateSpace::World : ESplineCoordinateSpace::Local;
	}

	ESplinePointType::Type ParsePointType(const FString& PointType)
	{
		const FString Normalized = PointType.ToLower();
		if (Normalized == TEXT("linear")) return ESplinePointType::Linear;
		if (Normalized == TEXT("constant")) return ESplinePointType::Constant;
		if (Normalized == TEXT("curveclamped") || Normalized == TEXT("clamped")) return ESplinePointType::CurveClamped;
		if (Normalized == TEXT("curvecustomtangent") || Normalized == TEXT("custom")) return ESplinePointType::CurveCustomTangent;
		return ESplinePointType::Curve;
	}

	FString PointTypeToString(ESplinePointType::Type PointType)
	{
		switch (PointType)
		{
		case ESplinePointType::Linear: return TEXT("Linear");
		case ESplinePointType::Constant: return TEXT("Constant");
		case ESplinePointType::CurveClamped: return TEXT("CurveClamped");
		case ESplinePointType::CurveCustomTangent: return TEXT("CurveCustomTangent");
		case ESplinePointType::Curve:
		default:
			return TEXT("Curve");
		}
	}
}

TArray<FBridgeSplineInfo> UUnrealBridgeSplineLibrary::ListSplineComponents(const FString& ActorLabel)
{
	TArray<FBridgeSplineInfo> Result;
	UWorld* World = BridgeSplineImpl::GetEditorWorld();
	if (!World)
	{
		return Result;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor || (!ActorLabel.IsEmpty() && Actor->GetActorLabel() != ActorLabel && Actor->GetName() != ActorLabel))
		{
			continue;
		}
		TArray<USplineComponent*> Splines;
		Actor->GetComponents(Splines);
		for (USplineComponent* Spline : Splines)
		{
			if (!Spline)
			{
				continue;
			}
			FBridgeSplineInfo Info;
			Info.ActorLabel = Actor->GetActorLabel();
			Info.ActorPath = Actor->GetPathName();
			Info.ComponentName = Spline->GetName();
			Info.PointCount = Spline->GetNumberOfSplinePoints();
			Info.bClosedLoop = Spline->IsClosedLoop();
			Info.Length = Spline->GetSplineLength();
			Result.Add(Info);
		}
	}
	return Result;
}

FString UUnrealBridgeSplineLibrary::AddSplineComponent(const FString& ActorLabel, const FString& ComponentName)
{
	AActor* Actor = BridgeSplineImpl::FindActor(ActorLabel);
	if (!Actor)
	{
		return FString();
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddSplineComponent", "Bridge: Add Spline Component"));
	Actor->Modify();

	const FName DesiredName(*ComponentName);
	USplineComponent* Spline = NewObject<USplineComponent>(Actor, DesiredName, RF_Transactional);
	if (!Spline)
	{
		return FString();
	}
	Spline->SetMobility(EComponentMobility::Movable);
	if (USceneComponent* Root = Actor->GetRootComponent())
	{
		Spline->SetupAttachment(Root);
	}
	Actor->AddInstanceComponent(Spline);
	Spline->RegisterComponent();
	Actor->MarkPackageDirty();
	return Spline->GetName();
}

TArray<FBridgeSplinePointInfo> UUnrealBridgeSplineLibrary::GetSplinePoints(
	const FString& ActorLabel,
	const FString& ComponentName,
	bool bWorldSpace)
{
	TArray<FBridgeSplinePointInfo> Result;
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline)
	{
		return Result;
	}
	const ESplineCoordinateSpace::Type Space = BridgeSplineImpl::CoordSpace(bWorldSpace);
	const int32 Count = Spline->GetNumberOfSplinePoints();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FBridgeSplinePointInfo Info;
		Info.Index = Index;
		Info.Location = Spline->GetLocationAtSplinePoint(Index, Space);
		Info.Tangent = Spline->GetTangentAtSplinePoint(Index, Space);
		Info.Rotation = Spline->GetRotationAtSplinePoint(Index, Space);
		Info.Scale = Spline->GetScaleAtSplinePoint(Index);
		Info.PointType = BridgeSplineImpl::PointTypeToString(Spline->GetSplinePointType(Index));
		Result.Add(Info);
	}
	return Result;
}

bool UUnrealBridgeSplineLibrary::AddSplinePoint(
	const FString& ActorLabel,
	const FVector& Location,
	const FString& ComponentName,
	int32 Index,
	bool bWorldSpace,
	const FString& PointType,
	bool bUpdateSpline)
{
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline)
	{
		return false;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddSplinePoint", "Bridge: Add Spline Point"));
	Spline->Modify();
	const int32 Count = Spline->GetNumberOfSplinePoints();
	if (Index >= 0 && Index <= Count)
	{
		Spline->AddSplinePointAtIndex(Location, Index, BridgeSplineImpl::CoordSpace(bWorldSpace), bUpdateSpline);
		Spline->SetSplinePointType(Index, BridgeSplineImpl::ParsePointType(PointType), bUpdateSpline);
	}
	else
	{
		Spline->AddSplinePoint(Location, BridgeSplineImpl::CoordSpace(bWorldSpace), bUpdateSpline);
		Spline->SetSplinePointType(Spline->GetNumberOfSplinePoints() - 1, BridgeSplineImpl::ParsePointType(PointType), bUpdateSpline);
	}
	Spline->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSplineLibrary::SetSplinePointLocation(
	const FString& ActorLabel,
	int32 Index,
	const FVector& Location,
	const FString& ComponentName,
	bool bWorldSpace,
	bool bUpdateSpline)
{
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline || Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetSplinePointLocation", "Bridge: Set Spline Point Location"));
	Spline->Modify();
	Spline->SetLocationAtSplinePoint(Index, Location, BridgeSplineImpl::CoordSpace(bWorldSpace), bUpdateSpline);
	Spline->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSplineLibrary::SetSplinePointType(
	const FString& ActorLabel,
	int32 Index,
	const FString& PointType,
	const FString& ComponentName,
	bool bUpdateSpline)
{
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline || Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetSplinePointType", "Bridge: Set Spline Point Type"));
	Spline->Modify();
	Spline->SetSplinePointType(Index, BridgeSplineImpl::ParsePointType(PointType), bUpdateSpline);
	Spline->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSplineLibrary::RemoveSplinePoint(
	const FString& ActorLabel,
	int32 Index,
	const FString& ComponentName,
	bool bUpdateSpline)
{
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline || Index < 0 || Index >= Spline->GetNumberOfSplinePoints())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeRemoveSplinePoint", "Bridge: Remove Spline Point"));
	Spline->Modify();
	Spline->RemoveSplinePoint(Index, bUpdateSpline);
	Spline->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSplineLibrary::ClearSplinePoints(
	const FString& ActorLabel,
	const FString& ComponentName,
	bool bUpdateSpline)
{
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeClearSplinePoints", "Bridge: Clear Spline Points"));
	Spline->Modify();
	Spline->ClearSplinePoints(bUpdateSpline);
	Spline->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSplineLibrary::SetSplineClosedLoop(
	const FString& ActorLabel,
	bool bClosedLoop,
	const FString& ComponentName,
	bool bUpdateSpline)
{
	USplineComponent* Spline = BridgeSplineImpl::FindSpline(BridgeSplineImpl::FindActor(ActorLabel), ComponentName);
	if (!Spline)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetSplineClosedLoop", "Bridge: Set Spline Closed Loop"));
	Spline->Modify();
	Spline->SetClosedLoop(bClosedLoop, bUpdateSpline);
	Spline->MarkPackageDirty();
	return true;
}

#undef LOCTEXT_NAMESPACE
