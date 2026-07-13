#include "UnrealBridgeIKLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/Factory.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "Retargeter/IKRetargeter.h"
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeIKLibrary"

namespace BridgeIKImpl
{
	bool ClassMatches(const FString& ClassName, const FString& Wanted)
	{
		return ClassName == Wanted || ClassName.Contains(Wanted);
	}

	void ExportTopLevelProperties(UObject* Object, FBridgeIKAssetInfo& Info, int32 MaxPropertyValueLength)
	{
		if (!Object)
		{
			return;
		}
		for (TFieldIterator<FProperty> It(Object->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Property = *It;
			if (!Property)
			{
				continue;
			}
			const FString PropertyName = Property->GetName();
			Info.TopLevelProperties.Add(PropertyName);
			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
			FString Exported;
			Property->ExportTextItem_Direct(Exported, ValuePtr, nullptr, Object, PPF_None);
			if (MaxPropertyValueLength > 0 && Exported.Len() > MaxPropertyValueLength)
			{
				Exported = Exported.Left(MaxPropertyValueLength) + TEXT("...");
			}
			Info.PropertyValues.Add(PropertyName, Exported);
		}
	}

	TArray<FBridgeIKAssetInfo> ListAssets(const FString& PackagePath, int32 MaxResults, const FString& WantedClass)
	{
		TArray<FBridgeIKAssetInfo> Result;
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
			if (!ClassMatches(ClassName, WantedClass))
			{
				continue;
			}
			FBridgeIKAssetInfo Info;
			Info.Path = Asset.GetSoftObjectPath().ToString();
			Info.Name = Asset.AssetName.ToString();
			Info.ClassName = ClassName;
			Result.Add(Info);
		}
		return Result;
	}

	FString CreateAsset(
		const FString& Path,
		const FString& Name,
		const FString& FactoryClassPath,
		const FString& AssetClassPath,
		bool bSave)
	{
		if (Path.IsEmpty() || Name.IsEmpty())
		{
			return FString();
		}
		UClass* AssetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *AssetClassPath);
		UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, *FactoryClassPath);
		if (!AssetClass || !FactoryClass)
		{
			return FString();
		}
		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
		UObject* Asset = AssetToolsModule.Get().CreateAsset(Name, Path, AssetClass, Factory);
		if (!Asset)
		{
			return FString();
		}
		FAssetRegistryModule::AssetCreated(Asset);
		Asset->MarkPackageDirty();
		if (bSave)
		{
			UEditorAssetLibrary::SaveAsset(Asset->GetPathName(), false);
		}
		return Asset->GetPathName();
	}

	bool SaveIfRequested(UObject* Asset, bool bSave, FBridgeIKEditResult& Result)
	{
		if (!Asset)
		{
			Result.Error = TEXT("IK asset is unavailable.");
			return false;
		}
		Asset->PostEditChange();
		Asset->MarkPackageDirty();
		if (bSave && !UEditorAssetLibrary::SaveAsset(Asset->GetPathName(), false))
		{
			Result.Error = TEXT("The edit succeeded but the asset could not be saved.");
			return false;
		}
		return true;
	}

	bool IsBoneValid(const UIKRigController* Controller, const FName BoneName)
	{
		if (!Controller || BoneName.IsNone())
		{
			return false;
		}
		const USkeletalMesh* Mesh = Controller->GetSkeletalMesh();
		return Mesh && Mesh->GetRefSkeleton().FindBoneIndex(BoneName) != INDEX_NONE;
	}

	EAutoMapChainType ParseAutoMapMode(const FString& Mode, bool& bOutValid)
	{
		bOutValid = true;
		if (Mode.Equals(TEXT("Exact"), ESearchCase::IgnoreCase))
		{
			return EAutoMapChainType::Exact;
		}
		if (Mode.Equals(TEXT("Fuzzy"), ESearchCase::IgnoreCase))
		{
			return EAutoMapChainType::Fuzzy;
		}
		if (Mode.Equals(TEXT("Clear"), ESearchCase::IgnoreCase))
		{
			return EAutoMapChainType::Clear;
		}
		bOutValid = false;
		return EAutoMapChainType::Fuzzy;
	}
}

TArray<FBridgeIKAssetInfo> UUnrealBridgeIKLibrary::ListIKRigAssets(const FString& PackagePath, int32 MaxResults)
{
	return BridgeIKImpl::ListAssets(PackagePath, MaxResults, TEXT("IKRigDefinition"));
}

TArray<FBridgeIKAssetInfo> UUnrealBridgeIKLibrary::ListIKRetargeterAssets(const FString& PackagePath, int32 MaxResults)
{
	return BridgeIKImpl::ListAssets(PackagePath, MaxResults, TEXT("IKRetargeter"));
}

FString UUnrealBridgeIKLibrary::CreateIKRig(
	const FString& Path,
	const FString& Name,
	const FString& FactoryClassPath,
	const FString& AssetClassPath,
	bool bSave)
{
	return BridgeIKImpl::CreateAsset(Path, Name, FactoryClassPath, AssetClassPath, bSave);
}

FString UUnrealBridgeIKLibrary::CreateIKRetargeter(
	const FString& Path,
	const FString& Name,
	const FString& FactoryClassPath,
	const FString& AssetClassPath,
	bool bSave)
{
	return BridgeIKImpl::CreateAsset(Path, Name, FactoryClassPath, AssetClassPath, bSave);
}

FBridgeIKAssetInfo UUnrealBridgeIKLibrary::GetIKAssetInfo(const FString& AssetPath, int32 MaxPropertyValueLength)
{
	FBridgeIKAssetInfo Info;
	UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Object)
	{
		return Info;
	}
	Info.Path = Object->GetPathName();
	Info.Name = Object->GetName();
	Info.ClassName = Object->GetClass()->GetName();
	BridgeIKImpl::ExportTopLevelProperties(Object, Info, MaxPropertyValueLength);
	return Info;
}

FString UUnrealBridgeIKLibrary::GetIKAssetProperty(
	const FString& AssetPath,
	const FString& PropertyName,
	bool& bOutSuccess)
{
	bOutSuccess = false;
	UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Object || PropertyName.IsEmpty())
	{
		return FString();
	}
	FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return FString();
	}
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	FString Exported;
	Property->ExportTextItem_Direct(Exported, ValuePtr, nullptr, Object, PPF_None);
	bOutSuccess = true;
	return Exported;
}

bool UUnrealBridgeIKLibrary::SetIKAssetProperty(
	const FString& AssetPath,
	const FString& PropertyName,
	const FString& ValueExportText,
	bool bSave)
{
	UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	if (!Object || PropertyName.IsEmpty())
	{
		return false;
	}
	FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetIKAssetProperty", "Bridge: Set IK Asset Property"));
	Object->Modify();
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	const TCHAR* Start = *ValueExportText;
	const TCHAR* AfterParse = Property->ImportText_Direct(Start, ValuePtr, Object, PPF_None, GLog);
	if (!AfterParse || AfterParse == Start)
	{
		return false;
	}
	Object->PostEditChange();
	Object->MarkPackageDirty();
	return !bSave || UEditorAssetLibrary::SaveAsset(Object->GetPathName(), false);
}

FBridgeIKRigStructure UUnrealBridgeIKLibrary::GetIKRigStructure(const FString& IKRigPath)
{
	FBridgeIKRigStructure Result;
	UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigPath);
	UIKRigController* Controller = IKRig ? UIKRigController::GetController(IKRig) : nullptr;
	if (!IKRig || !Controller)
	{
		return Result;
	}

	Result.bFound = true;
	Result.AssetPath = IKRig->GetPathName();
	if (USkeletalMesh* Mesh = Controller->GetSkeletalMesh())
	{
		Result.SkeletalMeshPath = Mesh->GetPathName();
	}
	Result.RetargetRoot = Controller->GetRetargetRoot().ToString();

	for (UIKRigEffectorGoal* Goal : Controller->GetAllGoals())
	{
		if (!Goal)
		{
			continue;
		}
		FBridgeIKGoalInfo Info;
		Info.Name = Goal->GoalName.ToString();
		Info.BoneName = Goal->BoneName.ToString();
		for (int32 SolverIndex = 0; SolverIndex < Controller->GetNumSolvers(); ++SolverIndex)
		{
			if (Controller->IsGoalConnectedToSolver(Goal->GoalName, SolverIndex))
			{
				Info.ConnectedSolverIndices.Add(SolverIndex);
			}
		}
		Result.Goals.Add(MoveTemp(Info));
	}

	for (int32 SolverIndex = 0; SolverIndex < Controller->GetNumSolvers(); ++SolverIndex)
	{
		FBridgeIKSolverInfo Info;
		Info.Index = SolverIndex;
		Info.DisplayName = Controller->GetSolverUniqueName(SolverIndex);
		Info.bEnabled = Controller->GetSolverEnabled(SolverIndex);
		Info.StartBone = Controller->GetStartBone(SolverIndex).ToString();
		Info.EndBone = Controller->GetEndBone(SolverIndex).ToString();
		if (const FInstancedStruct* SolverStruct = Controller->GetSolverStructAtIndex(SolverIndex))
		{
			if (const UScriptStruct* ScriptStruct = SolverStruct->GetScriptStruct())
			{
				Info.StructPath = ScriptStruct->GetPathName();
			}
		}
		Result.Solvers.Add(MoveTemp(Info));
	}

	for (const FBoneChain& Chain : Controller->GetRetargetChains())
	{
		FBridgeIKRetargetChainInfo Info;
		Info.Name = Chain.ChainName.ToString();
		Info.StartBone = Chain.StartBone.BoneName.ToString();
		Info.EndBone = Chain.EndBone.BoneName.ToString();
		Info.GoalName = Chain.IKGoalName.ToString();
		Result.Chains.Add(MoveTemp(Info));
	}
	return Result;
}

FBridgeIKValidationResult UUnrealBridgeIKLibrary::ValidateIKRig(const FString& IKRigPath)
{
	FBridgeIKValidationResult Result;
	UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigPath);
	UIKRigController* Controller = IKRig ? UIKRigController::GetController(IKRig) : nullptr;
	if (!IKRig || !Controller)
	{
		Result.ErrorCount = 1;
		Result.Messages.Add(TEXT("Error: IK Rig asset could not be loaded."));
		return Result;
	}

	if (!Controller->GetSkeletalMesh())
	{
		++Result.ErrorCount;
		Result.Messages.Add(TEXT("Error: IK Rig has no skeletal mesh."));
	}
	for (UIKRigEffectorGoal* Goal : Controller->GetAllGoals())
	{
		if (!Goal)
		{
			++Result.ErrorCount;
			Result.Messages.Add(TEXT("Error: IK Rig contains a null goal."));
			continue;
		}
		if (!BridgeIKImpl::IsBoneValid(Controller, Goal->BoneName))
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(TEXT("Error: Goal %s references missing bone %s."),
				*Goal->GoalName.ToString(), *Goal->BoneName.ToString()));
		}
		if (!Controller->IsGoalConnectedToAnySolver(Goal->GoalName))
		{
			++Result.WarningCount;
			Result.Messages.Add(FString::Printf(TEXT("Warning: Goal %s is not connected to a solver."), *Goal->GoalName.ToString()));
		}
	}
	for (const FBoneChain& Chain : Controller->GetRetargetChains())
	{
		if (!BridgeIKImpl::IsBoneValid(Controller, Chain.StartBone.BoneName)
			|| !BridgeIKImpl::IsBoneValid(Controller, Chain.EndBone.BoneName))
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(TEXT("Error: Retarget chain %s has an invalid start or end bone."), *Chain.ChainName.ToString()));
		}
		if (!Chain.IKGoalName.IsNone() && !Controller->GetGoal(Chain.IKGoalName))
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(TEXT("Error: Retarget chain %s references missing goal %s."),
				*Chain.ChainName.ToString(), *Chain.IKGoalName.ToString()));
		}
	}
	Result.bSuccess = Result.ErrorCount == 0;
	if (Result.Messages.IsEmpty())
	{
		Result.Messages.Add(TEXT("IK Rig validation passed."));
	}
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::AddIKRigGoal(
	const FString& IKRigPath,
	const FString& GoalName,
	const FString& BoneName,
	bool bSave)
{
	FBridgeIKEditResult Result;
	UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigPath);
	UIKRigController* Controller = IKRig ? UIKRigController::GetController(IKRig) : nullptr;
	if (!IKRig || !Controller || GoalName.IsEmpty() || !BridgeIKImpl::IsBoneValid(Controller, FName(*BoneName)))
	{
		Result.Error = TEXT("IK Rig/goal is invalid or BoneName is not present in the preview skeleton.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddIKRigGoal", "Bridge: Add IK Rig Goal"));
	IKRig->Modify();
	const FName AddedName = Controller->AddNewGoal(FName(*GoalName), FName(*BoneName));
	if (AddedName.IsNone())
	{
		Result.Error = TEXT("The IK Rig controller rejected the goal.");
		return Result;
	}
	Result.Name = AddedName.ToString();
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(IKRig, bSave, Result);
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::AddIKRigSolver(
	const FString& IKRigPath,
	const FString& SolverStructPath,
	const FString& StartBone,
	const FString& EndBone,
	bool bSave)
{
	FBridgeIKEditResult Result;
	UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigPath);
	UIKRigController* Controller = IKRig ? UIKRigController::GetController(IKRig) : nullptr;
	if (!IKRig || !Controller || SolverStructPath.IsEmpty())
	{
		Result.Error = TEXT("IK Rig/controller is unavailable or SolverStructPath is empty.");
		return Result;
	}
	if ((!StartBone.IsEmpty() && !BridgeIKImpl::IsBoneValid(Controller, FName(*StartBone)))
		|| (!EndBone.IsEmpty() && !BridgeIKImpl::IsBoneValid(Controller, FName(*EndBone))))
	{
		Result.Error = TEXT("StartBone or EndBone is not present in the preview skeleton.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddIKRigSolver", "Bridge: Add IK Rig Solver"));
	IKRig->Modify();
	Result.Index = Controller->AddSolver(SolverStructPath);
	if (Result.Index == INDEX_NONE)
	{
		Result.Error = TEXT("The IK Rig controller rejected the solver type.");
		return Result;
	}
	if (!StartBone.IsEmpty() && !Controller->SetStartBone(FName(*StartBone), Result.Index))
	{
		Result.Error = TEXT("Solver was added, but this solver type does not accept the requested start bone.");
	}
	if (!EndBone.IsEmpty() && !Controller->SetEndBone(FName(*EndBone), Result.Index))
	{
		Result.Error += Result.Error.IsEmpty() ? TEXT("Solver was added, but this solver type does not accept the requested end bone.")
			: TEXT(" It also rejected the requested end bone.");
	}
	Result.Name = Controller->GetSolverUniqueName(Result.Index);
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(IKRig, bSave, Result);
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::ConnectIKRigGoalToSolver(
	const FString& IKRigPath,
	const FString& GoalName,
	int32 SolverIndex,
	bool bSave)
{
	FBridgeIKEditResult Result;
	UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigPath);
	UIKRigController* Controller = IKRig ? UIKRigController::GetController(IKRig) : nullptr;
	if (!IKRig || !Controller || !Controller->GetGoal(FName(*GoalName))
		|| SolverIndex < 0 || SolverIndex >= Controller->GetNumSolvers())
	{
		Result.Error = TEXT("IK Rig, goal, or solver index is invalid.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeConnectIKRigGoal", "Bridge: Connect IK Rig Goal"));
	IKRig->Modify();
	if (!Controller->ConnectGoalToSolver(FName(*GoalName), SolverIndex))
	{
		Result.Error = TEXT("The solver rejected the goal connection.");
		return Result;
	}
	Result.Name = GoalName;
	Result.Index = SolverIndex;
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(IKRig, bSave, Result);
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::AddIKRigRetargetChain(
	const FString& IKRigPath,
	const FString& ChainName,
	const FString& StartBone,
	const FString& EndBone,
	const FString& GoalName,
	bool bSave)
{
	FBridgeIKEditResult Result;
	UIKRigDefinition* IKRig = LoadObject<UIKRigDefinition>(nullptr, *IKRigPath);
	UIKRigController* Controller = IKRig ? UIKRigController::GetController(IKRig) : nullptr;
	if (!IKRig || !Controller || ChainName.IsEmpty()
		|| !BridgeIKImpl::IsBoneValid(Controller, FName(*StartBone))
		|| !BridgeIKImpl::IsBoneValid(Controller, FName(*EndBone))
		|| (!GoalName.IsEmpty() && !Controller->GetGoal(FName(*GoalName))))
	{
		Result.Error = TEXT("IK Rig, chain name, bones, or optional goal is invalid.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddIKRetargetChain", "Bridge: Add IK Retarget Chain"));
	IKRig->Modify();
	const FName AddedName = Controller->AddRetargetChain(
		FName(*ChainName), FName(*StartBone), FName(*EndBone), GoalName.IsEmpty() ? NAME_None : FName(*GoalName));
	if (AddedName.IsNone())
	{
		Result.Error = TEXT("The IK Rig controller rejected the retarget chain.");
		return Result;
	}
	Result.Name = AddedName.ToString();
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(IKRig, bSave, Result);
	return Result;
}

FBridgeIKRetargeterStructure UUnrealBridgeIKLibrary::GetIKRetargeterStructure(
	const FString& RetargeterPath,
	const FString& MappingOpName)
{
	FBridgeIKRetargeterStructure Result;
	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, *RetargeterPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Retargeter || !Controller)
	{
		return Result;
	}

	Result.bFound = true;
	Result.AssetPath = Retargeter->GetPathName();
	Result.MappingOpName = MappingOpName;
	if (const UIKRigDefinition* Source = Controller->GetIKRig(ERetargetSourceOrTarget::Source))
	{
		Result.SourceIKRigPath = Source->GetPathName();
	}
	if (const UIKRigDefinition* Target = Controller->GetIKRig(ERetargetSourceOrTarget::Target))
	{
		Result.TargetIKRigPath = Target->GetPathName();
	}
	if (const FRetargetChainMapping* Mapping = Controller->GetChainMapping(FName(*MappingOpName)))
	{
		for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
		{
			FBridgeIKRetargetMappingInfo Info;
			Info.TargetChain = Pair.TargetChainName.ToString();
			Info.SourceChain = Pair.SourceChainName.ToString();
			Result.Mappings.Add(MoveTemp(Info));
		}
	}
	return Result;
}

FBridgeIKValidationResult UUnrealBridgeIKLibrary::ValidateIKRetargeter(
	const FString& RetargeterPath,
	const FString& MappingOpName)
{
	FBridgeIKValidationResult Result;
	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, *RetargeterPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Retargeter || !Controller)
	{
		Result.ErrorCount = 1;
		Result.Messages.Add(TEXT("Error: IK Retargeter asset could not be loaded."));
		return Result;
	}

	const UIKRigDefinition* Source = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
	const UIKRigDefinition* Target = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
	if (!Source || !Target)
	{
		++Result.ErrorCount;
		Result.Messages.Add(TEXT("Error: Source and target IK Rigs must both be assigned."));
	}
	const FRetargetChainMapping* Mapping = Controller->GetChainMapping(FName(*MappingOpName));
	if (!Mapping)
	{
		++Result.WarningCount;
		Result.Messages.Add(TEXT("Warning: No retarget operation with a chain mapping was found."));
	}
	else
	{
		for (const FRetargetChainPair& Pair : Mapping->GetChainPairs())
		{
			if (Pair.SourceChainName.IsNone())
			{
				++Result.WarningCount;
				Result.Messages.Add(FString::Printf(TEXT("Warning: Target chain %s is unmapped."), *Pair.TargetChainName.ToString()));
			}
			else if (!Mapping->HasChain(Pair.SourceChainName, ERetargetSourceOrTarget::Source))
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(TEXT("Error: Source chain %s does not exist."), *Pair.SourceChainName.ToString()));
			}
		}
	}
	Result.bSuccess = Result.ErrorCount == 0;
	if (Result.Messages.IsEmpty())
	{
		Result.Messages.Add(TEXT("IK Retargeter validation passed."));
	}
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::SetIKRetargeterRigs(
	const FString& RetargeterPath,
	const FString& SourceIKRigPath,
	const FString& TargetIKRigPath,
	bool bAddDefaultOps,
	bool bSave)
{
	FBridgeIKEditResult Result;
	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, *RetargeterPath);
	UIKRigDefinition* Source = LoadObject<UIKRigDefinition>(nullptr, *SourceIKRigPath);
	UIKRigDefinition* Target = LoadObject<UIKRigDefinition>(nullptr, *TargetIKRigPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Retargeter || !Controller || !Source || !Target)
	{
		Result.Error = TEXT("Retargeter, source IK Rig, or target IK Rig could not be loaded.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetIKRetargeterRigs", "Bridge: Set IK Retargeter Rigs"));
	Retargeter->Modify();
	Controller->SetIKRig(ERetargetSourceOrTarget::Source, Source);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, Target);
	if (bAddDefaultOps)
	{
		Controller->AddDefaultOps();
	}
	Controller->CleanAsset();
	Result.Name = Retargeter->GetName();
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(Retargeter, bSave, Result);
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::SetIKRetargetChainMapping(
	const FString& RetargeterPath,
	const FString& TargetChainName,
	const FString& SourceChainName,
	const FString& MappingOpName,
	bool bSave)
{
	FBridgeIKEditResult Result;
	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, *RetargeterPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Retargeter || !Controller || TargetChainName.IsEmpty())
	{
		Result.Error = TEXT("Retargeter/controller is unavailable or TargetChainName is empty.");
		return Result;
	}
	const FRetargetChainMapping* Mapping = Controller->GetChainMapping(FName(*MappingOpName));
	if (!Mapping || !Mapping->HasChain(FName(*TargetChainName), ERetargetSourceOrTarget::Target)
		|| (!SourceChainName.IsEmpty() && !Mapping->HasChain(FName(*SourceChainName), ERetargetSourceOrTarget::Source)))
	{
		Result.Error = TEXT("Mapping operation, target chain, or source chain is invalid.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetIKRetargetMapping", "Bridge: Set IK Retarget Chain Mapping"));
	Retargeter->Modify();
	if (!Controller->SetSourceChain(
		SourceChainName.IsEmpty() ? NAME_None : FName(*SourceChainName),
		FName(*TargetChainName),
		FName(*MappingOpName)))
	{
		Result.Error = TEXT("The IK Retargeter controller rejected the chain mapping.");
		return Result;
	}
	Result.Name = TargetChainName;
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(Retargeter, bSave, Result);
	return Result;
}

FBridgeIKEditResult UUnrealBridgeIKLibrary::AutoMapIKRetargetChains(
	const FString& RetargeterPath,
	const FString& Mode,
	bool bForceRemap,
	const FString& MappingOpName,
	bool bSave)
{
	FBridgeIKEditResult Result;
	bool bModeValid = false;
	const EAutoMapChainType AutoMapMode = BridgeIKImpl::ParseAutoMapMode(Mode, bModeValid);
	UIKRetargeter* Retargeter = LoadObject<UIKRetargeter>(nullptr, *RetargeterPath);
	UIKRetargeterController* Controller = Retargeter ? UIKRetargeterController::GetController(Retargeter) : nullptr;
	if (!Retargeter || !Controller || !bModeValid || !Controller->GetChainMapping(FName(*MappingOpName)))
	{
		Result.Error = TEXT("Retargeter, mapping operation, or auto-map mode is invalid.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAutoMapIKRetargetChains", "Bridge: Auto Map IK Retarget Chains"));
	Retargeter->Modify();
	Controller->AutoMapChains(AutoMapMode, bForceRemap, FName(*MappingOpName));
	Result.Name = Mode;
	Result.bSuccess = BridgeIKImpl::SaveIfRequested(Retargeter, bSave, Result);
	return Result;
}

#undef LOCTEXT_NAMESPACE
