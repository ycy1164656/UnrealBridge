#include "UnrealBridgeAssetFactoryLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "EditorAssetLibrary.h"
#include "Engine/DataTable.h"
#include "Engine/Blueprint.h"
#include "Engine/UserDefinedEnum.h"
#include "Misc/EngineVersionComparison.h"
#if UE_VERSION_OLDER_THAN(5, 5, 0)
	#include "Engine/UserDefinedStruct.h"
#else
	#include "StructUtils/UserDefinedStruct.h"
#endif
#include "Factories/DataTableFactory.h"
#include "Factories/BlueprintFactory.h"
#include "Factories/EnumFactory.h"
#include "Factories/StructureFactory.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "InputMappingContext.h"
#include "Kismet2/EnumEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "AssetToolsModule.h"

namespace BridgeAssetFactoryImpl
{
	FString PackageNameFor(const FString& Path, const FString& Name)
	{
		const FString EffectivePath = Path.IsEmpty() ? TEXT("/Game") : Path;
		return EffectivePath / Name;
	}

	UObject* LoadExisting(const FString& PackageName)
	{
		if (PackageName.IsEmpty())
		{
			return nullptr;
		}
		const FString ObjectPath = FString::Printf(TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName));
		return LoadObject<UObject>(nullptr, *ObjectPath);
	}

	FString SaveAsset(UObject* Asset)
	{
		if (!Asset)
		{
			return FString();
		}
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		const FString PackageName = Asset->GetOutermost()->GetName();
		UEditorAssetLibrary::SaveAsset(PackageName, false);
		return PackageName;
	}

	UObject* CreateWithFactory(const FString& Path, const FString& Name, UClass* AssetClass, UFactory* Factory)
	{
		if (Name.IsEmpty() || !AssetClass || !Factory)
		{
			return nullptr;
		}
		const FString PackageName = PackageNameFor(Path, Name);
		if (UObject* Existing = LoadExisting(PackageName))
		{
			return Existing;
		}
		FAssetToolsModule& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		return AssetTools.Get().CreateAsset(Name, Path.IsEmpty() ? TEXT("/Game") : Path, AssetClass, Factory);
	}

	UObject* CreatePlainAsset(const FString& Path, const FString& Name, UClass* AssetClass)
	{
		if (Name.IsEmpty() || !AssetClass)
		{
			return nullptr;
		}
		const FString PackageName = PackageNameFor(Path, Name);
		if (UObject* Existing = LoadExisting(PackageName))
		{
			return Existing;
		}
		UPackage* Package = CreatePackage(*PackageName);
		if (!Package)
		{
			return nullptr;
		}
		Package->FullyLoad();
		return NewObject<UObject>(Package, AssetClass, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	}

	EInputActionValueType ParseInputValueType(const FString& ValueType)
	{
		if (ValueType.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase))
		{
			return EInputActionValueType::Axis1D;
		}
		if (ValueType.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase))
		{
			return EInputActionValueType::Axis2D;
		}
		if (ValueType.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
		{
			return EInputActionValueType::Axis3D;
		}
		return EInputActionValueType::Boolean;
	}

	UClass* ResolveParentClass(const FString& ParentClassPath)
	{
		if (ParentClassPath.IsEmpty())
		{
			return nullptr;
		}

		if (UClass* NativeOrGenerated = LoadObject<UClass>(nullptr, *ParentClassPath))
		{
			return NativeOrGenerated;
		}
		if (!ParentClassPath.EndsWith(TEXT("_C")))
		{
			if (UClass* Generated = LoadObject<UClass>(nullptr, *(ParentClassPath + TEXT("_C"))))
			{
				return Generated;
			}
		}
		if (UBlueprint* ParentBlueprint = LoadObject<UBlueprint>(nullptr, *ParentClassPath))
		{
			return ParentBlueprint->GeneratedClass;
		}
		return FindObject<UClass>(nullptr, *ParentClassPath);
	}
}

FString UUnrealBridgeAssetFactoryLibrary::CreateBlueprint(
	const FString& Path,
	const FString& Name,
	const FString& ParentClassPath,
	bool bSave)
{
	if (Name.IsEmpty())
	{
		return FString();
	}

	const FString PackageName = BridgeAssetFactoryImpl::PackageNameFor(Path, Name);
	if (UObject* Existing = BridgeAssetFactoryImpl::LoadExisting(PackageName))
	{
		return Existing->IsA<UBlueprint>() ? PackageName : FString();
	}

	UClass* ParentClass = BridgeAssetFactoryImpl::ResolveParentClass(ParentClassPath);
	if (!ParentClass || !FKismetEditorUtilities::CanCreateBlueprintOfClass(ParentClass))
	{
		return FString();
	}

	UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
	Factory->ParentClass = ParentClass;
	UBlueprint* Blueprint = Cast<UBlueprint>(BridgeAssetFactoryImpl::CreateWithFactory(
		Path, Name, UBlueprint::StaticClass(), Factory));
	if (!Blueprint)
	{
		return FString();
	}

	Blueprint->MarkPackageDirty();
	if (bSave && !UEditorAssetLibrary::SaveAsset(PackageName, false))
	{
		return FString();
	}
	return PackageName;
}

FString UUnrealBridgeAssetFactoryLibrary::CreateUserDefinedEnum(
	const FString& Path,
	const FString& Name,
	const TArray<FString>& Entries)
{
	UObject* Asset = BridgeAssetFactoryImpl::CreateWithFactory(Path, Name, UUserDefinedEnum::StaticClass(), NewObject<UEnumFactory>());
	UUserDefinedEnum* EnumAsset = Cast<UUserDefinedEnum>(Asset);
	if (!EnumAsset)
	{
		return FString();
	}
	for (const FString& Entry : Entries)
	{
		if (!Entry.IsEmpty())
		{
			const int32 NewIndex = FMath::Max(0, EnumAsset->NumEnums() - 1);
			FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(EnumAsset);
			FEnumEditorUtils::SetEnumeratorDisplayName(EnumAsset, NewIndex, FText::FromString(Entry));
		}
	}
	return BridgeAssetFactoryImpl::SaveAsset(EnumAsset);
}

FString UUnrealBridgeAssetFactoryLibrary::CreateUserDefinedStruct(const FString& Path, const FString& Name)
{
	UObject* Asset = BridgeAssetFactoryImpl::CreateWithFactory(Path, Name, UUserDefinedStruct::StaticClass(), NewObject<UStructureFactory>());
	return BridgeAssetFactoryImpl::SaveAsset(Asset);
}

FString UUnrealBridgeAssetFactoryLibrary::CreateDataTable(
	const FString& Path,
	const FString& Name,
	const FString& RowStructPath)
{
	UDataTableFactory* Factory = NewObject<UDataTableFactory>();
	UScriptStruct* RowStruct = nullptr;
	if (!RowStructPath.IsEmpty())
	{
		RowStruct = LoadObject<UScriptStruct>(nullptr, *RowStructPath);
	}
	if (!RowStruct)
	{
		RowStruct = FTableRowBase::StaticStruct();
	}
	Factory->Struct = RowStruct;
	UObject* Asset = BridgeAssetFactoryImpl::CreateWithFactory(Path, Name, UDataTable::StaticClass(), Factory);
	return BridgeAssetFactoryImpl::SaveAsset(Asset);
}

FString UUnrealBridgeAssetFactoryLibrary::CreateInputAction(
	const FString& Path,
	const FString& Name,
	const FString& ValueType)
{
	UInputAction* Asset = Cast<UInputAction>(BridgeAssetFactoryImpl::CreatePlainAsset(Path, Name, UInputAction::StaticClass()));
	if (!Asset)
	{
		return FString();
	}
	Asset->ValueType = BridgeAssetFactoryImpl::ParseInputValueType(ValueType);
	return BridgeAssetFactoryImpl::SaveAsset(Asset);
}

FString UUnrealBridgeAssetFactoryLibrary::CreateInputMappingContext(const FString& Path, const FString& Name)
{
	UObject* Asset = BridgeAssetFactoryImpl::CreatePlainAsset(Path, Name, UInputMappingContext::StaticClass());
	return BridgeAssetFactoryImpl::SaveAsset(Asset);
}
