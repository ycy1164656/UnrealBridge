#include "UnrealBridgeUMGLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BaseWidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/ContentWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/RichTextBlock.h"
#include "Components/TextBlock.h"
#include "EditorAssetLibrary.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Animation/WidgetAnimation.h"
#include "Animation/WidgetAnimationBinding.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "MovieScene.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieScenePossessable.h"
#include "Sections/MovieSceneFloatSection.h"
#include "Serialization/BufferArchive.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_ComponentBoundEvent.h"
#include "WidgetBlueprintEditorUtils.h"
#include "Widgets/SWidget.h"

// ─── Helpers ────────────────────────────────────────────────

namespace BridgeUMGImpl
{
	FString NormalizeKey(FString Key)
	{
		Key.ReplaceInline(TEXT("_"), TEXT(""));
		Key.ReplaceInline(TEXT("-"), TEXT(""));
		Key.ReplaceInline(TEXT(" "), TEXT(""));
		return Key.ToLower();
	}

	FString NormalizeAssetPath(const FString& InputPath)
	{
		if (InputPath.IsEmpty() || InputPath.Contains(TEXT(".")))
		{
			return InputPath;
		}
		const FString AssetName = FPackageName::GetShortName(InputPath);
		return FString::Printf(TEXT("%s.%s"), *InputPath, *AssetName);
	}

	UWidgetBlueprint* LoadWBP(const FString& Path)
	{
		UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *NormalizeAssetPath(Path));
		if (!WBP)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealBridge: Could not load Widget Blueprint '%s'"), *Path);
		}
		return WBP;
	}

	UWidgetTree* EnsureWidgetTree(UWidgetBlueprint* WBP)
	{
		UBaseWidgetBlueprint* BaseWBP = Cast<UBaseWidgetBlueprint>(WBP);
		if (!BaseWBP)
		{
			return nullptr;
		}
		if (!BaseWBP->WidgetTree)
		{
			BaseWBP->Modify();
			BaseWBP->WidgetTree = NewObject<UWidgetTree>(BaseWBP, TEXT("WidgetTree"), RF_Transactional);
			BaseWBP->MarkPackageDirty();
		}
		return BaseWBP->WidgetTree;
	}

	FString BlueprintStatusToString(const UBlueprint* Blueprint)
	{
		if (!Blueprint)
		{
			return TEXT("Unknown");
		}
		switch (Blueprint->Status)
		{
		case BS_Unknown: return TEXT("Unknown");
		case BS_Dirty: return TEXT("Dirty");
		case BS_Error: return TEXT("Error");
		case BS_UpToDate: return TEXT("UpToDate");
		case BS_UpToDateWithWarnings: return TEXT("UpToDateWithWarnings");
		default: return TEXT("Other");
		}
	}

	FString VisibilityToString(ESlateVisibility V)
	{
		switch (V)
		{
		case ESlateVisibility::Visible:				return TEXT("Visible");
		case ESlateVisibility::Collapsed:			return TEXT("Collapsed");
		case ESlateVisibility::Hidden:				return TEXT("Hidden");
		case ESlateVisibility::HitTestInvisible:	return TEXT("HitTestInvisible");
		case ESlateVisibility::SelfHitTestInvisible:return TEXT("SelfHitTestInvisible");
		default:									return TEXT("Unknown");
		}
	}

	FString PropertyTypeToString(const FProperty* Prop)
	{
		if (!Prop) return TEXT("Unknown");
		return Prop->GetCPPType();
	}

	void GatherWidgets(UWidget* Widget, const FString& ParentName, TArray<FBridgeWidgetInfo>& Out)
	{
		if (!Widget) return;

		FBridgeWidgetInfo Info;
		Info.Name = Widget->GetName();
		Info.WidgetClass = Widget->GetClass()->GetName();
		Info.ParentName = ParentName;
		Info.bIsVariable = Widget->bIsVariable;
		Info.Visibility = VisibilityToString(Widget->GetVisibility());

		if (UPanelSlot* Slot = Widget->Slot)
		{
			Info.SlotType = Slot->GetClass()->GetName();
		}

		Out.Add(Info);

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				GatherWidgets(Panel->GetChildAt(i), Info.Name, Out);
			}
		}
	}

	FString WorldTypeToString(EWorldType::Type WorldType)
	{
		switch (WorldType)
		{
		case EWorldType::None: return TEXT("None");
		case EWorldType::Game: return TEXT("Game");
		case EWorldType::Editor: return TEXT("Editor");
		case EWorldType::PIE: return TEXT("PIE");
		case EWorldType::EditorPreview: return TEXT("EditorPreview");
		case EWorldType::GamePreview: return TEXT("GamePreview");
		case EWorldType::GameRPC: return TEXT("GameRPC");
		case EWorldType::Inactive: return TEXT("Inactive");
		default: return TEXT("Unknown");
		}
	}

	FString RuntimeWidgetText(const UWidget* Widget)
	{
		if (const UTextBlock* TextBlock = Cast<UTextBlock>(Widget))
		{
			return TextBlock->GetText().ToString();
		}
		if (const URichTextBlock* RichTextBlock = Cast<URichTextBlock>(Widget))
		{
			return RichTextBlock->GetText().ToString();
		}
		if (const UEditableText* EditableText = Cast<UEditableText>(Widget))
		{
			return EditableText->GetText().ToString();
		}
		if (const UEditableTextBox* EditableTextBox = Cast<UEditableTextBox>(Widget))
		{
			return EditableTextBox->GetText().ToString();
		}
		return FString();
	}

	FBridgeRuntimeWidgetInfo BuildRuntimeWidgetInfo(
		UUserWidget* Owner,
		UWidget* Widget,
		const FString& SemanticPath,
		const FString& ParentObjectPath,
		int32 Depth)
	{
		FBridgeRuntimeWidgetInfo Info;
		if (!Widget)
		{
			Info.Error = TEXT("Runtime widget is null");
			return Info;
		}
		Info.bFound = true;
		Info.WidgetObjectPath = Widget->GetPathName();
		Info.SemanticPath = SemanticPath;
		Info.Name = Widget->GetName();
		Info.WidgetClass = Widget->GetClass()->GetPathName();
		Info.ParentObjectPath = ParentObjectPath;
		Info.Depth = Depth;
		Info.Text = RuntimeWidgetText(Widget);
		Info.Visibility = VisibilityToString(Widget->GetVisibility());
		Info.bEnabled = Widget->GetIsEnabled();
		Info.bHasKeyboardFocus = Widget->HasKeyboardFocus();
		Info.bHovered = Widget->IsHovered();
		if (APlayerController* PlayerController = Widget->GetOwningPlayer())
		{
			Info.bHasUserFocus = Widget->HasUserFocus(PlayerController);
		}
		if (Owner)
		{
			Info.OwnerUserWidgetPath = Owner->GetPathName();
			Info.OwnerUserWidgetClass = Owner->GetClass()->GetPathName();
			Info.bInViewport = Owner->IsInViewport();
		}
		if (UWorld* World = Widget->GetWorld())
		{
			Info.WorldPath = World->GetPathName();
			Info.WorldType = WorldTypeToString(World->WorldType);
		}

		const FGeometry& Geometry = Widget->GetCachedGeometry();
		Info.AbsolutePosition = FVector2D(Geometry.GetAbsolutePosition());
		Info.AbsoluteSize = FVector2D(Geometry.GetAbsoluteSize());
		Info.LocalSize = FVector2D(Geometry.GetLocalSize());
		if (const TSharedPtr<SWidget> SlateWidget = Widget->GetCachedWidget())
		{
			Info.SlateType = SlateWidget->GetTypeAsString();
			Info.SlateAddress = FString::Printf(TEXT("0x%p"), SlateWidget.Get());
		}
		return Info;
	}

	void GatherRuntimeWidgets(
		UUserWidget* Owner,
		UWidget* Widget,
		const FString& SemanticPath,
		const FString& ParentObjectPath,
		int32 Depth,
		TSet<UWidget*>& Seen,
		TArray<FBridgeRuntimeWidgetInfo>& Out)
	{
		if (!Widget || Seen.Contains(Widget))
		{
			return;
		}
		Seen.Add(Widget);
		Out.Add(BuildRuntimeWidgetInfo(Owner, Widget, SemanticPath, ParentObjectPath, Depth));

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				if (UWidget* Child = Panel->GetChildAt(Index))
				{
					GatherRuntimeWidgets(
						Owner,
						Child,
						SemanticPath + TEXT("/") + Child->GetName(),
						Widget->GetPathName(),
						Depth + 1,
						Seen,
						Out);
				}
			}
		}

		UUserWidget* NestedOwner = Cast<UUserWidget>(Widget);
		if (NestedOwner && NestedOwner != Owner && NestedOwner->WidgetTree
			&& NestedOwner->WidgetTree->RootWidget)
		{
			UWidget* NestedRoot = NestedOwner->WidgetTree->RootWidget;
			GatherRuntimeWidgets(
				NestedOwner,
				NestedRoot,
				SemanticPath + TEXT("/") + NestedRoot->GetName(),
				Widget->GetPathName(),
				Depth + 1,
				Seen,
				Out);
		}
	}

	UWidget* FindWidgetByName(UWidgetBlueprint* WBP, const FString& WidgetName)
	{
		if (!WBP || !WBP->WidgetTree) return nullptr;

		UWidget* Found = nullptr;
		WBP->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (W && W->GetName() == WidgetName)
			{
				Found = W;
			}
		});
		return Found;
	}

	UClass* ResolveWidgetClass(const FString& ClassNameOrPath, UClass* RequiredBaseClass = UWidget::StaticClass())
	{
		FString ClassName = ClassNameOrPath;
		ClassName.TrimStartAndEndInline();
		if (ClassName.IsEmpty())
		{
			return nullptr;
		}
		if (ClassName.StartsWith(TEXT("Class'")) && ClassName.EndsWith(TEXT("'")))
		{
			ClassName = ClassName.Mid(6, ClassName.Len() - 7);
		}

		static const TMap<FString, FString> CommonWidgetClasses = {
			{TEXT("border"), TEXT("/Script/UMG.Border")},
			{TEXT("button"), TEXT("/Script/UMG.Button")},
			{TEXT("canvaspanel"), TEXT("/Script/UMG.CanvasPanel")},
			{TEXT("checkbox"), TEXT("/Script/UMG.CheckBox")},
			{TEXT("editabletext"), TEXT("/Script/UMG.EditableText")},
			{TEXT("editabletextbox"), TEXT("/Script/UMG.EditableTextBox")},
			{TEXT("gridpanel"), TEXT("/Script/UMG.GridPanel")},
			{TEXT("horizontalbox"), TEXT("/Script/UMG.HorizontalBox")},
			{TEXT("image"), TEXT("/Script/UMG.Image")},
			{TEXT("listview"), TEXT("/Script/UMG.ListView")},
			{TEXT("namedslot"), TEXT("/Script/UMG.NamedSlot")},
			{TEXT("overlay"), TEXT("/Script/UMG.Overlay")},
			{TEXT("progressbar"), TEXT("/Script/UMG.ProgressBar")},
			{TEXT("richtextblock"), TEXT("/Script/UMG.RichTextBlock")},
			{TEXT("scalebox"), TEXT("/Script/UMG.ScaleBox")},
			{TEXT("scrollbox"), TEXT("/Script/UMG.ScrollBox")},
			{TEXT("sizebox"), TEXT("/Script/UMG.SizeBox")},
			{TEXT("slider"), TEXT("/Script/UMG.Slider")},
			{TEXT("spacer"), TEXT("/Script/UMG.Spacer")},
			{TEXT("textblock"), TEXT("/Script/UMG.TextBlock")},
			{TEXT("tileview"), TEXT("/Script/UMG.TileView")},
			{TEXT("treeview"), TEXT("/Script/UMG.TreeView")},
			{TEXT("uniformgridpanel"), TEXT("/Script/UMG.UniformGridPanel")},
			{TEXT("verticalbox"), TEXT("/Script/UMG.VerticalBox")},
			{TEXT("widgetswitcher"), TEXT("/Script/UMG.WidgetSwitcher")},
			{TEXT("wrapbox"), TEXT("/Script/UMG.WrapBox")},
			{TEXT("commonactivatablewidget"), TEXT("/Script/CommonUI.CommonActivatableWidget")},
			{TEXT("commonborder"), TEXT("/Script/CommonUI.CommonBorder")},
			{TEXT("commonbuttonbase"), TEXT("/Script/CommonUI.CommonButtonBase")},
			{TEXT("commonlazyimage"), TEXT("/Script/CommonUI.CommonLazyImage")},
			{TEXT("commontextblock"), TEXT("/Script/CommonUI.CommonTextBlock")}
		};

		FString ClassPath = ClassName;
		if (!ClassPath.StartsWith(TEXT("/")))
		{
			if (const FString* MappedPath = CommonWidgetClasses.Find(NormalizeKey(ClassPath)))
			{
				ClassPath = *MappedPath;
			}
			else
			{
				ClassPath = FString::Printf(TEXT("/Script/UMG.%s"), *ClassPath);
			}
		}

		UClass* WidgetClass = StaticLoadClass(RequiredBaseClass, nullptr, *ClassPath);
		if (!WidgetClass && !ClassPath.Contains(TEXT(".")))
		{
			WidgetClass = StaticLoadClass(RequiredBaseClass, nullptr, *(ClassPath + TEXT("_C")));
		}
		return WidgetClass && WidgetClass->IsChildOf(RequiredBaseClass) ? WidgetClass : nullptr;
	}

	FProperty* FindPropertyByFlexibleName(UStruct* Struct, const FString& PropertyName)
	{
		if (!Struct)
		{
			return nullptr;
		}
		if (FProperty* Direct = Struct->FindPropertyByName(FName(*PropertyName)))
		{
			return Direct;
		}
		const FString Normalized = NormalizeKey(PropertyName);
		const FString Alias = Normalized == TEXT("isvariable") ? TEXT("bIsVariable")
			: (Normalized == TEXT("autosize") || Normalized == TEXT("sizetocontent")) ? TEXT("bAutoSize")
			: Normalized == TEXT("enabled") ? TEXT("bIsEnabled")
			: TEXT("");
		if (!Alias.IsEmpty())
		{
			if (FProperty* Aliased = Struct->FindPropertyByName(FName(*Alias)))
			{
				return Aliased;
			}
		}
		for (TFieldIterator<FProperty> It(Struct, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop && NormalizeKey(Prop->GetName()) == Normalized)
			{
				return Prop;
			}
		}
		return nullptr;
	}

	bool SetPropertyFromText(UObject* Object, const FString& PropertyName, const FString& Value)
	{
		if (!Object)
		{
			return false;
		}
		FProperty* Prop = FindPropertyByFlexibleName(Object->GetClass(), PropertyName);
		if (!Prop)
		{
			return false;
		}
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Object);
		if (FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Prop))
		{
			UObject* LoadedObject = LoadObject<UObject>(nullptr, *Value);
			if (!LoadedObject && !Value.IsEmpty() && !Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
			{
				return false;
			}
			ObjectProp->SetObjectPropertyValue(ValuePtr, LoadedObject);
			return true;
		}
		return Prop->ImportText_Direct(*Value, ValuePtr, Object, PPF_None) != nullptr;
	}

	bool SetPropertiesFromText(UObject* Object, const TMap<FString, FString>& Properties)
	{
		for (const TPair<FString, FString>& Pair : Properties)
		{
			if (!SetPropertyFromText(Object, Pair.Key, Pair.Value))
			{
				UE_LOG(LogTemp, Warning, TEXT("UnrealBridge UMG: failed to set %s on %s"), *Pair.Key, *GetNameSafe(Object));
				return false;
			}
		}
		return true;
	}

	void MarkWidgetBlueprintModified(UWidgetBlueprint* WBP, bool bStructural)
	{
		if (!WBP)
		{
			return;
		}
		WBP->Modify();
		if (bStructural)
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WBP);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(WBP);
		}
		WBP->MarkPackageDirty();
	}

	bool AddWidgetToParent(UWidgetTree* WidgetTree, UWidget* ParentWidget, UWidget* ChildWidget, int32 Index)
	{
		if (!WidgetTree || !ChildWidget)
		{
			return false;
		}
		if (!ParentWidget)
		{
			if (WidgetTree->RootWidget && WidgetTree->RootWidget != ChildWidget)
			{
				return false;
			}
			WidgetTree->Modify();
			WidgetTree->RootWidget = ChildWidget;
			return true;
		}
		UPanelWidget* PanelParent = Cast<UPanelWidget>(ParentWidget);
		if (!PanelParent || !PanelParent->CanAddMoreChildren())
		{
			return false;
		}
		PanelParent->Modify();
		UPanelSlot* Slot = (Index >= 0 && Index <= PanelParent->GetChildrenCount())
			? PanelParent->InsertChildAt(Index, ChildWidget)
			: PanelParent->AddChild(ChildWidget);
		return Slot != nullptr;
	}

	bool RemoveFromCurrentParent(UWidgetTree* WidgetTree, UWidget* Widget)
	{
		if (!WidgetTree || !Widget)
		{
			return false;
		}
		if (WidgetTree->RootWidget == Widget)
		{
			WidgetTree->Modify();
			WidgetTree->RootWidget = nullptr;
			return true;
		}
		if (Widget->Slot && Widget->Slot->Parent)
		{
			Widget->Slot->Parent->Modify();
			return Widget->Slot->Parent->RemoveChild(Widget);
		}
		int32 ChildIndex = INDEX_NONE;
		if (UPanelWidget* Parent = UWidgetTree::FindWidgetParent(Widget, ChildIndex))
		{
			Parent->Modify();
			return Parent->RemoveChild(Widget);
		}
		return false;
	}

	bool IsDescendantOf(UWidget* CandidateDescendant, UWidget* CandidateAncestor)
	{
		if (!CandidateDescendant || !CandidateAncestor)
		{
			return false;
		}
		if (UPanelWidget* Panel = Cast<UPanelWidget>(CandidateAncestor))
		{
			for (int32 Index = 0; Index < Panel->GetChildrenCount(); ++Index)
			{
				UWidget* Child = Panel->GetChildAt(Index);
				if (Child == CandidateDescendant || IsDescendantOf(CandidateDescendant, Child))
				{
					return true;
				}
			}
		}
		return false;
	}

	UWidgetAnimation* FindAnimationByName(UWidgetBlueprint* WBP, const FString& AnimationName)
	{
		if (!WBP)
		{
			return nullptr;
		}
		for (UWidgetAnimation* Animation : WBP->Animations)
		{
			if (!Animation)
			{
				continue;
			}
			if (Animation->GetName().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Animation;
			}
#if WITH_EDITOR
			if (Animation->GetDisplayLabel().Equals(AnimationName, ESearchCase::IgnoreCase))
			{
				return Animation;
			}
#endif
		}
		return nullptr;
	}

	FWidgetAnimationBinding* EnsureAnimationBinding(UWidgetAnimation* Animation, UMovieScene* MovieScene, UWidgetTree* WidgetTree, UWidget* Widget)
	{
		if (!Animation || !MovieScene || !Widget)
		{
			return nullptr;
		}
		for (FWidgetAnimationBinding& Binding : Animation->AnimationBindings)
		{
			if (Binding.WidgetName == Widget->GetFName())
			{
				return &Binding;
			}
		}
		const FGuid BindingGuid = MovieScene->AddPossessable(Widget->GetName(), Widget->GetClass());
		FWidgetAnimationBinding NewBinding;
		NewBinding.WidgetName = Widget->GetFName();
		NewBinding.AnimationGuid = BindingGuid;
		NewBinding.bIsRootWidget = WidgetTree && WidgetTree->RootWidget == Widget;
		Animation->AnimationBindings.Add(NewBinding);
		return &Animation->AnimationBindings.Last();
	}

	FFrameNumber SecondsToFrame(const UMovieScene* MovieScene, float TimeSeconds)
	{
		const FFrameRate TickResolution = MovieScene ? MovieScene->GetTickResolution() : FFrameRate(30, 1);
		return (TimeSeconds * TickResolution).RoundToFrame();
	}
}

// ─── GetWidgetTree ──────────────────────────────────────────

TArray<FBridgeWidgetInfo> UUnrealBridgeUMGLibrary::GetWidgetTree(const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	if (WBP->WidgetTree && WBP->WidgetTree->RootWidget)
	{
		BridgeUMGImpl::GatherWidgets(WBP->WidgetTree->RootWidget, TEXT(""), Result);
	}

	return Result;
}

TArray<FBridgeRuntimeWidgetInfo> UUnrealBridgeUMGLibrary::GetRuntimeWidgetTree(
	const FString& UserWidgetClassFilter,
	const FString& InstanceNameFilter)
{
	TArray<FBridgeRuntimeWidgetInfo> Result;
	TSet<UWidget*> Seen;

	for (int32 RootPass = 0; RootPass < 2; ++RootPass)
	{
		for (TObjectIterator<UUserWidget> It; It; ++It)
		{
			UUserWidget* UserWidget = *It;
			if (!IsValid(UserWidget)
				|| UserWidget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
				|| Seen.Contains(UserWidget))
			{
				continue;
			}

			UWorld* World = UserWidget->GetWorld();
			if (!World || (World->WorldType != EWorldType::PIE
				&& World->WorldType != EWorldType::Game
				&& World->WorldType != EWorldType::GamePreview
				&& World->WorldType != EWorldType::EditorPreview))
			{
				continue;
			}
			const bool bRootInstance = UserWidget->GetParent() == nullptr;
			if ((RootPass == 0) != bRootInstance)
			{
				continue;
			}
			const FString ClassPath = UserWidget->GetClass()->GetPathName();
			if (!UserWidgetClassFilter.IsEmpty()
				&& !ClassPath.Contains(UserWidgetClassFilter, ESearchCase::IgnoreCase)
				&& !UserWidget->GetClass()->GetName().Contains(UserWidgetClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
			if (!InstanceNameFilter.IsEmpty()
				&& !UserWidget->GetName().Contains(InstanceNameFilter, ESearchCase::IgnoreCase)
				&& !UserWidget->GetPathName().Contains(InstanceNameFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}

			const FString SemanticRoot = UserWidget->GetName();
			const FString ParentPath = UserWidget->GetParent()
				? UserWidget->GetParent()->GetPathName() : FString();
			BridgeUMGImpl::GatherRuntimeWidgets(
				UserWidget, UserWidget, SemanticRoot, ParentPath, 0, Seen, Result);
			if (UserWidget->WidgetTree && UserWidget->WidgetTree->RootWidget)
			{
				UWidget* RootWidget = UserWidget->WidgetTree->RootWidget;
				BridgeUMGImpl::GatherRuntimeWidgets(
					UserWidget,
					RootWidget,
					SemanticRoot + TEXT("/") + RootWidget->GetName(),
					UserWidget->GetPathName(),
					1,
					Seen,
					Result);
			}
		}
	}

	return Result;
}

FBridgeRuntimeWidgetInfo UUnrealBridgeUMGLibrary::GetRuntimeWidgetState(
	const FString& WidgetObjectPath)
{
	for (const FBridgeRuntimeWidgetInfo& Info : GetRuntimeWidgetTree(TEXT(""), TEXT("")))
	{
		if (Info.WidgetObjectPath == WidgetObjectPath)
		{
			return Info;
		}
	}

	FBridgeRuntimeWidgetInfo Result;
	Result.WidgetObjectPath = WidgetObjectPath;
	UWidget* Widget = Cast<UWidget>(StaticFindObject(UWidget::StaticClass(), nullptr, *WidgetObjectPath));
	if (!Widget || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		Result.Error = FString::Printf(TEXT("Live widget '%s' was not found"), *WidgetObjectPath);
		return Result;
	}
	UUserWidget* Owner = Cast<UUserWidget>(Widget);
	if (!Owner)
	{
		Owner = Widget->GetTypedOuter<UUserWidget>();
	}
	const FString ParentPath = Widget->GetParent()
		? Widget->GetParent()->GetPathName() : FString();
	return BridgeUMGImpl::BuildRuntimeWidgetInfo(
		Owner, Widget, Widget->GetName(), ParentPath, 0);
}

// ─── CreateWidgetBlueprint ─────────────────────────────────

FString UUnrealBridgeUMGLibrary::CreateWidgetBlueprint(
	const FString& Path,
	const FString& Name,
	const FString& ParentClass,
	const FString& RootClass,
	const FString& RootName,
	bool bCompile,
	bool bSave)
{
	if (Name.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealBridge UMG: CreateWidgetBlueprint requires Name"));
		return FString();
	}

	const FString PackagePath = Path.IsEmpty() ? TEXT("/Game") : Path;
	const FString PackageName = PackagePath / Name;
	if (LoadObject<UWidgetBlueprint>(nullptr, *BridgeUMGImpl::NormalizeAssetPath(PackageName)))
	{
		return PackageName;
	}

	UClass* ResolvedParent = UUserWidget::StaticClass();
	if (!ParentClass.IsEmpty())
	{
		ResolvedParent = BridgeUMGImpl::ResolveWidgetClass(ParentClass, UUserWidget::StaticClass());
		if (!ResolvedParent)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealBridge UMG: unable to resolve parent class %s"), *ParentClass);
			return FString();
		}
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FString();
	}
	Package->FullyLoad();

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		ResolvedParent,
		Package,
		FName(*Name),
		BPTYPE_Normal,
		UWidgetBlueprint::StaticClass(),
		UWidgetBlueprintGeneratedClass::StaticClass(),
		NAME_None);

	UWidgetBlueprint* WBP = Cast<UWidgetBlueprint>(Blueprint);
	if (!WBP)
	{
		return FString();
	}

	FAssetRegistryModule::AssetCreated(WBP);
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	if (!WidgetTree)
	{
		return FString();
	}

	if (!RootClass.Equals(TEXT("none"), ESearchCase::IgnoreCase))
	{
		const FString EffectiveRootClass = RootClass.IsEmpty() ? TEXT("CanvasPanel") : RootClass;
		const FString EffectiveRootName = RootName.IsEmpty() ? TEXT("RootCanvas") : RootName;
		UClass* ResolvedRootClass = BridgeUMGImpl::ResolveWidgetClass(EffectiveRootClass, UWidget::StaticClass());
		if (!ResolvedRootClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("UnrealBridge UMG: unable to resolve root class %s"), *EffectiveRootClass);
			return FString();
		}
		UWidget* RootWidget = WidgetTree->ConstructWidget<UWidget>(ResolvedRootClass, FName(*EffectiveRootName));
		WidgetTree->RootWidget = RootWidget;
	}

	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	if (bCompile)
	{
		FKismetEditorUtilities::CompileBlueprint(WBP);
	}
	if (bSave)
	{
		UEditorAssetLibrary::SaveAsset(PackageName, false);
	}
	return PackageName;
}

// ─── GetWidgetProperties ────────────────────────────────────

TArray<FBridgeWidgetPropertyValue> UUnrealBridgeUMGLibrary::GetWidgetProperties(
	const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	TArray<FBridgeWidgetPropertyValue> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget) return Result;

	UObject* CDO = Widget->GetClass()->GetDefaultObject();
	if (!CDO) return Result;

	for (TFieldIterator<FProperty> It(Widget->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop) continue;

		// Skip internal/transient properties
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient | CPF_Deprecated))
			continue;

		void* WidgetValue = Prop->ContainerPtrToValuePtr<void>(Widget);
		void* CDOValue = Prop->ContainerPtrToValuePtr<void>(CDO);

		if (!Prop->Identical(WidgetValue, CDOValue))
		{
			FBridgeWidgetPropertyValue PV;
			PV.Name = Prop->GetName();
			PV.Type = BridgeUMGImpl::PropertyTypeToString(Prop);

			FString ExportedValue;
			Prop->ExportTextItem_Direct(ExportedValue, WidgetValue, CDOValue, Widget, PPF_None);
			PV.Value = ExportedValue;

			Result.Add(PV);
		}
	}

	return Result;
}

// ─── GetWidgetAnimations ────────────────────────────────────

TArray<FBridgeWidgetAnimationInfo> UUnrealBridgeUMGLibrary::GetWidgetAnimations(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetAnimationInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	for (UWidgetAnimation* Anim : WBP->Animations)
	{
		if (!Anim) continue;

		FBridgeWidgetAnimationInfo Info;
#if WITH_EDITOR
		Info.Name = Anim->GetDisplayLabel();
		if (Info.Name.IsEmpty())
#endif
		{
			Info.Name = Anim->GetName();
		}

		Info.Duration = Anim->GetEndTime() - Anim->GetStartTime();

		UMovieScene* Scene = Anim->GetMovieScene();
		if (Scene)
		{
			// Build Guid -> widget name map from animation bindings
			TMap<FGuid, FString> GuidToWidget;
			for (const FWidgetAnimationBinding& Binding : Anim->AnimationBindings)
			{
				GuidToWidget.Add(Binding.AnimationGuid, Binding.WidgetName.ToString());
			}

			const TArray<FMovieSceneBinding>& Bindings = const_cast<const UMovieScene*>(Scene)->GetBindings();
			for (const FMovieSceneBinding& Binding : Bindings)
			{
				FString TargetWidget;
				// Find possessable to get bound widget name
				FMovieScenePossessable* Possessable = Scene->FindPossessable(Binding.GetObjectGuid());
				if (Possessable)
				{
					TargetWidget = Possessable->GetName();
				}
				// Fallback: look up via animation binding guid
				if (TargetWidget.IsEmpty())
				{
					if (FString* Found = GuidToWidget.Find(Binding.GetObjectGuid()))
					{
						TargetWidget = *Found;
					}
				}

				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (!Track) continue;

					FBridgeWidgetAnimTrack TrackInfo;
					TrackInfo.WidgetName = TargetWidget;
					TrackInfo.TrackType = Track->GetClass()->GetName();
					TrackInfo.DisplayName = Track->GetDisplayName().ToString();
					Info.Tracks.Add(TrackInfo);
				}
			}

			// Master/global tracks (not bound to a widget)
			for (UMovieSceneTrack* Track : Scene->GetTracks())
			{
				if (!Track) continue;

				FBridgeWidgetAnimTrack TrackInfo;
				TrackInfo.TrackType = Track->GetClass()->GetName();
				TrackInfo.DisplayName = Track->GetDisplayName().ToString();
				Info.Tracks.Add(TrackInfo);
			}
		}

		Result.Add(Info);
	}

	return Result;
}

// ─── GetWidgetBindings ──────────────────────────────────────

TArray<FBridgeWidgetBindingInfo> UUnrealBridgeUMGLibrary::GetWidgetBindings(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetBindingInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	for (const FDelegateEditorBinding& Binding : WBP->Bindings)
	{
		FBridgeWidgetBindingInfo Info;
		Info.WidgetName = Binding.ObjectName;
		Info.PropertyName = Binding.PropertyName.ToString();
		Info.FunctionName = Binding.FunctionName.ToString();

		if (Info.FunctionName.IsEmpty() && !Binding.SourceProperty.IsNone())
		{
			Info.FunctionName = Binding.SourceProperty.ToString();
		}

		Info.Kind = (Binding.Kind == EBindingKind::Function) ? TEXT("Function") : TEXT("Property");

		Result.Add(Info);
	}

	return Result;
}

// ─── GetWidgetEvents ────────────────────────────────────────

TArray<FBridgeWidgetEventInfo> UUnrealBridgeUMGLibrary::GetWidgetEvents(
	const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetEventInfo> Result;

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return Result;

	// Scan event graph for component bound event nodes (OnClicked, etc.)
	for (UEdGraph* Graph : WBP->UbergraphPages)
	{
		if (!Graph) continue;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_ComponentBoundEvent* EventNode = Cast<UK2Node_ComponentBoundEvent>(Node))
			{
				FBridgeWidgetEventInfo Info;
				Info.WidgetName = EventNode->ComponentPropertyName.ToString();
				Info.EventName = EventNode->DelegatePropertyName.ToString();
				Info.HandlerName = EventNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
				Result.Add(Info);
			}
		}
	}

	return Result;
}

// ─── SearchWidgets ──────────────────────────────────────────

TArray<FBridgeWidgetInfo> UUnrealBridgeUMGLibrary::SearchWidgets(
	const FString& WidgetBlueprintPath, const FString& Query)
{
	TArray<FBridgeWidgetInfo> All = GetWidgetTree(WidgetBlueprintPath);
	TArray<FBridgeWidgetInfo> Result;

	FString Q = Query.ToLower();
	for (const FBridgeWidgetInfo& W : All)
	{
		if (W.Name.ToLower().Contains(Q) || W.WidgetClass.ToLower().Contains(Q))
		{
			Result.Add(W);
		}
	}

	return Result;
}

// ─── Widget Tree Editing ────────────────────────────────────

bool UUnrealBridgeUMGLibrary::AddWidget(
	const FString& WidgetBlueprintPath,
	const FString& ClassName,
	const FString& WidgetName,
	const FString& ParentWidgetName,
	bool bIsVariable,
	const TMap<FString, FString>& Properties,
	const TMap<FString, FString>& SlotProperties,
	int32 Index)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	if (!WidgetTree) return false;

	UClass* WidgetClass = BridgeUMGImpl::ResolveWidgetClass(ClassName);
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("UnrealBridge UMG: unable to resolve widget class %s"), *ClassName);
		return false;
	}

	const FName NewWidgetName = WidgetName.IsEmpty() ? NAME_None : FName(*WidgetName);
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, NewWidgetName);
	if (!NewWidget)
	{
		return false;
	}
	NewWidget->bIsVariable = bIsVariable;

	if (!BridgeUMGImpl::SetPropertiesFromText(NewWidget, Properties))
	{
		return false;
	}

	UWidget* ParentWidget = ParentWidgetName.IsEmpty() ? nullptr : BridgeUMGImpl::FindWidgetByName(WBP, ParentWidgetName);
	if (!ParentWidgetName.IsEmpty() && !ParentWidget)
	{
		return false;
	}
	if (!BridgeUMGImpl::AddWidgetToParent(WidgetTree, ParentWidget, NewWidget, Index))
	{
		return false;
	}
	if (NewWidget->Slot && !BridgeUMGImpl::SetPropertiesFromText(NewWidget->Slot, SlotProperties))
	{
		return false;
	}

	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::RemoveWidget(const FString& WidgetBlueprintPath, const FString& WidgetName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	if (!WidgetTree) return false;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget)
	{
		return false;
	}
	if (!BridgeUMGImpl::RemoveFromCurrentParent(WidgetTree, Widget))
	{
		return false;
	}
	Widget->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::RenameWidget(const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& NewName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP || NewName.IsEmpty()) return false;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget)
	{
		return false;
	}
	if (BridgeUMGImpl::FindWidgetByName(WBP, NewName))
	{
		return false;
	}
	Widget->Modify();
	Widget->Rename(*NewName, Widget->GetOuter(), REN_DontCreateRedirectors);
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::ReparentWidget(
	const FString& WidgetBlueprintPath,
	const FString& WidgetName,
	const FString& NewParentWidgetName,
	const TMap<FString, FString>& SlotProperties,
	int32 Index)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	if (!WidgetTree) return false;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	UWidget* NewParent = NewParentWidgetName.IsEmpty() ? nullptr : BridgeUMGImpl::FindWidgetByName(WBP, NewParentWidgetName);
	if (!Widget || (!NewParentWidgetName.IsEmpty() && !NewParent))
	{
		return false;
	}
	if (NewParent && BridgeUMGImpl::IsDescendantOf(NewParent, Widget))
	{
		return false;
	}
	if (!BridgeUMGImpl::RemoveFromCurrentParent(WidgetTree, Widget))
	{
		return false;
	}
	if (!BridgeUMGImpl::AddWidgetToParent(WidgetTree, NewParent, Widget, Index))
	{
		return false;
	}
	if (Widget->Slot && !BridgeUMGImpl::SetPropertiesFromText(Widget->Slot, SlotProperties))
	{
		return false;
	}
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return true;
}

// ─── SetWidgetProperty ──────────────────────────────────────

bool UUnrealBridgeUMGLibrary::SetWidgetProperty(
	const FString& WidgetBlueprintPath, const FString& WidgetName,
	const FString& PropertyName, const FString& Value)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;

	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget) return false;

	if (!BridgeUMGImpl::SetPropertyFromText(Widget, PropertyName, Value))
	{
		return false;
	}

	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, false);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetWidgetProperties(
	const FString& WidgetBlueprintPath,
	const FString& WidgetName,
	const TMap<FString, FString>& Properties)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget) return false;

	if (!BridgeUMGImpl::SetPropertiesFromText(Widget, Properties))
	{
		return false;
	}
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, false);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetWidgetSlotProperties(
	const FString& WidgetBlueprintPath,
	const FString& WidgetName,
	const TMap<FString, FString>& SlotProperties)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget || !Widget->Slot) return false;

	if (!BridgeUMGImpl::SetPropertiesFromText(Widget->Slot, SlotProperties))
	{
		return false;
	}
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, false);
	return true;
}

bool UUnrealBridgeUMGLibrary::SetWidgetCommonUIStyle(
	const FString& WidgetBlueprintPath,
	const FString& WidgetName,
	const FString& StylePath,
	const FString& StyleProperty)
{
	return SetWidgetProperty(WidgetBlueprintPath, WidgetName, StyleProperty, StylePath);
}

FString UUnrealBridgeUMGLibrary::BindWidgetEvent(
	const FString& WidgetBlueprintPath,
	const FString& WidgetName,
	const FString& EventName,
	const FString& FunctionName,
	int32 PosX,
	int32 PosY)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return FString();
	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!Widget) return FString();

	FObjectProperty* ComponentProperty = FindFProperty<FObjectProperty>(WBP->SkeletonGeneratedClass, Widget->GetFName());
	if (!ComponentProperty)
	{
		ComponentProperty = FindFProperty<FObjectProperty>(WBP->GeneratedClass, Widget->GetFName());
	}
	if (!ComponentProperty)
	{
		return FString();
	}
	FMulticastDelegateProperty* DelegateProperty = FindFProperty<FMulticastDelegateProperty>(Widget->GetClass(), FName(*EventName));
	if (!DelegateProperty)
	{
		return FString();
	}

	UEdGraph* Graph = WBP->UbergraphPages.Num() > 0 ? WBP->UbergraphPages[0] : nullptr;
	if (!Graph)
	{
		Graph = FBlueprintEditorUtils::CreateNewGraph(WBP, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		if (Graph)
		{
			FBlueprintEditorUtils::AddUbergraphPage(WBP, Graph);
		}
	}
	if (!Graph)
	{
		return FString();
	}

	for (UEdGraphNode* ExistingNode : Graph->Nodes)
	{
		UK2Node_ComponentBoundEvent* ExistingEvent = Cast<UK2Node_ComponentBoundEvent>(ExistingNode);
		if (ExistingEvent &&
			ExistingEvent->ComponentPropertyName == Widget->GetFName() &&
			ExistingEvent->DelegatePropertyName == FName(*EventName))
		{
			return ExistingEvent->NodeGuid.ToString();
		}
	}

	const FString EffectiveFunctionName = FunctionName.IsEmpty()
		? FString::Printf(TEXT("%s_%s"), *WidgetName, *EventName)
		: FunctionName;

	Graph->Modify();
	UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
	EventNode->InitializeComponentBoundEventParams(ComponentProperty, DelegateProperty);
	EventNode->CustomFunctionName = FName(*EffectiveFunctionName);
	EventNode->NodePosX = PosX;
	EventNode->NodePosY = PosY;
	Graph->AddNode(EventNode, true, false);
	EventNode->CreateNewGuid();
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return EventNode->NodeGuid.ToString();
}

FString UUnrealBridgeUMGLibrary::CreateWidgetAnimation(const FString& WidgetBlueprintPath, const FString& AnimationName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP || AnimationName.IsEmpty()) return FString();

	if (UWidgetAnimation* Existing = BridgeUMGImpl::FindAnimationByName(WBP, AnimationName))
	{
		return Existing->GetName();
	}

	UWidgetAnimation* Animation = NewObject<UWidgetAnimation>(WBP, FName(*AnimationName), RF_Transactional);
	if (!Animation)
	{
		return FString();
	}
	Animation->MovieScene = NewObject<UMovieScene>(Animation, TEXT("MovieScene"), RF_Transactional);
#if WITH_EDITOR
	Animation->SetDisplayLabel(AnimationName);
#endif
	if (Animation->MovieScene)
	{
		Animation->MovieScene->SetPlaybackRange(0, 150);
	}
	WBP->Animations.Add(Animation);
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return Animation->GetName();
}

FString UUnrealBridgeUMGLibrary::AddWidgetAnimationBinding(
	const FString& WidgetBlueprintPath,
	const FString& AnimationName,
	const FString& WidgetName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return FString();
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	UWidgetAnimation* Animation = BridgeUMGImpl::FindAnimationByName(WBP, AnimationName);
	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!WidgetTree || !Animation || !Widget)
	{
		return FString();
	}
	if (!Animation->MovieScene)
	{
		Animation->MovieScene = NewObject<UMovieScene>(Animation, TEXT("MovieScene"), RF_Transactional);
	}
	FWidgetAnimationBinding* Binding = BridgeUMGImpl::EnsureAnimationBinding(Animation, Animation->MovieScene, WidgetTree, Widget);
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return Binding ? Binding->AnimationGuid.ToString(EGuidFormats::DigitsWithHyphens) : FString();
}

bool UUnrealBridgeUMGLibrary::AddWidgetAnimationFloatKey(
	const FString& WidgetBlueprintPath,
	const FString& AnimationName,
	const FString& WidgetName,
	const FString& PropertyPath,
	float TimeSeconds,
	float Value)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	UWidgetAnimation* Animation = BridgeUMGImpl::FindAnimationByName(WBP, AnimationName);
	UWidget* Widget = BridgeUMGImpl::FindWidgetByName(WBP, WidgetName);
	if (!WidgetTree || !Animation || !Widget || PropertyPath.IsEmpty())
	{
		return false;
	}
	if (!Animation->MovieScene)
	{
		Animation->MovieScene = NewObject<UMovieScene>(Animation, TEXT("MovieScene"), RF_Transactional);
	}

	FString LeafPropertyName = PropertyPath;
	if (PropertyPath.Contains(TEXT(".")))
	{
		PropertyPath.Split(TEXT("."), nullptr, &LeafPropertyName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	}
	FProperty* Property = BridgeUMGImpl::FindPropertyByFlexibleName(Widget->GetClass(), LeafPropertyName);
	if (!Property || !CastField<FFloatProperty>(Property))
	{
		return false;
	}

	FWidgetAnimationBinding* Binding = BridgeUMGImpl::EnsureAnimationBinding(Animation, Animation->MovieScene, WidgetTree, Widget);
	if (!Binding)
	{
		return false;
	}

	UMovieSceneFloatTrack* Track = Cast<UMovieSceneFloatTrack>(
		Animation->MovieScene->FindTrack(UMovieSceneFloatTrack::StaticClass(), Binding->AnimationGuid, FName(*PropertyPath)));
	if (!Track)
	{
		Track = Animation->MovieScene->AddTrack<UMovieSceneFloatTrack>(Binding->AnimationGuid);
		Track->SetPropertyNameAndPath(Property->GetFName(), PropertyPath);
	}

	UMovieSceneFloatSection* Section = nullptr;
	if (Track->GetAllSections().Num() > 0)
	{
		Section = Cast<UMovieSceneFloatSection>(Track->GetAllSections()[0]);
	}
	if (!Section)
	{
		Section = Cast<UMovieSceneFloatSection>(Track->CreateNewSection());
		Track->AddSection(*Section);
	}

	const FFrameNumber Frame = BridgeUMGImpl::SecondsToFrame(Animation->MovieScene, TimeSeconds);
	Section->GetChannel().GetData().AddKey(Frame, FMovieSceneFloatValue(Value));
	Section->SetRange(TRange<FFrameNumber>::Inclusive(Frame, Frame));
	Animation->MovieScene->SetPlaybackRange(0, FMath::Max(1, Frame.Value + 1));

	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return true;
}

bool UUnrealBridgeUMGLibrary::RemoveWidgetAnimation(const FString& WidgetBlueprintPath, const FString& AnimationName)
{
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP) return false;
	UWidgetAnimation* Animation = BridgeUMGImpl::FindAnimationByName(WBP, AnimationName);
	if (!Animation)
	{
		return false;
	}
	WBP->Animations.Remove(Animation);
	Animation->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors);
	BridgeUMGImpl::MarkWidgetBlueprintModified(WBP, true);
	return true;
}

FBridgeWidgetPreviewResult UUnrealBridgeUMGLibrary::PreviewWidget(
	const FString& WidgetBlueprintPath,
	int32 Width,
	int32 Height,
	const FString& OutputPath)
{
	FBridgeWidgetPreviewResult Result;
	Result.Width = FMath::Max(1, Width);
	Result.Height = FMath::Max(1, Height);

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP)
	{
		Result.Error = TEXT("Widget Blueprint not found");
		return Result;
	}

	FString EffectiveOutputPath = OutputPath;
	if (EffectiveOutputPath.IsEmpty())
	{
		EffectiveOutputPath = FPaths::ProjectSavedDir() / TEXT("UnrealBridgePreviews") / (WBP->GetName() + TEXT(".png"));
	}
	Result.OutputPath = EffectiveOutputPath;

	FWidgetBlueprintEditorUtils::FCreateWidgetFromBlueprintParams CreateParams;
	CreateParams.FlagsToApply = EWidgetDesignFlags::Designing | EWidgetDesignFlags::Previewing | EWidgetDesignFlags::ExecutePreConstruct;
	UUserWidget* PreviewUserWidget = FWidgetBlueprintEditorUtils::CreateUserWidgetFromBlueprint(GetTransientPackage(), WBP, CreateParams);
	if (!PreviewUserWidget)
	{
		Result.Error = TEXT("Failed to create preview widget");
		return Result;
	}

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(GetTransientPackage(), TEXT("UnrealBridgeWidgetPreviewRenderTarget"), RF_Transient);
	RenderTarget->ClearColor = FLinearColor::Transparent;
	RenderTarget->InitCustomFormat(Result.Width, Result.Height, PF_B8G8R8A8, true);
	RenderTarget->UpdateResourceImmediate(true);

	TOptional<FWidgetBlueprintEditorUtils::FWidgetThumbnailProperties> DrawResult =
		FWidgetBlueprintEditorUtils::DrawSWidgetInRenderTarget(PreviewUserWidget, RenderTarget);
	if (!DrawResult.IsSet())
	{
		FWidgetBlueprintEditorUtils::DestroyUserWidget(PreviewUserWidget);
		Result.Error = TEXT("Failed to draw widget into render target");
		return Result;
	}

	FBufferArchive Buffer;
	if (!FImageUtils::ExportRenderTarget2DAsPNG(RenderTarget, Buffer))
	{
		FWidgetBlueprintEditorUtils::DestroyUserWidget(PreviewUserWidget);
		Result.Error = TEXT("Failed to encode preview PNG");
		return Result;
	}
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(EffectiveOutputPath), true);
	if (!FFileHelper::SaveArrayToFile(Buffer, *EffectiveOutputPath))
	{
		FWidgetBlueprintEditorUtils::DestroyUserWidget(PreviewUserWidget);
		Result.Error = TEXT("Failed to write preview PNG");
		return Result;
	}

	FWidgetBlueprintEditorUtils::DestroyUserWidget(PreviewUserWidget);
	Result.Bytes = Buffer.Num();
	Result.bSuccess = true;
	return Result;
}

FBridgeWidgetCompileResult UUnrealBridgeUMGLibrary::CompileSaveWidgetBlueprint(
	const FString& WidgetBlueprintPath,
	bool bCompile,
	bool bSave,
	bool bValidate)
{
	FBridgeWidgetCompileResult Result;
	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP)
	{
		Result.Status = TEXT("NotFound");
		return Result;
	}

	Result.AssetPath = WBP->GetOutermost()->GetName();
	if (bCompile)
	{
		FKismetEditorUtilities::CompileBlueprint(WBP);
		Result.bCompiled = true;
	}
	if (bSave)
	{
		Result.bSaved = UEditorAssetLibrary::SaveAsset(Result.AssetPath, false);
	}
	Result.Status = BridgeUMGImpl::BlueprintStatusToString(WBP);
	if (bValidate)
	{
		Result.ValidationIssues = ValidateWidgetBlueprint(WidgetBlueprintPath);
	}
	return Result;
}

TArray<FBridgeWidgetValidationIssue> UUnrealBridgeUMGLibrary::ValidateWidgetBlueprint(const FString& WidgetBlueprintPath)
{
	TArray<FBridgeWidgetValidationIssue> Issues;
	auto AddIssue = [&Issues](const FString& Severity, const FString& Message)
	{
		FBridgeWidgetValidationIssue Issue;
		Issue.Severity = Severity;
		Issue.Message = Message;
		Issues.Add(Issue);
	};

	UWidgetBlueprint* WBP = BridgeUMGImpl::LoadWBP(WidgetBlueprintPath);
	if (!WBP)
	{
		AddIssue(TEXT("error"), TEXT("Widget Blueprint not found"));
		return Issues;
	}
	UWidgetTree* WidgetTree = BridgeUMGImpl::EnsureWidgetTree(WBP);
	if (!WidgetTree || !WidgetTree->RootWidget)
	{
		AddIssue(TEXT("error"), TEXT("WidgetTree has no root widget"));
		return Issues;
	}

	TArray<UWidget*> Widgets;
	WidgetTree->GetAllWidgets(Widgets);
	TSet<FName> Names;
	for (UWidget* Widget : Widgets)
	{
		if (!Widget)
		{
			AddIssue(TEXT("warning"), TEXT("WidgetTree contains a null widget"));
			continue;
		}
		if (Names.Contains(Widget->GetFName()))
		{
			AddIssue(TEXT("error"), FString::Printf(TEXT("Duplicate widget name: %s"), *Widget->GetName()));
		}
		Names.Add(Widget->GetFName());
		if (Widget != WidgetTree->RootWidget && !Widget->Slot)
		{
			AddIssue(TEXT("warning"), FString::Printf(TEXT("Widget has no slot: %s"), *Widget->GetName()));
		}
	}
	return Issues;
}
