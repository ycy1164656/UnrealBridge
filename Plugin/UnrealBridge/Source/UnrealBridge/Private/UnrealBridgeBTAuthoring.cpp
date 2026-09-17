#include "UnrealBridgeAILibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/ValueOrBBKey.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Composites/BTComposite_Selector.h"
#include "BehaviorTree/Composites/BTComposite_Sequence.h"
#include "BehaviorTree/Decorators/BTDecorator_Blackboard.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "EdGraphSchema_BehaviorTree.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace BridgeBTAuthoring
{
	using namespace BridgeAuthoring;
	using BridgeAuthoring::Error;
	using FNodes=TMap<FString,UBehaviorTreeGraphNode*>;
	FString Guid(const UEdGraphNode* Node) { return Node->NodeGuid.ToString(EGuidFormats::Digits); }
	FString Kind(const UBehaviorTreeGraphNode* Node)
	{
		if (Node->IsA<UBehaviorTreeGraphNode_Root>()) return TEXT("root");
		if (Cast<UBTCompositeNode>(Node->NodeInstance)) return TEXT("composite");
		if (Cast<UBTTaskNode>(Node->NodeInstance)) return TEXT("task");
		if (Cast<UBTDecorator>(Node->NodeInstance)) return TEXT("decorator");
		if (Cast<UBTService>(Node->NodeInstance)) return TEXT("service");
		return TEXT("unsupported");
	}
	bool Collect(UBehaviorTreeGraph* Graph,FNodes& Nodes,FString& Message)
	{
		if (!Graph || Graph->Nodes.Num()>128) { Message=TEXT("A bounded existing BehaviorTreeGraph is required"); return false; }
		TArray<UBehaviorTreeGraphNode*> Pending;
		for (UEdGraphNode* Raw:Graph->Nodes) if (auto* Node=Cast<UBehaviorTreeGraphNode>(Raw)) Pending.Add(Node);
		for (int32 Index=0;Index<Pending.Num();++Index)
		{
			auto* Node=Pending[Index];
			if (!Node || !Node->NodeGuid.IsValid() || Node->bInjectedNode || (Node->bIsReadOnly && !Node->IsA<UBehaviorTreeGraphNode_Root>()) || Nodes.Contains(Guid(Node)) || Pending.Num()>256)
			{ Message=TEXT("Invalid/duplicate GUID, injected/read-only node or subnode budget"); return false; }
			if (auto* Composite=Cast<UBTCompositeNode>(Node->NodeInstance); Composite && Composite->GetClass()!=UBTComposite_Selector::StaticClass() && Composite->GetClass()!=UBTComposite_Sequence::StaticClass())
			{ Message=TEXT("Unsupported composite semantics: first support is Selector/Sequence; SimpleParallel needs a dedicated adapter"); return false; }
			Nodes.Add(Guid(Node),Node);
			for (UAIGraphNode* Sub:Node->SubNodes) if (auto* BT=Cast<UBehaviorTreeGraphNode>(Sub)) Pending.Add(BT); else { Message=TEXT("Unsupported subnode"); return false; }
		}
		return true;
	}
	TArray<UBehaviorTreeGraphNode*> Children(UBehaviorTreeGraphNode* Node)
	{
		TArray<UBehaviorTreeGraphNode*> Result;
		if (auto* Pin=Node->GetOutputPin()) for (auto* Link:Pin->LinkedTo) if (auto* Child=Cast<UBehaviorTreeGraphNode>(Link->GetOwningNode())) Result.Add(Child);
		Result.Sort([](const auto& A,const auto& B){return A.NodePosX<B.NodePosX;}); return Result;
	}
	UClass* KeyClass(const FString& Type)
	{
		if (Type==TEXT("bool")) return UBlackboardKeyType_Bool::StaticClass();
		if (Type==TEXT("int")) return UBlackboardKeyType_Int::StaticClass();
		if (Type==TEXT("float")) return UBlackboardKeyType_Float::StaticClass();
		if (Type==TEXT("vector")) return UBlackboardKeyType_Vector::StaticClass();
		if (Type==TEXT("object")) return UBlackboardKeyType_Object::StaticClass();
		return nullptr;
	}
	bool Property(UObject* Object,UBlackboardData* Blackboard,const FString& Name,const TSharedPtr<FJsonObject>& Input,FString& Message)
	{
		if (!Fields(Input,{TEXT("type"),TEXT("value")})) { Message=TEXT("Exact typed property required"); return false; }
		auto* Prop=FindFProperty<FProperty>(Object->GetClass(),*Name);
		static const TSet<FString> Builtins={TEXT("WaitTime"),TEXT("RandomDeviation"),TEXT("Interval"),TEXT("FlowAbortMode"),TEXT("BlackboardKey"),TEXT("BasicOperation"),TEXT("NotifyObserver"),TEXT("NodeName")};
		if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Config|CPF_Transient|CPF_EditConst)
			|| (!Builtins.Contains(Name) && Prop->GetMetaData(TEXT("UnrealBridgeEditable"))!=TEXT("true")))
		{ Message=TEXT("Property is outside verified editable whitelist: ")+Name; return false; }
		const FString Type=String(Input,TEXT("type")); void* Address=Prop->ContainerPtrToValuePtr<void>(Object); double NumberValue=0;
		if (auto* Struct=CastField<FStructProperty>(Prop); Struct && Struct->Struct==FValueOrBBKey_Float::StaticStruct())
		{
			if (Type!=TEXT("seconds") || !Number(Input,TEXT("value"),NumberValue) || NumberValue<0 || NumberValue>60 || (Name==TEXT("WaitTime") && NumberValue<0.01))
			{ Message=TEXT("Verified constant seconds required for ValueOrBBKey_Float"); return false; }
			*static_cast<FValueOrBBKey_Float*>(Address)=FValueOrBBKey_Float(NumberValue); return true;
		}
		if (auto* Struct=CastField<FStructProperty>(Prop); Struct && Struct->Struct==FBlackboardKeySelector::StaticStruct())
		{
			const auto* Raw=Input->Values.Find(TEXT("value")); const TSharedPtr<FJsonObject>* Value=nullptr;
			if (Type!=TEXT("blackboard_key") || !Raw || !(*Raw)->TryGetObject(Value) || !Fields(*Value,{TEXT("key_name"),TEXT("key_type")})) { Message=TEXT("Typed Blackboard key selector required"); return false; }
			const FName Key(*String(*Value,TEXT("key_name"))); const auto Id=Blackboard->GetKeyID(Key); auto* Expected=KeyClass(String(*Value,TEXT("key_type")));
			UClass* KeyType=Blackboard->GetKeyType(Id).Get();
			if (Id==FBlackboard::InvalidKey || !Expected || KeyType!=Expected) { Message=TEXT("Missing Blackboard key or mismatched key type"); return false; }
			auto* Selector=static_cast<FBlackboardKeySelector*>(Address);
			const auto* Entry=Blackboard->GetKey(Id);
			if (Selector->AllowedTypes.Num() && !Selector->AllowedTypes.ContainsByPredicate([&](UBlackboardKeyType* Filter){return Entry && Entry->KeyType && Entry->KeyType->IsAllowedByFilter(Filter);}))
			{ Message=TEXT("Key selector filter refuses the selected type"); return false; }
			Selector->SelectedKeyName=Key; Selector->ResolveSelectedKey(*Blackboard); return true;
		}
		if (auto* Float=CastField<FFloatProperty>(Prop); Float && Type==TEXT("seconds") && Number(Input,TEXT("value"),NumberValue) && NumberValue>=0 && NumberValue<=60)
		{
			if ((Name==TEXT("Interval") || Name==TEXT("WaitTime")) && NumberValue<0.01) { Message=TEXT("Task/service intervals must be at least 0.01 seconds"); return false; }
			Float->SetPropertyValue(Address,NumberValue); return true;
		}
		if (auto* Byte=CastField<FByteProperty>(Prop); Byte && Type==TEXT("enum") && Byte->Enum)
		{
			const FString Value=String(Input,TEXT("value")); const int64 EnumValue=Byte->Enum->GetValueByNameString(Value);
			const int64 Max=Name==TEXT("FlowAbortMode")?3:1;
			if (EnumValue<0 || EnumValue>Max) { Message=TEXT("Unsupported enum value"); return false; }
			Byte->SetPropertyValue(Address,EnumValue); return true;
		}
		if (auto* NameProp=CastField<FNameProperty>(Prop); NameProp && Type==TEXT("name") && !String(Input,TEXT("value")).IsEmpty() && String(Input,TEXT("value")).Len()<=64)
		{ NameProp->SetPropertyValue(Address,FName(*String(Input,TEXT("value")))); return true; }
		if (auto* Text=CastField<FStrProperty>(Prop); Text && Type==TEXT("text") && String(Input,TEXT("value")).Len()<=128)
		{ Text->SetPropertyValue(Address,String(Input,TEXT("value"))); return true; }
		if (auto* Bool=CastField<FBoolProperty>(Prop); Bool && Type==TEXT("bool") && Input->HasTypedField<EJson::Boolean>(TEXT("value")))
		{ Bool->SetPropertyValue(Address,Input->GetBoolField(TEXT("value"))); return true; }
		Message=TEXT("Unsupported property type or value: ")+Name; return false;
	}
	bool SetProperties(UBehaviorTreeGraphNode* Node,UBlackboardData* Blackboard,const TSharedPtr<FJsonObject>& Props,FString& Message)
	{
		if (!Node->NodeInstance || !Props || Props->Values.Num()>16) { Message=TEXT("Bounded properties require a runtime node instance"); return false; }
		for (const auto& Pair:Props->Values)
		{
			const TSharedPtr<FJsonObject>* Input=nullptr;
			if (!Pair.Value->TryGetObject(Input) || !Property(Node->NodeInstance,Blackboard,FString(Pair.Key),*Input,Message)) return false;
			if (auto* Prop=FindFProperty<FProperty>(Node->NodeInstance->GetClass(),*FString(Pair.Key)))
			{ FPropertyChangedEvent Event(Prop); Node->NodeInstance->PostEditChangeProperty(Event); }
		}
		if (Cast<UBTService>(Node->NodeInstance))
		{
			auto* Interval=FindFProperty<FFloatProperty>(Node->NodeInstance->GetClass(),TEXT("Interval"));
			auto* Deviation=FindFProperty<FFloatProperty>(Node->NodeInstance->GetClass(),TEXT("RandomDeviation"));
			if (Interval && Deviation && Deviation->GetPropertyValue_InContainer(Node->NodeInstance)>=Interval->GetPropertyValue_InContainer(Node->NodeInstance))
			{ Message=TEXT("Service random deviation must be smaller than interval"); return false; }
		}
		return true;
	}
	TSharedRef<FJsonObject> Model(UBehaviorTree* Tree)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true); Value->SetStringField(TEXT("asset_path"),Tree->GetPathName());
		Value->SetStringField(TEXT("blackboard_path"),GetPathNameSafe(Tree->BlackboardAsset));
		TArray<TSharedPtr<FJsonValue>> Items; FNodes Nodes; FString Message;
		if (!Collect(Cast<UBehaviorTreeGraph>(Tree->BTGraph),Nodes,Message)) { Value->SetBoolField(TEXT("ok"),false); Value->SetStringField(TEXT("error"),Message); return Value; }
		TArray<FString> Keys; Nodes.GetKeys(Keys); Keys.Sort();
		for (const FString& Key:Keys)
		{
			auto* Node=Nodes[Key]; auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("node_guid"),Key); Item->SetStringField(TEXT("kind"),Kind(Node));
			Item->SetStringField(TEXT("runtime_class"),Node->NodeInstance?Node->NodeInstance->GetClass()->GetPathName():TEXT(""));
			Item->SetNumberField(TEXT("x"),Node->NodePosX); Item->SetNumberField(TEXT("y"),Node->NodePosY);
			Item->SetNumberField(TEXT("execution_index"),Cast<UBTNode>(Node->NodeInstance)?CastChecked<UBTNode>(Node->NodeInstance)->GetExecutionIndex():-1);
			Item->SetStringField(TEXT("parent_guid"),Node->ParentNode?Guid(Node->ParentNode):TEXT(""));
			if (auto* Input=Node->GetInputPin(); Input && Input->LinkedTo.Num()) Item->SetStringField(TEXT("parent_guid"),Guid(Input->LinkedTo[0]->GetOwningNode()));
			TArray<TSharedPtr<FJsonValue>> ChildrenJson,SubNodes;
			for (auto* Child:Children(Node)) ChildrenJson.Add(MakeShared<FJsonValueString>(Guid(Child)));
			for (UAIGraphNode* Sub:Node->SubNodes) SubNodes.Add(MakeShared<FJsonValueString>(Guid(Sub)));
			Item->SetArrayField(TEXT("children"),ChildrenJson); Item->SetArrayField(TEXT("subnodes"),SubNodes);
			auto Props=MakeShared<FJsonObject>();
			if (Node->NodeInstance) for (TFieldIterator<FProperty> It(Node->NodeInstance->GetClass());It;++It)
			{
				auto* Prop=*It; if (!Prop->HasAnyPropertyFlags(CPF_Edit) || Prop->HasAnyPropertyFlags(CPF_Config|CPF_Transient)) continue;
				FString Text; Prop->ExportText_InContainer(0,Text,Node->NodeInstance,Node->NodeInstance,Node->NodeInstance,PPF_None);
				if (Text.Len()<=512) Props->SetStringField(Prop->GetName(),Text);
			}
			Item->SetObjectField(TEXT("properties"),Props); Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Value->SetArrayField(TEXT("nodes"),Items); Value->SetStringField(TEXT("structure_revision"),Hash(Encode(Value))); return Value;
	}
	bool Validate(UBehaviorTree* Tree,FString& Message,bool Runtime)
	{
		auto* Graph=Cast<UBehaviorTreeGraph>(Tree->BTGraph); FNodes Nodes;
		if (!Tree->BlackboardAsset || !Collect(Graph,Nodes,Message)) return false;
		UBehaviorTreeGraphNode* Root=nullptr;
		for (const auto& Pair:Nodes)
		{
			auto* Node=Pair.Value; const FString NodeKind=Kind(Node);
			if (NodeKind==TEXT("root")) { if (Root) { Message=TEXT("Multiple roots"); return false; } Root=Node; }
			else if (NodeKind==TEXT("unsupported") || !Node->NodeInstance || Node->NodeInstance->GetOuter()!=Tree)
			{ Message=TEXT("Unsupported node type or foreign runtime Outer"); return false; }
			if (NodeKind==TEXT("task") && Children(Node).Num()) { Message=TEXT("Tasks cannot parent tree children"); return false; }
			if (auto* Input=Node->GetInputPin(); Input && Input->LinkedTo.Num()!=1 && NodeKind!=TEXT("root"))
			{ Message=TEXT("Every tree node must have exactly one parent"); return false; }
		}
		if (!Root || Children(Root).Num()!=1 || Kind(Children(Root)[0])!=TEXT("composite")) { Message=TEXT("One root composite required"); return false; }
		TSet<FString> Seen; TArray<UBehaviorTreeGraphNode*> Pending{Root};
		while (Pending.Num())
		{
			auto* Node=Pending.Pop(); if (Seen.Contains(Guid(Node))) { Message=TEXT("Cycle or shared child rejected"); return false; } Seen.Add(Guid(Node));
			for (auto* Child:Children(Node)) Pending.Add(Child);
			for (UAIGraphNode* Sub:Node->SubNodes) Pending.Add(CastChecked<UBehaviorTreeGraphNode>(Sub));
		}
		if (Seen.Num()!=Nodes.Num()) { Message=TEXT("Disconnected/unreachable nodes are not silently discarded"); return false; }
		if (Runtime)
		{
			if (Tree->RootNode!=Children(Root)[0]->NodeInstance) { Message=TEXT("Graph root and runtime root differ"); return false; }
			for (const auto& Pair:Nodes) if (auto* Composite=Cast<UBTCompositeNode>(Pair.Value->NodeInstance))
			{
				const auto GraphChildren=Children(Pair.Value);
				if (GraphChildren.Num()!=Composite->Children.Num()) { Message=TEXT("Runtime child count differs"); return false; }
				for (int32 I=0;I<GraphChildren.Num();++I)
					if (Composite->Children[I].ChildComposite!=GraphChildren[I]->NodeInstance && Composite->Children[I].ChildTask!=GraphChildren[I]->NodeInstance)
					{ Message=TEXT("Runtime child order differs from graph"); return false; }
			}
		}
		return true;
	}
	bool Ops(UBehaviorTree* Tree,const TArray<TSharedPtr<FJsonValue>>& Operations,bool Creating,FString& Message)
	{
		auto* Graph=Cast<UBehaviorTreeGraph>(Tree->BTGraph);
		if (Creating)
		{
			Graph=NewObject<UBehaviorTreeGraph>(Tree,NAME_None,RF_Transactional); Tree->BTGraph=Graph;
			Graph->GetSchema()->CreateDefaultNodesForGraph(*Graph);
		}
		if (!Graph) { Message=TEXT("Runtime-only tree needs an explicit migration, not implicit replacement"); return false; }
		Graph->LockUpdates();
		ON_SCOPE_EXIT { Graph->UnlockUpdates(); };
		FNodes Nodes; if (!Collect(Graph,Nodes,Message)) return false;
		for (int32 OpIndex=0;OpIndex<Operations.Num();++OpIndex)
		{
			const TSharedPtr<FJsonObject>* Ptr=nullptr; if (!Operations[OpIndex]->TryGetObject(Ptr)) return false;
			const auto& Op=*Ptr; const FString Action=String(Op,TEXT("op"));
			if (Action==TEXT("create_tree"))
			{
				FGuid RootGuid;
				if (!Creating || OpIndex!=0 || !Fields(Op,{TEXT("op"),TEXT("blackboard_path"),TEXT("root_guid")}) || !FGuid::ParseExact(String(Op,TEXT("root_guid")),EGuidFormats::Digits,RootGuid) || !RootGuid.IsValid()) { Message=TEXT("Create only on an absent target with valid root GUID"); return false; }
				Tree->BlackboardAsset=LoadObject<UBlackboardData>(nullptr,*String(Op,TEXT("blackboard_path")));
				if (!Tree->BlackboardAsset) { Message=TEXT("Existing Blackboard required"); return false; }
				auto* Root=Cast<UBehaviorTreeGraphNode_Root>(Graph->Nodes[0]); if (!Root) return false;
				Nodes.Remove(Guid(Root)); Root->NodeGuid=RootGuid; Nodes.Add(Guid(Root),Root); continue;
			}
			if (!Tree->BlackboardAsset) { Message=TEXT("Tree Blackboard is missing"); return false; }
			if (Action==TEXT("add_node") || Action==TEXT("attach_decorator") || Action==TEXT("attach_service"))
			{
				const bool Main=Action==TEXT("add_node"); const FString Type=Main?String(Op,TEXT("kind")):Action==TEXT("attach_decorator")?TEXT("decorator"):TEXT("service");
				if (Main?!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("kind"),TEXT("class_path"),TEXT("x"),TEXT("y"),TEXT("properties")}):!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("parent_guid"),TEXT("class_path"),TEXT("properties")})) { Message=TEXT("Exact add/attach node fields required"); return false; }
				FGuid NewGuid; if (!FGuid::ParseExact(String(Op,TEXT("node_guid")),EGuidFormats::Digits,NewGuid) || !NewGuid.IsValid() || Nodes.Contains(NewGuid.ToString(EGuidFormats::Digits)) || Nodes.Num()>=256) { Message=TEXT("Unique node GUID and budget required"); return false; }
				UClass* Class=LoadClass<UBTNode>(nullptr,*String(Op,TEXT("class_path"))); UClass* GraphClass=nullptr;
				if (!Class || Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists)) { Message=TEXT("Concrete current BT node subclass required"); return false; }
				if (Type==TEXT("composite") && (Class==UBTComposite_Selector::StaticClass() || Class==UBTComposite_Sequence::StaticClass())) GraphClass=UBehaviorTreeGraphNode_Composite::StaticClass();
				if (Type==TEXT("task") && Class->IsChildOf(UBTTaskNode::StaticClass())) GraphClass=UBehaviorTreeGraphNode_Task::StaticClass();
				if (Type==TEXT("decorator") && Class->IsChildOf(UBTDecorator::StaticClass())) GraphClass=UBehaviorTreeGraphNode_Decorator::StaticClass();
				if (Type==TEXT("service") && Class->IsChildOf(UBTService::StaticClass())) GraphClass=UBehaviorTreeGraphNode_Service::StaticClass();
				if (!GraphClass) { Message=TEXT("Unsupported class/kind; first composite set is Selector/Sequence"); return false; }
				auto* Node=NewObject<UBehaviorTreeGraphNode>(Graph,GraphClass,NAME_None,RF_Transactional);
				Node->ClassData=FGraphNodeClassData(Class,FString()); Node->NodeGuid=NewGuid;
				Node->NodeInstance=NewObject<UBTNode>(Tree,Class,NAME_None,RF_Transactional); Node->InitializeInstance();
				const TSharedPtr<FJsonObject>* Props=nullptr;
				if (!Op->TryGetObjectField(TEXT("properties"),Props) || !SetProperties(Node,Tree->BlackboardAsset,*Props,Message)) return false;
				if (Main)
				{
					double X=0,Y=0; if (!Number(Op,TEXT("x"),X) || !Number(Op,TEXT("y"),Y) || FMath::Abs(X)>100000 || FMath::Abs(Y)>100000 || X!=FMath::FloorToDouble(X) || Y!=FMath::FloorToDouble(Y)) { Message=TEXT("Bounded integer graph location required"); return false; }
					Node->NodePosX=X; Node->NodePosY=Y; Graph->AddNode(Node,false,false); Node->AllocateDefaultPins();
				}
				else
				{
					auto* Parent=Nodes.FindRef(String(Op,TEXT("parent_guid")));
					if (!Parent || (Kind(Parent)!=TEXT("composite") && Kind(Parent)!=TEXT("task"))) { Message=TEXT("Decorator/service needs a task or composite owner"); return false; }
					Parent->AddSubNode(Node,Graph); Node->NodeGuid=NewGuid;
				}
				Nodes.Add(Guid(Node),Node); continue;
			}
			auto* Node=Nodes.FindRef(String(Op,TEXT("node_guid")));
			if (Action==TEXT("set_property"))
			{
				const TSharedPtr<FJsonObject>* Props=nullptr;
				if (!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("properties")}) || !Node || !Node->NodeInstance || !Op->TryGetObjectField(TEXT("properties"),Props)) { Message=TEXT("Exact property operation and runtime instance required"); return false; }
				Node->NodeInstance->Modify(); if (!SetProperties(Node,Tree->BlackboardAsset,*Props,Message)) return false; continue;
			}
			if (Action==TEXT("set_location"))
			{
				double X=0,Y=0; if (!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("x"),TEXT("y")}) || !Node || !Number(Op,TEXT("x"),X) || !Number(Op,TEXT("y"),Y) || FMath::Abs(X)>100000 || FMath::Abs(Y)>100000 || X!=FMath::FloorToDouble(X) || Y!=FMath::FloorToDouble(Y)) { Message=TEXT("Bounded location required"); return false; }
				Node->Modify(); Node->NodePosX=X; Node->NodePosY=Y; continue;
			}
			if (Action==TEXT("connect") || Action==TEXT("reparent"))
			{
				auto* Parent=Nodes.FindRef(String(Op,TEXT("parent_guid"))); auto* Child=Nodes.FindRef(String(Op,TEXT("child_guid")));
				if (!Fields(Op,{TEXT("op"),TEXT("parent_guid"),TEXT("child_guid")}) || !Parent || !Child || !Parent->GetOutputPin() || !Child->GetInputPin()) { Message=TEXT("Valid parent/child pins required"); return false; }
				if (Action==TEXT("connect") && Child->GetInputPin()->LinkedTo.Num()) { Message=TEXT("Existing parent requires explicit reparent"); return false; }
				if (!Graph->GetSchema()->TryCreateConnection(Parent->GetOutputPin(),Child->GetInputPin())) { Message=TEXT("Native BT schema refused connection"); return false; }
				continue;
			}
			if (Action==TEXT("reorder_children"))
			{
				auto* Parent=Nodes.FindRef(String(Op,TEXT("parent_guid"))); const TArray<TSharedPtr<FJsonValue>>* Ordered=nullptr;
				if (!Fields(Op,{TEXT("op"),TEXT("parent_guid"),TEXT("children")}) || !Parent || !Op->TryGetArrayField(TEXT("children"),Ordered)) { Message=TEXT("Exact ordered children required"); return false; }
				const auto Old=Children(Parent); TSet<FString> Seen;
				if (Ordered->Num()!=Old.Num()) { Message=TEXT("Reorder cannot add/remove children"); return false; }
				for (int32 I=0;I<Ordered->Num();++I)
				{
					const FString Id=(*Ordered)[I]->AsString(); auto* Child=Nodes.FindRef(Id);
					if (!Child || !Old.Contains(Child) || Seen.Contains(Id)) { Message=TEXT("Each original child is required exactly once"); return false; }
					Seen.Add(Id); Child->Modify(); Child->NodePosX=Parent->NodePosX+(I-Old.Num()/2)*240;
				}
				continue;
			}
			Message=TEXT("Unsupported BT operation; node deletion/replacement is forbidden"); return false;
		}
		if (!Validate(Tree,Message,false)) return false;
		Graph->UnlockUpdates(); Graph->UpdateAsset(); Graph->RebuildExecutionOrder(); Graph->LockUpdates();
		return Validate(Tree,Message,true);
	}
	FString Execute(const FString& Text,bool PreviewOnly)
	{
		FRequest Request; FString Reply; if (!Parse(Text,TEXT("ai.behavior_tree"),Request,Reply)) return Reply;
		if (Request.DryRun!=PreviewOnly) return Error(TEXT("ScopeViolation"),TEXT("Preview/apply mismatch"));
		const bool Exists=Request.Snapshot->GetObjectField(TEXT("targets"))->GetObjectField(Request.Target)->GetBoolField(TEXT("exists"));
		auto* Existing=Exists?LoadObject<UBehaviorTree>(nullptr,*Request.Target):nullptr;
		if (Exists && !Existing) return Error(TEXT("ValidationFailed"),TEXT("Target is not a BehaviorTree"));
		TStrongObjectPtr<UBehaviorTree> Staged(Existing?DuplicateObject<UBehaviorTree>(Existing,GetTransientPackage()):NewObject<UBehaviorTree>(GetTransientPackage(),NAME_None,RF_Transient));
		FString Message; if (!Ops(Staged.Get(),Request.Operations,!Exists,Message)) return Error(TEXT("ValidationFailed"),Message);
		auto Planned=Model(Staged.Get()); Planned->SetStringField(TEXT("asset_path"),Request.Target); Planned->SetStringField(TEXT("layout_order_semantics"),TEXT("BT sibling X coordinates determine execution order; preview exposes the resulting child order"));
		if (Existing) Planned->SetObjectField(TEXT("previous_model"),Model(Existing));
		if (PreviewOnly) return Preview(Request,Planned);
		if (!Begin(Request,Reply)) return Reply;
		auto* Tree=Existing;
		if (!Tree) { Tree=NewObject<UBehaviorTree>(CreatePackage(*Request.Target),*FPackageName::GetLongPackageAssetName(Request.Target),RF_Public|RF_Standalone|RF_Transactional); FAssetRegistryModule::AssetCreated(Tree); }
		Tree->Modify(); if (Tree->BTGraph) Tree->BTGraph->Modify();
		if (!Ops(Tree,Request.Operations,!Exists,Message)) return Error(TEXT("NeedsReconciliation"),Message,TEXT("retained_unsaved"));
		Tree->MarkPackageDirty(); return Finish(Request,Model(Tree));
	}
}

FString UUnrealBridgeAILibrary::GetBehaviorTreeEditModel(const FString& BehaviorTreePath)
{
	if (auto* Tree=LoadObject<UBehaviorTree>(nullptr,*BehaviorTreePath)) return BridgeAuthoring::Encode(BridgeBTAuthoring::Model(Tree));
	return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("BehaviorTree unavailable"));
}
FString UUnrealBridgeAILibrary::PreviewBehaviorTreeOps(const FString& RequestJson) { return BridgeBTAuthoring::Execute(RequestJson,true); }
FString UUnrealBridgeAILibrary::ApplyBehaviorTreeOps(const FString& RequestJson) { return BridgeBTAuthoring::Execute(RequestJson,false); }
FString UUnrealBridgeAILibrary::ValidateBehaviorTreeAsset(const FString& BehaviorTreePath)
{
	auto* Tree=LoadObject<UBehaviorTree>(nullptr,*BehaviorTreePath); FString Message;
	if (!Tree || !BridgeBTAuthoring::Validate(Tree,Message,true)) return BridgeAuthoring::Error(TEXT("ValidationFailed"),Tree?Message:TEXT("BehaviorTree unavailable"));
	return BridgeAuthoring::Encode(BridgeBTAuthoring::Model(Tree));
}
FString UUnrealBridgeAILibrary::ApplyBlackboardKeyOps(const FString& RequestJson)
{
	using namespace BridgeAuthoring; FRequest Request; FString Reply;
	if (!Parse(RequestJson,TEXT("ai.blackboard_keys"),Request,Reply)) return Reply;
	const bool Exists=Request.Snapshot->GetObjectField(TEXT("targets"))->GetObjectField(Request.Target)->GetBoolField(TEXT("exists"));
	auto* Blackboard=Exists?LoadObject<UBlackboardData>(nullptr,*Request.Target):nullptr;
	if (Exists && !Blackboard) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Target must be BlackboardData"));
	TArray<FBlackboardEntry> Added; TSet<FName> Names;
	for (const auto& Raw:Request.Operations)
	{
		const TSharedPtr<FJsonObject>* Op=nullptr;
		if (!Raw->TryGetObject(Op) || !Fields(*Op,{TEXT("op"),TEXT("key_name"),TEXT("key_type")}) || String(*Op,TEXT("op"))!=TEXT("add_key")) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Only typed add_key supported"));
		FBlackboardEntry Entry; Entry.EntryName=FName(*String(*Op,TEXT("key_name"))); UClass* Class=BridgeBTAuthoring::KeyClass(String(*Op,TEXT("key_type")));
		if (!Class || Entry.EntryName.IsNone() || Entry.EntryName.ToString().Len()>64 || Names.Contains(Entry.EntryName) || (Blackboard && Blackboard->GetKeyID(Entry.EntryName)!=FBlackboard::InvalidKey)) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("New unique bounded key and verified type required"));
		Names.Add(Entry.EntryName); Entry.KeyType=NewObject<UBlackboardKeyType>(GetTransientPackage(),Class,NAME_None,RF_Transient); Added.Add(Entry);
	}
	if (Added.Num()+(Blackboard?Blackboard->GetNumKeys():0)>128) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Blackboard key budget exceeded"));
	auto Model=MakeShared<FJsonObject>(); Model->SetStringField(TEXT("asset_path"),Request.Target); Model->SetArrayField(TEXT("added_keys"),Request.Operations);
	if (Request.DryRun) return Preview(Request,Model);
	if (!Begin(Request,Reply)) return Reply;
	if (!Blackboard) { Blackboard=NewObject<UBlackboardData>(CreatePackage(*Request.Target),*FPackageName::GetLongPackageAssetName(Request.Target),RF_Public|RF_Standalone|RF_Transactional); FAssetRegistryModule::AssetCreated(Blackboard); }
	Blackboard->Modify();
	for (auto& Entry:Added) { Entry.KeyType=NewObject<UBlackboardKeyType>(Blackboard,Entry.KeyType->GetClass(),NAME_None,RF_Transactional); Blackboard->Keys.Add(Entry); }
	Blackboard->PostEditChange(); Blackboard->MarkPackageDirty(); return Finish(Request,Model);
}
