#include "UnrealBridgeNiagaraLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EdGraph/EdGraph.h"
#include "Modules/ModuleManager.h"
#include "NiagaraComponent.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraParameterStore.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "ScopedTransaction.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"

namespace BridgeNiagaraImpl
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UNiagaraComponent* FindComponentByPath(const FString& ComponentPath)
	{
		if (ComponentPath.IsEmpty())
		{
			return nullptr;
		}
		if (UNiagaraComponent* Component = FindObject<UNiagaraComponent>(nullptr, *ComponentPath))
		{
			return Component;
		}
		return Cast<UNiagaraComponent>(StaticFindObject(UObject::StaticClass(), nullptr, *ComponentPath));
	}

	FBridgeNiagaraComponentInfo MakeComponentInfo(UNiagaraComponent* Component)
	{
		FBridgeNiagaraComponentInfo Info;
		if (!Component)
		{
			return Info;
		}
		Info.Path = Component->GetPathName();
		Info.Name = Component->GetName();
		Info.bActive = Component->IsActive();
		Info.Location = Component->GetComponentLocation();
		if (AActor* Owner = Component->GetOwner())
		{
			Info.OwnerLabel = Owner->GetActorLabel();
		}
		if (UNiagaraSystem* System = Component->GetAsset())
		{
			Info.SystemPath = System->GetPathName();
		}
		return Info;
	}

	FString UsageToString(ENiagaraScriptUsage Usage)
	{
		if (const UEnum* Enum = StaticEnum<ENiagaraScriptUsage>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Usage));
		}
		return FString::FromInt(static_cast<int32>(Usage));
	}

	bool ParseUsage(const FString& UsageText, ENiagaraScriptUsage& OutUsage)
	{
		FString Key = UsageText.TrimStartAndEnd().ToLower();
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		if (Key.EndsWith(TEXT("script")))
		{
			Key.LeftChopInline(6);
		}
		if (Key == TEXT("systemspawn")) OutUsage = ENiagaraScriptUsage::SystemSpawnScript;
		else if (Key == TEXT("systemupdate")) OutUsage = ENiagaraScriptUsage::SystemUpdateScript;
		else if (Key == TEXT("emitterspawn")) OutUsage = ENiagaraScriptUsage::EmitterSpawnScript;
		else if (Key == TEXT("emitterupdate")) OutUsage = ENiagaraScriptUsage::EmitterUpdateScript;
		else if (Key == TEXT("particlespawn")) OutUsage = ENiagaraScriptUsage::ParticleSpawnScript;
		else if (Key == TEXT("particleupdate")) OutUsage = ENiagaraScriptUsage::ParticleUpdateScript;
		else if (Key == TEXT("particleevent")) OutUsage = ENiagaraScriptUsage::ParticleEventScript;
		else if (Key == TEXT("particlesimulationstage") || Key == TEXT("simulationstage"))
			OutUsage = ENiagaraScriptUsage::ParticleSimulationStageScript;
		else return false;
		return true;
	}

	UNiagaraGraph* GetSystemGraph(UNiagaraSystem* System)
	{
		if (!System)
		{
			return nullptr;
		}
		UNiagaraScript* Script = System->GetSystemSpawnScript();
		if (!Script)
		{
			Script = System->GetSystemUpdateScript();
		}
		UNiagaraScriptSource* Source = Script ? Cast<UNiagaraScriptSource>(Script->GetLatestSource()) : nullptr;
		return Source ? Source->NodeGraph : nullptr;
	}

	UNiagaraGraph* GetEmitterGraph(const FNiagaraEmitterHandle& Handle)
	{
		FVersionedNiagaraEmitterData* Data = Handle.GetEmitterData();
		UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
		return Source ? Source->NodeGraph : nullptr;
	}

	UNiagaraNodeOutput* FindOutputNode(
		UNiagaraGraph* Graph,
		ENiagaraScriptUsage Usage,
		const FGuid& UsageId,
		bool bRequireUniqueWhenUsageIdMissing)
	{
		if (!Graph)
		{
			return nullptr;
		}
		UNiagaraNodeOutput* Match = nullptr;
		int32 MatchCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node);
			if (!Output || Output->GetUsage() != Usage || (UsageId.IsValid() && Output->GetUsageId() != UsageId))
			{
				continue;
			}
			Match = Output;
			++MatchCount;
		}
		if (bRequireUniqueWhenUsageIdMissing && !UsageId.IsValid() && MatchCount != 1)
		{
			return nullptr;
		}
		return Match;
	}

	void GatherGraphStacks(
		UNiagaraGraph* Graph,
		const FString& EmitterName,
		const FString& EmitterHandleId,
		TArray<FBridgeNiagaraStackInfo>& OutStacks,
		int32& InOutTotalModuleCount)
	{
		if (!Graph)
		{
			return;
		}
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UNiagaraNodeOutput* Output = Cast<UNiagaraNodeOutput>(Node);
			if (!Output)
			{
				continue;
			}
			FBridgeNiagaraStackInfo Stack;
			Stack.EmitterName = EmitterName;
			Stack.EmitterHandleId = EmitterHandleId;
			Stack.Usage = UsageToString(Output->GetUsage());
			Stack.UsageId = Output->GetUsageId().ToString();
			Stack.GraphPath = Graph->GetPathName();
			TArray<UNiagaraNode*> Traversal;
			Graph->BuildTraversal(Traversal, Output->GetUsage(), Output->GetUsageId(), false);
			Stack.TraversalNodeCount = Traversal.Num();
			for (UNiagaraNode* TraversalNode : Traversal)
			{
				UNiagaraNodeFunctionCall* FunctionNode = Cast<UNiagaraNodeFunctionCall>(TraversalNode);
				if (!FunctionNode)
				{
					continue;
				}
				FBridgeNiagaraModuleInfo Module;
				Module.NodeGuid = FunctionNode->NodeGuid.ToString();
				Module.FunctionName = FunctionNode->GetFunctionName();
				if (Module.FunctionName.IsEmpty())
				{
					Module.FunctionName = FunctionNode->GetName();
				}
				Module.ScriptPath = FunctionNode->FunctionScript
					? FunctionNode->FunctionScript->GetPathName()
					: FunctionNode->FunctionScriptAssetObjectPath.ToString();
				Module.StackIndex = Stack.Modules.Num();
				Module.bEnabled = FunctionNode->IsNodeEnabled();
				Stack.Modules.Add(Module);
				++InOutTotalModuleCount;
			}
			OutStacks.Add(Stack);
		}
	}

	void GatherAllGraphs(UNiagaraSystem* System, TArray<UNiagaraGraph*>& OutGraphs)
	{
		TSet<UNiagaraGraph*> UniqueGraphs;
		if (UNiagaraGraph* SystemGraph = GetSystemGraph(System))
		{
			UniqueGraphs.Add(SystemGraph);
		}
		if (System)
		{
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				if (UNiagaraGraph* Graph = GetEmitterGraph(Handle))
				{
					UniqueGraphs.Add(Graph);
				}
			}
		}
		for (UNiagaraGraph* Graph : UniqueGraphs)
		{
			OutGraphs.Add(Graph);
		}
	}

	UNiagaraNodeFunctionCall* FindModuleNodeByGuid(UNiagaraSystem* System, const FGuid& NodeGuid, UNiagaraGraph*& OutGraph)
	{
		OutGraph = nullptr;
		TArray<UNiagaraGraph*> Graphs;
		GatherAllGraphs(System, Graphs);
		for (UNiagaraGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UNiagaraNodeFunctionCall* FunctionNode = Cast<UNiagaraNodeFunctionCall>(Node);
				if (FunctionNode && FunctionNode->NodeGuid == NodeGuid)
				{
					OutGraph = Graph;
					return FunctionNode;
				}
			}
		}
		return nullptr;
	}

	struct FStackTarget
	{
		UNiagaraGraph* Graph = nullptr;
		UNiagaraNodeOutput* Output = nullptr;
		FString EmitterHandleId;
	};

	FStackTarget FindStackTarget(
		UNiagaraSystem* System,
		const FString& EmitterHandleId,
		ENiagaraScriptUsage Usage,
		const FGuid& UsageId)
	{
		FStackTarget Target;
		if (!System)
		{
			return Target;
		}
		const bool bSystemUsage = Usage == ENiagaraScriptUsage::SystemSpawnScript
			|| Usage == ENiagaraScriptUsage::SystemUpdateScript;
		if (bSystemUsage)
		{
			Target.Graph = GetSystemGraph(System);
		}
		else
		{
			FGuid HandleGuid;
			if (!FGuid::Parse(EmitterHandleId, HandleGuid))
			{
				return Target;
			}
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				if (Handle.GetId() == HandleGuid)
				{
					Target.Graph = GetEmitterGraph(Handle);
					Target.EmitterHandleId = Handle.GetId().ToString();
					break;
				}
			}
		}
		Target.Output = FindOutputNode(Target.Graph, Usage, UsageId, true);
		return Target;
	}

	bool SaveSystemIfRequested(UNiagaraSystem* System, bool bSave)
	{
		return !bSave || (System && UEditorAssetLibrary::SaveAsset(System->GetPathName(), false));
	}

	bool RequestCompileIfRequested(UNiagaraSystem* System, bool bRequestCompile)
	{
		return bRequestCompile && System && System->RequestCompile(true);
	}
}

TArray<FBridgeNiagaraSystemInfo> UUnrealBridgeNiagaraLibrary::ListNiagaraSystems(
	const FString& PackagePath,
	int32 MaxResults)
{
	TArray<FBridgeNiagaraSystemInfo> Result;
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
		if (!ClassName.Contains(TEXT("NiagaraSystem")))
		{
			continue;
		}
		FBridgeNiagaraSystemInfo Info;
		Info.Path = Asset.GetSoftObjectPath().ToString();
		Info.Name = Asset.AssetName.ToString();
		Info.ClassName = ClassName;
		Result.Add(Info);
	}
	return Result;
}

FString UUnrealBridgeNiagaraLibrary::SpawnNiagaraSystemAtLocation(
	const FString& SystemPath,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale)
{
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System || !GEditor)
	{
		return FString();
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return FString();
	}
	UNiagaraComponent* Component = UNiagaraFunctionLibrary::SpawnSystemAtLocation(World, System, Location, Rotation, Scale);
	return Component ? Component->GetPathName() : FString();
}

TArray<FBridgeNiagaraUserParameterInfo> UUnrealBridgeNiagaraLibrary::GetNiagaraUserParameters(const FString& SystemPath)
{
	TArray<FBridgeNiagaraUserParameterInfo> Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		return Result;
	}
	TArray<FNiagaraVariable> Parameters;
	System->GetExposedParameters().GetUserParameters(Parameters);
	for (const FNiagaraVariable& Parameter : Parameters)
	{
		FBridgeNiagaraUserParameterInfo Info;
		Info.Name = Parameter.GetName().ToString();
		Info.TypeName = Parameter.GetType().GetName();
		Info.bIsDataInterface = Parameter.IsDataInterface();
		Info.bIsUObject = Parameter.IsUObject();
		Result.Add(Info);
	}
	return Result;
}

TArray<FBridgeNiagaraComponentInfo> UUnrealBridgeNiagaraLibrary::ListNiagaraComponents(const FString& ActorLabel)
{
	TArray<FBridgeNiagaraComponentInfo> Result;
	UWorld* World = BridgeNiagaraImpl::GetEditorWorld();
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
		TArray<UNiagaraComponent*> Components;
		Actor->GetComponents(Components);
		for (UNiagaraComponent* Component : Components)
		{
			Result.Add(BridgeNiagaraImpl::MakeComponentInfo(Component));
		}
	}
	return Result;
}

FBridgeNiagaraComponentInfo UUnrealBridgeNiagaraLibrary::GetNiagaraComponentInfo(const FString& ComponentPath)
{
	return BridgeNiagaraImpl::MakeComponentInfo(BridgeNiagaraImpl::FindComponentByPath(ComponentPath));
}

bool UUnrealBridgeNiagaraLibrary::SetNiagaraComponentVariableFloat(
	const FString& ComponentPath,
	const FString& VariableName,
	float Value)
{
	UNiagaraComponent* Component = BridgeNiagaraImpl::FindComponentByPath(ComponentPath);
	if (!Component || VariableName.IsEmpty())
	{
		return false;
	}
	Component->SetVariableFloat(FName(*VariableName), Value);
	return true;
}

bool UUnrealBridgeNiagaraLibrary::SetNiagaraComponentVariableBool(
	const FString& ComponentPath,
	const FString& VariableName,
	bool bValue)
{
	UNiagaraComponent* Component = BridgeNiagaraImpl::FindComponentByPath(ComponentPath);
	if (!Component || VariableName.IsEmpty())
	{
		return false;
	}
	Component->SetVariableBool(FName(*VariableName), bValue);
	return true;
}

bool UUnrealBridgeNiagaraLibrary::SetNiagaraComponentVariableVec3(
	const FString& ComponentPath,
	const FString& VariableName,
	const FVector& Value)
{
	UNiagaraComponent* Component = BridgeNiagaraImpl::FindComponentByPath(ComponentPath);
	if (!Component || VariableName.IsEmpty())
	{
		return false;
	}
	Component->SetVariableVec3(FName(*VariableName), Value);
	return true;
}

bool UUnrealBridgeNiagaraLibrary::SetNiagaraComponentVariableLinearColor(
	const FString& ComponentPath,
	const FString& VariableName,
	const FLinearColor& Value)
{
	UNiagaraComponent* Component = BridgeNiagaraImpl::FindComponentByPath(ComponentPath);
	if (!Component || VariableName.IsEmpty())
	{
		return false;
	}
	Component->SetVariableLinearColor(FName(*VariableName), Value);
	return true;
}

bool UUnrealBridgeNiagaraLibrary::SetNiagaraComponentVariableObject(
	const FString& ComponentPath,
	const FString& VariableName,
	const FString& ObjectPath)
{
	UNiagaraComponent* Component = BridgeNiagaraImpl::FindComponentByPath(ComponentPath);
	UObject* Object = StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
	if (!Component || !Object || VariableName.IsEmpty())
	{
		return false;
	}
	Component->SetVariableObject(FName(*VariableName), Object);
	return true;
}

FBridgeNiagaraSystemStructure UUnrealBridgeNiagaraLibrary::GetNiagaraSystemStructure(const FString& SystemPath)
{
	FBridgeNiagaraSystemStructure Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		return Result;
	}
	Result.bFound = true;
	Result.SystemPath = System->GetPathName();
	Result.Name = System->GetName();
	Result.bReadyToRun = System->IsReadyToRun();
	Result.bNeedsCompile = System->NeedsRequestCompile();
	BridgeNiagaraImpl::GatherGraphStacks(
		BridgeNiagaraImpl::GetSystemGraph(System), TEXT("<System>"), FString(), Result.Stacks, Result.TotalModuleCount);

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		FBridgeNiagaraEmitterInfo EmitterInfo;
		EmitterInfo.Name = Handle.GetName().ToString();
		EmitterInfo.HandleId = Handle.GetId().ToString();
		EmitterInfo.Mode = Handle.GetEmitterMode() == ENiagaraEmitterMode::Stateless ? TEXT("Stateless") : TEXT("Standard");
		EmitterInfo.bValid = Handle.IsValid();
		EmitterInfo.bEnabled = Handle.GetIsEnabled();
		const FVersionedNiagaraEmitter Instance = Handle.GetInstance();
		if (Instance.Emitter)
		{
			EmitterInfo.SourceEmitterPath = Instance.Emitter->GetPathName();
		}
		Result.Emitters.Add(EmitterInfo);
		BridgeNiagaraImpl::GatherGraphStacks(
			BridgeNiagaraImpl::GetEmitterGraph(Handle),
			EmitterInfo.Name,
			EmitterInfo.HandleId,
			Result.Stacks,
			Result.TotalModuleCount);
	}
	return Result;
}

FBridgeNiagaraValidationResult UUnrealBridgeNiagaraLibrary::ValidateNiagaraSystemGraph(const FString& SystemPath)
{
	FBridgeNiagaraValidationResult Result;
	const FBridgeNiagaraSystemStructure Structure = GetNiagaraSystemStructure(SystemPath);
	if (!Structure.bFound)
	{
		++Result.ErrorCount;
		Result.Messages.Add(FString::Printf(TEXT("ERROR: NiagaraSystem not found: %s"), *SystemPath));
		return Result;
	}
	if (Structure.Stacks.IsEmpty())
	{
		++Result.ErrorCount;
		Result.Messages.Add(TEXT("ERROR: NiagaraSystem contains no readable output stacks."));
	}
	if (Structure.Emitters.IsEmpty())
	{
		++Result.WarningCount;
		Result.Messages.Add(TEXT("WARNING: NiagaraSystem contains no emitters."));
	}
	for (const FBridgeNiagaraEmitterInfo& Emitter : Structure.Emitters)
	{
		if (!Emitter.bValid)
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(TEXT("ERROR: Emitter handle is invalid: %s"), *Emitter.Name));
		}
		if (Emitter.Mode == TEXT("Standard"))
		{
			const bool bHasStack = Structure.Stacks.ContainsByPredicate([&Emitter](const FBridgeNiagaraStackInfo& Stack)
			{
				return Stack.EmitterHandleId == Emitter.HandleId;
			});
			if (!bHasStack)
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(TEXT("ERROR: Emitter has no readable graph: %s"), *Emitter.Name));
			}
		}
	}
	for (const FBridgeNiagaraStackInfo& Stack : Structure.Stacks)
	{
		if (Stack.TraversalNodeCount == 0)
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(
				TEXT("ERROR: Stack traversal is empty: %s/%s"), *Stack.EmitterName, *Stack.Usage));
		}
		if (Stack.Modules.IsEmpty())
		{
			++Result.WarningCount;
			Result.Messages.Add(FString::Printf(
				TEXT("WARNING: Stack contains no modules: %s/%s"), *Stack.EmitterName, *Stack.Usage));
		}
	}
	if (Structure.bNeedsCompile)
	{
		++Result.WarningCount;
		Result.Messages.Add(TEXT("WARNING: NiagaraSystem has pending graph changes and needs compilation."));
	}
	if (!Structure.bReadyToRun)
	{
		++Result.WarningCount;
		Result.Messages.Add(TEXT("WARNING: NiagaraSystem is not currently ready to run."));
	}
	Result.bSuccess = Result.ErrorCount == 0;
	if (Result.bSuccess && Result.WarningCount == 0)
	{
		Result.Messages.Add(TEXT("OK: NiagaraSystem graph structure is valid."));
	}
	return Result;
}

FBridgeNiagaraEditResult UUnrealBridgeNiagaraLibrary::AddNiagaraEmitterToSystem(
	const FString& SystemPath,
	const FString& EmitterAssetPath,
	bool bRequestCompile,
	bool bSave)
{
	FBridgeNiagaraEditResult Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	UNiagaraEmitter* Emitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterAssetPath);
	if (!System || !Emitter)
	{
		Result.Message = !System ? TEXT("NiagaraSystem not found.") : TEXT("NiagaraEmitter not found.");
		return Result;
	}
	Result.SystemPath = System->GetPathName();
	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.UsesEmitter(*Emitter))
		{
			Result.bSuccess = true;
			Result.EmitterHandleId = Handle.GetId().ToString();
			Result.Message = TEXT("Emitter already exists in the system; no duplicate was created.");
			return Result;
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT(
		"UnrealBridge", "AddNiagaraEmitterToSystem", "UnrealBridge Add Niagara Emitter"));
	System->Modify();
	const FGuid HandleId = FNiagaraEditorUtilities::AddEmitterToSystem(
		*System, *Emitter, Emitter->GetExposedVersion().VersionGuid, true);
	if (!HandleId.IsValid())
	{
		Result.Message = TEXT("NiagaraEditor rejected the emitter addition.");
		return Result;
	}
	System->MarkPackageDirty();
	Result.bChanged = true;
	Result.EmitterHandleId = HandleId.ToString();
	Result.bCompileRequested = BridgeNiagaraImpl::RequestCompileIfRequested(System, bRequestCompile);
	const bool bSaveSucceeded = BridgeNiagaraImpl::SaveSystemIfRequested(System, bSave);
	Result.bSaved = bSave && bSaveSucceeded;
	Result.bSuccess = bSaveSucceeded;
	Result.Message = bSaveSucceeded
		? TEXT("Emitter added to NiagaraSystem.") : TEXT("Emitter added, but saving the NiagaraSystem failed.");
	return Result;
}

FBridgeNiagaraEditResult UUnrealBridgeNiagaraLibrary::AddNiagaraModuleToStack(
	const FString& SystemPath,
	const FString& EmitterHandleId,
	const FString& Usage,
	const FString& ModuleScriptPath,
	const FString& UsageId,
	int32 TargetIndex,
	bool bRequestCompile,
	bool bSave)
{
	FBridgeNiagaraEditResult Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *ModuleScriptPath);
	if (!System || !ModuleScript)
	{
		Result.Message = !System ? TEXT("NiagaraSystem not found.") : TEXT("Niagara module script not found.");
		return Result;
	}
	Result.SystemPath = System->GetPathName();
	if (ModuleScript->GetUsage() != ENiagaraScriptUsage::Module)
	{
		Result.Message = TEXT("The requested NiagaraScript is not a Module script.");
		return Result;
	}
	ENiagaraScriptUsage ParsedUsage;
	if (!BridgeNiagaraImpl::ParseUsage(Usage, ParsedUsage))
	{
		Result.Message = TEXT("Unsupported Niagara stack usage.");
		return Result;
	}
	FGuid ParsedUsageId;
	if (!UsageId.IsEmpty() && !FGuid::Parse(UsageId, ParsedUsageId))
	{
		Result.Message = TEXT("UsageId is not a valid GUID.");
		return Result;
	}
	const BridgeNiagaraImpl::FStackTarget Target = BridgeNiagaraImpl::FindStackTarget(
		System, EmitterHandleId, ParsedUsage, ParsedUsageId);
	if (!Target.Graph || !Target.Output)
	{
		Result.Message = TEXT("The requested Niagara output stack was not found or is ambiguous; provide UsageId for event/simulation stacks.");
		return Result;
	}
	Result.EmitterHandleId = Target.EmitterHandleId;
	TArray<UNiagaraNode*> Traversal;
	Target.Graph->BuildTraversal(Traversal, Target.Output->GetUsage(), Target.Output->GetUsageId(), false);
	for (UNiagaraNode* Node : Traversal)
	{
		UNiagaraNodeFunctionCall* Existing = Cast<UNiagaraNodeFunctionCall>(Node);
		if (Existing && Existing->FunctionScript == ModuleScript)
		{
			Result.bSuccess = true;
			Result.NodeGuid = Existing->NodeGuid.ToString();
			Result.Message = TEXT("Module already exists in the requested stack; no duplicate was created.");
			return Result;
		}
	}

	const FScopedTransaction Transaction(NSLOCTEXT(
		"UnrealBridge", "AddNiagaraModuleToStack", "UnrealBridge Add Niagara Module"));
	System->Modify();
	Target.Graph->Modify();
	Target.Output->Modify();
	UNiagaraNodeFunctionCall* AddedNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		ModuleScript, *Target.Output, TargetIndex);
	if (!AddedNode)
	{
		Result.Message = TEXT("NiagaraEditor rejected the module addition.");
		return Result;
	}
	System->MarkPackageDirty();
	Result.bChanged = true;
	Result.NodeGuid = AddedNode->NodeGuid.ToString();
	Result.bCompileRequested = BridgeNiagaraImpl::RequestCompileIfRequested(System, bRequestCompile);
	const bool bSaveSucceeded = BridgeNiagaraImpl::SaveSystemIfRequested(System, bSave);
	Result.bSaved = bSave && bSaveSucceeded;
	Result.bSuccess = bSaveSucceeded;
	Result.Message = bSaveSucceeded
		? TEXT("Module added to Niagara stack.") : TEXT("Module added, but saving the NiagaraSystem failed.");
	return Result;
}

FBridgeNiagaraEditResult UUnrealBridgeNiagaraLibrary::SetNiagaraModuleEnabled(
	const FString& SystemPath,
	const FString& NodeGuid,
	bool bEnabled,
	bool bRequestCompile,
	bool bSave)
{
	FBridgeNiagaraEditResult Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	FGuid ParsedNodeGuid;
	if (!System || !FGuid::Parse(NodeGuid, ParsedNodeGuid))
	{
		Result.Message = !System ? TEXT("NiagaraSystem not found.") : TEXT("NodeGuid is not a valid GUID.");
		return Result;
	}
	Result.SystemPath = System->GetPathName();
	Result.NodeGuid = ParsedNodeGuid.ToString();
	UNiagaraGraph* Graph = nullptr;
	UNiagaraNodeFunctionCall* ModuleNode = BridgeNiagaraImpl::FindModuleNodeByGuid(System, ParsedNodeGuid, Graph);
	if (!ModuleNode || !Graph)
	{
		Result.Message = TEXT("Niagara module node was not found in this system.");
		return Result;
	}
	if (ModuleNode->IsNodeEnabled() == bEnabled)
	{
		Result.bSuccess = true;
		Result.Message = TEXT("Module already has the requested enabled state.");
		return Result;
	}

	const FScopedTransaction Transaction(NSLOCTEXT(
		"UnrealBridge", "SetNiagaraModuleEnabled", "UnrealBridge Set Niagara Module Enabled"));
	System->Modify();
	Graph->Modify();
	ModuleNode->Modify();
	FNiagaraStackGraphUtilities::SetModuleIsEnabled(*ModuleNode, bEnabled);
	System->MarkPackageDirty();
	Result.bChanged = true;
	Result.bCompileRequested = BridgeNiagaraImpl::RequestCompileIfRequested(System, bRequestCompile);
	const bool bSaveSucceeded = BridgeNiagaraImpl::SaveSystemIfRequested(System, bSave);
	Result.bSaved = bSave && bSaveSucceeded;
	Result.bSuccess = bSaveSucceeded;
	Result.Message = bSaveSucceeded
		? TEXT("Niagara module enabled state updated.") : TEXT("Module state updated, but saving the NiagaraSystem failed.");
	return Result;
}

FBridgeNiagaraEditResult UUnrealBridgeNiagaraLibrary::RequestNiagaraSystemCompile(
	const FString& SystemPath,
	bool bForce)
{
	FBridgeNiagaraEditResult Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		Result.Message = TEXT("NiagaraSystem not found.");
		return Result;
	}
	Result.SystemPath = System->GetPathName();
	Result.bCompileRequested = System->RequestCompile(bForce);
	Result.bSuccess = true;
	Result.Message = Result.bCompileRequested
		? TEXT("Niagara compilation requested.") : TEXT("Niagara compilation was already current or already queued.");
	return Result;
}

FBridgeNiagaraCompileStatus UUnrealBridgeNiagaraLibrary::PollNiagaraSystemCompilation(const FString& SystemPath)
{
	FBridgeNiagaraCompileStatus Result;
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
	if (!System)
	{
		Result.Message = TEXT("NiagaraSystem not found.");
		return Result;
	}
	Result.bFound = true;
	Result.bComplete = System->PollForCompilationComplete(false);
	Result.bReadyToRun = System->IsReadyToRun();
	Result.bNeedsCompile = System->NeedsRequestCompile();
	Result.bSuccess = Result.bComplete && Result.bReadyToRun && !Result.bNeedsCompile;
	Result.Message = !Result.bComplete
		? TEXT("Niagara compilation is still running.")
		: Result.bSuccess ? TEXT("Niagara compilation completed successfully.")
		: TEXT("Niagara compilation completed, but the system is not ready to run or still needs compilation.");
	return Result;
}
