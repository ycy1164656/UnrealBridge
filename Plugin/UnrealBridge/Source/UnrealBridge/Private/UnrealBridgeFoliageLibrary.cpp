#include "UnrealBridgeFoliageLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "FoliageType.h"
#include "InstancedFoliage.h"
#include "InstancedFoliageActor.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeFoliageLibrary"

namespace BridgeFoliageImpl
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AInstancedFoliageActor* GetIFA(bool bCreate)
	{
		UWorld* World = GetEditorWorld();
		return World ? AInstancedFoliageActor::GetInstancedFoliageActorForCurrentLevel(World, bCreate) : nullptr;
	}

	UFoliageType* LoadFoliageType(const FString& FoliageTypePath)
	{
		return LoadObject<UFoliageType>(nullptr, *FoliageTypePath);
	}

	FString SourcePath(UFoliageType* FoliageType)
	{
		if (!FoliageType)
		{
			return FString();
		}
		if (UObject* Source = FoliageType->GetSource())
		{
			return Source->GetPathName();
		}
		return FString();
	}

	int32 RemoveInstances(UFoliageType* FoliageType, const TArray<int32>& Indices)
	{
		if (!FoliageType || Indices.Num() == 0)
		{
			return 0;
		}
		AInstancedFoliageActor* IFA = GetIFA(false);
		if (!IFA)
		{
			return 0;
		}
		FFoliageInfo* Info = IFA->FindInfo(FoliageType);
		if (!Info)
		{
			return 0;
		}
		IFA->Modify();
		Info->RemoveInstances(MakeArrayView(Indices), true);
		IFA->MarkPackageDirty();
		return Indices.Num();
	}
}

TArray<FBridgeFoliageTypeInfo> UUnrealBridgeFoliageLibrary::ListFoliageTypes(
	const FString& PackagePath,
	int32 MaxResults)
{
	TArray<FBridgeFoliageTypeInfo> Result;
	const int32 Limit = MaxResults <= 0 ? MAX_int32 : MaxResults;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	TArray<FAssetData> Assets;
	AssetRegistryModule.Get().GetAssetsByPath(FName(*PackagePath), Assets, true);
	for (const FAssetData& Asset : Assets)
	{
		if (Result.Num() >= Limit)
		{
			break;
		}
		const FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (!ClassName.Contains(TEXT("FoliageType")))
		{
			continue;
		}
		FBridgeFoliageTypeInfo Info;
		Info.Path = Asset.GetSoftObjectPath().ToString();
		Info.Name = Asset.AssetName.ToString();
		Info.ClassName = ClassName;
		if (UFoliageType* Type = Cast<UFoliageType>(Asset.GetAsset()))
		{
			Info.SourcePath = BridgeFoliageImpl::SourcePath(Type);
		}
		Result.Add(Info);
	}
	return Result;
}

TArray<FBridgeFoliageInstanceStats> UUnrealBridgeFoliageLibrary::GetCurrentLevelFoliageStats()
{
	TArray<FBridgeFoliageInstanceStats> Result;
	AInstancedFoliageActor* IFA = BridgeFoliageImpl::GetIFA(false);
	if (!IFA)
	{
		return Result;
	}
	for (const TPair<UFoliageType*, TUniqueObj<FFoliageInfo>>& Pair : IFA->GetFoliageInfos())
	{
		UFoliageType* Type = Pair.Key;
		const FFoliageInfo& Info = Pair.Value.Get();
		FBridgeFoliageInstanceStats Stats;
		Stats.FoliageTypePath = Type ? Type->GetPathName() : FString();
		Stats.SourcePath = BridgeFoliageImpl::SourcePath(Type);
		Stats.InstanceCount = Info.GetPlacedInstanceCount();
		Result.Add(Stats);
	}
	return Result;
}

int32 UUnrealBridgeFoliageLibrary::AddFoliageInstance(
	const FString& FoliageTypePath,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale)
{
	UFoliageType* Type = BridgeFoliageImpl::LoadFoliageType(FoliageTypePath);
	AInstancedFoliageActor* IFA = BridgeFoliageImpl::GetIFA(true);
	if (!Type || !IFA)
	{
		return 0;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeAddFoliageInstance", "Bridge: Add Foliage Instance"));
	IFA->Modify();
	FFoliageInstance Instance;
	Instance.SetInstanceWorldTransform(FTransform(Rotation, Location, Scale));
	FFoliageInfo* Info = nullptr;
	UFoliageType* LocalType = IFA->AddFoliageType(Type, &Info);
	if (!LocalType || !Info)
	{
		return 0;
	}
	Info->AddInstance(LocalType, Instance);
	IFA->MarkPackageDirty();
	return 1;
}

int32 UUnrealBridgeFoliageLibrary::AddFoliageInstances(
	const FString& FoliageTypePath,
	const TArray<FTransform>& Transforms)
{
	UFoliageType* Type = BridgeFoliageImpl::LoadFoliageType(FoliageTypePath);
	AInstancedFoliageActor* IFA = BridgeFoliageImpl::GetIFA(true);
	if (!Type || !IFA || Transforms.Num() == 0)
	{
		return 0;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeAddFoliageInstances", "Bridge: Add Foliage Instances"));
	IFA->Modify();
	FFoliageInfo* Info = nullptr;
	UFoliageType* LocalType = IFA->AddFoliageType(Type, &Info);
	if (!LocalType || !Info)
	{
		return 0;
	}
	int32 Added = 0;
	for (const FTransform& Transform : Transforms)
	{
		FFoliageInstance Instance;
		Instance.SetInstanceWorldTransform(Transform);
		Info->AddInstance(LocalType, Instance);
		++Added;
	}
	IFA->MarkPackageDirty();
	return Added;
}

int32 UUnrealBridgeFoliageLibrary::RemoveFoliageInstancesInBox(
	const FString& FoliageTypePath,
	const FVector& BoundsMin,
	const FVector& BoundsMax)
{
	UFoliageType* Type = BridgeFoliageImpl::LoadFoliageType(FoliageTypePath);
	AInstancedFoliageActor* IFA = BridgeFoliageImpl::GetIFA(false);
	if (!Type || !IFA)
	{
		return 0;
	}
	FFoliageInfo* Info = IFA->FindInfo(Type);
	if (!Info)
	{
		return 0;
	}
	TArray<int32> Indices = Info->GetInstancesOverlappingBox(FBox(BoundsMin, BoundsMax));
	const FScopedTransaction Transaction(LOCTEXT("BridgeRemoveFoliageBox", "Bridge: Remove Foliage Instances In Box"));
	return BridgeFoliageImpl::RemoveInstances(Type, Indices);
}

int32 UUnrealBridgeFoliageLibrary::RemoveFoliageInstancesInSphere(
	const FString& FoliageTypePath,
	const FVector& Center,
	float Radius)
{
	UFoliageType* Type = BridgeFoliageImpl::LoadFoliageType(FoliageTypePath);
	AInstancedFoliageActor* IFA = BridgeFoliageImpl::GetIFA(false);
	if (!Type || !IFA || Radius <= 0.0f)
	{
		return 0;
	}
	FFoliageInfo* Info = IFA->FindInfo(Type);
	if (!Info)
	{
		return 0;
	}
	TArray<int32> Indices;
	Info->GetInstancesInsideSphere(FSphere(Center, Radius), Indices);
	const FScopedTransaction Transaction(LOCTEXT("BridgeRemoveFoliageSphere", "Bridge: Remove Foliage Instances In Sphere"));
	return BridgeFoliageImpl::RemoveInstances(Type, Indices);
}

#undef LOCTEXT_NAMESPACE
