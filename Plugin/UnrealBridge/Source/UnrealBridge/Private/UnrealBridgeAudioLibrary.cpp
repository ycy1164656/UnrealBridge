#include "UnrealBridgeAudioLibrary.h"

#include "AudioParameter.h"
#include "Components/AudioComponent.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "MetasoundBuilderBase.h"
#include "Metasound.h"
#include "MetasoundAssetBase.h"
#include "MetasoundDocumentInterface.h"
#include "MetasoundEditorSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendDocumentBuilderRegistry.h"
#include "MetasoundFrontendLiteral.h"
#include "MetasoundSource.h"
#include "Misc/DataValidation.h"
#include "Misc/ScopeExit.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectIterator.h"

namespace BridgeAudioImpl
{
	FString ValidationResultToString(EDataValidationResult Result)
	{
		switch (Result)
		{
		case EDataValidationResult::Valid: return TEXT("Valid");
		case EDataValidationResult::Invalid: return TEXT("Invalid");
		default: return TEXT("NotValidated");
		}
	}

	void ValidateObject(UObject* Object, FString& OutResult, TArray<FString>& OutMessages)
	{
#if WITH_EDITOR
		if (!Object)
		{
			OutResult = TEXT("NotValidated");
			return;
		}
		FDataValidationContext Context;
		OutResult = ValidationResultToString(Object->IsDataValid(Context));
		for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
		{
			const FString Severity = Issue.Severity == EMessageSeverity::Error
				? TEXT("error")
				: Issue.Severity == EMessageSeverity::Warning ? TEXT("warning") : TEXT("info");
			const FString Message = Issue.TokenizedMessage.IsValid()
				? Issue.TokenizedMessage->ToText().ToString()
				: Issue.Message.ToString();
			OutMessages.Add(FString::Printf(TEXT("%s: %s"), *Severity, *Message));
		}
#else
		OutResult = TEXT("NotValidated");
#endif
	}

	TScriptInterface<IMetaSoundDocumentInterface> MakeDocumentInterface(UObject* Object)
	{
		TScriptInterface<IMetaSoundDocumentInterface> Result;
		if (Object)
		{
			if (IMetaSoundDocumentInterface* Interface = Cast<IMetaSoundDocumentInterface>(Object))
			{
				Result.SetObject(Object);
				Result.SetInterface(Interface);
			}
		}
		return Result;
	}

	const FMetasoundFrontendDocument* GetConstDocument(UObject* Object)
	{
		if (const UMetaSoundSource* Source = Cast<UMetaSoundSource>(Object))
		{
			return &static_cast<const FMetasoundAssetBase&>(*Source).GetConstDocumentChecked();
		}
		if (const UMetaSoundPatch* Patch = Cast<UMetaSoundPatch>(Object))
		{
			return &static_cast<const FMetasoundAssetBase&>(*Patch).GetConstDocumentChecked();
		}
		return nullptr;
	}

	FString AccessTypeToString(EMetasoundFrontendVertexAccessType Type)
	{
		return LexToString(Type);
	}

	FBridgeMetaSoundMemberInfo MakeInputInfo(const FMetasoundFrontendClassInput& Input)
	{
		FBridgeMetaSoundMemberInfo Info;
		Info.Name = Input.Name.ToString();
		Info.DataType = Input.TypeName.ToString();
		Info.AccessType = AccessTypeToString(Input.AccessType);
		if (const FMetasoundFrontendLiteral* Default = Input.FindConstDefault(Metasound::Frontend::DefaultPageID))
		{
			Info.DefaultValue = Default->ToString();
		}
		return Info;
	}

	FBridgeMetaSoundMemberInfo MakeOutputInfo(const FMetasoundFrontendClassOutput& Output)
	{
		FBridgeMetaSoundMemberInfo Info;
		Info.Name = Output.Name.ToString();
		Info.DataType = Output.TypeName.ToString();
		Info.AccessType = AccessTypeToString(Output.AccessType);
		return Info;
	}

	FBridgeMetaSoundGraphInfo MakeGraphInfo(UObject* Asset, int32 MaxNodes)
	{
		FBridgeMetaSoundGraphInfo Info;
		if (!Asset)
		{
			Info.Error = TEXT("MetaSound asset not found");
			return Info;
		}
		TScriptInterface<IMetaSoundDocumentInterface> Interface = MakeDocumentInterface(Asset);
		if (!Interface)
		{
			Info.Error = FString::Printf(TEXT("Asset is not a MetaSound document: %s"), *Asset->GetPathName());
			return Info;
		}

		Info.bFound = true;
		Info.Path = Asset->GetPathName();
		Info.AssetClass = Asset->GetClass()->GetPathName();
		const FMetasoundFrontendDocument* DocumentPtr = GetConstDocument(Asset);
		if (!DocumentPtr)
		{
			Info.bFound = false;
			Info.Error = FString::Printf(TEXT("Unsupported MetaSound asset class: %s"), *Asset->GetClass()->GetPathName());
			return Info;
		}
		const FMetasoundFrontendDocument& Document = *DocumentPtr;
		Info.DocumentVersion = Document.Metadata.Version.ToString();
		Info.RootClassName = Document.RootGraph.Metadata.GetClassName().ToString();
		Info.RootClassVersion = Document.RootGraph.Metadata.GetVersion().ToString();
		if (const UScriptStruct* TemplateStruct = Document.Template.GetScriptStruct())
		{
			Info.TemplateType = TemplateStruct->GetPathName();
			Info.bPreset = Info.TemplateType.Contains(TEXT("Preset"), ESearchCase::IgnoreCase);
		}
		for (const FMetasoundFrontendVersion& Version : Document.Interfaces)
		{
			Info.Interfaces.Add(Version.ToString());
		}
		Info.Interfaces.Sort();
		const FMetasoundFrontendClassInterface& RootInterface = Document.RootGraph.GetDefaultInterface();
		for (const FMetasoundFrontendClassInput& Input : RootInterface.Inputs)
		{
			Info.Inputs.Add(MakeInputInfo(Input));
		}
		for (const FMetasoundFrontendClassOutput& Output : RootInterface.Outputs)
		{
			Info.Outputs.Add(MakeOutputInfo(Output));
		}
		Info.DependencyCount = Document.Dependencies.Num();
		TMap<FGuid, FString> ClassNames;
		for (const FMetasoundFrontendClass& Dependency : Document.Dependencies)
		{
			ClassNames.Add(Dependency.ID, Dependency.Metadata.GetClassName().ToString());
		}

		MaxNodes = FMath::Clamp(MaxNodes, 0, 65536);
		const TArray<FMetasoundFrontendGraph>& Pages = Document.RootGraph.GetConstGraphPages();
		Info.TotalPageCount = Pages.Num();
		for (const FMetasoundFrontendGraph& Page : Pages)
		{
			FBridgeMetaSoundPageInfo PageInfo;
			PageInfo.PageId = Page.PageID.ToString(EGuidFormats::DigitsWithHyphensLower);
			PageInfo.NodeCount = Page.Nodes.Num();
			PageInfo.EdgeCount = Page.Edges.Num();
			PageInfo.VariableCount = Page.Variables.Num();
			Info.Pages.Add(MoveTemp(PageInfo));
			Info.TotalNodeCount += Page.Nodes.Num();
			Info.TotalEdgeCount += Page.Edges.Num();
			for (const FMetasoundFrontendNode& Node : Page.Nodes)
			{
				if (Info.Nodes.Num() >= MaxNodes)
				{
					break;
				}
				FBridgeMetaSoundNodeInfo NodeInfo;
				NodeInfo.NodeId = Node.GetID().ToString(EGuidFormats::DigitsWithHyphensLower);
				NodeInfo.Name = Node.Name.ToString();
				NodeInfo.ClassId = Node.ClassID.ToString(EGuidFormats::DigitsWithHyphensLower);
				NodeInfo.ClassName = ClassNames.FindRef(Node.ClassID);
				NodeInfo.PageId = Page.PageID.ToString(EGuidFormats::DigitsWithHyphensLower);
				NodeInfo.InputCount = Node.Interface.Inputs.Num();
				NodeInfo.OutputCount = Node.Interface.Outputs.Num();
				Info.Nodes.Add(MoveTemp(NodeInfo));
			}
		}
		ValidateObject(Asset, Info.ValidationResult, Info.ValidationMessages);
		return Info;
	}

	bool ResolveNodeRef(const FString& Ref, const TArray<FGuid>& Produced, FGuid& OutGuid, FString& OutError)
	{
		if (Ref.StartsWith(TEXT("$")))
		{
			const FString IndexText = Ref.Mid(1);
			if (!IndexText.IsNumeric())
			{
				OutError = FString::Printf(TEXT("Invalid back-reference: %s"), *Ref);
				return false;
			}
			const int32 Index = FCString::Atoi(*IndexText);
			if (!Produced.IsValidIndex(Index) || !Produced[Index].IsValid())
			{
				OutError = FString::Printf(TEXT("Back-reference did not produce a node: %s"), *Ref);
				return false;
			}
			OutGuid = Produced[Index];
			return true;
		}
		if (!FGuid::Parse(Ref, OutGuid) || !OutGuid.IsValid())
		{
			OutError = FString::Printf(TEXT("Invalid node GUID: %s"), *Ref);
			return false;
		}
		return true;
	}

	bool MakeLiteral(const FString& Type, const FString& Value, FMetasoundFrontendLiteral& OutLiteral, FString& OutError)
	{
		const FString Normalized = Type.TrimStartAndEnd().ToLower();
		if (Normalized.IsEmpty() || Normalized == TEXT("default"))
		{
			OutLiteral.Set(FMetasoundFrontendLiteral::FDefault{});
			return true;
		}
		if (Normalized == TEXT("bool") || Normalized == TEXT("boolean"))
		{
			OutLiteral.Set(Value.ToBool());
			return true;
		}
		if (Normalized == TEXT("int") || Normalized == TEXT("integer") || Normalized == TEXT("int32"))
		{
			OutLiteral.Set(FCString::Atoi(*Value));
			return true;
		}
		if (Normalized == TEXT("float") || Normalized == TEXT("double"))
		{
			OutLiteral.Set(FCString::Atof(*Value));
			return true;
		}
		if (Normalized == TEXT("string"))
		{
			OutLiteral.Set(Value);
			return true;
		}
		if (Normalized == TEXT("object") || Normalized == TEXT("uobject"))
		{
			UObject* Object = Value.IsEmpty() ? nullptr : LoadObject<UObject>(nullptr, *Value);
			if (!Value.IsEmpty() && !Object)
			{
				OutError = FString::Printf(TEXT("Literal object not found: %s"), *Value);
				return false;
			}
			OutLiteral.Set(Object);
			return true;
		}
		OutError = FString::Printf(TEXT("Unsupported literal_type: %s"), *Type);
		return false;
	}

	FString PlayStateToString(EAudioComponentPlayState State)
	{
		if (const UEnum* Enum = StaticEnum<EAudioComponentPlayState>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(State));
		}
		return TEXT("Unknown");
	}

	FString AudioParameterTypeToString(EAudioParameterType Type)
	{
		switch (Type)
		{
		case EAudioParameterType::None: return TEXT("None");
		case EAudioParameterType::Boolean: return TEXT("Boolean");
		case EAudioParameterType::Integer: return TEXT("Integer");
		case EAudioParameterType::Float: return TEXT("Float");
		case EAudioParameterType::String: return TEXT("String");
		case EAudioParameterType::Object: return TEXT("Object");
		case EAudioParameterType::NoneArray: return TEXT("NoneArray");
		case EAudioParameterType::BooleanArray: return TEXT("BooleanArray");
		case EAudioParameterType::IntegerArray: return TEXT("IntegerArray");
		case EAudioParameterType::FloatArray: return TEXT("FloatArray");
		case EAudioParameterType::StringArray: return TEXT("StringArray");
		case EAudioParameterType::ObjectArray: return TEXT("ObjectArray");
		case EAudioParameterType::Trigger: return TEXT("Trigger");
		default: return TEXT("Unknown");
		}
	}

	FString ParameterSummary(const FAudioParameter& Parameter)
	{
		return FString::Printf(TEXT("%s:%s"), *Parameter.ParamName.ToString(), *AudioParameterTypeToString(Parameter.ParamType));
	}
}

FBridgeMetaSoundGraphInfo UUnrealBridgeAudioLibrary::GetMetaSoundGraphInfo(
	const FString& MetaSoundPath,
	int32 MaxNodes)
{
	return BridgeAudioImpl::MakeGraphInfo(LoadObject<UObject>(nullptr, *MetaSoundPath), MaxNodes);
}

FBridgeMetaSoundGraphOpResult UUnrealBridgeAudioLibrary::ApplyMetaSoundGraphOps(
	const FString& MetaSoundPath,
	const TArray<FBridgeMetaSoundGraphOp>& Ops,
	bool bRegisterWithFrontend)
{
	FBridgeMetaSoundGraphOpResult Out;
	Out.ProducedNodeIds.SetNum(Ops.Num());
	UObject* Asset = LoadObject<UObject>(nullptr, *MetaSoundPath);
	if (!Asset)
	{
		Out.Error = FString::Printf(TEXT("MetaSound asset not found: %s"), *MetaSoundPath);
		return Out;
	}
	TScriptInterface<IMetaSoundDocumentInterface> Interface = BridgeAudioImpl::MakeDocumentInterface(Asset);
	if (!Interface)
	{
		Out.Error = FString::Printf(TEXT("Asset is not a MetaSound document: %s"), *MetaSoundPath);
		return Out;
	}
	UMetaSoundEditorSubsystem* EditorSubsystem = GEditor ? GEditor->GetEditorSubsystem<UMetaSoundEditorSubsystem>() : nullptr;
	if (!EditorSubsystem)
	{
		Out.Error = TEXT("MetaSoundEditorSubsystem is unavailable");
		return Out;
	}
	EMetaSoundBuilderResult BuilderResult = EMetaSoundBuilderResult::Failed;
	UMetaSoundBuilderBase* Builder = EditorSubsystem->FindOrBeginBuilding(Interface, BuilderResult);
	if (!Builder || BuilderResult != EMetaSoundBuilderResult::Succeeded)
	{
		Out.Error = TEXT("FindOrBeginBuilding failed for MetaSound asset");
		return Out;
	}
	const FMetasoundFrontendClassName BuilderClassName = Builder->GetRootGraphClassName();
	const FTopLevelAssetPath BuilderAssetPath = Interface->GetAssetPathChecked();
	ON_SCOPE_EXIT
	{
		// FindOrBeginBuilding registers an editor builder whose document still points at
		// the asset. ChangeSet rollback may delete or unload a newly duplicated asset, so
		// release the builder on every exit path before rollback is allowed to run.
		if (Metasound::Frontend::IDocumentBuilderRegistry* Registry = Metasound::Frontend::IDocumentBuilderRegistry::Get())
		{
			Registry->FinishBuilding(BuilderClassName, BuilderAssetPath);
		}
	};

	Asset->Modify();
	TArray<FGuid> Produced;
	Produced.SetNum(Ops.Num());
	for (int32 Index = 0; Index < Ops.Num(); ++Index)
	{
		const FBridgeMetaSoundGraphOp& Op = Ops[Index];
		const FString Kind = Op.Op.TrimStartAndEnd().ToLower();
		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Failed;
		FString Error;
		if (Kind == TEXT("add_input") || Kind == TEXT("add_output"))
		{
			FMetasoundFrontendLiteral Literal;
			if (!BridgeAudioImpl::MakeLiteral(Op.LiteralType, Op.Value, Literal, Error))
			{
				Out.Error = Error;
				Out.FailedAtIndex = Index;
				return Out;
			}
			if (Kind == TEXT("add_input"))
			{
				const FMetaSoundBuilderNodeOutputHandle Handle = Builder->AddGraphInputNode(
					FName(*Op.Name), FName(*Op.DataType), Literal, Result, false);
				Produced[Index] = Handle.NodeID;
			}
			else
			{
				const FMetaSoundBuilderNodeInputHandle Handle = Builder->AddGraphOutputNode(
					FName(*Op.Name), FName(*Op.DataType), Literal, Result, false);
				Produced[Index] = Handle.NodeID;
			}
		}
		else if (Kind == TEXT("remove_input"))
		{
			Builder->RemoveGraphInput(FName(*Op.Name), Result);
		}
		else if (Kind == TEXT("remove_output"))
		{
			Builder->RemoveGraphOutput(FName(*Op.Name), Result);
		}
		else if (Kind == TEXT("add_node"))
		{
			const FMetasoundFrontendClassName ClassName(FName(*Op.Namespace), FName(*Op.ClassName), FName(*Op.Variant));
			const FMetaSoundNodeHandle Handle = Builder->AddNodeByClassName(ClassName, Result, FMath::Max(1, Op.MajorVersion));
			Produced[Index] = Handle.NodeID;
		}
		else if (Kind == TEXT("connect"))
		{
			FGuid SourceId;
			FGuid DestinationId;
			if (!BridgeAudioImpl::ResolveNodeRef(Op.SrcRef, Produced, SourceId, Error)
				|| !BridgeAudioImpl::ResolveNodeRef(Op.DstRef, Produced, DestinationId, Error))
			{
				Out.Error = Error;
				Out.FailedAtIndex = Index;
				return Out;
			}
			EMetaSoundBuilderResult SourceResult;
			EMetaSoundBuilderResult DestinationResult;
			const FMetaSoundBuilderNodeOutputHandle Source = Builder->FindNodeOutputByName(
				FMetaSoundNodeHandle(SourceId), FName(*Op.SrcOutput), SourceResult);
			const FMetaSoundBuilderNodeInputHandle Destination = Builder->FindNodeInputByName(
				FMetaSoundNodeHandle(DestinationId), FName(*Op.DstInput), DestinationResult);
			if (SourceResult == EMetaSoundBuilderResult::Succeeded && DestinationResult == EMetaSoundBuilderResult::Succeeded)
			{
				Builder->ConnectNodes(Source, Destination, Result);
			}
		}
		else if (Kind == TEXT("disconnect_input") || Kind == TEXT("disconnect_output") || Kind == TEXT("set_default"))
		{
			FGuid NodeId;
			const FString& Ref = Kind == TEXT("disconnect_output") ? Op.SrcRef : Op.DstRef;
			if (!BridgeAudioImpl::ResolveNodeRef(Ref, Produced, NodeId, Error))
			{
				Out.Error = Error;
				Out.FailedAtIndex = Index;
				return Out;
			}
			if (Kind == TEXT("disconnect_output"))
			{
				const FMetaSoundBuilderNodeOutputHandle Handle = Builder->FindNodeOutputByName(
					FMetaSoundNodeHandle(NodeId), FName(*Op.SrcOutput), Result);
				if (Result == EMetaSoundBuilderResult::Succeeded)
				{
					Builder->DisconnectNodeOutput(Handle, Result);
				}
			}
			else
			{
				const FMetaSoundBuilderNodeInputHandle Handle = Builder->FindNodeInputByName(
					FMetaSoundNodeHandle(NodeId), FName(*Op.DstInput), Result);
				if (Result == EMetaSoundBuilderResult::Succeeded)
				{
					if (Kind == TEXT("disconnect_input"))
					{
						Builder->DisconnectNodeInput(Handle, Result);
					}
					else
					{
						FMetasoundFrontendLiteral Literal;
						if (!BridgeAudioImpl::MakeLiteral(Op.LiteralType, Op.Value, Literal, Error))
						{
							Out.Error = Error;
							Out.FailedAtIndex = Index;
							return Out;
						}
						Builder->SetNodeInputDefault(Handle, Literal, Result);
					}
				}
			}
		}
		else if (Kind == TEXT("remove_node") || Kind == TEXT("set_location"))
		{
			FGuid NodeId;
			if (!BridgeAudioImpl::ResolveNodeRef(Op.DstRef, Produced, NodeId, Error))
			{
				Out.Error = Error;
				Out.FailedAtIndex = Index;
				return Out;
			}
			if (Kind == TEXT("remove_node"))
			{
				Builder->RemoveNode(FMetaSoundNodeHandle(NodeId), Result, true);
			}
			else
			{
				Builder->SetNodeLocation(FMetaSoundNodeHandle(NodeId), FVector2D(Op.X, Op.Y), Result);
			}
		}
		else
		{
			Out.Error = FString::Printf(TEXT("Unsupported MetaSound graph op: %s"), *Op.Op);
			Out.FailedAtIndex = Index;
			return Out;
		}

		if (Result != EMetaSoundBuilderResult::Succeeded)
		{
			Out.Error = FString::Printf(TEXT("MetaSound graph op %d (%s) failed"), Index, *Op.Op);
			Out.FailedAtIndex = Index;
			return Out;
		}
		Out.ProducedNodeIds[Index] = Produced[Index].IsValid()
			? Produced[Index].ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
		++Out.OpsApplied;
	}

	if (bRegisterWithFrontend)
	{
		EditorSubsystem->RegisterGraphWithFrontend(*Asset, true);
		Out.bRegisteredWithFrontend = true;
	}
	BridgeAudioImpl::ValidateObject(Asset, Out.ValidationResult, Out.ValidationMessages);
	Out.bSuccess = Out.ValidationResult != TEXT("Invalid");
	if (!Out.bSuccess && Out.Error.IsEmpty())
	{
		Out.Error = TEXT("MetaSound data validation failed after graph operations");
	}
	return Out;
}

TArray<FBridgeAudioComponentRuntimeInfo> UUnrealBridgeAudioLibrary::GetRuntimeAudioComponents(
	bool bPlayingOnly,
	int32 MaxComponents)
{
	TArray<FBridgeAudioComponentRuntimeInfo> Result;
	MaxComponents = FMath::Clamp(MaxComponents, 0, 65536);
	for (TObjectIterator<UAudioComponent> It; It && Result.Num() < MaxComponents; ++It)
	{
		UAudioComponent* Component = *It;
		UWorld* World = IsValid(Component) ? Component->GetWorld() : nullptr;
		if (!World || (World->WorldType != EWorldType::PIE
			&& World->WorldType != EWorldType::Game
			&& World->WorldType != EWorldType::GamePreview
			&& World->WorldType != EWorldType::Editor))
		{
			continue;
		}
		if (bPlayingOnly && !Component->IsPlaying())
		{
			continue;
		}
		FBridgeAudioComponentRuntimeInfo Info;
		Info.World = World->GetPathName();
		Info.ComponentPath = Component->GetPathName();
		Info.OwnerPath = IsValid(Component->GetOwner()) ? Component->GetOwner()->GetPathName() : FString();
		if (USoundBase* Sound = Component->GetSound())
		{
			Info.SoundPath = Sound->GetPathName();
			Info.SoundClass = Sound->GetClass()->GetPathName();
		}
		Info.PlayState = BridgeAudioImpl::PlayStateToString(Component->GetPlayState());
		Info.Location = Component->GetComponentLocation();
		Info.VolumeMultiplier = Component->VolumeMultiplier;
		Info.PitchMultiplier = Component->PitchMultiplier;
		Info.DefaultParameterCount = Component->DefaultParameters.Num();
		Info.InstanceParameterCount = Component->GetInstanceParameters().Num();
		for (const FAudioParameter& Parameter : Component->DefaultParameters)
		{
			Info.DefaultParameterNames.Add(BridgeAudioImpl::ParameterSummary(Parameter));
		}
		for (const FAudioParameter& Parameter : Component->GetInstanceParameters())
		{
			Info.InstanceParameterNames.Add(BridgeAudioImpl::ParameterSummary(Parameter));
		}
		Info.DefaultParameterNames.Sort();
		Info.InstanceParameterNames.Sort();
		Info.bPlaying = Component->IsPlaying();
		Info.bVirtualized = Component->IsVirtualized();
		Info.bActive = Component->IsActive();
		Info.bAutoActivate = Component->bAutoActivate;
		Info.bUISound = Component->bIsUISound;
		Result.Add(MoveTemp(Info));
	}
	return Result;
}
