#include "UnrealBridgeStateTreeLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Components/StateTreeComponent.h"
#include "Engine/World.h"
#include "EditorAssetLibrary.h"
#include "Factories/Factory.h"
#include "GameFramework/Actor.h"
#include "Modules/ModuleManager.h"
#include "Misc/EngineVersionComparison.h"
#include "PropertyBindingBinding.h"
#include "PropertyBindingDataView.h"
#include "PropertyBindingPath.h"
#include "PropertyBindingTypes.h"
#include "ScopedTransaction.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeConditionBase.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeExecutionTypes.h"
#include "StateTreeNodeBase.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"
#include "StructUtils/PropertyBag.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeStateTreeLibrary"

namespace BridgeStateTreeImpl
{
	bool ClassLooksLikeStateTree(const FString& ClassName)
	{
		return ClassName.Contains(TEXT("StateTree"));
	}

	void ExportTopLevelProperties(UObject* Object, FBridgeStateTreeAssetInfo& Info, int32 MaxPropertyValueLength)
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

	UObject* LoadAssetObject(const FString& AssetPath)
	{
		return StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
	}

	bool SetFactorySchema(UFactory* Factory, const FString& SchemaClassPath)
	{
		if (!Factory || SchemaClassPath.IsEmpty())
		{
			return false;
		}
		UClass* SchemaClass = StaticLoadClass(UObject::StaticClass(), nullptr, *SchemaClassPath);
		if (!SchemaClass)
		{
			return false;
		}
		FProperty* Property = Factory->GetClass()->FindPropertyByName(TEXT("StateTreeSchemaClass"));
		FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
		if (!ObjectProperty)
		{
			return false;
		}
		ObjectProperty->SetObjectPropertyValue_InContainer(Factory, SchemaClass);
		return true;
	}

	UStateTree* LoadStateTree(const FString& AssetPath)
	{
		return LoadObject<UStateTree>(nullptr, *AssetPath);
	}

	UStateTreeEditorData* GetEditorData(UStateTree* StateTree)
	{
		return StateTree ? Cast<UStateTreeEditorData>(StateTree->EditorData) : nullptr;
	}

	bool ParseGuid(const FString& Text, FGuid& OutGuid)
	{
		return !Text.IsEmpty() && FGuid::Parse(Text, OutGuid);
	}

	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		if (const UEnum* Enum = StaticEnum<TEnum>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Value));
		}
		return FString::FromInt(static_cast<int32>(Value));
	}

	template <typename TEnum>
	bool ParseEnum(const FString& Text, TEnum& OutValue)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		if (!Enum || Text.IsEmpty())
		{
			return false;
		}
		int64 Value = Enum->GetValueByNameString(Text, EGetByNameFlags::None);
		if (Value == INDEX_NONE)
		{
			Value = Enum->GetValueByName(FName(*Text), EGetByNameFlags::None);
		}
		if (Value == INDEX_NONE)
		{
			return false;
		}
		OutValue = static_cast<TEnum>(Value);
		return true;
	}

	FBridgeStateTreeValidationResult CompileStateTree(UStateTree* StateTree)
	{
		FBridgeStateTreeValidationResult Result;
		if (!StateTree || !GetEditorData(StateTree))
		{
			Result.Messages.Add(TEXT("Error: StateTree or editor data is unavailable."));
			Result.ErrorCount = 1;
			return Result;
		}

		UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
		FStateTreeCompilerLog CompilerLog;
		Result.bCompiled = true;
		Result.bSuccess = UStateTreeEditingSubsystem::CompileStateTree(StateTree, CompilerLog);
		for (const TSharedRef<FTokenizedMessage>& Message : CompilerLog.ToTokenizedMessages())
		{
			const EMessageSeverity::Type Severity = Message->GetSeverity();
			FString Prefix = TEXT("Info");
			if (Severity == EMessageSeverity::Error)
			{
				Prefix = TEXT("Error");
				++Result.ErrorCount;
			}
			else if (Severity == EMessageSeverity::Warning || Severity == EMessageSeverity::PerformanceWarning)
			{
				Prefix = TEXT("Warning");
				++Result.WarningCount;
			}
			Result.Messages.Add(FString::Printf(TEXT("%s: %s"), *Prefix, *Message->ToText().ToString()));
		}
		if (!Result.bSuccess && Result.ErrorCount == 0)
		{
			Result.ErrorCount = 1;
			Result.Messages.Add(TEXT("Error: StateTree compilation failed without a compiler message."));
		}
		return Result;
	}

	FBridgeStateTreeEditResult FinalizeEdit(UStateTree* StateTree, bool bCompile, bool bSave)
	{
		FBridgeStateTreeEditResult Result;
		if (!StateTree)
		{
			Result.Error = TEXT("StateTree is unavailable.");
			return Result;
		}

		StateTree->PostEditChange();
		StateTree->MarkPackageDirty();
		if (bCompile)
		{
			Result.Validation = CompileStateTree(StateTree);
			Result.bSuccess = Result.Validation.bSuccess;
			if (!Result.bSuccess)
			{
				Result.Error = TEXT("StateTree compilation failed; the edit remains undoable and was not saved.");
				return Result;
			}
		}
		else
		{
			Result.Validation.bSuccess = true;
			Result.bSuccess = true;
		}

		if (bSave && !UEditorAssetLibrary::SaveAsset(StateTree->GetPathName(), false))
		{
			Result.bSuccess = false;
			Result.Error = TEXT("The edit succeeded but the asset could not be saved.");
		}
		return Result;
	}

	FBridgeStateTreeTaskInfo MakeTaskInfo(const FStateTreeEditorNode& Task)
	{
		FBridgeStateTreeTaskInfo Info;
		Info.Id = Task.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
		Info.Name = Task.GetName().ToString();
		if (const UScriptStruct* NodeStruct = Task.Node.GetScriptStruct())
		{
			Info.StructPath = NodeStruct->GetPathName();
		}
		if (const UScriptStruct* InstanceStruct = Task.Instance.GetScriptStruct())
		{
			Info.InstanceType = InstanceStruct->GetPathName();
		}
		else if (Task.InstanceObject)
		{
			Info.InstanceType = Task.InstanceObject->GetClass()->GetPathName();
		}
		return Info;
	}

	FBridgeStateTreeNodeInfo MakeNodeInfo(
		const FStateTreeEditorNode& Node,
		const FString& Kind,
		const FString& StateId = FString(),
		const FString& TransitionId = FString())
	{
		FBridgeStateTreeNodeInfo Info;
		Info.Id = Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
		Info.Kind = Kind;
		Info.StateId = StateId;
		Info.TransitionId = TransitionId;
		Info.Name = Node.GetName().ToString();
		if (const UScriptStruct* NodeStruct = Node.Node.GetScriptStruct())
		{
			Info.StructPath = NodeStruct->GetPathName();
		}
		if (const UScriptStruct* InstanceStruct = Node.Instance.GetScriptStruct())
		{
			Info.InstanceType = InstanceStruct->GetPathName();
		}
		else if (Node.InstanceObject)
		{
			Info.InstanceType = Node.InstanceObject->GetClass()->GetPathName();
		}
		Info.ExpressionOperand = EnumName(Node.ExpressionOperand);
		Info.ExpressionIndent = Node.ExpressionIndent;
		return Info;
	}

	FString PropertyBagContainerTypesToString(const FPropertyBagContainerTypes& Types)
	{
		TArray<FString> Names;
		for (uint32 Index = 0; Index < Types.Num(); ++Index)
		{
			Names.Add(EnumName(Types[static_cast<int32>(Index)]));
		}
		return FString::Join(Names, TEXT("/"));
	}

	void AppendParameters(
		const FInstancedPropertyBag& Bag,
		const FGuid& StructId,
		const FString& StateId,
		const FString& StatePath,
		TArray<FBridgeStateTreeParameterInfo>& OutParameters)
	{
		const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
		if (!BagStruct)
		{
			return;
		}
		for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
		{
			FBridgeStateTreeParameterInfo Info;
			Info.StructId = StructId.ToString(EGuidFormats::DigitsWithHyphensLower);
			Info.StateId = StateId;
			Info.StatePath = StatePath;
			Info.Name = Desc.Name.ToString();
			Info.ValueType = EnumName(Desc.ValueType);
			Info.ContainerTypes = PropertyBagContainerTypesToString(Desc.ContainerTypes);
			Info.ValueTypeObject = Desc.ValueTypeObject ? Desc.ValueTypeObject->GetPathName() : FString();
			const TValueOrError<FString, EPropertyBagResult> Value = Bag.GetValueSerializedString(Desc.Name);
			if (Value.IsValid())
			{
				Info.Value = Value.GetValue();
			}
			OutParameters.Add(MoveTemp(Info));
		}
	}

	UScriptStruct* LoadNodeStruct(const FString& StructPath)
	{
		UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *StructPath);
		return Struct ? Struct : LoadObject<UScriptStruct>(nullptr, *StructPath);
	}

	bool InitializeEditorNode(
		FStateTreeEditorNode& Node,
		UObject* Outer,
		UScriptStruct* NodeStruct,
		const FString& InstanceDataExportText,
		FString& OutError)
	{
		if (!Outer || !NodeStruct)
		{
			OutError = TEXT("Node outer or struct is unavailable.");
			return false;
		}
#if UE_VERSION_NEWER_THAN(5, 7, 99)
		Node.InitializeAs(Outer, NodeStruct);
#else
		Node.ID = FGuid::NewGuid();
		Node.Node.InitializeAs(NodeStruct);
		const FStateTreeNodeBase& NodeBase = Node.Node.Get<FStateTreeNodeBase>();
		if (const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(NodeBase.GetInstanceDataType()))
		{
			Node.Instance.InitializeAs(InstanceStruct);
		}
		else if (const UClass* InstanceClass = Cast<const UClass>(NodeBase.GetInstanceDataType()))
		{
			Node.InstanceObject = NewObject<UObject>(Outer, InstanceClass, NAME_None, RF_Transactional);
		}
#endif
		if (InstanceDataExportText.IsEmpty())
		{
			return true;
		}
		if (Node.InstanceObject)
		{
			OutError = TEXT("Whole-object instance import is not supported; set individual properties after adding the node.");
			return false;
		}
		UScriptStruct* InstanceStruct = const_cast<UScriptStruct*>(Node.Instance.GetScriptStruct());
		if (!InstanceStruct || !Node.Instance.GetMutableMemory())
		{
			OutError = TEXT("The node has no struct instance data to import.");
			return false;
		}
		const TCHAR* Parsed = InstanceStruct->ImportText(
			*InstanceDataExportText,
			Node.Instance.GetMutableMemory(),
			Outer,
			PPF_None,
			GLog,
			InstanceStruct->GetName());
		if (!Parsed)
		{
			OutError = TEXT("InstanceDataExportText could not be imported.");
			return false;
		}
		return true;
	}

	FStateTreeEditorNode* FindAnyNode(
		UStateTreeEditorData* EditorData,
		const FGuid& NodeId,
		UObject*& OutOwner)
	{
		OutOwner = nullptr;
		if (!EditorData)
		{
			return nullptr;
		}
		for (FStateTreeEditorNode& Node : EditorData->Evaluators)
		{
			if (Node.ID == NodeId)
			{
				OutOwner = EditorData;
				return &Node;
			}
		}
		for (FStateTreeEditorNode& Node : EditorData->GlobalTasks)
		{
			if (Node.ID == NodeId)
			{
				OutOwner = EditorData;
				return &Node;
			}
		}

		FStateTreeEditorNode* Found = nullptr;
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
		{
			auto FindIn = [&](TArray<FStateTreeEditorNode>& Nodes) -> bool
			{
				for (FStateTreeEditorNode& Node : Nodes)
				{
					if (Node.ID == NodeId)
					{
						Found = &Node;
						OutOwner = &State;
						return true;
					}
				}
				return false;
			};
			if (FindIn(State.EnterConditions) || FindIn(State.Tasks) || FindIn(State.Considerations))
			{
				return EStateTreeVisitor::Break;
			}
			for (FStateTreeTransition& Transition : State.Transitions)
			{
				if (FindIn(Transition.Conditions))
				{
					return EStateTreeVisitor::Break;
				}
			}
			return EStateTreeVisitor::Continue;
		});
		return Found;
	}

	FBridgeStateTreeTransitionInfo MakeTransitionInfo(const FStateTreeTransition& Transition)
	{
		FBridgeStateTreeTransitionInfo Info;
		Info.Id = Transition.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
		Info.Trigger = EnumName(Transition.Trigger);
		Info.TransitionType = EnumName(Transition.State.LinkType);
		Info.TargetStateId = Transition.State.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
		Info.TargetStateName = Transition.State.Name.ToString();
		Info.RequiredEventTag = Transition.RequiredEvent.Tag.ToString();
		Info.bEnabled = Transition.bTransitionEnabled;
		const FString TransitionId = Info.Id;
		for (const FStateTreeEditorNode& Condition : Transition.Conditions)
		{
			Info.Conditions.Add(MakeNodeInfo(Condition, TEXT("TransitionCondition"), FString(), TransitionId));
		}
		return Info;
	}

	FBridgeStateTreeStateInfo MakeStateInfo(const UStateTreeState& State, const UStateTreeState* ParentState)
	{
		FBridgeStateTreeStateInfo Info;
		Info.Id = State.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
		Info.ParentId = ParentState ? ParentState->ID.ToString(EGuidFormats::DigitsWithHyphensLower) : FString();
		Info.Name = State.Name.ToString();
		Info.Path = State.GetPath();
		Info.StateType = EnumName(State.Type);
		Info.SelectionBehavior = EnumName(State.SelectionBehavior);
		Info.bEnabled = State.bEnabled;
		Info.ChildCount = State.Children.Num();
		for (const FStateTreeEditorNode& Task : State.Tasks)
		{
			Info.Tasks.Add(MakeTaskInfo(Task));
		}
		for (const FStateTreeEditorNode& Condition : State.EnterConditions)
		{
			Info.EnterConditions.Add(MakeNodeInfo(Condition, TEXT("EnterCondition"), Info.Id));
		}
		for (const FStateTreeTransition& Transition : State.Transitions)
		{
			Info.Transitions.Add(MakeTransitionInfo(Transition));
		}
		return Info;
	}

	FStateTreeEditorNode* FindTask(UStateTreeEditorData* EditorData, const FGuid& TaskId, UStateTreeState*& OutState)
	{
		FStateTreeEditorNode* Found = nullptr;
		OutState = nullptr;
		if (!EditorData)
		{
			return nullptr;
		}
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
		{
			for (FStateTreeEditorNode& Task : State.Tasks)
			{
				if (Task.ID == TaskId)
				{
					Found = &Task;
					OutState = &State;
					return EStateTreeVisitor::Break;
				}
			}
			return EStateTreeVisitor::Continue;
		});
		return Found;
	}

	FStateTreeTransition* FindTransition(UStateTreeEditorData* EditorData, const FGuid& TransitionId, UStateTreeState*& OutState)
	{
		FStateTreeTransition* Found = nullptr;
		OutState = nullptr;
		if (!EditorData)
		{
			return nullptr;
		}
		EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState*)
		{
			for (FStateTreeTransition& Transition : State.Transitions)
			{
				if (Transition.ID == TransitionId)
				{
					Found = &Transition;
					OutState = &State;
					return EStateTreeVisitor::Break;
				}
			}
			return EStateTreeVisitor::Continue;
		});
		return Found;
	}
}

TArray<FBridgeStateTreeAssetInfo> UUnrealBridgeStateTreeLibrary::ListStateTreeAssets(
	const FString& PackagePath,
	int32 MaxResults)
{
	TArray<FBridgeStateTreeAssetInfo> Result;
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
		if (!BridgeStateTreeImpl::ClassLooksLikeStateTree(ClassName))
		{
			continue;
		}
		FBridgeStateTreeAssetInfo Info;
		Info.Path = Asset.GetSoftObjectPath().ToString();
		Info.Name = Asset.AssetName.ToString();
		Info.ClassName = ClassName;
		Result.Add(Info);
	}
	return Result;
}

FString UUnrealBridgeStateTreeLibrary::CreateStateTree(
	const FString& Path,
	const FString& Name,
	const FString& SchemaClassPath,
	const FString& FactoryClassPath,
	const FString& AssetClassPath,
	bool bSave)
{
	if (Path.IsEmpty() || Name.IsEmpty() || SchemaClassPath.IsEmpty())
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
	if (!BridgeStateTreeImpl::SetFactorySchema(Factory, SchemaClassPath))
	{
		return FString();
	}

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

FBridgeStateTreeAssetInfo UUnrealBridgeStateTreeLibrary::GetStateTreeInfo(
	const FString& StateTreePath,
	int32 MaxPropertyValueLength)
{
	FBridgeStateTreeAssetInfo Info;
	UObject* Object = BridgeStateTreeImpl::LoadAssetObject(StateTreePath);
	if (!Object)
	{
		return Info;
	}
	Info.Path = Object->GetPathName();
	Info.Name = Object->GetName();
	Info.ClassName = Object->GetClass()->GetName();
	BridgeStateTreeImpl::ExportTopLevelProperties(Object, Info, MaxPropertyValueLength);
	return Info;
}

FString UUnrealBridgeStateTreeLibrary::GetStateTreeProperty(
	const FString& StateTreePath,
	const FString& PropertyName,
	bool& bOutSuccess)
{
	bOutSuccess = false;
	UObject* Object = BridgeStateTreeImpl::LoadAssetObject(StateTreePath);
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

bool UUnrealBridgeStateTreeLibrary::SetStateTreeProperty(
	const FString& StateTreePath,
	const FString& PropertyName,
	const FString& ValueExportText,
	bool bCompile,
	bool bSave)
{
	UStateTree* Object = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	if (!Object || PropertyName.IsEmpty())
	{
		return false;
	}
	FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetStateTreeProperty", "Bridge: Set StateTree Property"));
	Object->Modify();
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	const TCHAR* Start = *ValueExportText;
	const TCHAR* AfterParse = Property->ImportText_Direct(Start, ValuePtr, Object, PPF_None, GLog);
	if (!AfterParse || AfterParse == Start)
	{
		return false;
	}
	return BridgeStateTreeImpl::FinalizeEdit(Object, bCompile, bSave).bSuccess;
}

FBridgeStateTreeStructure UUnrealBridgeStateTreeLibrary::GetStateTreeStructure(const FString& StateTreePath)
{
	FBridgeStateTreeStructure Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	if (!StateTree || !EditorData)
	{
		return Result;
	}

	Result.bFound = true;
	Result.AssetPath = StateTree->GetPathName();
	if (EditorData->Schema)
	{
		Result.SchemaClassPath = EditorData->Schema->GetClass()->GetPathName();
	}
	for (const FStateTreeEditorNode& Evaluator : EditorData->Evaluators)
	{
		Result.Evaluators.Add(BridgeStateTreeImpl::MakeNodeInfo(Evaluator, TEXT("Evaluator")));
	}
	for (const FStateTreeEditorNode& GlobalTask : EditorData->GlobalTasks)
	{
		Result.GlobalTasks.Add(BridgeStateTreeImpl::MakeNodeInfo(GlobalTask, TEXT("GlobalTask")));
	}
	BridgeStateTreeImpl::AppendParameters(
		EditorData->GetRootParametersPropertyBag(),
		EditorData->GetRootParametersGuid(),
		FString(),
		TEXT("Root"),
		Result.Parameters);
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* ParentState)
	{
		Result.States.Add(BridgeStateTreeImpl::MakeStateInfo(State, ParentState));
		BridgeStateTreeImpl::AppendParameters(
			State.Parameters.Parameters,
			State.Parameters.ID,
			State.ID.ToString(EGuidFormats::DigitsWithHyphensLower),
			State.GetPath(),
			Result.Parameters);
		return EStateTreeVisitor::Continue;
	});
	if (const FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings())
	{
		Bindings->ForEachBinding([&](const FPropertyBindingBinding& Binding)
		{
			FBridgeStateTreeBindingInfo Info;
			Info.SourceStructId = Binding.GetSourcePath().GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower);
			Info.SourcePath = Binding.GetSourcePath().ToString();
			Info.TargetStructId = Binding.GetTargetPath().GetStructID().ToString(EGuidFormats::DigitsWithHyphensLower);
			Info.TargetPath = Binding.GetTargetPath().ToString();
			Info.Description = Binding.ToString();
			Result.Bindings.Add(MoveTemp(Info));
		});
	}
	return Result;
}

FBridgeStateTreeValidationResult UUnrealBridgeStateTreeLibrary::ValidateStateTreeAsset(const FString& StateTreePath)
{
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	if (!StateTree)
	{
		FBridgeStateTreeValidationResult Result;
		Result.ErrorCount = 1;
		Result.Messages.Add(TEXT("Error: StateTree asset could not be loaded."));
		return Result;
	}

	UStateTree* ValidationCopy = DuplicateObject<UStateTree>(StateTree, GetTransientPackage());
	if (!ValidationCopy)
	{
		FBridgeStateTreeValidationResult Result;
		Result.ErrorCount = 1;
		Result.Messages.Add(TEXT("Error: Failed to create a transient validation copy."));
		return Result;
	}
	ValidationCopy->SetFlags(RF_Transient);
	return BridgeStateTreeImpl::CompileStateTree(ValidationCopy);
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeState(
	const FString& StateTreePath,
	const FString& ParentStateId,
	const FString& Name,
	const FString& StateType,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	if (!StateTree || !EditorData || Name.IsEmpty())
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or Name is empty.");
		return Result;
	}

	EStateTreeStateType ParsedType = EStateTreeStateType::State;
	if (!BridgeStateTreeImpl::ParseEnum(StateType, ParsedType))
	{
		Result.Error = FString::Printf(TEXT("Unknown StateTree state type: %s"), *StateType);
		return Result;
	}

	UStateTreeState* ParentState = nullptr;
	if (!ParentStateId.IsEmpty())
	{
		FGuid ParentGuid;
		if (!BridgeStateTreeImpl::ParseGuid(ParentStateId, ParentGuid))
		{
			Result.Error = TEXT("ParentStateId is not a valid GUID.");
			return Result;
		}
		ParentState = EditorData->GetMutableStateByID(ParentGuid);
		if (!ParentState)
		{
			Result.Error = TEXT("Parent state was not found.");
			return Result;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeState", "Bridge: Add StateTree State"));
	StateTree->Modify();
	EditorData->Modify();
	if (ParentState)
	{
		ParentState->Modify();
	}
	UStateTreeState& NewState = ParentState
		? ParentState->AddChildState(FName(*Name), ParsedType)
		: EditorData->AddSubTree(FName(*Name));
	NewState.Type = ParsedType;
	NewState.Modify();

	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = NewState.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeTask(
	const FString& StateTreePath,
	const FString& StateId,
	const FString& TaskStructPath,
	const FString& InstanceDataExportText,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid StateGuid;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(StateId, StateGuid))
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or StateId is invalid.");
		return Result;
	}
	UStateTreeState* State = EditorData->GetMutableStateByID(StateGuid);
	if (!State)
	{
		Result.Error = TEXT("State was not found.");
		return Result;
	}

	UScriptStruct* TaskStruct = FindObject<UScriptStruct>(nullptr, *TaskStructPath);
	if (!TaskStruct)
	{
		TaskStruct = LoadObject<UScriptStruct>(nullptr, *TaskStructPath);
	}
	if (!TaskStruct || !TaskStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
	{
		Result.Error = FString::Printf(TEXT("TaskStructPath is not a native FStateTreeTaskBase struct: %s"), *TaskStructPath);
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeTask", "Bridge: Add StateTree Task"));
	StateTree->Modify();
	EditorData->Modify();
	State->Modify();
	FStateTreeEditorNode& Task = State->Tasks.AddDefaulted_GetRef();
	Task.ID = FGuid::NewGuid();
	Task.Node.InitializeAs(TaskStruct);
	const FStateTreeTaskBase& TaskBase = Task.Node.Get<FStateTreeTaskBase>();
	if (const UScriptStruct* InstanceStruct = Cast<const UScriptStruct>(TaskBase.GetInstanceDataType()))
	{
		Task.Instance.InitializeAs(InstanceStruct);
		if (!InstanceDataExportText.IsEmpty())
		{
			const TCHAR* Parsed = InstanceStruct->ImportText(
				*InstanceDataExportText,
				Task.Instance.GetMutableMemory(),
				State,
				PPF_None,
				GLog,
				InstanceStruct->GetName());
			if (!Parsed)
			{
				State->Tasks.Pop();
				Result.Error = TEXT("InstanceDataExportText could not be imported.");
				return Result;
			}
		}
	}
	else if (const UClass* InstanceClass = Cast<const UClass>(TaskBase.GetInstanceDataType()))
	{
		Task.InstanceObject = NewObject<UObject>(State, InstanceClass, NAME_None, RF_Transactional);
		if (!InstanceDataExportText.IsEmpty())
		{
			State->Tasks.Pop();
			Result.Error = TEXT("Whole-object instance import is not supported; add the task, then set individual properties.");
			return Result;
		}
	}

	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = Task.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeEvaluator(
	const FString& StateTreePath,
	const FString& EvaluatorStructPath,
	const FString& InstanceDataExportText,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	UScriptStruct* EvaluatorStruct = BridgeStateTreeImpl::LoadNodeStruct(EvaluatorStructPath);
	if (!StateTree || !EditorData || !EvaluatorStruct
		|| !EvaluatorStruct->IsChildOf(FStateTreeEvaluatorBase::StaticStruct()))
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or EvaluatorStructPath is not an FStateTreeEvaluatorBase struct.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeEvaluator", "Bridge: Add StateTree Evaluator"));
	StateTree->Modify();
	EditorData->Modify();
	FStateTreeEditorNode& Node = EditorData->Evaluators.AddDefaulted_GetRef();
	if (!BridgeStateTreeImpl::InitializeEditorNode(Node, EditorData, EvaluatorStruct, InstanceDataExportText, Result.Error))
	{
		EditorData->Evaluators.Pop();
		return Result;
	}
	EditorData->UpdateBindings();
	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeGlobalTask(
	const FString& StateTreePath,
	const FString& TaskStructPath,
	const FString& InstanceDataExportText,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	UScriptStruct* TaskStruct = BridgeStateTreeImpl::LoadNodeStruct(TaskStructPath);
	if (!StateTree || !EditorData || !TaskStruct
		|| !TaskStruct->IsChildOf(FStateTreeTaskBase::StaticStruct()))
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or TaskStructPath is not an FStateTreeTaskBase struct.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeGlobalTask", "Bridge: Add StateTree Global Task"));
	StateTree->Modify();
	EditorData->Modify();
	FStateTreeEditorNode& Node = EditorData->GlobalTasks.AddDefaulted_GetRef();
	if (!BridgeStateTreeImpl::InitializeEditorNode(Node, EditorData, TaskStruct, InstanceDataExportText, Result.Error))
	{
		EditorData->GlobalTasks.Pop();
		return Result;
	}
	EditorData->UpdateBindings();
	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeEnterCondition(
	const FString& StateTreePath,
	const FString& StateId,
	const FString& ConditionStructPath,
	const FString& InstanceDataExportText,
	const FString& ExpressionOperand,
	int32 ExpressionIndent,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	UScriptStruct* ConditionStruct = BridgeStateTreeImpl::LoadNodeStruct(ConditionStructPath);
	FGuid StateGuid;
	EStateTreeExpressionOperand Operand = EStateTreeExpressionOperand::And;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(StateId, StateGuid)
		|| !ConditionStruct || !ConditionStruct->IsChildOf(FStateTreeConditionBase::StaticStruct())
		|| !BridgeStateTreeImpl::ParseEnum(ExpressionOperand, Operand))
	{
		Result.Error = TEXT("StateTree/editor data, StateId, condition struct, or expression operand is invalid.");
		return Result;
	}
	UStateTreeState* State = EditorData->GetMutableStateByID(StateGuid);
	if (!State)
	{
		Result.Error = TEXT("State was not found.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeEnterCondition", "Bridge: Add StateTree Enter Condition"));
	StateTree->Modify();
	EditorData->Modify();
	State->Modify();
	FStateTreeEditorNode& Node = State->EnterConditions.AddDefaulted_GetRef();
	if (!BridgeStateTreeImpl::InitializeEditorNode(Node, State, ConditionStruct, InstanceDataExportText, Result.Error))
	{
		State->EnterConditions.Pop();
		return Result;
	}
	Node.ExpressionOperand = Operand;
	Node.ExpressionIndent = static_cast<uint8>(FMath::Clamp(ExpressionIndent, 0, 255));
	EditorData->UpdateBindings();
	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeTransitionCondition(
	const FString& StateTreePath,
	const FString& TransitionId,
	const FString& ConditionStructPath,
	const FString& InstanceDataExportText,
	const FString& ExpressionOperand,
	int32 ExpressionIndent,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	UScriptStruct* ConditionStruct = BridgeStateTreeImpl::LoadNodeStruct(ConditionStructPath);
	FGuid TransitionGuid;
	EStateTreeExpressionOperand Operand = EStateTreeExpressionOperand::And;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(TransitionId, TransitionGuid)
		|| !ConditionStruct || !ConditionStruct->IsChildOf(FStateTreeConditionBase::StaticStruct())
		|| !BridgeStateTreeImpl::ParseEnum(ExpressionOperand, Operand))
	{
		Result.Error = TEXT("StateTree/editor data, TransitionId, condition struct, or expression operand is invalid.");
		return Result;
	}
	UStateTreeState* OwnerState = nullptr;
	FStateTreeTransition* Transition = BridgeStateTreeImpl::FindTransition(EditorData, TransitionGuid, OwnerState);
	if (!Transition || !OwnerState)
	{
		Result.Error = TEXT("Transition was not found.");
		return Result;
	}

	const FScopedTransaction TransactionScope(LOCTEXT("BridgeAddStateTreeTransitionCondition", "Bridge: Add StateTree Transition Condition"));
	StateTree->Modify();
	EditorData->Modify();
	OwnerState->Modify();
	FStateTreeEditorNode& Node = Transition->Conditions.AddDefaulted_GetRef();
	if (!BridgeStateTreeImpl::InitializeEditorNode(Node, OwnerState, ConditionStruct, InstanceDataExportText, Result.Error))
	{
		Transition->Conditions.Pop();
		return Result;
	}
	Node.ExpressionOperand = Operand;
	Node.ExpressionIndent = static_cast<uint8>(FMath::Clamp(ExpressionIndent, 0, 255));
	EditorData->UpdateBindings();
	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = Node.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeParameter(
	const FString& StateTreePath,
	const FString& StateId,
	const FString& Name,
	const FString& ValueType,
	const FString& ValueTypeObjectPath,
	const FString& DefaultValueExportText,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	if (!StateTree || !EditorData || Name.IsEmpty())
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or Name is empty.");
		return Result;
	}

	EPropertyBagPropertyType ParsedType = EPropertyBagPropertyType::None;
	if (!BridgeStateTreeImpl::ParseEnum(ValueType, ParsedType)
		|| ParsedType == EPropertyBagPropertyType::None
		|| ParsedType == EPropertyBagPropertyType::Count)
	{
		Result.Error = FString::Printf(TEXT("Unknown or unsupported property-bag ValueType: %s"), *ValueType);
		return Result;
	}
	const bool bNeedsTypeObject = ParsedType == EPropertyBagPropertyType::Enum
		|| ParsedType == EPropertyBagPropertyType::Struct
		|| ParsedType == EPropertyBagPropertyType::Object
		|| ParsedType == EPropertyBagPropertyType::SoftObject
		|| ParsedType == EPropertyBagPropertyType::Class
		|| ParsedType == EPropertyBagPropertyType::SoftClass;
	const UObject* ValueTypeObject = ValueTypeObjectPath.IsEmpty()
		? nullptr : StaticLoadObject(UObject::StaticClass(), nullptr, *ValueTypeObjectPath);
	if (bNeedsTypeObject && !ValueTypeObject)
	{
		Result.Error = TEXT("ValueTypeObjectPath is required and must resolve for this ValueType.");
		return Result;
	}

	FGuid StructGuid = EditorData->GetRootParametersGuid();
	UStateTreeState* State = nullptr;
	if (!StateId.IsEmpty())
	{
		FGuid StateGuid;
		if (!BridgeStateTreeImpl::ParseGuid(StateId, StateGuid)
			|| !(State = EditorData->GetMutableStateByID(StateGuid)))
		{
			Result.Error = TEXT("StateId is invalid or the state was not found.");
			return Result;
		}
		StructGuid = State->Parameters.ID;
	}
	if (!StructGuid.IsValid() || !EditorData->CanCreateParameter(StructGuid))
	{
		Result.Error = TEXT("The selected StateTree struct does not allow parameter creation.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeParameter", "Bridge: Add StateTree Parameter"));
	StateTree->Modify();
	EditorData->Modify();
	if (State)
	{
		State->Modify();
	}
	UE::PropertyBinding::FPropertyCreationDescriptor Descriptor;
	Descriptor.PropertyDesc.Name = FName(*Name);
	Descriptor.PropertyDesc.ValueType = ParsedType;
	Descriptor.PropertyDesc.ValueTypeObject = ValueTypeObject;
	TArray<UE::PropertyBinding::FPropertyCreationDescriptor, TFixedAllocator<1>> Descriptors;
	Descriptors.Add(Descriptor);
	EditorData->CreateParametersForStruct(StructGuid, Descriptors);
	const FName ActualName = Descriptors[0].PropertyDesc.Name;

	if (!DefaultValueExportText.IsEmpty())
	{
		FPropertyBindingDataView View;
		if (!EditorData->GetBindingDataViewByID(StructGuid, View) || !View.IsValid())
		{
			Result.Error = TEXT("The created parameter data view could not be resolved.");
			return Result;
		}
		FProperty* Property = FindFProperty<FProperty>(View.GetStruct(), ActualName);
		void* ValueAddress = Property ? Property->ContainerPtrToValuePtr<void>(View.GetMutableMemory()) : nullptr;
		const TCHAR* Start = *DefaultValueExportText;
		const TCHAR* Parsed = Property && ValueAddress
			? Property->ImportText_Direct(Start, ValueAddress, State ? static_cast<UObject*>(State) : static_cast<UObject*>(EditorData), PPF_None, GLog)
			: nullptr;
		if (!Parsed || Parsed == Start)
		{
			Result.Error = TEXT("DefaultValueExportText could not be imported for the created parameter.");
			return Result;
		}
	}
	EditorData->UpdateBindings();
	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = FString::Printf(
		TEXT("%s:%s"),
		*StructGuid.ToString(EGuidFormats::DigitsWithHyphensLower),
		*ActualName.ToString());
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeBinding(
	const FString& StateTreePath,
	const FString& SourceStructId,
	const FString& SourcePath,
	const FString& TargetStructId,
	const FString& TargetPath,
	bool bReplaceExisting,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid SourceGuid;
	FGuid TargetGuid;
	if (!StateTree || !EditorData
		|| !BridgeStateTreeImpl::ParseGuid(SourceStructId, SourceGuid)
		|| !BridgeStateTreeImpl::ParseGuid(TargetStructId, TargetGuid)
		|| SourcePath.IsEmpty() || TargetPath.IsEmpty())
	{
		Result.Error = TEXT("StateTree/editor data, struct GUIDs, or property paths are invalid.");
		return Result;
	}

	FPropertyBindingDataView SourceView;
	FPropertyBindingDataView TargetView;
	if (!EditorData->GetBindingDataViewByID(SourceGuid, SourceView) || !SourceView.IsValid()
		|| !EditorData->GetBindingDataViewByID(TargetGuid, TargetView) || !TargetView.IsValid())
	{
		Result.Error = TEXT("SourceStructId or TargetStructId does not resolve to bindable data.");
		return Result;
	}
	FPropertyBindingPath SourceBindingPath(SourceGuid);
	FPropertyBindingPath TargetBindingPath(TargetGuid);
	FString PathError;
	if (!SourceBindingPath.FromString(SourcePath)
		|| !SourceBindingPath.UpdateSegmentsFromValue(SourceView, &PathError)
		|| !TargetBindingPath.FromString(TargetPath)
		|| !TargetBindingPath.UpdateSegmentsFromValue(TargetView, &PathError))
	{
		Result.Error = FString::Printf(TEXT("A property binding path could not be resolved: %s"), *PathError);
		return Result;
	}
	FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
	if (!Bindings)
	{
		Result.Error = TEXT("StateTree editor bindings are unavailable.");
		return Result;
	}
	if (!bReplaceExisting && Bindings->HasBinding(TargetBindingPath))
	{
		Result.Error = TEXT("TargetPath already has a binding; set bReplaceExisting=true to replace it.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeBinding", "Bridge: Add StateTree Binding"));
	StateTree->Modify();
	EditorData->Modify();
	Bindings->AddBinding(SourceBindingPath, TargetBindingPath);
	EditorData->OnPropertyBindingChanged(SourceBindingPath, TargetBindingPath);
	EditorData->UpdateBindings();
	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = FString::Printf(
		TEXT("%s:%s->%s:%s"),
		*SourceStructId, *SourcePath, *TargetStructId, *TargetPath);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::AddStateTreeTransition(
	const FString& StateTreePath,
	const FString& SourceStateId,
	const FString& Trigger,
	const FString& TransitionType,
	const FString& TargetStateId,
	const FString& RequiredEventTag,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid SourceGuid;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(SourceStateId, SourceGuid))
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or SourceStateId is invalid.");
		return Result;
	}
	UStateTreeState* SourceState = EditorData->GetMutableStateByID(SourceGuid);
	if (!SourceState)
	{
		Result.Error = TEXT("Source state was not found.");
		return Result;
	}

	EStateTreeTransitionTrigger ParsedTrigger = EStateTreeTransitionTrigger::OnStateCompleted;
	EStateTreeTransitionType ParsedType = EStateTreeTransitionType::None;
	if (!BridgeStateTreeImpl::ParseEnum(Trigger, ParsedTrigger)
		|| !BridgeStateTreeImpl::ParseEnum(TransitionType, ParsedType))
	{
		Result.Error = TEXT("Trigger or TransitionType is unknown.");
		return Result;
	}

	UStateTreeState* TargetState = nullptr;
	if (!TargetStateId.IsEmpty())
	{
		FGuid TargetGuid;
		if (!BridgeStateTreeImpl::ParseGuid(TargetStateId, TargetGuid))
		{
			Result.Error = TEXT("TargetStateId is invalid or the target state was not found.");
			return Result;
		}
		TargetState = EditorData->GetMutableStateByID(TargetGuid);
		if (!TargetState)
		{
			Result.Error = TEXT("TargetStateId is invalid or the target state was not found.");
			return Result;
		}
	}
	if (ParsedType == EStateTreeTransitionType::GotoState && !TargetState)
	{
		Result.Error = TEXT("GotoState transitions require TargetStateId.");
		return Result;
	}

	FGameplayTag EventTag;
	if (!RequiredEventTag.IsEmpty())
	{
		EventTag = FGameplayTag::RequestGameplayTag(FName(*RequiredEventTag), false);
		if (!EventTag.IsValid())
		{
			Result.Error = FString::Printf(TEXT("Required event GameplayTag is not registered: %s"), *RequiredEventTag);
			return Result;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeAddStateTreeTransition", "Bridge: Add StateTree Transition"));
	StateTree->Modify();
	EditorData->Modify();
	SourceState->Modify();
	FStateTreeTransition& NewTransition = EventTag.IsValid()
		? SourceState->AddTransition(ParsedTrigger, EventTag, ParsedType, TargetState)
		: SourceState->AddTransition(ParsedTrigger, ParsedType, TargetState);

	Result = BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
	Result.CreatedId = NewTransition.ID.ToString(EGuidFormats::DigitsWithHyphensLower);
	return Result;
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::SetStateTreeStateEnabled(
	const FString& StateTreePath,
	const FString& StateId,
	bool bEnabled,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid StateGuid;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(StateId, StateGuid))
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or StateId is invalid.");
		return Result;
	}
	UStateTreeState* State = EditorData->GetMutableStateByID(StateGuid);
	if (!State)
	{
		Result.Error = TEXT("State was not found.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetStateTreeStateEnabled", "Bridge: Set StateTree State Enabled"));
	StateTree->Modify();
	EditorData->Modify();
	State->Modify();
	State->bEnabled = bEnabled;
	return BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::SetStateTreeTaskInstanceProperty(
	const FString& StateTreePath,
	const FString& TaskId,
	const FString& PropertyName,
	const FString& ValueExportText,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid TaskGuid;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(TaskId, TaskGuid) || PropertyName.IsEmpty())
	{
		Result.Error = TEXT("StateTree/editor data is unavailable, TaskId is invalid, or PropertyName is empty.");
		return Result;
	}

	UStateTreeState* OwnerState = nullptr;
	FStateTreeEditorNode* Task = BridgeStateTreeImpl::FindTask(EditorData, TaskGuid, OwnerState);
	if (!Task || !OwnerState)
	{
		Result.Error = TEXT("Task was not found.");
		return Result;
	}

	UStruct* InstanceType = nullptr;
	void* Container = nullptr;
	UObject* ImportOwner = OwnerState;
	if (Task->InstanceObject)
	{
		InstanceType = Task->InstanceObject->GetClass();
		Container = Task->InstanceObject;
		ImportOwner = Task->InstanceObject;
	}
	else if (UScriptStruct* InstanceStruct = const_cast<UScriptStruct*>(Task->Instance.GetScriptStruct()))
	{
		InstanceType = InstanceStruct;
		Container = Task->Instance.GetMutableMemory();
	}
	FProperty* Property = InstanceType ? InstanceType->FindPropertyByName(FName(*PropertyName)) : nullptr;
	if (!Property || !Container)
	{
		Result.Error = TEXT("Task instance property was not found.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetStateTreeTaskProperty", "Bridge: Set StateTree Task Property"));
	StateTree->Modify();
	EditorData->Modify();
	OwnerState->Modify();
	if (Task->InstanceObject)
	{
		Task->InstanceObject->Modify();
	}
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
	const TCHAR* Start = *ValueExportText;
	const TCHAR* Parsed = Property->ImportText_Direct(Start, ValuePtr, ImportOwner, PPF_None, GLog);
	if (!Parsed || Parsed == Start)
	{
		Result.Error = TEXT("ValueExportText could not be imported for the task property.");
		return Result;
	}
	return BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::SetStateTreeNodeInstanceProperty(
	const FString& StateTreePath,
	const FString& NodeId,
	const FString& PropertyName,
	const FString& ValueExportText,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid NodeGuid;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(NodeId, NodeGuid) || PropertyName.IsEmpty())
	{
		Result.Error = TEXT("StateTree/editor data is unavailable, NodeId is invalid, or PropertyName is empty.");
		return Result;
	}

	UObject* Owner = nullptr;
	FStateTreeEditorNode* Node = BridgeStateTreeImpl::FindAnyNode(EditorData, NodeGuid, Owner);
	if (!Node || !Owner)
	{
		Result.Error = TEXT("Evaluator, task, or condition node was not found.");
		return Result;
	}
	FStateTreeDataView InstanceView = Node->GetInstance();
	UStruct* InstanceType = const_cast<UStruct*>(InstanceView.GetStruct());
	void* Container = InstanceView.GetMutableMemory();
	FProperty* Property = InstanceType ? InstanceType->FindPropertyByName(FName(*PropertyName)) : nullptr;
	if (!Property || !Container)
	{
		Result.Error = TEXT("Node instance property was not found.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetStateTreeNodeProperty", "Bridge: Set StateTree Node Property"));
	StateTree->Modify();
	EditorData->Modify();
	Owner->Modify();
	if (Node->InstanceObject)
	{
		Node->InstanceObject->Modify();
	}
	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Container);
	const TCHAR* Start = *ValueExportText;
	const TCHAR* Parsed = Property->ImportText_Direct(Start, ValuePtr, Owner, PPF_None, GLog);
	if (!Parsed || Parsed == Start)
	{
		Result.Error = TEXT("ValueExportText could not be imported for the node property.");
		return Result;
	}
	return BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
}

FBridgeStateTreeEditResult UUnrealBridgeStateTreeLibrary::SetStateTreeTransitionEnabled(
	const FString& StateTreePath,
	const FString& TransitionId,
	bool bEnabled,
	bool bCompile,
	bool bSave)
{
	FBridgeStateTreeEditResult Result;
	UStateTree* StateTree = BridgeStateTreeImpl::LoadStateTree(StateTreePath);
	UStateTreeEditorData* EditorData = BridgeStateTreeImpl::GetEditorData(StateTree);
	FGuid TransitionGuid;
	if (!StateTree || !EditorData || !BridgeStateTreeImpl::ParseGuid(TransitionId, TransitionGuid))
	{
		Result.Error = TEXT("StateTree/editor data is unavailable or TransitionId is invalid.");
		return Result;
	}

	UStateTreeState* OwnerState = nullptr;
	FStateTreeTransition* Transition = BridgeStateTreeImpl::FindTransition(EditorData, TransitionGuid, OwnerState);
	if (!Transition || !OwnerState)
	{
		Result.Error = TEXT("Transition was not found.");
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeSetStateTreeTransitionEnabled", "Bridge: Set StateTree Transition Enabled"));
	StateTree->Modify();
	EditorData->Modify();
	OwnerState->Modify();
	Transition->bTransitionEnabled = bEnabled;
	return BridgeStateTreeImpl::FinalizeEdit(StateTree, bCompile, bSave);
}

TArray<FBridgeStateTreeRuntimeInfo> UUnrealBridgeStateTreeLibrary::GetRuntimeStateTrees(int32 MaxComponents)
{
	TArray<FBridgeStateTreeRuntimeInfo> Result;
	MaxComponents = FMath::Clamp(MaxComponents, 1, 4096);
	for (TObjectIterator<UStateTreeComponent> It; It && Result.Num() < MaxComponents; ++It)
	{
		UStateTreeComponent* Component = *It;
		UWorld* World = IsValid(Component) ? Component->GetWorld() : nullptr;
		if (!IsValid(Component) || Component->IsTemplate() || !World
			|| (World->WorldType != EWorldType::PIE
				&& World->WorldType != EWorldType::Game
				&& World->WorldType != EWorldType::GamePreview))
		{
			continue;
		}

		FBridgeStateTreeRuntimeInfo Info;
		Info.World = World->GetPathName();
		Info.ComponentPath = Component->GetPathName();
		Info.OwnerPath = IsValid(Component->GetOwner()) ? Component->GetOwner()->GetPathName() : FString();
		Info.bRunning = Component->IsRunning();
		Info.bPaused = Component->IsPaused();
		if (const UEnum* StatusEnum = StaticEnum<EStateTreeRunStatus>())
		{
			Info.RunStatus = StatusEnum->GetNameStringByValue(static_cast<int64>(Component->GetStateTreeRunStatus()));
		}
		if (FProperty* ReferenceProperty = Component->GetClass()->FindPropertyByName(TEXT("StateTreeRef")))
		{
			const void* ValuePtr = ReferenceProperty->ContainerPtrToValuePtr<void>(Component);
			ReferenceProperty->ExportTextItem_Direct(
				Info.StateTreeReference, ValuePtr, nullptr, Component, PPF_None);
		}
#if WITH_GAMEPLAY_DEBUGGER
		for (const FName StateName : Component->GetActiveStateNames())
		{
			Info.ActiveStates.Add(StateName.ToString());
		}
		Info.DebugInfo = Component->GetDebugInfoString();
#endif
		Result.Add(MoveTemp(Info));
	}
	return Result;
}

#undef LOCTEXT_NAMESPACE
