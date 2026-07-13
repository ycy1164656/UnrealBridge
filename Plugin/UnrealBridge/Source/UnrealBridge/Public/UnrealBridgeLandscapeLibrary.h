#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UnrealBridgeLandscapeLibrary.generated.h"

USTRUCT(BlueprintType)
struct FBridgeLandscapeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FString Label;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FString MaterialPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FString HoleMaterialPath;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FVector BoundsMin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	FVector BoundsMax = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape")
	int32 ComponentCount = 0;
};

USTRUCT(BlueprintType)
struct FBridgeLandscapeLayerMapping
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FString LayerName;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FString LayerInfoPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FString PhysicalMaterialPath;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") bool bNoWeightBlend = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") bool bValid = false;
};

USTRUCT(BlueprintType)
struct FBridgeLandscapeImportResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FString Error;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FString Warning;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FString ModifiedPackage;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") int32 Width = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") int32 Height = 0;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") TArray<FString> ImportedLayers;
};

USTRUCT(BlueprintType)
struct FBridgeLandscapeLayerSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") FVector WorldLocation = FVector::ZeroVector;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") float Height = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") bool bHeightValid = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") bool bInsideExtent = false;
	UPROPERTY(BlueprintReadOnly, Category = "UnrealBridge|Landscape") TMap<FString, float> LayerWeights;
};

UCLASS()
class UNREALBRIDGE_API UUnrealBridgeLandscapeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static TArray<FBridgeLandscapeInfo> ListLandscapes();

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static FBridgeLandscapeInfo GetLandscapeInfo(const FString& LandscapeNameOrLabel);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static bool SetLandscapeMaterial(
		const FString& LandscapeNameOrLabel,
		const FString& MaterialPath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static bool SetLandscapeMaterialScalarParameter(
		const FString& LandscapeNameOrLabel,
		const FString& ParameterName,
		float Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static bool SetLandscapeMaterialVectorParameter(
		const FString& LandscapeNameOrLabel,
		const FString& ParameterName,
		const FLinearColor& Value);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static bool SetLandscapeMaterialTextureParameter(
		const FString& LandscapeNameOrLabel,
		const FString& ParameterName,
		const FString& TexturePath);

	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape")
	static float GetLandscapeHeightAtLocation(
		const FString& LandscapeNameOrLabel,
		const FVector& WorldLocation,
		bool& bOutSuccess);

	/** Return Landscape material layer names and their LayerInfo asset bindings. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadShort", ToolSaveBehavior = "Never"))
	static TArray<FBridgeLandscapeLayerMapping> GetLandscapeLayerInfoMapping(
		const FString& LandscapeNameOrLabel);

	/** Batch sample height and multiple paint layers without per-point bridge round trips. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape", meta = (
		ToolRisk = "ReadOnly", ToolExecution = "GameThreadLong", ToolSaveBehavior = "Never"))
	static TArray<FBridgeLandscapeLayerSample> SampleLandscapeLayersBatch(
		const FString& LandscapeNameOrLabel,
		const TArray<FVector>& WorldLocations,
		const TArray<FString>& LayerNames);

	/** Import a heightmap into an existing Landscape. Marks the level dirty but never saves it. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape", meta = (
		ToolRisk = "Mutating", ToolExecution = "AsyncJob", ToolSaveBehavior = "Never"))
	static FBridgeLandscapeImportResult LandscapeImportHeightmap(
		const FString& LandscapeNameOrLabel,
		const FString& HeightmapFilePath,
		bool bFlipYAxis = false,
		bool bResampleToFit = false,
		bool bUpdateCollision = true);

	/** Import multiple named weightmaps atomically after all files pass validation. */
	UFUNCTION(BlueprintCallable, Category = "UnrealBridge|Landscape", meta = (
		ToolRisk = "Mutating", ToolExecution = "AsyncJob", ToolSaveBehavior = "Never"))
	static FBridgeLandscapeImportResult LandscapeImportWeightmaps(
		const FString& LandscapeNameOrLabel,
		const TMap<FString, FString>& LayerToFilePath,
		bool bFlipYAxis = false,
		bool bResampleToFit = false,
		bool bWeightAdjust = false);
};
