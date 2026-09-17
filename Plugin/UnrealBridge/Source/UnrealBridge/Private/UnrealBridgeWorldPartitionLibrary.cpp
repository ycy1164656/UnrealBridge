#include "UnrealBridgeWorldPartitionLibrary.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "WorldPartition/DataLayer/DataLayerAsset.h"
#include "WorldPartition/DataLayer/DataLayerInstance.h"
#include "WorldPartition/DataLayer/DataLayerManager.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

namespace BridgeWorldPartitionImpl
{
	FString WorldTypeToString(EWorldType::Type Type)
	{
		switch (Type)
		{
		case EWorldType::None: return TEXT("None");
		case EWorldType::Game: return TEXT("Game");
		case EWorldType::Editor: return TEXT("Editor");
		case EWorldType::PIE: return TEXT("PIE");
		case EWorldType::EditorPreview: return TEXT("EditorPreview");
		case EWorldType::GamePreview: return TEXT("GamePreview");
		case EWorldType::GameRPC: return TEXT("GameRPC");
		case EWorldType::Inactive: return TEXT("Inactive");
		default: return TEXT("Other");
		}
	}

	FString NetModeToString(ENetMode Mode)
	{
		switch (Mode)
		{
		case NM_Standalone: return TEXT("Standalone");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_Client: return TEXT("Client");
		default: return TEXT("Unknown");
		}
	}

	FString PriorityToString(EStreamingSourcePriority Priority)
	{
		switch (Priority)
		{
		case EStreamingSourcePriority::Highest: return TEXT("Highest");
		case EStreamingSourcePriority::High: return TEXT("High");
		case EStreamingSourcePriority::Normal: return TEXT("Normal");
		case EStreamingSourcePriority::Low: return TEXT("Low");
		case EStreamingSourcePriority::Lowest: return TEXT("Lowest");
		default: return FString::FromInt(static_cast<uint8>(Priority));
		}
	}

	FString DataLayerTypeToString(EDataLayerType Type)
	{
		switch (Type)
		{
		case EDataLayerType::Runtime: return TEXT("Runtime");
		case EDataLayerType::Editor: return TEXT("Editor");
		default: return TEXT("Unknown");
		}
	}

	FBridgeWorldPartitionStreamingSourceInfo MakeStreamingSourceInfo(const FWorldPartitionStreamingSource& Source)
	{
		FBridgeWorldPartitionStreamingSourceInfo Info;
		Info.Name = Source.Name.ToString();
		Info.Location = Source.Location;
		Info.Rotation = Source.Rotation;
		Info.Velocity = Source.Velocity;
		Info.TargetState = GetStreamingSourceTargetStateName(Source.TargetState);
		Info.Priority = PriorityToString(Source.Priority);
		Info.TargetBehavior = Source.TargetBehavior == EStreamingSourceTargetBehavior::Include
			? TEXT("Include") : TEXT("Exclude");
		for (const FName Grid : Source.TargetGrids)
		{
			Info.TargetGrids.Add(Grid.ToString());
		}
		Info.TargetGrids.Sort();
		Info.ShapeCount = Source.Shapes.Num();
		Info.bBlockOnSlowLoading = Source.bBlockOnSlowLoading;
		Info.bRemote = Source.bRemote;
		Info.bReplay = Source.bReplay;
		Info.bForce2D = Source.bForce2D;
		return Info;
	}

	FBridgeDataLayerRuntimeInfo MakeDataLayerInfo(const UDataLayerInstance* Instance)
	{
		FBridgeDataLayerRuntimeInfo Info;
		if (!Instance)
		{
			return Info;
		}
		Info.Name = Instance->GetDataLayerShortName();
		Info.FullName = Instance->GetDataLayerFullName();
		Info.ObjectPath = Instance->GetPathName();
		if (const UDataLayerAsset* Asset = Instance->GetAsset())
		{
			Info.AssetPath = Asset->GetPathName();
		}
		if (const UDataLayerInstance* Parent = Instance->GetParent())
		{
			Info.ParentPath = Parent->GetPathName();
		}
		Info.Type = DataLayerTypeToString(Instance->GetType());
		Info.InitialRuntimeState = GetDataLayerRuntimeStateName(Instance->GetInitialRuntimeState());
		Info.RuntimeState = GetDataLayerRuntimeStateName(Instance->GetRuntimeState());
		Info.EffectiveRuntimeState = GetDataLayerRuntimeStateName(Instance->GetEffectiveRuntimeState());
		Info.ChildCount = Instance->GetChildren().Num();
		Info.bRuntime = Instance->IsRuntime();
		Info.bClientOnly = Instance->IsClientOnly();
		Info.bServerOnly = Instance->IsServerOnly();
#if WITH_EDITOR
		Info.StreamingPriority = Instance->GetStreamingPriority();
		Info.bVisibleInEditor = Instance->IsEffectiveVisible();
		Info.bLoadedInEditor = Instance->IsLoadedInEditor();
		Info.bEffectiveLoadedInEditor = Instance->IsEffectiveLoadedInEditor();
#endif
		return Info;
	}

	void AddIssue(
		TArray<FBridgeWorldPartitionIssue>& Issues,
		int32 MaxIssues,
		const FString& Severity,
		const FString& Code,
		const FString& Message,
		const FString& ObjectPath)
	{
		if (Issues.Num() >= MaxIssues)
		{
			return;
		}
		FBridgeWorldPartitionIssue& Issue = Issues.AddDefaulted_GetRef();
		Issue.Severity = Severity;
		Issue.Code = Code;
		Issue.Message = Message;
		Issue.ObjectPath = ObjectPath;
	}

	bool IsRelevantWorld(const UWorld* World)
	{
		return World
			&& World->WorldType != EWorldType::None
			&& World->WorldType != EWorldType::Inactive;
	}

	FBridgeWorldPartitionSnapshot MakeSnapshot(
		const FWorldContext& Context,
		int32 MaxStreamingSources,
		int32 MaxDataLayers)
	{
		FBridgeWorldPartitionSnapshot Snapshot;
		UWorld* World = Context.World();
		if (!World)
		{
			return Snapshot;
		}
		Snapshot.World = World->GetPathName();
		Snapshot.WorldPackage = World->GetPackage() ? World->GetPackage()->GetName() : FString();
		Snapshot.WorldType = WorldTypeToString(World->WorldType);
		Snapshot.NetMode = NetModeToString(World->GetNetMode());
		Snapshot.PIEInstance = Context.PIEInstance;

		UWorldPartition* WorldPartition = World->GetWorldPartition();
		Snapshot.bPartitioned = WorldPartition != nullptr;
		if (!WorldPartition)
		{
			return Snapshot;
		}
		Snapshot.bInitialized = WorldPartition->IsInitialized();
		if (UWorldPartitionSubsystem* Subsystem = World->GetSubsystem<UWorldPartitionSubsystem>())
		{
			Snapshot.bStreamingCompleted = Subsystem->IsAllStreamingCompleted();
		}

		const TArray<FWorldPartitionStreamingSource>& Sources = WorldPartition->GetStreamingSources();
		Snapshot.TotalStreamingSourceCount = Sources.Num();
		for (int32 Index = 0; Index < FMath::Min(Sources.Num(), MaxStreamingSources); ++Index)
		{
			Snapshot.StreamingSources.Add(MakeStreamingSourceInfo(Sources[Index]));
		}

		if (UDataLayerManager* Manager = UDataLayerManager::GetDataLayerManager(World))
		{
			TArray<UDataLayerInstance*> Instances = Manager->GetDataLayerInstances();
			Instances.Sort([](const UDataLayerInstance& A, const UDataLayerInstance& B)
			{
				return A.GetDataLayerFullName() < B.GetDataLayerFullName();
			});
			Snapshot.TotalDataLayerCount = Instances.Num();
			for (int32 Index = 0; Index < FMath::Min(Instances.Num(), MaxDataLayers); ++Index)
			{
				Snapshot.DataLayers.Add(MakeDataLayerInfo(Instances[Index]));
			}
		}
		return Snapshot;
	}

	void ValidateSnapshot(
		const FBridgeWorldPartitionSnapshot& Snapshot,
		bool bWarnWhenStreamingIncomplete,
		int32 MaxIssues,
		TArray<FBridgeWorldPartitionIssue>& OutIssues)
	{
		if (!Snapshot.bPartitioned)
		{
			return;
		}
		if (!Snapshot.bInitialized)
		{
			AddIssue(OutIssues, MaxIssues, TEXT("error"), TEXT("WP_NOT_INITIALIZED"),
				TEXT("World Partition exists but is not initialized."), Snapshot.World);
		}
		if (bWarnWhenStreamingIncomplete && !Snapshot.bStreamingCompleted)
		{
			AddIssue(OutIssues, MaxIssues, TEXT("warning"), TEXT("WP_STREAMING_INCOMPLETE"),
				TEXT("World Partition streaming has not reached a stable completed state."), Snapshot.World);
		}
		for (const FBridgeDataLayerRuntimeInfo& Layer : Snapshot.DataLayers)
		{
			if (Layer.bClientOnly && Layer.bServerOnly)
			{
				AddIssue(OutIssues, MaxIssues, TEXT("error"), TEXT("DL_CONFLICTING_LOAD_FILTER"),
					TEXT("Data Layer reports both client-only and server-only."), Layer.ObjectPath);
			}
			if (Layer.bRuntime && Layer.Type != TEXT("Runtime"))
			{
				AddIssue(OutIssues, MaxIssues, TEXT("warning"), TEXT("DL_RUNTIME_TYPE_MISMATCH"),
					TEXT("Data Layer runtime flag and declared type disagree."), Layer.ObjectPath);
			}
		}
	}
}

TArray<FBridgeWorldPartitionSnapshot> UUnrealBridgeWorldPartitionLibrary::GetWorldPartitionSnapshots(
	int32 MaxStreamingSourcesPerWorld,
	int32 MaxDataLayersPerWorld)
{
	TArray<FBridgeWorldPartitionSnapshot> Result;
	if (!GEngine)
	{
		return Result;
	}
	MaxStreamingSourcesPerWorld = FMath::Clamp(MaxStreamingSourcesPerWorld, 0, 4096);
	MaxDataLayersPerWorld = FMath::Clamp(MaxDataLayersPerWorld, 0, 16384);
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (BridgeWorldPartitionImpl::IsRelevantWorld(Context.World()))
		{
			Result.Add(BridgeWorldPartitionImpl::MakeSnapshot(
				Context, MaxStreamingSourcesPerWorld, MaxDataLayersPerWorld));
		}
	}
	return Result;
}

TArray<FBridgeWorldPartitionIssue> UUnrealBridgeWorldPartitionLibrary::ValidateWorldPartitionRuntime(
	bool bWarnWhenStreamingIncomplete,
	int32 MaxIssues)
{
	TArray<FBridgeWorldPartitionIssue> Issues;
	MaxIssues = FMath::Clamp(MaxIssues, 1, 16384);
	const TArray<FBridgeWorldPartitionSnapshot> Snapshots = GetWorldPartitionSnapshots(4096, 16384);
	for (const FBridgeWorldPartitionSnapshot& Snapshot : Snapshots)
	{
		BridgeWorldPartitionImpl::ValidateSnapshot(
			Snapshot, bWarnWhenStreamingIncomplete, MaxIssues, Issues);
		if (Issues.Num() >= MaxIssues)
		{
			break;
		}
	}
	return Issues;
}
