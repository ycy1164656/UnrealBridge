#include "UnrealBridgeStateTreeLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "Factories/Factory.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeEditorNode.h"
#include "StateTreeNodeBase.h"
#include "StateTreeState.h"
#include "StateTreeTaskBase.h"
#include "StateTreeTypes.h"
#include "UObject/UnrealType.h"

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
	EditorData->VisitHierarchy([&](UStateTreeState& State, UStateTreeState* ParentState)
	{
		Result.States.Add(BridgeStateTreeImpl::MakeStateInfo(State, ParentState));
		return EStateTreeVisitor::Continue;
	});
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

#undef LOCTEXT_NAMESPACE
