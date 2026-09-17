#include "UnrealBridgeAILibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "EditorAssetLibrary.h"
#include "EnvironmentQuery/EnvQueryInstanceBlueprintWrapper.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/PackageName.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace BridgeAIImpl
{
	FString PackageNameFor(const FString& Path, const FString& Name)
	{
		const FString EffectivePath = Path.IsEmpty() ? TEXT("/Game") : Path;
		return EffectivePath / Name;
	}

	FString ObjectPathForPackage(const FString& PackageName)
	{
		return FString::Printf(TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName));
	}

	template <typename AssetType>
	AssetType* CreateAsset(const FString& Path, const FString& Name)
	{
		const FString PackageName = PackageNameFor(Path, Name);
		if (AssetType* Existing = LoadObject<AssetType>(nullptr, *ObjectPathForPackage(PackageName)))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}
		Package->FullyLoad();
		AssetType* Asset = NewObject<AssetType>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
		if (Asset)
		{
			FAssetRegistryModule::AssetCreated(Asset);
			Asset->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(PackageName, false);
		}
		return Asset;
	}

	UClass* ResolveBlackboardKeyType(const FString& KeyType)
	{
		const FString Normalized = KeyType.ToLower();
		if (Normalized == TEXT("bool")) return UBlackboardKeyType_Bool::StaticClass();
		if (Normalized == TEXT("int")) return UBlackboardKeyType_Int::StaticClass();
		if (Normalized == TEXT("float")) return UBlackboardKeyType_Float::StaticClass();
		if (Normalized == TEXT("name")) return UBlackboardKeyType_Name::StaticClass();
		if (Normalized == TEXT("string")) return UBlackboardKeyType_String::StaticClass();
		if (Normalized == TEXT("vector")) return UBlackboardKeyType_Vector::StaticClass();
		if (Normalized == TEXT("rotator")) return UBlackboardKeyType_Rotator::StaticClass();
		if (Normalized == TEXT("class")) return UBlackboardKeyType_Class::StaticClass();
		if (Normalized == TEXT("enum")) return UBlackboardKeyType_Enum::StaticClass();
		return UBlackboardKeyType_Object::StaticClass();
	}

	bool IsRuntimeWorld(const UWorld* World)
	{
		return World
			&& (World->WorldType == EWorldType::PIE
				|| World->WorldType == EWorldType::Game
				|| World->WorldType == EWorldType::GamePreview);
	}

	FString EQSStatusToString(EEnvQueryStatus::Type Status)
	{
		switch (Status)
		{
		case EEnvQueryStatus::Processing: return TEXT("Processing");
		case EEnvQueryStatus::Success: return TEXT("Success");
		case EEnvQueryStatus::Failed: return TEXT("Failed");
		case EEnvQueryStatus::Aborted: return TEXT("Aborted");
		case EEnvQueryStatus::OwnerLost: return TEXT("OwnerLost");
		case EEnvQueryStatus::MissingParam: return TEXT("MissingParam");
		default: return TEXT("Unknown");
		}
	}
}

FString UUnrealBridgeAILibrary::CreateBehaviorTree(const FString& Path, const FString& Name)
{
	UBehaviorTree* Asset = BridgeAIImpl::CreateAsset<UBehaviorTree>(Path, Name);
	return Asset ? Asset->GetOutermost()->GetName() : FString();
}

FString UUnrealBridgeAILibrary::CreateBlackboard(const FString& Path, const FString& Name)
{
	UBlackboardData* Asset = BridgeAIImpl::CreateAsset<UBlackboardData>(Path, Name);
	return Asset ? Asset->GetOutermost()->GetName() : FString();
}

bool UUnrealBridgeAILibrary::SetBehaviorTreeBlackboard(const FString& BehaviorTreePath, const FString& BlackboardPath)
{
	UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *BehaviorTreePath);
	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!Tree || !Blackboard)
	{
		return false;
	}
	Tree->Modify();
	Tree->BlackboardAsset = Blackboard;
	Tree->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Tree->GetOutermost()->GetName(), false);
	return true;
}

bool UUnrealBridgeAILibrary::AddBlackboardKey(
	const FString& BlackboardPath,
	const FString& KeyName,
	const FString& KeyType,
	const FString& BaseClassPath,
	const FString& EnumPath)
{
	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!Blackboard || KeyName.IsEmpty())
	{
		return false;
	}
	for (const FBlackboardEntry& Existing : Blackboard->Keys)
	{
		if (Existing.EntryName == FName(*KeyName))
		{
			return true;
		}
	}

	UClass* KeyTypeClass = BridgeAIImpl::ResolveBlackboardKeyType(KeyType);
	UBlackboardKeyType* KeyTypeObject = NewObject<UBlackboardKeyType>(Blackboard, KeyTypeClass, NAME_None, RF_Transactional);
	if (!KeyTypeObject)
	{
		return false;
	}
	if (UBlackboardKeyType_Object* ObjectType = Cast<UBlackboardKeyType_Object>(KeyTypeObject))
	{
		ObjectType->BaseClass = !BaseClassPath.IsEmpty() ? LoadClass<UObject>(nullptr, *BaseClassPath) : UObject::StaticClass();
	}
	if (UBlackboardKeyType_Class* ClassType = Cast<UBlackboardKeyType_Class>(KeyTypeObject))
	{
		ClassType->BaseClass = !BaseClassPath.IsEmpty() ? LoadClass<UObject>(nullptr, *BaseClassPath) : UObject::StaticClass();
	}
	if (UBlackboardKeyType_Enum* EnumType = Cast<UBlackboardKeyType_Enum>(KeyTypeObject))
	{
		EnumType->EnumType = !EnumPath.IsEmpty() ? LoadObject<UEnum>(nullptr, *EnumPath) : nullptr;
	}

	FBlackboardEntry Entry;
	Entry.EntryName = FName(*KeyName);
	Entry.KeyType = KeyTypeObject;
	Blackboard->Modify();
	Blackboard->Keys.Add(Entry);
	Blackboard->MarkPackageDirty();
	UEditorAssetLibrary::SaveAsset(Blackboard->GetOutermost()->GetName(), false);
	return true;
}

TArray<FBridgeBlackboardKeyInfo> UUnrealBridgeAILibrary::GetBlackboardKeys(const FString& BlackboardPath)
{
	TArray<FBridgeBlackboardKeyInfo> Result;
	UBlackboardData* Blackboard = LoadObject<UBlackboardData>(nullptr, *BlackboardPath);
	if (!Blackboard)
	{
		return Result;
	}
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		FBridgeBlackboardKeyInfo Info;
		Info.Name = Entry.EntryName.ToString();
		Info.KeyType = Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : FString();
		Result.Add(Info);
	}
	return Result;
}

FBridgeBehaviorTreeInfo UUnrealBridgeAILibrary::GetBehaviorTreeInfo(const FString& BehaviorTreePath)
{
	FBridgeBehaviorTreeInfo Info;
	UBehaviorTree* Tree = LoadObject<UBehaviorTree>(nullptr, *BehaviorTreePath);
	if (!Tree)
	{
		return Info;
	}
	Info.Path = Tree->GetPathName();
	Info.BlackboardPath = Tree->BlackboardAsset ? Tree->BlackboardAsset->GetPathName() : FString();
	Info.RootNodeName = Tree->RootNode ? Tree->RootNode->GetName() : FString();
	return Info;
}

TArray<FBridgeBehaviorTreeRuntimeInfo> UUnrealBridgeAILibrary::GetRuntimeBehaviorTrees(
	int32 MaxComponents,
	int32 MaxBlackboardKeysPerComponent)
{
	TArray<FBridgeBehaviorTreeRuntimeInfo> Result;
	MaxComponents = FMath::Clamp(MaxComponents, 1, 4096);
	MaxBlackboardKeysPerComponent = FMath::Clamp(MaxBlackboardKeysPerComponent, 0, 4096);

	for (TObjectIterator<UBehaviorTreeComponent> It; It && Result.Num() < MaxComponents; ++It)
	{
		UBehaviorTreeComponent* Component = *It;
		if (!IsValid(Component) || Component->IsTemplate() || !BridgeAIImpl::IsRuntimeWorld(Component->GetWorld()))
		{
			continue;
		}

		FBridgeBehaviorTreeRuntimeInfo Info;
		Info.World = Component->GetWorld()->GetPathName();
		Info.ComponentPath = Component->GetPathName();
		Info.OwnerPath = IsValid(Component->GetOwner()) ? Component->GetOwner()->GetPathName() : FString();
		if (const UBehaviorTree* Tree = Component->GetCurrentTree())
		{
			Info.TreePath = Tree->GetPathName();
		}
		if (const UBTNode* ActiveNode = Component->GetActiveNode())
		{
			Info.ActiveNode = ActiveNode->GetPathName();
		}
		Info.bRunning = Component->IsRunning();
		Info.bPaused = Component->IsPaused();

		if (const UBlackboardComponent* Blackboard = Component->GetBlackboardComponent())
		{
			const int32 KeyCount = FMath::Min(Blackboard->GetNumKeys(), MaxBlackboardKeysPerComponent);
			Info.BlackboardValues.Reserve(KeyCount);
			for (int32 Index = 0; Index < KeyCount; ++Index)
			{
				const FBlackboard::FKey KeyId = static_cast<FBlackboard::FKey>(Index);
				FBridgeBlackboardRuntimeValue Value;
				Value.Name = Blackboard->GetKeyName(KeyId).ToString();
				const TSubclassOf<UBlackboardKeyType> KeyType = Blackboard->GetKeyType(KeyId);
				Value.KeyType = KeyType ? KeyType->GetPathName() : FString();
				Value.Value = Blackboard->DescribeKeyValue(KeyId, EBlackboardDescription::OnlyValue);
				Info.BlackboardValues.Add(MoveTemp(Value));
			}
		}
		Result.Add(MoveTemp(Info));
	}

	return Result;
}

TArray<FBridgeEQSRuntimeInfo> UUnrealBridgeAILibrary::GetRuntimeEQSQueries(int32 MaxQueries, int32 MaxItemsPerQuery)
{
	TArray<FBridgeEQSRuntimeInfo> Result;
	MaxQueries = FMath::Clamp(MaxQueries, 1, 4096);
	MaxItemsPerQuery = FMath::Clamp(MaxItemsPerQuery, 0, 4096);

	for (TObjectIterator<UEnvQueryInstanceBlueprintWrapper> It; It && Result.Num() < MaxQueries; ++It)
	{
		UEnvQueryInstanceBlueprintWrapper* Wrapper = *It;
		if (!IsValid(Wrapper) || Wrapper->IsTemplate() || !BridgeAIImpl::IsRuntimeWorld(Wrapper->GetWorld()))
		{
			continue;
		}
		const FEnvQueryResult* QueryResult = Wrapper->GetQueryResult();
		const FEnvQueryInstance* QueryInstance = Wrapper->GetQueryInstance();
		if (!QueryResult && !QueryInstance)
		{
			continue;
		}

		FBridgeEQSRuntimeInfo Info;
		Info.World = Wrapper->GetWorld()->GetPathName();
		Info.WrapperPath = Wrapper->GetPathName();
		if (QueryResult)
		{
			Info.QueryId = QueryResult->QueryID;
			Info.Status = BridgeAIImpl::EQSStatusToString(QueryResult->GetRawStatus());
			Info.ItemType = QueryResult->ItemType ? QueryResult->ItemType->GetPathName() : FString();
			Info.OwnerPath = QueryResult->Owner.IsValid() ? QueryResult->Owner->GetPathName() : FString();
			Info.TotalItemCount = QueryResult->Items.Num();
			const int32 ItemCount = FMath::Min(Info.TotalItemCount, MaxItemsPerQuery);
			Info.Items.Reserve(ItemCount);
			for (int32 Index = 0; Index < ItemCount; ++Index)
			{
				FBridgeEQSItemRuntimeInfo Item;
				Item.Index = Index;
				Item.Score = QueryResult->GetItemScore(Index);
				if (AActor* Actor = QueryResult->GetItemAsActor(Index))
				{
					Item.ActorPath = Actor->GetPathName();
				}
				Item.Location = QueryResult->GetItemAsLocation(Index);
				Info.Items.Add(MoveTemp(Item));
			}
		}
		else
		{
			Info.Status = TEXT("Processing");
		}
		Result.Add(MoveTemp(Info));
	}

	return Result;
}

TArray<FBridgePerceptionRuntimeInfo> UUnrealBridgeAILibrary::GetRuntimePerception(
	int32 MaxComponents,
	int32 MaxStimuliPerComponent)
{
	TArray<FBridgePerceptionRuntimeInfo> Result;
	MaxComponents = FMath::Clamp(MaxComponents, 1, 4096);
	MaxStimuliPerComponent = FMath::Clamp(MaxStimuliPerComponent, 0, 16384);

	for (TObjectIterator<UAIPerceptionComponent> It; It && Result.Num() < MaxComponents; ++It)
	{
		UAIPerceptionComponent* Component = *It;
		if (!IsValid(Component) || Component->IsTemplate() || !BridgeAIImpl::IsRuntimeWorld(Component->GetWorld()))
		{
			continue;
		}

		FBridgePerceptionRuntimeInfo Info;
		Info.World = Component->GetWorld()->GetPathName();
		Info.ComponentPath = Component->GetPathName();
		Info.OwnerPath = IsValid(Component->GetOwner()) ? Component->GetOwner()->GetPathName() : FString();
		for (auto PerceptionIt = Component->GetPerceptualDataConstIterator(); PerceptionIt; ++PerceptionIt)
		{
			++Info.TotalKnownActorCount;
			const FActorPerceptionInfo& ActorInfo = PerceptionIt->Value;
			for (const FAIStimulus& Stimulus : ActorInfo.LastSensedStimuli)
			{
				if (Info.Stimuli.Num() >= MaxStimuliPerComponent)
				{
					break;
				}
				FBridgePerceptionStimulusRuntimeInfo Item;
				Item.TargetActorPath = ActorInfo.Target.IsValid() ? ActorInfo.Target->GetPathName() : FString();
				const TSubclassOf<UAISense> SenseClass = UAIPerceptionSystem::GetSenseClassForStimulus(Component, Stimulus);
				Item.SenseClass = SenseClass ? SenseClass->GetPathName() : FString();
				Item.bSuccessfullySensed = Stimulus.WasSuccessfullySensed();
				Item.bExpired = Stimulus.IsExpired();
				Item.Age = Stimulus.GetAge();
				Item.Strength = Stimulus.Strength;
				Item.StimulusLocation = Stimulus.StimulusLocation;
				Item.ReceiverLocation = Stimulus.ReceiverLocation;
				Item.Tag = Stimulus.Tag;
				Info.Stimuli.Add(MoveTemp(Item));
			}
		}
		Result.Add(MoveTemp(Info));
	}

	return Result;
}
