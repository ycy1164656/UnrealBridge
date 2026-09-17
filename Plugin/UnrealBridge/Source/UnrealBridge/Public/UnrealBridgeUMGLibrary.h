#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeUMGLibrary.generated.h"

/** Describes a single widget in a Widget Blueprint hierarchy. */
USTRUCT(BlueprintType)
struct FBridgeWidgetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Name;

	/** Widget class, e.g. "CanvasPanel", "TextBlock", "Button" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetClass;

	/** Parent widget name (empty for root) */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString ParentName;

	/** Slot class if parented, e.g. "CanvasPanelSlot", "OverlaySlot" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString SlotType;

	/** Whether this widget is exposed as a variable in the Blueprint */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bIsVariable = false;

	/** Visibility setting, e.g. "Visible", "Collapsed", "Hidden" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Visibility;
};

/** Non-default property value on a widget. */
USTRUCT(BlueprintType)
struct FBridgeWidgetPropertyValue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Type;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Value;
};

/** A track within a widget animation. */
USTRUCT(BlueprintType)
struct FBridgeWidgetAnimTrack
{
	GENERATED_BODY()

	/** The widget targeted by this track */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetName;

	/** Track type, e.g. "Color", "Transform", "Visibility" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString TrackType;

	/** Display name of the track */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString DisplayName;
};

/** Describes a widget animation. */
USTRUCT(BlueprintType)
struct FBridgeWidgetAnimationInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	float Duration = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	TArray<FBridgeWidgetAnimTrack> Tracks;
};

/** A property binding on a widget. */
USTRUCT(BlueprintType)
struct FBridgeWidgetBindingInfo
{
	GENERATED_BODY()

	/** The widget this binding is on */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetName;

	/** The property being bound, e.g. "Text", "Visibility", "ColorAndOpacity" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString PropertyName;

	/** The function providing the value */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString FunctionName;

	/** "Function" or "Property" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Kind;
};

/** An event binding on a widget (OnClicked, OnHovered, etc.). */
USTRUCT(BlueprintType)
struct FBridgeWidgetEventInfo
{
	GENERATED_BODY()

	/** The widget this event is on */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString WidgetName;

	/** Event name, e.g. "OnClicked", "OnHovered" */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString EventName;

	/** Bound function or node description */
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString HandlerName;
};

/** Validation issue found in a Widget Blueprint tree. */
USTRUCT(BlueprintType)
struct FBridgeWidgetValidationIssue
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Severity;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Message;
};

/** Result from compiling and/or saving a Widget Blueprint. */
USTRUCT(BlueprintType)
struct FBridgeWidgetCompileResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString AssetPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Status;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bCompiled = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bSaved = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	TArray<FBridgeWidgetValidationIssue> ValidationIssues;
};

/** Result from rendering a Widget Blueprint preview to disk. */
USTRUCT(BlueprintType)
struct FBridgeWidgetPreviewResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString OutputPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	int32 Height = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	int32 Bytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG")
	FString Error;
};

/** Live UMG/Slate state for one widget instance in PIE or a game world. */
USTRUCT(BlueprintType)
struct FBridgeRuntimeWidgetInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString WidgetObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString SemanticPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString WidgetClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString OwnerUserWidgetPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString OwnerUserWidgetClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString ParentObjectPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString WorldPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString WorldType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString Visibility;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString SlateType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString SlateAddress;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") int32 Depth = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bEnabled = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bInViewport = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bHasKeyboardFocus = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bHasUserFocus = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") bool bHovered = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FVector2D AbsolutePosition = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FVector2D AbsoluteSize = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FVector2D LocalSize = FVector2D::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|UMG|Runtime") FString Error;
};

/**
 * UMG / Widget Blueprint introspection via UnrealBridge.
 */
UCLASS()
class UNREALBRIDGE_API UUnrealBridgeUMGLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Get the widget hierarchy of a Widget Blueprint.
	 * Returns a flat list with parent references to reconstruct the tree.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetInfo> GetWidgetTree(const FString& WidgetBlueprintPath);

	/** Enumerate live UUserWidget instances and their runtime widget trees. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeRuntimeWidgetInfo> GetRuntimeWidgetTree(
		const FString& UserWidgetClassFilter = TEXT(""),
		const FString& InstanceNameFilter = TEXT(""));

	/** Read one live widget by the exact WidgetObjectPath returned above. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG|Runtime", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeRuntimeWidgetInfo GetRuntimeWidgetState(const FString& WidgetObjectPath);

	/** Create a Widget Blueprint and optionally create a root widget. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FString CreateWidgetBlueprint(
		const FString& Path,
		const FString& Name,
		const FString& ParentClass = TEXT(""),
		const FString& RootClass = TEXT("CanvasPanel"),
		const FString& RootName = TEXT("RootCanvas"),
		bool bCompile = false,
		bool bSave = false);

	/**
	 * Get non-default property values for a specific widget.
	 * Only returns properties that differ from the widget class defaults.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetPropertyValue> GetWidgetProperties(
		const FString& WidgetBlueprintPath, const FString& WidgetName);

	/**
	 * Get all widget animations with their tracks and durations.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetAnimationInfo> GetWidgetAnimations(const FString& WidgetBlueprintPath);

	/**
	 * Get all property bindings (e.g. Text bound to a function).
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetBindingInfo> GetWidgetBindings(const FString& WidgetBlueprintPath);

	/**
	 * Get widget event bindings (OnClicked, OnHovered, etc.) from the event graph.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetEventInfo> GetWidgetEvents(const FString& WidgetBlueprintPath);

	/**
	 * Search widgets by name or class substring.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetInfo> SearchWidgets(
		const FString& WidgetBlueprintPath, const FString& Query);

	/** Add a widget to a Widget Blueprint. Properties and slot values use export-text strings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool AddWidget(
		const FString& WidgetBlueprintPath,
		const FString& ClassName,
		const FString& WidgetName,
		const FString& ParentWidgetName,
		bool bIsVariable,
		const TMap<FString, FString>& Properties,
		const TMap<FString, FString>& SlotProperties,
		int32 Index);

	/** Remove a widget from a Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool RemoveWidget(const FString& WidgetBlueprintPath, const FString& WidgetName);

	/** Rename a widget in a Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool RenameWidget(const FString& WidgetBlueprintPath, const FString& WidgetName, const FString& NewName);

	/** Reparent a widget under another panel widget. Empty NewParentWidgetName makes it the root widget. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool ReparentWidget(
		const FString& WidgetBlueprintPath,
		const FString& WidgetName,
		const FString& NewParentWidgetName,
		const TMap<FString, FString>& SlotProperties,
		int32 Index);

	/**
	 * Set a property on a widget by name. Value is parsed as text.
	 */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetProperty(
		const FString& WidgetBlueprintPath, const FString& WidgetName,
		const FString& PropertyName, const FString& Value);

	/** Set several widget properties by export-text strings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetProperties(
		const FString& WidgetBlueprintPath,
		const FString& WidgetName,
		const TMap<FString, FString>& Properties);

	/** Set slot properties on a widget by export-text strings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetSlotProperties(
		const FString& WidgetBlueprintPath,
		const FString& WidgetName,
		const TMap<FString, FString>& SlotProperties);

	/** Set a CommonUI style-like property on a widget. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool SetWidgetCommonUIStyle(
		const FString& WidgetBlueprintPath,
		const FString& WidgetName,
		const FString& StylePath,
		const FString& StyleProperty = TEXT("Style"));

	/** Create a component-bound event node for a widget event such as OnClicked. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FString BindWidgetEvent(
		const FString& WidgetBlueprintPath,
		const FString& WidgetName,
		const FString& EventName,
		const FString& FunctionName = TEXT(""),
		int32 PosX = 0,
		int32 PosY = 0);

	/** Create a widget animation if it does not already exist. Returns the animation object name. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FString CreateWidgetAnimation(const FString& WidgetBlueprintPath, const FString& AnimationName);

	/** Add a widget binding to an existing widget animation. Returns the animation binding guid. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FString AddWidgetAnimationBinding(
		const FString& WidgetBlueprintPath,
		const FString& AnimationName,
		const FString& WidgetName);

	/** Add a float key to a widget animation track, e.g. RenderOpacity. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool AddWidgetAnimationFloatKey(
		const FString& WidgetBlueprintPath,
		const FString& AnimationName,
		const FString& WidgetName,
		const FString& PropertyPath,
		float TimeSeconds,
		float Value);

	/** Remove a widget animation by name. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static bool RemoveWidgetAnimation(const FString& WidgetBlueprintPath, const FString& AnimationName);

	/** Render a design-time preview PNG for a Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetPreviewResult PreviewWidget(
		const FString& WidgetBlueprintPath,
		int32 Width = 1920,
		int32 Height = 1080,
		const FString& OutputPath = TEXT(""));

	/** Compile, save, and optionally validate a Widget Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static FBridgeWidgetCompileResult CompileSaveWidgetBlueprint(
		const FString& WidgetBlueprintPath,
		bool bCompile = true,
		bool bSave = true,
		bool bValidate = true);

	/** Validate basic Widget Blueprint tree consistency. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|UMG")
	static TArray<FBridgeWidgetValidationIssue> ValidateWidgetBlueprint(const FString& WidgetBlueprintPath);
};
