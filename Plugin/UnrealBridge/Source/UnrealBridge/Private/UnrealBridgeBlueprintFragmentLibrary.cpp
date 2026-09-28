#include "UnrealBridgeBlueprintFragmentLibrary.h"

#include "UnrealBridgeSha256.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphUtilities.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_BaseMCDelegate.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "Internationalization/Text.h"

namespace BridgeFragmentImpl
{
	// Only locally exported, bounded native payloads can reach deserialization.
	// Re-export the recorded source after a restart; a hash alone is not trust.
	TMap<FString, TSharedPtr<FJsonObject>> Fragment_Exports;
	bool Fragment_Exporting = false;
	const TCHAR* Fragment_SourceGuidKey = TEXT("UnrealBridge.Fragment.SourceGuid");
	const TCHAR* Fragment_RequestKey = TEXT("UnrealBridge.Fragment.Request");
	const TCHAR* Fragment_PayloadKey = TEXT("UnrealBridge.Fragment.Payload");
	const TCHAR* Fragment_SaveBlockKey = TEXT("UnrealBridge.Fragment.SaveBlocked");

	FString Fragment_PinId(const UEdGraphPin* Pin)
	{
		return (Pin->ParentPin ? Fragment_PinId(Pin->ParentPin) + TEXT("/") : FString())
			+ Pin->PinName.ToString() + (Pin->Direction == EGPD_Input ? TEXT(":in") : TEXT(":out"));
	}

	FString Fragment_LogicalId(const UEdGraphNode* Node)
	{
		if (Fragment_Exporting) { return Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens); }
		const FString Stored = Node->GetOutermost()->GetMetaData().GetValue(Node, Fragment_SourceGuidKey);
		return Stored.IsEmpty() ? Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) : Stored;
	}
	/** Serialize a JSON object to a condensed string, matching the house convention. */
	FString ToJson(const TSharedRef<FJsonObject>& Root)
	{
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}

	FString Failure(const FString& Error, const FString& Phase)
	{
		const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetBoolField(TEXT("ok"), false);
		Root->SetStringField(TEXT("error"), Error);
		Root->SetStringField(TEXT("phase"), Phase);
		return ToJson(Root);
	}

	UBlueprint* LoadBlueprint(const FString& Path)
	{
		return Path.IsEmpty() ? nullptr : LoadObject<UBlueprint>(nullptr, *Path);
	}

	/**
	 * Find one K2 graph by name. An empty name selects the first Ubergraph page,
	 * mirroring UnrealBridgeBlueprintLibrary::FindGraphs.
	 */
	UEdGraph* FindGraph(UBlueprint* BP, const FString& GraphName)
	{
		if (!BP)
		{
			return nullptr;
		}
		if (GraphName.IsEmpty())
		{
			return BP->UbergraphPages.Num() > 0 ? BP->UbergraphPages[0] : nullptr;
		}
		// UBlueprint stores these as TArray<TObjectPtr<UEdGraph>>, so search each
		// container directly rather than through a common pointer type.
		auto FindIn = [&GraphName](const auto& Graphs) -> UEdGraph*
		{
			for (UEdGraph* Graph : Graphs)
			{
				if (Graph && Graph->GetName() == GraphName)
				{
					return Graph;
				}
			}
			return nullptr;
		};
		if (UEdGraph* Found = FindIn(BP->FunctionGraphs)) { return Found; }
		if (UEdGraph* Found = FindIn(BP->UbergraphPages)) { return Found; }
		if (UEdGraph* Found = FindIn(BP->MacroGraphs))    { return Found; }
		return nullptr;
	}

	FString PinTypeToString(const FEdGraphPinType& PinType)
	{
		FString Text = PinType.PinCategory.ToString();
		if (!PinType.PinSubCategory.IsNone())
		{
			Text += TEXT(":") + PinType.PinSubCategory.ToString();
		}
		if (PinType.PinSubCategoryObject.IsValid())
		{
			Text += TEXT("<") + PinType.PinSubCategoryObject->GetPathName() + TEXT(">");
		}
		if (PinType.IsArray())    { Text += TEXT("[]"); }
		if (PinType.IsSet())      { Text += TEXT("{set}"); }
		if (PinType.IsMap())      { Text += TEXT("{map}"); }
		if (PinType.bIsReference) { Text += TEXT("&"); }
		Text += FString::Printf(TEXT("|const=%d|weak=%d|wrapper=%d|value=%s:%s:%s"),
			PinType.bIsConst, PinType.bIsWeakPointer, PinType.bIsUObjectWrapper,
			*PinType.PinValueType.TerminalCategory.ToString(), *PinType.PinValueType.TerminalSubCategory.ToString(),
			PinType.PinValueType.TerminalSubCategoryObject.IsValid() ? *PinType.PinValueType.TerminalSubCategoryObject->GetPathName() : TEXT(""));
		Text += FString::Printf(TEXT("|value_const=%d|value_weak=%d|value_wrapper=%d|single_precision=%d"),
			PinType.PinValueType.bTerminalIsConst, PinType.PinValueType.bTerminalIsWeakPointer,
			PinType.PinValueType.bTerminalIsUObjectWrapper, PinType.bSerializeAsSinglePrecisionFloat);
		return Text;
	}

	/**
	 * Structural description of one pin, including its default value and link
	 * targets. This is what makes dynamic-pin and internal-wiring preservation
	 * checkable: the same node re-read after an import must produce the same
	 * pin signature.
	 */
	TSharedRef<FJsonObject> DescribePin(const UEdGraphPin* Pin)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Pin->PinName.ToString());
		Obj->SetStringField(TEXT("pin_id"), Fragment_PinId(Pin));
		Obj->SetStringField(TEXT("parent_pin"), Pin->ParentPin ? Fragment_PinId(Pin->ParentPin) : FString());
		Obj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		Obj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
		Obj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		Obj->SetStringField(TEXT("default_object"),
			Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : FString());
		FString SerializedText;
		FTextStringHelper::WriteToBuffer(SerializedText, Pin->DefaultTextValue);
		Obj->SetStringField(TEXT("default_text"), SerializedText);
		Obj->SetStringField(TEXT("autogenerated_default"), Pin->AutogeneratedDefaultValue);
		Obj->SetBoolField(TEXT("orphaned"), Pin->bOrphanedPin);
		Obj->SetBoolField(TEXT("hidden"), Pin->bHidden);
		Obj->SetNumberField(TEXT("link_count"), Pin->LinkedTo.Num());

		TArray<TSharedPtr<FJsonValue>> Links;
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNodeUnchecked())
			{
				continue;
			}
			// Internal wiring is identified by (peer node guid, peer pin name) so
			// it stays comparable across an import that remaps node GUIDs.
			Links.Add(MakeShared<FJsonValueString>(FString::Printf(
				TEXT("%s|%s"),
				*Linked->GetOwningNode()->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens),
				*Linked->PinName.ToString())));
		}
		Links.Sort([](const TSharedPtr<FJsonValue>& A, const TSharedPtr<FJsonValue>& B)
		{
			return A->AsString() < B->AsString();
		});
		Obj->SetArrayField(TEXT("links"), Links);
		return Obj;
	}

	/** Stable per-node pin signature, independent of pin ordering. */
	FString PinSignature(const UEdGraphNode* Node)
	{
		TArray<FString> Parts;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}
			TSharedRef<FJsonObject> Shape = DescribePin(Pin);
			Shape->RemoveField(TEXT("links"));
			Shape->RemoveField(TEXT("link_count"));
			// UE reconstructs a Blueprint-defined self pin as either the source
			// declaring class or the destination child class. Normalize only this
			// proven self receiver; explicit object pins keep exact class types.
			const FMemberReference* SelfMember = nullptr;
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node)) { SelfMember = &Call->FunctionReference; }
			if (const UK2Node_BaseMCDelegate* Delegate = Cast<UK2Node_BaseMCDelegate>(Node)) { SelfMember = &Delegate->DelegateReference; }
			const UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node);
			const UClass* Owner = SelfMember && BP ? SelfMember->GetMemberParentClass(BP->GeneratedClass) : nullptr;
			if (SelfMember && SelfMember->IsSelfContext() && Owner && Owner->HasAnyClassFlags(CLASS_CompiledFromBlueprint)
				&& Pin->PinName == UEdGraphSchema_K2::PN_Self && Pin->Direction == EGPD_Input
				&& Pin->LinkedTo.IsEmpty() && !Pin->DefaultObject && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			{
				FEdGraphPinType SelfType = Pin->PinType;
				SelfType.PinSubCategory = UEdGraphSchema_K2::PSC_Self; SelfType.PinSubCategoryObject = nullptr;
				Shape->SetStringField(TEXT("type"), PinTypeToString(SelfType));
			}
			Parts.Add(ToJson(Shape));
		}
		Parts.Sort();
		return UnrealBridgeSha256::HexOfString(Node->GetClass()->GetPathName() + TEXT("\n") + FString::Join(Parts, TEXT("\n")));
	}

	TSharedRef<FJsonObject> DescribeNode(UEdGraphNode* Node)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Obj->SetStringField(TEXT("logical_id"), Fragment_LogicalId(Node));
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Obj->SetStringField(TEXT("title"),
			Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Obj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		Obj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
		Obj->SetBoolField(TEXT("can_duplicate"), Node->CanDuplicateNode());
		Obj->SetStringField(TEXT("pin_signature_sha256"), PinSignature(Node));
		Obj->SetStringField(TEXT("node_shape_hash"), PinSignature(Node));

		TArray<TSharedPtr<FJsonValue>> Pins;
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin)
			{
				Pins.Add(MakeShared<FJsonValueObject>(DescribePin(Pin)));
			}
		}
		Obj->SetArrayField(TEXT("pins"), Pins);
		return Obj;
	}

	/**
	 * External dependencies as the engine itself reports them
	 * (UK2Node::HasExternalDependencies), not a guess from pin types.
	 */
	void CollectExternalDependencies(const TArray<UEdGraphNode*>& Nodes, TSet<FString>& Out)
	{
		for (UEdGraphNode* Node : Nodes)
		{
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) { continue; }
				if (Pin->DefaultObject) { Out.Add(Pin->DefaultObject->GetPathName()); }
				if (Pin->PinType.PinSubCategoryObject.IsValid()) { Out.Add(Pin->PinType.PinSubCategoryObject->GetPathName()); }
			}
			UK2Node* K2Node = Cast<UK2Node>(Node);
			if (!K2Node)
			{
				continue;
			}
			TArray<UStruct*> Dependencies;
			if (K2Node->HasExternalDependencies(&Dependencies))
			{
				for (const UStruct* Dependency : Dependencies)
				{
					if (Dependency)
					{
						Out.Add(Dependency->GetPathName());
					}
				}
			}
		}
	}

	FString Fragment_MemberSignature(const UClass* Owner, FName Name, bool bFunction)
	{
		if (!Owner) { return FString(); }
		TArray<FString> Parts;
		if (bFunction)
		{
			const UFunction* Function = Owner->FindFunctionByName(Name);
			if (!Function) { return FString(); }
			Parts.Add(FString::Printf(TEXT("flags=%u"), static_cast<uint32>(Function->FunctionFlags)));
			for (TFieldIterator<FProperty> It(Function); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_Parm))
				{
					Parts.Add(It->GetName() + TEXT(":") + It->GetCPPType() + FString::Printf(TEXT(":%llu"), static_cast<uint64>(It->GetPropertyFlags())));
				}
			}
		}
		else
		{
			const FProperty* Property = FindFProperty<FProperty>(Owner, Name);
			if (!Property) { return FString(); }
			Parts.Add(Property->GetCPPType());
			Parts.Add(FString::Printf(TEXT("flags=%llu"), static_cast<uint64>(Property->GetPropertyFlags())));
			if (const FMulticastDelegateProperty* Delegate = CastField<FMulticastDelegateProperty>(Property))
			{
				if (Delegate->SignatureFunction)
				{
					for (TFieldIterator<FProperty> It(Delegate->SignatureFunction); It; ++It)
					{ if (It->HasAnyPropertyFlags(CPF_Parm)) { Parts.Add(It->GetName() + TEXT(":") + It->GetCPPType() + FString::Printf(TEXT(":%llu"), static_cast<uint64>(It->GetPropertyFlags()))); } }
				}
			}
		}
		return UnrealBridgeSha256::HexOfString(FString::Join(Parts, TEXT("|")));
	}

	TArray<TSharedPtr<FJsonValue>> Fragment_Members(const TArray<UEdGraphNode*>& Nodes, UBlueprint* BP)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (UEdGraphNode* Node : Nodes)
		{
			const FMemberReference* Ref = nullptr;
			const bool bFunction = Node->IsA<UK2Node_CallFunction>();
			if (const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node)) { Ref = &Call->FunctionReference; }
			if (const UK2Node_Variable* Variable = Cast<UK2Node_Variable>(Node)) { Ref = &Variable->VariableReference; }
			if (const UK2Node_BaseMCDelegate* Delegate = Cast<UK2Node_BaseMCDelegate>(Node)) { Ref = &Delegate->DelegateReference; }
			if (!Ref) { continue; }
			const UClass* Owner = Ref->GetMemberParentClass(BP->GeneratedClass);
			const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("node"), Fragment_LogicalId(Node));
			Item->SetStringField(TEXT("name"), Ref->GetMemberName().ToString());
			Item->SetStringField(TEXT("owner"), Owner ? Owner->GetPathName() : FString());
			Item->SetBoolField(TEXT("self_context"), Ref->IsSelfContext());
			Item->SetBoolField(TEXT("function"), bFunction);
			Item->SetStringField(TEXT("signature"), Fragment_MemberSignature(Owner, Ref->GetMemberName(), bFunction));
			Result.Add(MakeShared<FJsonValueObject>(Item));
		}
		return Result;
	}

	void Fragment_AddStructure(const TSharedRef<FJsonObject>& Root, const TArray<UEdGraphNode*>& Nodes)
	{
		TSet<UEdGraphNode*> Selected(Nodes);
		TArray<FString> Shapes, Edges, Boundaries;
		TArray<TSharedPtr<FJsonValue>> Ports;
		for (const UEdGraphNode* Node : Nodes)
		{
			const FString Id = Fragment_LogicalId(Node);
			Shapes.Add(Id + TEXT("|") + PinSignature(Node));
			for (const UEdGraphPin* Pin : Node->Pins)
			{
				if (!Pin) { continue; }
				for (const UEdGraphPin* Peer : Pin->LinkedTo)
				{
					if (!Peer || !Peer->GetOwningNodeUnchecked()) { continue; }
					const FString From = Id + TEXT("|") + Fragment_PinId(Pin);
					const FString To = Fragment_LogicalId(Peer->GetOwningNode()) + TEXT("|") + Fragment_PinId(Peer);
					if (Selected.Contains(Peer->GetOwningNode()))
					{
						if (Pin->Direction == EGPD_Output) { Edges.Add(From + TEXT("->") + To); }
					}
					else
					{
						Boundaries.Add(From + TEXT("->") + To);
						const TSharedRef<FJsonObject> Port = MakeShared<FJsonObject>();
						Port->SetStringField(TEXT("port_id"), UnrealBridgeSha256::HexOfString(From + TEXT("->") + To));
						Port->SetStringField(TEXT("source_pin"), From);
						Port->SetStringField(TEXT("external_peer"), To);
						Port->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
						Ports.Add(MakeShared<FJsonValueObject>(Port));
					}
				}
			}
		}
		Shapes.Sort(); Edges.Sort(); Boundaries.Sort();
		const FString ShapeHash = UnrealBridgeSha256::HexOfString(FString::Join(Shapes, TEXT("\n")));
		const FString TopologyHash = UnrealBridgeSha256::HexOfString(FString::Join(Edges, TEXT("\n")));
		Root->SetStringField(TEXT("node_shapes_hash"), ShapeHash);
		Root->SetStringField(TEXT("internal_topology_hash"), TopologyHash);
		TArray<FString> MemberShapes;
		UBlueprint* Blueprint = Nodes.IsEmpty() ? nullptr : FBlueprintEditorUtils::FindBlueprintForNode(Nodes[0]);
		if (Blueprint)
		{
			for (const auto& Value : Fragment_Members(Nodes, Blueprint))
			{
				const auto Member = Value->AsObject();
				// Self members resolve against each target; bind signature rather
				// than the source generated-class name for portable child BPs.
				if (Member->GetBoolField(TEXT("self_context"))) { Member->SetStringField(TEXT("owner"), TEXT("$self")); }
				MemberShapes.Add(ToJson(Member.ToSharedRef()));
			}
		}
		MemberShapes.Sort();
		const FString MemberHash = UnrealBridgeSha256::HexOfString(FString::Join(MemberShapes, TEXT("\n")));
		Root->SetStringField(TEXT("member_contract_hash"), MemberHash);
		Root->SetStringField(TEXT("fragment_structure_hash"), UnrealBridgeSha256::HexOfString(ShapeHash + TopologyHash + MemberHash + FString::Join(Boundaries, TEXT("\n"))));
		Root->SetArrayField(TEXT("boundary_ports"), Ports);
		Root->SetNumberField(TEXT("internal_edge_count"), Edges.Num());
	}

	FString Fragment_CheckTrusted(const FString& Text, UBlueprint* BP, UEdGraph* Graph)
	{
		if (!Graph->GetSchema()->IsA<UEdGraphSchema_K2>() || (!BP->UbergraphPages.Contains(Graph) && !BP->FunctionGraphs.Contains(Graph)))
		{ return TEXT("Only K2 event/function graphs are supported"); }
		if (Text.Len() > 2 * 1024 * 1024) { return TEXT("Fragment exceeds 2 MiB character budget"); }
		const TSharedPtr<FJsonObject>* Manifest = Fragment_Exports.Find(UnrealBridgeSha256::HexOfString(Text));
		if (!Manifest) { return TEXT("Payload is not a trusted export in this Editor session; re-export the recorded source"); }
		for (const TSharedPtr<FJsonValue>& Value : (*Manifest)->GetArrayField(TEXT("member_dependencies")))
		{
			const TSharedPtr<FJsonObject> Member = Value->AsObject();
			const bool bSelf = Member->GetBoolField(TEXT("self_context"));
			UClass* Owner = bSelf ? BP->GeneratedClass.Get() : LoadObject<UClass>(nullptr, *Member->GetStringField(TEXT("owner")));
			const FString Signature = Fragment_MemberSignature(Owner, FName(*Member->GetStringField(TEXT("name"))), Member->GetBoolField(TEXT("function")));
			if (Signature.IsEmpty() || Signature != Member->GetStringField(TEXT("signature")))
			{ return TEXT("Missing member or incompatible owner/signature: ") + Member->GetStringField(TEXT("name")); }
		}
		return FString();
	}

	/**
	 * Read the object path that follows `Token=` in clipboard text.
	 *
	 * Unreal spells object references as Type'Path' (commonly
	 * /Script/CoreUObject.Class'"/Script/Pkg.Name"'). The bare token before the
	 * quote is the reference's TYPE, not its target, so taking it yields
	 * /Script/CoreUObject.Class for every reference and makes any check built on
	 * it meaningless. When the inner form is present, the inner path wins.
	 *
	 * Returns the next scan position via OutCursor.
	 */
	FString ReadObjectPathAfter(
		const FString& Text,
		int32 ValueStart,
		int32& OutCursor)
	{
		int32 End = ValueStart;
		while (End < Text.Len()
			&& !FChar::IsWhitespace(Text[End])
			&& Text[End] != TEXT('\'')
			&& Text[End] != TEXT(','))
		{
			++End;
		}
		FString Outer = Text.Mid(ValueStart, End - ValueStart);
		OutCursor = End;

		if (End < Text.Len() && Text[End] == TEXT('\''))
		{
			const int32 InnerStart = End + 1;
			int32 InnerEnd = InnerStart;
			while (InnerEnd < Text.Len() && Text[InnerEnd] != TEXT('\''))
			{
				++InnerEnd;
			}
			FString Inner = Text.Mid(InnerStart, InnerEnd - InnerStart);
			OutCursor = FMath::Min(InnerEnd + 1, Text.Len());
			Inner.TrimStartAndEndInline();
			Inner.RemoveFromStart(TEXT("\""));
			Inner.RemoveFromEnd(TEXT("\""));
			if (!Inner.IsEmpty())
			{
				return Inner;
			}
		}

		Outer.TrimStartAndEndInline();
		Outer.RemoveFromStart(TEXT("\""));
		Outer.RemoveFromEnd(TEXT("\""));
		return Outer;
	}

	/** Collect every object path that follows `Token` in the text. */
	void ScanTokenPaths(const FString& FragmentText, const FString& Token, TSet<FString>& Out)
	{
		int32 Cursor = 0;
		while (true)
		{
			const int32 Found = FragmentText.Find(Token, ESearchCase::CaseSensitive,
				ESearchDir::FromStart, Cursor);
			if (Found == INDEX_NONE)
			{
				return;
			}
			int32 Next = Found + Token.Len();
			const FString Path = ReadObjectPathAfter(FragmentText, Found + Token.Len(), Next);
			if (!Path.IsEmpty())
			{
				Out.Add(Path);
			}
			Cursor = FMath::Max(Next, Found + Token.Len());
		}
	}

	/**
	 * Report `Class=` references in a fragment that do not resolve in this editor.
	 * Lexical: a resolving class does not prove the node imports or behaves.
	 */
	void ScanUnresolvedClasses(const FString& FragmentText, TSet<FString>& Out)
	{
		TSet<FString> Paths;
		ScanTokenPaths(FragmentText, TEXT("Class="), Paths);
		for (const FString& Path : Paths)
		{
			if (!FindObject<UClass>(nullptr, *Path))
			{
				Out.Add(Path);
			}
		}
	}

	/** Member-call owners a fragment references, via the Type'Path' inner path. */
	void ScanMemberParents(const FString& FragmentText, TSet<FString>& Out)
	{
		ScanTokenPaths(FragmentText, TEXT("MemberParent="), Out);
	}

	/**
	 * Report member-call owners the target class cannot satisfy.
	 *
	 * A structural CanImportNodesFromText pass says the nodes can be placed; it
	 * does NOT say their calls will bind. Pasting a GameplayAbility's graph into
	 * a plain Actor imports cleanly and then fails to compile, which is the
	 * failure this check is here to predict before anything is written.
	 *
	 * Static calls (Blueprint function libraries) bind anywhere, so only
	 * non-static owners are required to be in the target's hierarchy. An owner
	 * that cannot be resolved at all is reported separately rather than assumed
	 * satisfiable.
	 */
	void CollectUnbindableOwners(
		const FString& FragmentText,
		const UClass* TargetClass,
		TSet<FString>& OutUnbindable,
		TSet<FString>& OutUnresolvedOwners)
	{
		TSet<FString> Owners;
		ScanMemberParents(FragmentText, Owners);
		for (const FString& OwnerPath : Owners)
		{
			UClass* OwnerClass = FindObject<UClass>(nullptr, *OwnerPath);
			if (!OwnerClass)
			{
				OutUnresolvedOwners.Add(OwnerPath);
				continue;
			}
			if (OwnerClass->IsChildOf(UBlueprintFunctionLibrary::StaticClass()))
			{
				continue; // Static library calls bind regardless of the target.
			}
			if (!TargetClass || !TargetClass->IsChildOf(OwnerClass))
			{
				OutUnbindable.Add(OwnerPath);
			}
		}
	}

	void AddStringArray(const TSharedRef<FJsonObject>& Root, const FString& Field, const TSet<FString>& Values)
	{
		TArray<FString> Sorted = Values.Array();
		Sorted.Sort();
		TArray<TSharedPtr<FJsonValue>> Items;
		for (const FString& Value : Sorted)
		{
			Items.Add(MakeShared<FJsonValueString>(Value));
		}
		Root->SetArrayField(Field, Items);
	}
}

FString UUnrealBridgeBlueprintFragmentLibrary::ExportFragment(
	const FString& BlueprintPath,
	const FString& GraphName,
	const TArray<FString>& NodeGuids)
{
	using namespace BridgeFragmentImpl;
	TGuardValue<bool> ExportContext(Fragment_Exporting, true);

	UBlueprint* BP = LoadBlueprint(BlueprintPath);
	if (!BP)
	{
		return Failure(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath), TEXT("resolve"));
	}
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph)
	{
		return Failure(FString::Printf(TEXT("Graph not found: %s"), *GraphName), TEXT("resolve"));
	}

	// Empty GuidFilter means "whole graph".
	TSet<FString> GuidFilter;
	for (const FString& Guid : NodeGuids)
	{
		FGuid Parsed;
		if (FGuid::Parse(Guid.TrimStartAndEnd(), Parsed))
		{
			GuidFilter.Add(Parsed.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else { return Failure(TEXT("Invalid selected node GUID: ") + Guid, TEXT("select")); }
	}

	TArray<UEdGraphNode*> Selected;
	TSet<UObject*> ExportSet;
	TSet<FString> Skipped;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		const FString Guid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		if (GuidFilter.Num() > 0 && !GuidFilter.Contains(Guid))
		{
			continue;
		}
		if (!Node->CanDuplicateNode())
		{
			// Entry points and similar singletons cannot be copied; report them
			// instead of silently producing an incomplete fragment.
			Skipped.Add(FString::Printf(TEXT("%s (%s)"), *Guid, *Node->GetClass()->GetName()));
			continue;
		}
		Selected.Add(Node);
		ExportSet.Add(Node);
	}

	if (ExportSet.Num() == 0)
	{
		return Failure(TEXT("No duplicable nodes matched the selection"), TEXT("select"));
	}
	if (Selected.Num() > 512 || Skipped.Num() > 0)
	{
		return Failure(TEXT("Select at most 512 duplicable nodes explicitly; selection contains unsupported entry/singleton nodes"), TEXT("select"));
	}
	if (!Graph->GetSchema()->IsA<UEdGraphSchema_K2>() || (!BP->UbergraphPages.Contains(Graph) && !BP->FunctionGraphs.Contains(Graph)))
	{
		return Failure(TEXT("Only K2 event/function graphs are supported"), TEXT("select"));
	}
	const TSet<FString> Supported = { TEXT("K2Node_CallFunction"), TEXT("K2Node_VariableGet"), TEXT("K2Node_VariableSet"),
		TEXT("K2Node_IfThenElse"), TEXT("K2Node_ExecutionSequence"), TEXT("K2Node_Knot"), TEXT("EdGraphNode_Comment"),
		TEXT("K2Node_Self"), TEXT("K2Node_MakeStruct"), TEXT("K2Node_BreakStruct"), TEXT("K2Node_PromotableOperator"),
		TEXT("K2Node_CommutativeAssociativeBinaryOperator"), TEXT("K2Node_DynamicCast"), TEXT("K2Node_Message"),
		TEXT("K2Node_CallDelegate"), TEXT("K2Node_AddDelegate"), TEXT("K2Node_RemoveDelegate"), TEXT("K2Node_ClearDelegate") };
	for (UEdGraphNode* Node : Selected)
	{
		if (!Supported.Contains(Node->GetClass()->GetName()))
		{ return Failure(TEXT("Node class requires an explicit verified adapter: ") + Node->GetClass()->GetPathName(), TEXT("select")); }
	}
	const bool bDirtyBefore = BP->GetOutermost()->IsDirty();
	const TSharedRef<FJsonObject> Before = MakeShared<FJsonObject>();
	Fragment_AddStructure(Before, Selected);
	for (UEdGraphNode* Node : Selected) { Node->PrepareForCopying(); }

	FString FragmentText;
	FEdGraphUtilities::ExportNodesToText(ExportSet, FragmentText);
	if (FragmentText.IsEmpty())
	{
		return Failure(TEXT("ExportNodesToText produced an empty fragment"), TEXT("export"));
	}

	TSet<FString> Dependencies;
	CollectExternalDependencies(Selected, Dependencies);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("blueprint"), BP->GetPathName());
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	Root->SetStringField(TEXT("fragment_text"), FragmentText);
	Root->SetStringField(TEXT("fragment_sha256"), UnrealBridgeSha256::HexOfString(FragmentText));
	Root->SetNumberField(TEXT("node_count"), Selected.Num());

	TArray<TSharedPtr<FJsonValue>> NodeItems;
	for (UEdGraphNode* Node : Selected)
	{
		NodeItems.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
	}
	Root->SetArrayField(TEXT("nodes"), NodeItems);
	Fragment_AddStructure(Root, Selected);
	Root->SetArrayField(TEXT("member_dependencies"), Fragment_Members(Selected, BP));
	Root->SetStringField(TEXT("dependency_coverage"), TEXT("native_members_pin_types_and_external_dependencies_supported_whitelist"));
	Root->SetBoolField(TEXT("source_unchanged"), bDirtyBefore == BP->GetOutermost()->IsDirty()
		&& Before->GetStringField(TEXT("fragment_structure_hash")) == Root->GetStringField(TEXT("fragment_structure_hash")));
	if (!Root->GetBoolField(TEXT("source_unchanged")))
	{ return Failure(TEXT("Export copy hook changed source graph or Dirty state; do not publish this fragment"), TEXT("export")); }
	AddStringArray(Root, TEXT("external_dependencies"), Dependencies);
	AddStringArray(Root, TEXT("skipped_non_duplicable"), Skipped);
	Fragment_Exports.Add(UnrealBridgeSha256::HexOfString(FragmentText), Root);
	return ToJson(Root);
}

FString UUnrealBridgeBlueprintFragmentLibrary::InspectFragment(const FString& FragmentText)
{
	using namespace BridgeFragmentImpl;

	if (FragmentText.IsEmpty())
	{
		return Failure(TEXT("FragmentText is empty"), TEXT("validate"));
	}

	TSet<FString> Unresolved;
	ScanUnresolvedClasses(FragmentText, Unresolved);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("fragment_sha256"), UnrealBridgeSha256::HexOfString(FragmentText));
	Root->SetNumberField(TEXT("fragment_length"), FragmentText.Len());
	AddStringArray(Root, TEXT("unresolved_classes"), Unresolved);
	Root->SetStringField(TEXT("basis"),
		TEXT("lexical scan of clipboard text; not a compile and not an authorization"));
	return ToJson(Root);
}

FString UUnrealBridgeBlueprintFragmentLibrary::PrepareImport(
	const FString& TargetBlueprintPath,
	const FString& TargetGraphName,
	const FString& FragmentText)
{
	using namespace BridgeFragmentImpl;

	if (FragmentText.IsEmpty())
	{
		return Failure(TEXT("FragmentText is empty"), TEXT("validate"));
	}
	UBlueprint* BP = LoadBlueprint(TargetBlueprintPath);
	if (!BP)
	{
		return Failure(FString::Printf(TEXT("Blueprint not found: %s"), *TargetBlueprintPath), TEXT("resolve"));
	}
	UEdGraph* Graph = FindGraph(BP, TargetGraphName);
	if (!Graph)
	{
		return Failure(FString::Printf(TEXT("Graph not found: %s"), *TargetGraphName), TEXT("resolve"));
	}

	const FString TrustError = Fragment_CheckTrusted(FragmentText, BP, Graph);
	if (!TrustError.IsEmpty()) { return Failure(TrustError, TEXT("preflight")); }
	const bool bEngineAccepts = FEdGraphUtilities::CanImportNodesFromText(Graph, FragmentText);
	TSet<FString> Unresolved;
	ScanUnresolvedClasses(FragmentText, Unresolved);

	// Structural acceptance is not binding. Predict the compile failure here
	// rather than after the target graph has already been written to.
	TSet<FString> Unbindable;
	TSet<FString> UnresolvedOwners;
	CollectUnbindableOwners(FragmentText, BP->GeneratedClass, Unbindable, UnresolvedOwners);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	// Gate on what is actually decidable here. The owner list below is a
	// diagnostic, not a gate: a K2 call routed through a pin legitimately names
	// an owner outside the target's hierarchy, so gating on it refuses valid
	// fragments. Owners that ARE self-context still show up in the list and are
	// the ones worth reading before an import.
	Root->SetBoolField(TEXT("can_import"), bEngineAccepts && Unresolved.Num() == 0);
	Root->SetBoolField(TEXT("engine_accepts"), bEngineAccepts);
	Root->SetStringField(TEXT("blueprint"), BP->GetPathName());
	Root->SetStringField(TEXT("target_class"),
		BP->GeneratedClass ? BP->GeneratedClass->GetPathName() : FString());
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	Root->SetStringField(TEXT("fragment_sha256"), UnrealBridgeSha256::HexOfString(FragmentText));
	Root->SetNumberField(TEXT("existing_node_count"), Graph->Nodes.Num());
	AddStringArray(Root, TEXT("unresolved_classes"), Unresolved);
	AddStringArray(Root, TEXT("member_owners_outside_target_hierarchy"), Unbindable);
	AddStringArray(Root, TEXT("unresolved_member_owners"), UnresolvedOwners);
	Root->SetStringField(TEXT("note"),
		TEXT("Preflight only. A pass is a precondition, never an authorization or a completion. "
			 "member_owners_outside_target_hierarchy lists non-static call owners absent from "
			 "the target's class hierarchy. Self-context calls among them will fail to compile "
			 "after a structurally successful paste; calls routed through a pin will not. This "
			 "list does not distinguish the two, so it informs the decision rather than making it."));
	return ToJson(Root);
}

FString UUnrealBridgeBlueprintFragmentLibrary::ImportFragment(
	const FString& TargetBlueprintPath,
	const FString& TargetGraphName,
	const FString& FragmentText,
	bool bCompile,
	const FString& RequestId)
{
	using namespace BridgeFragmentImpl;

	if (FragmentText.IsEmpty())
	{
		return Failure(TEXT("FragmentText is empty"), TEXT("validate"));
	}
	UBlueprint* BP = LoadBlueprint(TargetBlueprintPath);
	if (!BP)
	{
		return Failure(FString::Printf(TEXT("Blueprint not found: %s"), *TargetBlueprintPath), TEXT("resolve"));
	}
	UEdGraph* Graph = FindGraph(BP, TargetGraphName);
	if (!Graph)
	{
		return Failure(FString::Printf(TEXT("Graph not found: %s"), *TargetGraphName), TEXT("resolve"));
	}
	const FString TrustError = Fragment_CheckTrusted(FragmentText, BP, Graph);
	if (!TrustError.IsEmpty()) { return Failure(TrustError, TEXT("preflight")); }
	const FString PayloadHash = UnrealBridgeSha256::HexOfString(FragmentText);
	const FString Request = RequestId.IsEmpty() ? PayloadHash : RequestId;
	const FString ReceiptKey = TEXT("UnrealBridge.Fragment.Receipt.") + UnrealBridgeSha256::HexOfString(Request);
	FMetaData& Metadata = BP->GetOutermost()->GetMetaData();
	const FString Previous = Metadata.GetValue(Graph, *ReceiptKey);
	if (!Previous.IsEmpty())
	{
		TSharedPtr<FJsonObject> Receipt;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Previous), Receipt) || !Receipt.IsValid())
		{ return Failure(TEXT("Import intent already exists; needs_reconciliation, do not paste again"), TEXT("reconcile")); }
		if (Receipt->GetStringField(TEXT("fragment_sha256")) != PayloadHash)
		{ return Failure(TEXT("RequestId conflicts with a different payload"), TEXT("reconcile")); }
		TArray<UEdGraphNode*> Existing;
		for (UEdGraphNode* Node : Graph->Nodes)
		{ if (Node && Metadata.GetValue(Node, Fragment_RequestKey) == Request) { Existing.Add(Node); } }
		const TSharedRef<FJsonObject> Readback = MakeShared<FJsonObject>();
		Fragment_AddStructure(Readback, Existing);
		// A previous validation failure may be reconciled in place after a
		// validator fix. Require the same trusted payload, unchanged count,
		// exact native structure/member proof and a real successful compile.
		const auto Manifest = Fragment_Exports.FindChecked(PayloadHash);
		if (!Receipt->GetBoolField(TEXT("ok")) && bCompile && Existing.Num() == Manifest->GetIntegerField(TEXT("node_count"))
			&& Readback->GetStringField(TEXT("node_shapes_hash")) == Manifest->GetStringField(TEXT("node_shapes_hash"))
			&& Readback->GetStringField(TEXT("internal_topology_hash")) == Manifest->GetStringField(TEXT("internal_topology_hash"))
			&& Readback->GetStringField(TEXT("member_contract_hash")) == Manifest->GetStringField(TEXT("member_contract_hash")))
		{
			FKismetEditorUtilities::CompileBlueprint(BP);
			if (BP->Status == BS_UpToDate || BP->Status == BS_UpToDateWithWarnings)
			{
				for (const TCHAR* Key : { TEXT("node_shapes_hash"), TEXT("internal_topology_hash"), TEXT("member_contract_hash"), TEXT("fragment_structure_hash") })
				{ Receipt->SetStringField(Key, Readback->GetStringField(Key)); }
				Receipt->SetBoolField(TEXT("ok"), true); Receipt->SetBoolField(TEXT("roundtrip_verified"), true);
				Receipt->SetBoolField(TEXT("compile_passed"), true); Receipt->SetBoolField(TEXT("save_permitted"), true);
				Receipt->SetStringField(TEXT("status"), TEXT("reconciled_in_place"));
				Metadata.SetValue(Graph, *ReceiptKey, *ToJson(Receipt.ToSharedRef()));
				Metadata.SetValue(BP, Fragment_SaveBlockKey, TEXT(""));
			}
		}
		if (Existing.Num() != Receipt->GetIntegerField(TEXT("imported_node_count"))
			|| Readback->GetStringField(TEXT("fragment_structure_hash")) != Receipt->GetStringField(TEXT("fragment_structure_hash")))
		{ return Failure(TEXT("Previously imported nodes changed; needs_reconciliation"), TEXT("reconcile")); }
		Receipt->SetBoolField(TEXT("idempotent_noop"), true);
		return ToJson(Receipt.ToSharedRef());
	}
	if (!FEdGraphUtilities::CanImportNodesFromText(Graph, FragmentText))
	{
		return Failure(TEXT("Target graph rejects this fragment (CanImportNodesFromText)"), TEXT("preflight"));
	}

	const int32 NodeCountBefore = Graph->Nodes.Num();

	TSet<UEdGraphNode*> ImportedNodes;
	{
		const FScopedTransaction Transaction(
			NSLOCTEXT("UnrealBridge", "ImportBlueprintFragment", "Import Blueprint Fragment"));
		Graph->Modify();
		BP->Modify();
		Metadata.SetValue(Graph, *ReceiptKey, TEXT("intent_pending_reconciliation"));
		FEdGraphUtilities::ImportNodesFromText(Graph, FragmentText, ImportedNodes);
	}

	if (ImportedNodes.Num() == 0)
	{
		return Failure(TEXT("ImportNodesFromText added no nodes"), TEXT("import"));
	}

	// Give imported nodes fresh GUIDs and nudge them clear of existing content so
	// the result is inspectable. Node positions are cosmetic, not behavioral.
	int32 MaxExistingY = 0;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node && !ImportedNodes.Contains(const_cast<UEdGraphNode*>(Node)))
		{
			MaxExistingY = FMath::Max(MaxExistingY, Node->NodePosY);
		}
	}
	TArray<UEdGraphNode*> ImportedOrdered = ImportedNodes.Array();
	TSet<FString> SourceIds;
	TSharedRef<FJsonObject> Mapping = MakeShared<FJsonObject>();
	for (UEdGraphNode* Node : ImportedOrdered)
	{
		const FString SourceId = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		if (!Node->NodeGuid.IsValid() || SourceIds.Contains(SourceId))
		{ Metadata.SetValue(BP, Fragment_SaveBlockKey, TEXT("ambiguous_mapping")); return Failure(TEXT("Ambiguous source GUID mapping; needs_reconciliation"), TEXT("import")); }
		SourceIds.Add(SourceId);
		Metadata.SetValue(Node, Fragment_SourceGuidKey, *SourceId);
		Metadata.SetValue(Node, Fragment_RequestKey, *Request);
		Metadata.SetValue(Node, Fragment_PayloadKey, *PayloadHash);
		Node->CreateNewGuid();
		Mapping->SetStringField(SourceId, Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Node->NodePosY += MaxExistingY + 320;
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);

	FString CompileStatus = TEXT("not_requested");
	int32 CompileErrors = 0;
	int32 CompileWarnings = 0;
	if (bCompile)
	{
		FKismetEditorUtilities::CompileBlueprint(BP);
		if (BP->Status == BS_Error)
		{
			CompileStatus = TEXT("error");
		}
		else if (BP->Status == BS_UpToDateWithWarnings)
		{
			CompileStatus = TEXT("up_to_date_with_warnings");
		}
		else if (BP->Status == BS_UpToDate)
		{
			CompileStatus = TEXT("up_to_date");
		}
		else
		{
			CompileStatus = TEXT("unknown");
		}
		// Node-level compiler messages survive on the nodes themselves.
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node || !Node->bHasCompilerMessage)
			{
				continue;
			}
			if (Node->ErrorType == EMessageSeverity::Error)
			{
				++CompileErrors;
			}
			else if (Node->ErrorType == EMessageSeverity::Warning)
			{
				++CompileWarnings;
			}
		}
	}

	TSet<FString> Dependencies;
	CollectExternalDependencies(ImportedOrdered, Dependencies);

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Fragment_AddStructure(Root, ImportedOrdered);
	const TSharedPtr<FJsonObject> Manifest = Fragment_Exports.FindChecked(PayloadHash);
	const bool bRoundtrip = ImportedOrdered.Num() == Manifest->GetIntegerField(TEXT("node_count"))
		&& Root->GetStringField(TEXT("node_shapes_hash")) == Manifest->GetStringField(TEXT("node_shapes_hash"))
		&& Root->GetStringField(TEXT("internal_topology_hash")) == Manifest->GetStringField(TEXT("internal_topology_hash"))
		&& Root->GetStringField(TEXT("member_contract_hash")) == Manifest->GetStringField(TEXT("member_contract_hash"));
	const bool bCompilePassed = bCompile && (BP->Status == BS_UpToDate || BP->Status == BS_UpToDateWithWarnings) && CompileErrors == 0;
	Root->SetBoolField(TEXT("ok"), bRoundtrip && bCompilePassed);
	Root->SetBoolField(TEXT("roundtrip_verified"), bRoundtrip);
	Root->SetBoolField(TEXT("compile_passed"), bCompilePassed);
	Root->SetBoolField(TEXT("structurally_imported"), true);
	Root->SetBoolField(TEXT("behavior_verified"), false);
	Root->SetBoolField(TEXT("save_permitted"), bRoundtrip && bCompilePassed);
	Root->SetStringField(TEXT("status"), !bRoundtrip ? TEXT("needs_reconciliation") : bCompilePassed ? TEXT("roundtrip_verified") : TEXT("compile_failed_or_not_run"));
	Root->SetObjectField(TEXT("source_to_target"), Mapping);
	Root->SetStringField(TEXT("request_id"), Request);
	Metadata.SetValue(BP, Fragment_SaveBlockKey, bRoundtrip && bCompilePassed ? TEXT("") : TEXT("fragment_validation_failed"));
	Root->SetStringField(TEXT("blueprint"), BP->GetPathName());
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	Root->SetStringField(TEXT("package"), BP->GetOutermost()->GetName());
	Root->SetStringField(TEXT("fragment_sha256"), UnrealBridgeSha256::HexOfString(FragmentText));
	Root->SetNumberField(TEXT("node_count_before"), NodeCountBefore);
	Root->SetNumberField(TEXT("node_count_after"), Graph->Nodes.Num());
	Root->SetNumberField(TEXT("imported_node_count"), ImportedOrdered.Num());
	Root->SetStringField(TEXT("compile_status"), CompileStatus);
	Root->SetNumberField(TEXT("compile_error_nodes"), CompileErrors);
	Root->SetNumberField(TEXT("compile_warning_nodes"), CompileWarnings);
	Root->SetBoolField(TEXT("package_saved"), false);
	Root->SetStringField(TEXT("save_behavior"),
		TEXT("Never. The package is left dirty; saving belongs to the caller's ChangeSet."));

	TArray<TSharedPtr<FJsonValue>> NodeItems;
	for (UEdGraphNode* Node : ImportedOrdered)
	{
		NodeItems.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
	}
	Root->SetArrayField(TEXT("imported_nodes"), NodeItems);
	TArray<TSharedPtr<FJsonValue>> Messages;
	for (const UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node || !Node->bHasCompilerMessage) { continue; }
		const TSharedRef<FJsonObject> Message = MakeShared<FJsonObject>();
		Message->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Message->SetStringField(TEXT("message"), Node->ErrorMsg);
		Message->SetNumberField(TEXT("severity"), Node->ErrorType);
		Messages.Add(MakeShared<FJsonValueObject>(Message));
	}
	Root->SetArrayField(TEXT("compiler_messages"), Messages);
	AddStringArray(Root, TEXT("external_dependencies"), Dependencies);
	Metadata.SetValue(Graph, *ReceiptKey, *ToJson(Root));
	return ToJson(Root);
}

FString UUnrealBridgeBlueprintFragmentLibrary::ReadbackFragment(
	const FString& BlueprintPath,
	const FString& GraphName,
	const TArray<FString>& NodeGuids)
{
	using namespace BridgeFragmentImpl;

	UBlueprint* BP = LoadBlueprint(BlueprintPath);
	if (!BP)
	{
		return Failure(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath), TEXT("resolve"));
	}
	UEdGraph* Graph = FindGraph(BP, GraphName);
	if (!Graph)
	{
		return Failure(FString::Printf(TEXT("Graph not found: %s"), *GraphName), TEXT("resolve"));
	}

	TSet<FString> GuidFilter;
	for (const FString& Guid : NodeGuids)
	{
		FGuid Parsed;
		if (FGuid::Parse(Guid.TrimStartAndEnd(), Parsed))
		{
			GuidFilter.Add(Parsed.ToString(EGuidFormats::DigitsWithHyphens));
		}
		else { return Failure(TEXT("Invalid selected node GUID: ") + Guid, TEXT("select")); }
	}

	TArray<UEdGraphNode*> Matched;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node)
		{
			continue;
		}
		const FString Guid = Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		if (GuidFilter.Num() == 0 || GuidFilter.Contains(Guid))
		{
			Matched.Add(Node);
		}
	}

	TSet<FString> MissingGuids = GuidFilter;
	for (const UEdGraphNode* Node : Matched)
	{
		MissingGuids.Remove(Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("blueprint"), BP->GetPathName());
	Root->SetStringField(TEXT("graph"), Graph->GetName());
	Root->SetNumberField(TEXT("graph_node_count"), Graph->Nodes.Num());
	Root->SetNumberField(TEXT("matched_node_count"), Matched.Num());

	TArray<TSharedPtr<FJsonValue>> NodeItems;
	TArray<FString> Signatures;
	for (UEdGraphNode* Node : Matched)
	{
		NodeItems.Add(MakeShared<FJsonValueObject>(DescribeNode(Node)));
		Signatures.Add(PinSignature(Node));
	}
	Signatures.Sort();
	Root->SetArrayField(TEXT("nodes"), NodeItems);
	Fragment_AddStructure(Root, Matched);
	// Order-independent digest of the matched set, for comparing an import
	// against its source fragment.
	Root->SetStringField(TEXT("structure_sha256"),
		UnrealBridgeSha256::HexOfString(FString::Join(Signatures, TEXT("\n"))));
	AddStringArray(Root, TEXT("missing_guids"), MissingGuids);
	return ToJson(Root);
}

FString UUnrealBridgeBlueprintFragmentLibrary::ListFragmentGraphs(const FString& BlueprintPath)
{
	using namespace BridgeFragmentImpl;

	UBlueprint* BP = LoadBlueprint(BlueprintPath);
	if (!BP)
	{
		return Failure(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintPath), TEXT("resolve"));
	}

	const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetBoolField(TEXT("ok"), true);
	Root->SetStringField(TEXT("blueprint"), BP->GetPathName());

	TArray<TSharedPtr<FJsonValue>> Items;
	auto AddGraphs = [&Items](const TArray<UEdGraph*>& Graphs, const TCHAR* Kind)
	{
		for (const UEdGraph* Graph : Graphs)
		{
			if (!Graph)
			{
				continue;
			}
			const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("name"), Graph->GetName());
			Obj->SetStringField(TEXT("kind"), Kind);
			Obj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
			Items.Add(MakeShared<FJsonValueObject>(Obj));
		}
	};
	AddGraphs(BP->UbergraphPages, TEXT("ubergraph"));
	AddGraphs(BP->FunctionGraphs, TEXT("function"));
	AddGraphs(BP->MacroGraphs, TEXT("macro"));

	Root->SetArrayField(TEXT("graphs"), Items);
	Root->SetStringField(TEXT("scope"),
		TEXT("K2 graphs only. Material/Niagara/AnimGraph/StateTree/BehaviorTree/WidgetTree are not covered."));
	return ToJson(Root);
}
