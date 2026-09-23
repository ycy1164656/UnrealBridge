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
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UObjectGlobals.h"

namespace BridgeFragmentImpl
{
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
		Obj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
		Obj->SetStringField(TEXT("type"), PinTypeToString(Pin->PinType));
		Obj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
		Obj->SetStringField(TEXT("default_object"),
			Pin->DefaultObject ? Pin->DefaultObject->GetPathName() : FString());
		Obj->SetStringField(TEXT("default_text"), Pin->DefaultTextValue.ToString());
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
			Parts.Add(FString::Printf(
				TEXT("%s|%s|%s|%s|%d"),
				*Pin->PinName.ToString(),
				Pin->Direction == EGPD_Input ? TEXT("in") : TEXT("out"),
				*PinTypeToString(Pin->PinType),
				*Pin->DefaultValue,
				Pin->LinkedTo.Num()));
		}
		Parts.Sort();
		return UnrealBridgeSha256::HexOfString(FString::Join(Parts, TEXT("\n")));
	}

	TSharedRef<FJsonObject> DescribeNode(UEdGraphNode* Node)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens));
		Obj->SetStringField(TEXT("class"), Node->GetClass()->GetPathName());
		Obj->SetStringField(TEXT("title"),
			Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Obj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		Obj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
		Obj->SetBoolField(TEXT("can_duplicate"), Node->CanDuplicateNode());
		Obj->SetStringField(TEXT("pin_signature_sha256"), PinSignature(Node));

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
		const FString Trimmed = Guid.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			GuidFilter.Add(Trimmed);
		}
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
	AddStringArray(Root, TEXT("external_dependencies"), Dependencies);
	AddStringArray(Root, TEXT("skipped_non_duplicable"), Skipped);
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
	bool bCompile)
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
	ImportedOrdered.Sort([](const UEdGraphNode& A, const UEdGraphNode& B)
	{
		return A.NodePosY == B.NodePosY ? A.NodePosX < B.NodePosX : A.NodePosY < B.NodePosY;
	});
	for (UEdGraphNode* Node : ImportedOrdered)
	{
		Node->CreateNewGuid();
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
	Root->SetBoolField(TEXT("ok"), true);
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
	AddStringArray(Root, TEXT("external_dependencies"), Dependencies);
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
		const FString Trimmed = Guid.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			GuidFilter.Add(Trimmed);
		}
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
