#include "UnrealBridgePCGLibrary.h"

#include "Misc/EngineVersionComparison.h"

#if !UE_VERSION_OLDER_THAN(5, 6, 0)

#include "PCGComponent.h"
#include "PCGCommon.h"
#include "PCGEdge.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGPin.h"
#include "PCGSettings.h"

#include "Engine/World.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/DateTime.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "StructUtils/InstancedStruct.h"
#include "StructUtils/PropertyBag.h"

#define LOCTEXT_NAMESPACE "UnrealBridgePCG"

namespace BridgePCGImpl
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	AActor* FindActor(UWorld* World, const FString& NameOrLabel)
	{
		if (!World || NameOrLabel.IsEmpty())
		{
			return nullptr;
		}
		const FName AsName(*NameOrLabel);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			if (!A) { continue; }
			if (A->GetFName() == AsName || A->GetActorLabel() == NameOrLabel)
			{
				return A;
			}
		}
		return nullptr;
	}

	UPCGComponent* FindPCGComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor)
		{
			return nullptr;
		}
		TArray<UPCGComponent*> Components;
		Actor->GetComponents<UPCGComponent>(Components);
		if (ComponentName.IsEmpty())
		{
			return Components.Num() > 0 ? Components[0] : nullptr;
		}
		const FName AsName(*ComponentName);
		for (UPCGComponent* C : Components)
		{
			if (C && (C->GetFName() == AsName || C->GetName() == ComponentName))
			{
				return C;
			}
		}
		return nullptr;
	}

	FString PathForGraph(const UPCGGraph* Graph)
	{
		return Graph ? Graph->GetPathName() : FString{};
	}

	UPCGGraph* LoadGraph(const FString& GraphPath)
	{
		return LoadObject<UPCGGraph>(nullptr, *GraphPath);
	}

	void GetAllNodes(UPCGGraph* Graph, TArray<UPCGNode*>& OutNodes)
	{
		OutNodes.Reset();
		if (!Graph)
		{
			return;
		}
		if (UPCGNode* InputNode = Graph->GetInputNode())
		{
			OutNodes.Add(InputNode);
		}
		for (UPCGNode* Node : Graph->GetNodes())
		{
			if (Node)
			{
				OutNodes.AddUnique(Node);
			}
		}
		if (UPCGNode* OutputNode = Graph->GetOutputNode())
		{
			OutNodes.AddUnique(OutputNode);
		}
	}

	UPCGNode* FindNode(UPCGGraph* Graph, const FString& NodeId)
	{
		TArray<UPCGNode*> Nodes;
		GetAllNodes(Graph, Nodes);
		for (UPCGNode* Node : Nodes)
		{
			if (Node && (Node->GetName() == NodeId || Node->GetPathName() == NodeId))
			{
				return Node;
			}
		}
		return nullptr;
	}

	FBridgePCGPinInfo MakePinInfo(const UPCGPin& Pin)
	{
		FBridgePCGPinInfo Info;
		Info.Label = Pin.Properties.Label.ToString();
		Info.AllowedTypes = static_cast<int64>(Pin.Properties.AllowedTypes);
		Info.bOutput = Pin.IsOutputPin();
		Info.bRequired = Pin.Properties.IsRequiredPin();
		Info.ConnectionCount = Pin.EdgeCount();
		return Info;
	}

	FBridgePCGNodeInfo MakeNodeInfo(UPCGGraph* Graph, UPCGNode& Node)
	{
		FBridgePCGNodeInfo Info;
		Info.Id = Node.GetName();
		Info.Title = Node.GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
		Info.bInputNode = Graph && Graph->GetInputNode() == &Node;
		Info.bOutputNode = Graph && Graph->GetOutputNode() == &Node;
		Node.GetNodePosition(Info.PositionX, Info.PositionY);
		if (UPCGSettings* Settings = Node.GetSettings())
		{
			Info.SettingsClassPath = Settings->GetClass()->GetPathName();
			Info.bEnabled = Settings->bEnabled;
		}
		for (UPCGPin* Pin : Node.GetInputPins())
		{
			if (Pin)
			{
				Info.Pins.Add(MakePinInfo(*Pin));
			}
		}
		for (UPCGPin* Pin : Node.GetOutputPins())
		{
			if (Pin)
			{
				Info.Pins.Add(MakePinInfo(*Pin));
			}
		}
		return Info;
	}

	bool SaveGraphIfRequested(UPCGGraph* Graph, EPCGChangeType ChangeType, bool bSave, FBridgePCGGraphEditResult& Result)
	{
		(void)ChangeType;
		if (!Graph)
		{
			Result.Error = TEXT("PCG graph is unavailable.");
			return false;
		}
		Graph->PostEditChange();
		Graph->MarkPackageDirty();
		if (bSave && !UEditorAssetLibrary::SaveAsset(Graph->GetPathName(), false))
		{
			Result.Error = TEXT("The graph edit succeeded but the asset could not be saved.");
			return false;
		}
		return true;
	}
}

TArray<FString> UUnrealBridgePCGLibrary::ListPCGGraphAssets(const FString& Filter, int32 Max)
{
	TArray<FString> Out;
	const int32 Cap = FMath::Max(1, Max);

	IAssetRegistry& Reg = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	Reg.GetAssetsByClass(UPCGGraph::StaticClass()->GetClassPathName(), Assets, /*bSearchSubClasses=*/true);

	for (const FAssetData& A : Assets)
	{
		if (Out.Num() >= Cap)
		{
			break;
		}
		const FString Path = A.GetObjectPathString();
		if (Filter.IsEmpty() || A.AssetName.ToString().Contains(Filter) || Path.Contains(Filter))
		{
			Out.Add(Path);
		}
	}
	return Out;
}

TArray<FBridgePCGComponentEntry> UUnrealBridgePCGLibrary::ListPCGComponentsInLevel(const FString& LevelFilter, int32 Max)
{
	using namespace BridgePCGImpl;
	TArray<FBridgePCGComponentEntry> Out;
	const int32 Cap = FMath::Max(1, Max);

	UWorld* World = GetEditorWorld();
	if (!World)
	{
		return Out;
	}

	for (TActorIterator<AActor> It(World); It && Out.Num() < Cap; ++It)
	{
		AActor* A = *It;
		if (!A) { continue; }

		// Optional level filter: skip actors whose outer-level package name doesn't contain the filter.
		if (!LevelFilter.IsEmpty())
		{
			const ULevel* Level = A->GetLevel();
			const FString LevelName = Level ? Level->GetOutermost()->GetName() : FString{};
			if (!LevelName.Contains(LevelFilter))
			{
				continue;
			}
		}

		TArray<UPCGComponent*> Comps;
		A->GetComponents<UPCGComponent>(Comps);
		for (UPCGComponent* C : Comps)
		{
			if (!C || Out.Num() >= Cap) { continue; }
			FBridgePCGComponentEntry E;
			E.ActorLabel    = A->GetActorLabel();
			E.ComponentName = C->GetName();
			E.GraphPath     = PathForGraph(C->GetGraph());
			E.bGenerated    = C->bGenerated;
			E.bGenerating   = C->IsGenerating();
			Out.Add(MoveTemp(E));
		}
	}
	return Out;
}

FBridgePCGComponentState UUnrealBridgePCGLibrary::GetPCGComponentState(const FString& ActorLabel, const FString& ComponentName)
{
	using namespace BridgePCGImpl;
	FBridgePCGComponentState State;
	UWorld* World = GetEditorWorld();
	AActor* A = FindActor(World, ActorLabel);
	UPCGComponent* C = FindPCGComponent(A, ComponentName);
	if (!C)
	{
		return State;
	}

	State.GraphPath        = PathForGraph(C->GetGraph());
	State.bGenerated       = C->bGenerated;
	State.bDirty           = false;  // bDirtyGenerated is private; expose via getter when available
	State.bGenerating      = C->IsGenerating();
	State.GeneratedBounds  = C->GetLastGeneratedBounds();
	State.LastGenerationIso = FDateTime::UtcNow().ToIso8601();  // best-effort marker (not persisted)
	return State;
}

TArray<FBridgePCGOverrideEntry> UUnrealBridgePCGLibrary::GetPCGComponentOverrides(const FString& ActorLabel, const FString& ComponentName)
{
	using namespace BridgePCGImpl;
	TArray<FBridgePCGOverrideEntry> Out;
	UWorld* World = GetEditorWorld();
	AActor* A = FindActor(World, ActorLabel);
	UPCGComponent* C = FindPCGComponent(A, ComponentName);
	if (!C)
	{
		return Out;
	}
	UPCGGraphInstance* GI = C->GetGraphInstance();
	if (!GI)
	{
		return Out;
	}
	const FInstancedPropertyBag* UserParams = GI->GetUserParametersStruct();
	if (!UserParams)
	{
		return Out;
	}
	const UPropertyBag* BagDesc = UserParams->GetPropertyBagStruct();
	const uint8* Memory          = UserParams->GetValue().GetMemory();
	if (!BagDesc || !Memory)
	{
		return Out;
	}

	for (const FPropertyBagPropertyDesc& Desc : BagDesc->GetPropertyDescs())
	{
		if (!Desc.CachedProperty)
		{
			continue;
		}
		FBridgePCGOverrideEntry E;
		E.Name    = Desc.Name.ToString();
		E.TypeStr = Desc.CachedProperty->GetCPPType();

		FString Exported;
		const void* Addr = Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory);
		Desc.CachedProperty->ExportText_Direct(Exported, Addr, Addr, /*Parent=*/nullptr, PPF_None);
		E.ValueStr = Exported;
		Out.Add(MoveTemp(E));
	}
	return Out;
}

bool UUnrealBridgePCGLibrary::SetPCGComponentOverride(
	const FString& ActorLabel, const FString& ComponentName,
	const FString& Name, const FString& ExportedValue)
{
	using namespace BridgePCGImpl;
	UWorld* World = GetEditorWorld();
	AActor* A = FindActor(World, ActorLabel);
	UPCGComponent* C = FindPCGComponent(A, ComponentName);
	if (!C)
	{
		return false;
	}
	UPCGGraphInstance* GI = C->GetGraphInstance();
	if (!GI)
	{
		return false;
	}
	FInstancedPropertyBag* UserParams = GI->GetMutableUserParametersStruct_Unsafe();
	if (!UserParams)
	{
		return false;
	}
	const UPropertyBag* BagDesc = UserParams->GetPropertyBagStruct();
	uint8* Memory               = const_cast<uint8*>(UserParams->GetValue().GetMemory());
	if (!BagDesc || !Memory)
	{
		return false;
	}

	const FName TargetName(*Name);
	for (const FPropertyBagPropertyDesc& Desc : BagDesc->GetPropertyDescs())
	{
		if (Desc.Name != TargetName || !Desc.CachedProperty)
		{
			continue;
		}
		// Reject empty input. UE's ImportText silently accepts "" on numeric
		// properties and writes zero — caller must use the property's exported
		// "empty" form (e.g. `""` for FString, not the empty Python string).
		if (ExportedValue.IsEmpty())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("UnrealBridge|PCG: empty value rejected for '%s' (type=%s)"),
				*Name, *Desc.CachedProperty->GetCPPType());
			return false;
		}

		void* Addr = Desc.CachedProperty->ContainerPtrToValuePtr<void>(Memory);

		// Snapshot the current value so we can restore on parse failure. UE's
		// FDoubleProperty / FFloatProperty / FIntProperty silently accept garbage:
		// they return the input pointer unchanged (non-null) and write zero to the
		// target. Without snapshot+restore, even when we detect the failure via
		// "no characters consumed", the target value has already been zeroed.
		FString OriginalExport;
		Desc.CachedProperty->ExportText_Direct(OriginalExport, Addr, Addr, /*Parent=*/nullptr, PPF_None);

		const TCHAR* Cursor = *ExportedValue;
		const TCHAR* Parsed = Desc.CachedProperty->ImportText_Direct(Cursor, Addr, /*Parent=*/nullptr, PPF_None);

		const bool bConsumedNothing = (Parsed == Cursor);
		if (!Parsed || bConsumedNothing)
		{
			// Restore the pre-write value via ImportText of the snapshot.
			const TCHAR* OrigCursor = *OriginalExport;
			Desc.CachedProperty->ImportText_Direct(OrigCursor, Addr, /*Parent=*/nullptr, PPF_None);

			UE_LOG(LogTemp, Warning,
				TEXT("UnrealBridge|PCG: ImportText failed for '%s' = '%s' (type=%s); restored '%s'"),
				*Name, *ExportedValue, *Desc.CachedProperty->GetCPPType(), *OriginalExport);
			return false;
		}
		C->Modify();
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("UnrealBridge|PCG: override property '%s' not found on component"), *Name);
	return false;
}

bool UUnrealBridgePCGLibrary::TriggerPCGGenerate(const FString& ActorLabel, const FString& ComponentName, bool bForce)
{
	using namespace BridgePCGImpl;
	UWorld* World = GetEditorWorld();
	AActor* A = FindActor(World, ActorLabel);
	UPCGComponent* C = FindPCGComponent(A, ComponentName);
	if (!C)
	{
		return false;
	}
	C->Generate(bForce);
	return true;
}

FBridgePCGWaitResult UUnrealBridgePCGLibrary::WaitForPCGGenerate(const FString& ActorLabel, const FString& ComponentName, float TimeoutSec)
{
	FBridgePCGWaitResult Result;
	const FBridgePCGGenerationPollResult Poll = WaitPCGGeneration(ActorLabel, ComponentName);
	Result.bSuccess = Poll.bComplete && Poll.Error.IsEmpty();
	Result.ElapsedMs = 0.f;
	Result.Note = Poll.Error.IsEmpty()
		? (Poll.bComplete ? Poll.Status : TEXT("pending; poll WaitPCGGeneration across Editor ticks"))
		: Poll.Error;
	return Result;
}

FBridgePCGGenerationPollResult UUnrealBridgePCGLibrary::WaitPCGGeneration(
	const FString& ActorLabel,
	const FString& ComponentName)
{
	using namespace BridgePCGImpl;
	FBridgePCGGenerationPollResult Result;
	UWorld* World = GetEditorWorld();
	AActor* Actor = FindActor(World, ActorLabel);
	UPCGComponent* Component = FindPCGComponent(Actor, ComponentName);
	if (!Component)
	{
		Result.Status = TEXT("Failed");
		Result.Error = TEXT("PCG component not found");
		return Result;
	}

	Result.bGenerated = Component->bGenerated;
	Result.bComplete = !Component->IsGenerating();
	Result.bSuccess = Result.bComplete;
	Result.Status = Result.bComplete
		? (Result.bGenerated ? TEXT("Generated") : TEXT("Idle"))
		: TEXT("Generating");
	return Result;
}

bool UUnrealBridgePCGLibrary::CleanupPCGComponent(const FString& ActorLabel, const FString& ComponentName, bool bRemoveComponents)
{
	using namespace BridgePCGImpl;
	UWorld* World = GetEditorWorld();
	AActor* A = FindActor(World, ActorLabel);
	UPCGComponent* C = FindPCGComponent(A, ComponentName);
	if (!C)
	{
		return false;
	}
	C->Cleanup(bRemoveComponents);
	return true;
}

FBridgePCGGraphInfo UUnrealBridgePCGLibrary::GetPCGGraphStructure(const FString& GraphPath)
{
	using namespace BridgePCGImpl;
	FBridgePCGGraphInfo Result;
	UPCGGraph* Graph = LoadGraph(GraphPath);
	if (!Graph)
	{
		return Result;
	}

	Result.bFound = true;
	Result.AssetPath = Graph->GetPathName();
	TArray<UPCGNode*> Nodes;
	GetAllNodes(Graph, Nodes);
	for (UPCGNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}
		Result.Nodes.Add(MakeNodeInfo(Graph, *Node));
		for (UPCGPin* Pin : Node->GetOutputPins())
		{
			if (!Pin)
			{
				continue;
			}
			for (UPCGEdge* Edge : Pin->Edges)
			{
				UPCGPin* OtherPin = Edge ? Edge->GetOtherPin(Pin) : nullptr;
				if (!OtherPin || !OtherPin->Node)
				{
					continue;
				}
				FBridgePCGEdgeInfo EdgeInfo;
				EdgeInfo.FromNodeId = Node->GetName();
				EdgeInfo.FromPin = Pin->Properties.Label.ToString();
				EdgeInfo.ToNodeId = OtherPin->Node->GetName();
				EdgeInfo.ToPin = OtherPin->Properties.Label.ToString();
				Result.Edges.Add(MoveTemp(EdgeInfo));
			}
		}
	}
	return Result;
}

FBridgePCGGraphValidationResult UUnrealBridgePCGLibrary::ValidatePCGGraph(const FString& GraphPath)
{
	using namespace BridgePCGImpl;
	FBridgePCGGraphValidationResult Result;
	UPCGGraph* Graph = LoadGraph(GraphPath);
	if (!Graph)
	{
		Result.ErrorCount = 1;
		Result.Messages.Add(TEXT("Error: PCG graph could not be loaded."));
		return Result;
	}
	if (!Graph->GetInputNode() || !Graph->GetOutputNode())
	{
		++Result.ErrorCount;
		Result.Messages.Add(TEXT("Error: PCG graph is missing its input or output node."));
	}

	TArray<UPCGNode*> Nodes;
	GetAllNodes(Graph, Nodes);
	TMap<UPCGNode*, TArray<UPCGNode*>> Adjacency;
	for (UPCGNode* Node : Nodes)
	{
		if (!Node)
		{
			++Result.ErrorCount;
			Result.Messages.Add(TEXT("Error: PCG graph contains a null node."));
			continue;
		}
		if (!Node->GetSettings())
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(TEXT("Error: Node %s has no settings object."), *Node->GetName()));
		}
		int32 ConnectionCount = 0;
		for (UPCGPin* InputPin : Node->GetInputPins())
		{
			if (!InputPin)
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(TEXT("Error: Node %s contains a null input pin."), *Node->GetName()));
				continue;
			}
			ConnectionCount += InputPin->EdgeCount();
			if (InputPin->Properties.IsRequiredPin() && !InputPin->IsConnected())
			{
				++Result.WarningCount;
				Result.Messages.Add(FString::Printf(TEXT("Warning: Required pin %s.%s is not connected."),
					*Node->GetName(), *InputPin->Properties.Label.ToString()));
			}
		}
		for (UPCGPin* OutputPin : Node->GetOutputPins())
		{
			if (!OutputPin)
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(TEXT("Error: Node %s contains a null output pin."), *Node->GetName()));
				continue;
			}
			ConnectionCount += OutputPin->EdgeCount();
			for (UPCGEdge* Edge : OutputPin->Edges)
			{
				UPCGPin* OtherPin = Edge ? Edge->GetOtherPin(OutputPin) : nullptr;
				if (!Edge || !Edge->IsValid() || !OtherPin || !OtherPin->Node)
				{
					++Result.ErrorCount;
					Result.Messages.Add(FString::Printf(TEXT("Error: Node %s has an invalid edge."), *Node->GetName()));
					continue;
				}
				if (!OutputPin->IsCompatible(OtherPin))
				{
					++Result.ErrorCount;
					Result.Messages.Add(FString::Printf(TEXT("Error: Incompatible edge %s.%s -> %s.%s."),
						*Node->GetName(), *OutputPin->Properties.Label.ToString(),
						*OtherPin->Node->GetName(), *OtherPin->Properties.Label.ToString()));
				}
				Adjacency.FindOrAdd(Node).AddUnique(OtherPin->Node);
			}
		}
		if (ConnectionCount == 0 && Node != Graph->GetInputNode() && Node != Graph->GetOutputNode())
		{
			++Result.WarningCount;
			Result.Messages.Add(FString::Printf(TEXT("Warning: Node %s is isolated."), *Node->GetName()));
		}
	}

	TMap<UPCGNode*, uint8> VisitState;
	TFunction<void(UPCGNode*)> Visit = [&](UPCGNode* Node)
	{
		VisitState.Add(Node, 1);
		for (UPCGNode* Next : Adjacency.FindRef(Node))
		{
			const uint8 State = VisitState.FindRef(Next);
			if (State == 1)
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(TEXT("Error: Cycle detected through node %s."), *Next->GetName()));
			}
			else if (State == 0)
			{
				Visit(Next);
			}
		}
		VisitState.Add(Node, 2);
	};
	for (UPCGNode* Node : Nodes)
	{
		if (Node && VisitState.FindRef(Node) == 0)
		{
			Visit(Node);
		}
	}

	Result.bSuccess = Result.ErrorCount == 0;
	if (Result.Messages.IsEmpty())
	{
		Result.Messages.Add(TEXT("PCG graph validation passed."));
	}
	return Result;
}

FBridgePCGGraphEditResult UUnrealBridgePCGLibrary::AddPCGGraphNode(
	const FString& GraphPath,
	const FString& SettingsClassPath,
	const FString& NodeTitle,
	int32 PositionX,
	int32 PositionY,
	bool bSave)
{
	using namespace BridgePCGImpl;
	FBridgePCGGraphEditResult Result;
	UPCGGraph* Graph = LoadGraph(GraphPath);
	UClass* SettingsClass = StaticLoadClass(UPCGSettings::StaticClass(), nullptr, *SettingsClassPath);
	if (!Graph || !SettingsClass || SettingsClass->HasAnyClassFlags(CLASS_Abstract))
	{
		Result.Error = TEXT("PCG graph or concrete UPCGSettings class could not be loaded.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddPCGNode", "Bridge: Add PCG Node"));
	Graph->Modify();
	UPCGSettings* NewSettings = nullptr;
	UPCGNode* Node = Graph->AddNodeOfType(SettingsClass, NewSettings);
	if (!Node || !NewSettings)
	{
		Result.Error = TEXT("UPCGGraph rejected the settings class.");
		return Result;
	}
	Node->Modify();
	NewSettings->Modify();
	if (!NodeTitle.IsEmpty())
	{
		Node->NodeTitle = FName(*NodeTitle);
	}
	Node->SetNodePosition(PositionX, PositionY);
	Node->UpdateAfterSettingsChangeDuringCreation();
	Result.NodeId = Node->GetName();
	Result.bSuccess = SaveGraphIfRequested(Graph, EPCGChangeType::Structural, bSave, Result);
	return Result;
}

FBridgePCGGraphEditResult UUnrealBridgePCGLibrary::ConnectPCGGraphNodes(
	const FString& GraphPath,
	const FString& FromNodeId,
	const FString& FromPin,
	const FString& ToNodeId,
	const FString& ToPin,
	bool bSave)
{
	using namespace BridgePCGImpl;
	FBridgePCGGraphEditResult Result;
	UPCGGraph* Graph = LoadGraph(GraphPath);
	UPCGNode* FromNode = FindNode(Graph, FromNodeId);
	UPCGNode* ToNode = FindNode(Graph, ToNodeId);
	UPCGPin* SourcePin = FromNode ? FromNode->GetOutputPin(FName(*FromPin)) : nullptr;
	UPCGPin* TargetPin = ToNode ? ToNode->GetInputPin(FName(*ToPin)) : nullptr;
	if (!Graph || !FromNode || !ToNode || !SourcePin || !TargetPin)
	{
		Result.Error = TEXT("Graph, node id, or pin label was not found.");
		return Result;
	}
	for (UPCGEdge* ExistingEdge : SourcePin->Edges)
	{
		if (ExistingEdge && ExistingEdge->GetOtherPin(SourcePin) == TargetPin)
		{
			Result.bSuccess = true;
			Result.NodeId = ToNode->GetName();
			return Result;
		}
	}
	if (!SourcePin->CanConnect(TargetPin))
	{
		Result.Error = TEXT("The PCG pins are not type-compatible or do not allow another connection.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeConnectPCGNodes", "Bridge: Connect PCG Nodes"));
	Graph->Modify();
	FromNode->Modify();
	ToNode->Modify();
	Graph->AddEdge(FromNode, FName(*FromPin), ToNode, FName(*ToPin));
	bool bConnected = false;
	for (UPCGEdge* AddedEdge : SourcePin->Edges)
	{
		if (AddedEdge && AddedEdge->GetOtherPin(SourcePin) == TargetPin)
		{
			bConnected = true;
			break;
		}
	}
	if (!bConnected)
	{
		Result.Error = TEXT("UPCGGraph did not create the requested edge.");
		return Result;
	}
	Result.NodeId = ToNode->GetName();
	Result.bSuccess = SaveGraphIfRequested(Graph, EPCGChangeType::Edge, bSave, Result);
	return Result;
}

FBridgePCGGraphEditResult UUnrealBridgePCGLibrary::SetPCGNodeSettingsProperty(
	const FString& GraphPath,
	const FString& NodeId,
	const FString& PropertyName,
	const FString& ValueExportText,
	bool bSave)
{
	using namespace BridgePCGImpl;
	FBridgePCGGraphEditResult Result;
	UPCGGraph* Graph = LoadGraph(GraphPath);
	UPCGNode* Node = FindNode(Graph, NodeId);
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	FProperty* Property = Settings ? Settings->GetClass()->FindPropertyByName(FName(*PropertyName)) : nullptr;
	if (!Graph || !Node || !Settings || !Property)
	{
		Result.Error = TEXT("Graph, node, settings object, or property was not found.");
		return Result;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Settings);
	FString OriginalValue;
	Property->ExportTextItem_Direct(OriginalValue, ValuePtr, nullptr, Settings, PPF_None);
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetPCGNodeProperty", "Bridge: Set PCG Node Property"));
	Graph->Modify();
	Node->Modify();
	Settings->Modify();
	const TCHAR* Start = *ValueExportText;
	const TCHAR* Parsed = Property->ImportText_Direct(Start, ValuePtr, Settings, PPF_None, GLog);
	if (!Parsed || Parsed == Start)
	{
		const TCHAR* OriginalStart = *OriginalValue;
		Property->ImportText_Direct(OriginalStart, ValuePtr, Settings, PPF_None, GLog);
		Result.Error = TEXT("ValueExportText could not be imported for the PCG settings property.");
		return Result;
	}
	Settings->PostEditChange();
	Node->UpdateAfterSettingsChangeDuringCreation();
	Result.NodeId = Node->GetName();
	Result.bSuccess = SaveGraphIfRequested(Graph, EPCGChangeType::Settings, bSave, Result);
	return Result;
}

FBridgePCGGraphEditResult UUnrealBridgePCGLibrary::SetPCGNodeEnabled(
	const FString& GraphPath,
	const FString& NodeId,
	bool bEnabled,
	bool bSave)
{
	using namespace BridgePCGImpl;
	FBridgePCGGraphEditResult Result;
	UPCGGraph* Graph = LoadGraph(GraphPath);
	UPCGNode* Node = FindNode(Graph, NodeId);
	UPCGSettings* Settings = Node ? Node->GetSettings() : nullptr;
	if (!Graph || !Node || !Settings || !Settings->CanBeDisabled())
	{
		Result.Error = TEXT("Graph/node was not found or this node type cannot be disabled.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetPCGNodeEnabled", "Bridge: Set PCG Node Enabled"));
	Graph->Modify();
	Node->Modify();
	Settings->Modify();
	Settings->SetEnabled(bEnabled);
	Result.NodeId = Node->GetName();
	Result.bSuccess = SaveGraphIfRequested(Graph, EPCGChangeType::Settings, bSave, Result);
	return Result;
}

#undef LOCTEXT_NAMESPACE

#endif // !UE_VERSION_OLDER_THAN(5, 6, 0)
