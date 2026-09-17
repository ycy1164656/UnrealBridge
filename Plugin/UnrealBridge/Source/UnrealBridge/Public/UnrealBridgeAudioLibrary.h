#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeAudioLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeMetaSoundMemberInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString DataType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString AccessType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString DefaultValue;
};

USTRUCT(BlueprintType)
struct FBridgeMetaSoundPageInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString PageId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 NodeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 EdgeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 VariableCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeMetaSoundNodeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString NodeId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString Name;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString ClassId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString ClassName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString PageId;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 InputCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 OutputCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeMetaSoundGraphInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") bool bFound = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString Path;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString AssetClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString RootClassName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString RootClassVersion;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString DocumentVersion;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString TemplateType;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") bool bPreset = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 TotalPageCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 TotalNodeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 TotalEdgeCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 DependencyCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FString> Interfaces;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FBridgeMetaSoundMemberInfo> Inputs;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FBridgeMetaSoundMemberInfo> Outputs;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FBridgeMetaSoundPageInfo> Pages;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FBridgeMetaSoundNodeInfo> Nodes;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString ValidationResult;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FString> ValidationMessages;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString Error;
};

/** One public Builder-API graph operation. Node refs accept a GUID or "$N" back-reference. */
USTRUCT(BlueprintType)
struct FBridgeMetaSoundGraphOp
{
	GENERATED_BODY()

	/** add_input/add_output/remove_input/remove_output/add_node/connect/disconnect_input/disconnect_output/set_default/remove_node/set_location */
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString Op;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString Name;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString DataType;
	/** Default/Bool/Int/Float/String/Object. */
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString LiteralType;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString Value;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString Namespace;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString ClassName;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString Variant;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") int32 MajorVersion = 1;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString SrcRef;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString SrcOutput;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString DstRef;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") FString DstInput;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") float X = 0.0f;
	UPROPERTY(BlueprintReadWrite, Category = "UnrealBridge|Audio|MetaSound") float Y = 0.0f;
};

USTRUCT(BlueprintType)
struct FBridgeMetaSoundGraphOpResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 OpsApplied = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") int32 FailedAtIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FString> ProducedNodeIds;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") bool bRegisteredWithFrontend = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString ValidationResult;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") TArray<FString> ValidationMessages;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|MetaSound") FString Error;
};

USTRUCT(BlueprintType)
struct FBridgeAudioComponentRuntimeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FString World;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FString ComponentPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FString OwnerPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FString SoundPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FString SoundClass;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FString PlayState;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") FVector Location = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") float VolumeMultiplier = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") float PitchMultiplier = 1.0f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") int32 DefaultParameterCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") int32 InstanceParameterCount = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") TArray<FString> DefaultParameterNames;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") TArray<FString> InstanceParameterNames;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") bool bPlaying = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") bool bVirtualized = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") bool bActive = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") bool bAutoActivate = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Audio|Runtime") bool bUISound = false;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeAudioLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Authoring",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetSoundCueEditModel(const FString& SoundCuePath);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Authoring",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString PreviewSoundCueOps(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Authoring",meta=(ToolRisk="Mutating",ToolSaveBehavior="Optional",ToolExecution="GameThreadShort"))
	static FString ApplySoundCueOps(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Authoring",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString ValidateSoundCueAsset(const FString& SoundCuePath);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Routing",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetAudioRoutingModel(const FString& AssetPathsJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Routing",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString PreviewAudioRoutingOps(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Routing",meta=(ToolRisk="Mutating",ToolSaveBehavior="Optional",ToolExecution="GameThreadShort"))
	static FString ApplyAudioRoutingOps(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Session",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetAudioMixContext(const FString& WorldHandle);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Session",meta=(ToolRisk="RuntimeInteraction",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString BeginAudioMixSession(const FString& RequestJson);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Session",meta=(ToolRisk="ReadOnly",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString GetAudioMixSession(const FString& SessionId,const FString& WorldHandle);
	UFUNCTION(BlueprintCallable,Category="UnrealBridge|Audio|Session",meta=(ToolRisk="RuntimeInteraction",ToolSaveBehavior="Never",ToolExecution="GameThreadShort"))
	static FString EndAudioMixSession(const FString& SessionId,const FString& WorldHandle);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Audio|MetaSound", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeMetaSoundGraphInfo GetMetaSoundGraphInfo(
		const FString& MetaSoundPath,
		int32 MaxNodes = 4096);

	/** Apply an ordered MetaSound Builder batch. Mutates the loaded asset but never saves it. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Audio|MetaSound", meta = (
		ToolRisk = "Mutating", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static FBridgeMetaSoundGraphOpResult ApplyMetaSoundGraphOps(
		const FString& MetaSoundPath,
		const TArray<FBridgeMetaSoundGraphOp>& Ops,
		bool bRegisterWithFrontend = true);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Audio|Runtime", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeAudioComponentRuntimeInfo> GetRuntimeAudioComponents(
		bool bPlayingOnly = false,
		int32 MaxComponents = 4096);
};
