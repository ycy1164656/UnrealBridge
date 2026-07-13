#include "UnrealBridgeAILibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
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
#include "Misc/PackageName.h"
#include "UObject/Package.h"

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
