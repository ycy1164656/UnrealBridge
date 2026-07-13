#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeSequencerLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeSequencerBindingInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer")
	FString BindingName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer")
	FString BindingId;
};

USTRUCT(BlueprintType)
struct FBridgeSequencerTrackInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer")
	FString BindingName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer")
	FString BindingId;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer")
	FString TrackName;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer")
	FString TrackClass;
};

UENUM(BlueprintType)
enum class EBridgeSequencerPropertyType : uint8
{
	Bool,
	Byte,
	Integer,
	Float,
	Double,
	String,
	Vector2,
	Vector3,
	Vector4,
	Color,
};

USTRUCT(BlueprintType)
struct FBridgeSequencerSectionInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") FString BindingName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") FString BindingId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") FString TrackName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") FString TrackClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") FString SectionClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") int32 SectionIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") float StartSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") float EndSeconds = 0.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") bool bHasStart = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") bool bHasEnd = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") int32 ChannelCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") int32 KeyCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeSequencerValidationResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") int32 ErrorCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") int32 WarningCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Sequencer") TArray<FString> Messages;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeSequencerLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer")
	static FString CreateLevelSequence(const FString& Path, const FString& Name, bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer")
	static FBridgeSequencerBindingInfo AddActorBinding(
		const FString& SequencePath,
		const FString& ActorLabel);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer")
	static FString AddTransformTrack(
		const FString& SequencePath,
		const FString& ActorLabel,
		float StartSeconds = 0.0f,
		float EndSeconds = 5.0f);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer")
	static TArray<FBridgeSequencerTrackInfo> ListSequenceTracks(const FString& SequencePath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer")
	static bool SetPlaybackRange(
		const FString& SequencePath,
		float StartSeconds,
		float EndSeconds);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer")
	static bool AddTransformKey(
		const FString& SequencePath,
		const FString& ActorLabel,
		float Seconds,
		const FVector& Location,
		const FRotator& Rotation = FRotator::ZeroRotator,
		const FVector& Scale = FVector(1.0, 1.0, 1.0),
		bool bCreateTrackIfMissing = true,
		const FString& Interpolation = TEXT("Auto"));

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeSequencerSectionInfo> ListSequenceSections(const FString& SequencePath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer", meta = (
		UnrealBridgeTool, ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeSequencerValidationResult ValidateLevelSequence(const FString& SequencePath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FString AddPropertyTrack(
		const FString& SequencePath,
		const FString& ActorLabel,
		const FString& PropertyName,
		const FString& PropertyPath,
		EBridgeSequencerPropertyType PropertyType,
		float StartSeconds = 0.0f,
		float EndSeconds = 5.0f,
		bool bSave = false);

	/** ValueExportText examples: true, 42, 3.5, Hello, (X=1,Y=2,Z=3), (R=1,G=0,B=0,A=1). */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static bool AddPropertyKey(
		const FString& SequencePath,
		const FString& ActorLabel,
		const FString& PropertyName,
		const FString& PropertyPath,
		EBridgeSequencerPropertyType PropertyType,
		float Seconds,
		const FString& ValueExportText,
		bool bCreateTrackIfMissing = true,
		const FString& Interpolation = TEXT("Auto"),
		bool bSave = false);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Sequencer", meta = (
		UnrealBridgeTool, ToolRisk = "AssetWrite", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Explicit"))
	static FString AddSkeletalAnimationSection(
		const FString& SequencePath,
		const FString& ActorLabel,
		const FString& AnimationPath,
		float StartSeconds = 0.0f,
		float EndSeconds = -1.0f,
		float PlayRate = 1.0f,
		bool bSave = false);
};
