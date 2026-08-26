#include "UnrealBridgeLandscapeLibrary.h"

#include "Algo/Unique.h"
#include "Editor.h"
#include "Engine/Texture.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeEdit.h"
#include "LandscapeImportHelper.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Materials/MaterialInterface.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeLandscapeLibrary"

namespace BridgeLandscapeImpl
{
	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	ALandscapeProxy* FindLandscape(const FString& NameOrLabel)
	{
		UWorld* World = GetEditorWorld();
		if (!World)
		{
			return nullptr;
		}
		const FName AsName(*NameOrLabel);
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Landscape = *It;
			if (!Landscape)
			{
				continue;
			}
			if (NameOrLabel.IsEmpty()
				|| Landscape->GetActorLabel() == NameOrLabel
				|| Landscape->GetFName() == AsName
				|| Landscape->GetPathName() == NameOrLabel)
			{
				return Landscape;
			}
		}
		return nullptr;
	}

	FBridgeLandscapeInfo MakeInfo(ALandscapeProxy* Landscape)
	{
		FBridgeLandscapeInfo Info;
		if (!Landscape)
		{
			return Info;
		}
		Info.Label = Landscape->GetActorLabel();
		Info.Name = Landscape->GetName();
		Info.Path = Landscape->GetPathName();
		if (UMaterialInterface* Material = Landscape->GetLandscapeMaterial())
		{
			Info.MaterialPath = Material->GetPathName();
		}
		if (UMaterialInterface* HoleMaterial = Landscape->GetLandscapeHoleMaterial())
		{
			Info.HoleMaterialPath = HoleMaterial->GetPathName();
		}
		const FBox Bounds = Landscape->GetComponentsBoundingBox(true);
		Info.BoundsMin = Bounds.Min;
		Info.BoundsMax = Bounds.Max;
		TArray<ULandscapeComponent*> Components;
		Landscape->GetComponents(Components);
		Info.ComponentCount = Components.Num();
		return Info;
	}

	template <typename T>
	bool LoadImportData(
		const FString& FilePath,
		FName LayerName,
		int32 TargetWidth,
		int32 TargetHeight,
		bool bFlipYAxis,
		bool bResampleToFit,
		TArray<T>& OutData,
		FString& OutWarning,
		FString& OutError)
	{
		FLandscapeImportDescriptor Descriptor;
		FText Message;
		const ELandscapeImportResult DescriptorResult = FLandscapeImportHelper::GetImportDescriptor<T>(
			FilePath, true, bFlipYAxis, LayerName, Descriptor, Message);
		if (DescriptorResult == ELandscapeImportResult::Error || Descriptor.ImportResolutions.IsEmpty())
		{
			OutError = Message.IsEmpty()
				? FString::Printf(TEXT("Unable to read landscape import descriptor: %s"), *FilePath)
				: Message.ToString();
			return false;
		}
		if (DescriptorResult == ELandscapeImportResult::Warning && !Message.IsEmpty())
		{
			OutWarning = Message.ToString();
		}

		int32 DescriptorIndex = Descriptor.FindDescriptorIndex(TargetWidth, TargetHeight);
		if (DescriptorIndex == INDEX_NONE)
		{
			if (!bResampleToFit)
			{
				const FLandscapeImportResolution& Source = Descriptor.ImportResolutions[0];
				OutError = FString::Printf(
					TEXT("Import resolution %ux%u does not match Landscape resolution %dx%d"),
					Source.Width, Source.Height, TargetWidth, TargetHeight);
				return false;
			}
			DescriptorIndex = 0;
		}

		TArray<T> SourceData;
		Message = FText::GetEmpty();
		const ELandscapeImportResult DataResult = FLandscapeImportHelper::GetImportData<T>(
			Descriptor, DescriptorIndex, LayerName, SourceData, Message);
		if (DataResult == ELandscapeImportResult::Error)
		{
			OutError = Message.IsEmpty()
				? FString::Printf(TEXT("Unable to import landscape data: %s"), *FilePath)
				: Message.ToString();
			return false;
		}
		if (DataResult == ELandscapeImportResult::Warning && !Message.IsEmpty())
		{
			if (!OutWarning.IsEmpty())
			{
				OutWarning += TEXT("; ");
			}
			OutWarning += Message.ToString();
		}

		const FLandscapeImportResolution SourceResolution = Descriptor.ImportResolutions[DescriptorIndex];
		const FLandscapeImportResolution TargetResolution(TargetWidth, TargetHeight);
		if (SourceResolution != TargetResolution)
		{
			FLandscapeImportHelper::TransformImportData<T>(
				SourceData, OutData, SourceResolution, TargetResolution,
				ELandscapeImportTransformType::Resample);
		}
		else
		{
			OutData = MoveTemp(SourceData);
		}

		if (OutData.Num() != TargetWidth * TargetHeight)
		{
			OutError = FString::Printf(
				TEXT("Imported data has %d samples; expected %d"),
				OutData.Num(), TargetWidth * TargetHeight);
			return false;
		}
		return true;
	}

	bool GetExtent(ALandscapeProxy* Landscape, ULandscapeInfo*& OutInfo, FIntRect& OutExtent)
	{
		OutInfo = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
		return OutInfo && OutInfo->GetLandscapeExtent(Landscape, OutExtent);
	}

	void ModifyLandscapeAndComponents(ALandscapeProxy* Landscape)
	{
		Landscape->Modify();
		TArray<ULandscapeComponent*> Components;
		Landscape->GetComponents(Components);
		for (ULandscapeComponent* Component : Components)
		{
			if (Component)
			{
				Component->Modify();
			}
		}
	}
}

TArray<FBridgeLandscapeInfo> UUnrealBridgeLandscapeLibrary::ListLandscapes()
{
	TArray<FBridgeLandscapeInfo> Result;
	UWorld* World = BridgeLandscapeImpl::GetEditorWorld();
	if (!World)
	{
		return Result;
	}
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Result.Add(BridgeLandscapeImpl::MakeInfo(*It));
	}
	return Result;
}

FBridgeLandscapeInfo UUnrealBridgeLandscapeLibrary::GetLandscapeInfo(const FString& LandscapeNameOrLabel)
{
	return BridgeLandscapeImpl::MakeInfo(BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel));
}

bool UUnrealBridgeLandscapeLibrary::SetLandscapeMaterial(
	const FString& LandscapeNameOrLabel,
	const FString& MaterialPath)
{
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Landscape || !Material)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetLandscapeMaterial", "Bridge: Set Landscape Material"));
	Landscape->Modify();
	FObjectPropertyBase* MaterialProperty = CastField<FObjectPropertyBase>(
		Landscape->GetClass()->FindPropertyByName(TEXT("LandscapeMaterial")));
	if (!MaterialProperty)
	{
		return false;
	}
	MaterialProperty->SetObjectPropertyValue_InContainer(Landscape, Material);
	FPropertyChangedEvent ChangedEvent(MaterialProperty);
	Landscape->PostEditChangeProperty(ChangedEvent);
	Landscape->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeLandscapeLibrary::SetLandscapeMaterialScalarParameter(
	const FString& LandscapeNameOrLabel,
	const FString& ParameterName,
	float Value)
{
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	if (!Landscape || ParameterName.IsEmpty())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetLandscapeScalar", "Bridge: Set Landscape Scalar Parameter"));
	Landscape->Modify();
	Landscape->SetLandscapeMaterialScalarParameterValue(FName(*ParameterName), Value);
	Landscape->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeLandscapeLibrary::SetLandscapeMaterialVectorParameter(
	const FString& LandscapeNameOrLabel,
	const FString& ParameterName,
	const FLinearColor& Value)
{
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	if (!Landscape || ParameterName.IsEmpty())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetLandscapeVector", "Bridge: Set Landscape Vector Parameter"));
	Landscape->Modify();
	Landscape->SetLandscapeMaterialVectorParameterValue(FName(*ParameterName), Value);
	Landscape->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeLandscapeLibrary::SetLandscapeMaterialTextureParameter(
	const FString& LandscapeNameOrLabel,
	const FString& ParameterName,
	const FString& TexturePath)
{
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	UTexture* Texture = LoadObject<UTexture>(nullptr, *TexturePath);
	if (!Landscape || !Texture || ParameterName.IsEmpty())
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetLandscapeTexture", "Bridge: Set Landscape Texture Parameter"));
	Landscape->Modify();
	Landscape->SetLandscapeMaterialTextureParameterValue(FName(*ParameterName), Texture);
	Landscape->MarkPackageDirty();
	return true;
}

float UUnrealBridgeLandscapeLibrary::GetLandscapeHeightAtLocation(
	const FString& LandscapeNameOrLabel,
	const FVector& WorldLocation,
	bool& bOutSuccess)
{
	bOutSuccess = false;
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	if (!Landscape)
	{
		return 0.0f;
	}
	const TOptional<float> Height = Landscape->GetHeightAtLocation(WorldLocation);
	if (!Height.IsSet())
	{
		return 0.0f;
	}
	bOutSuccess = true;
	return Height.GetValue();
}

TArray<FBridgeLandscapeLayerMapping> UUnrealBridgeLandscapeLibrary::GetLandscapeLayerInfoMapping(
	const FString& LandscapeNameOrLabel)
{
	TArray<FBridgeLandscapeLayerMapping> Result;
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	ULandscapeInfo* Info = Landscape ? Landscape->GetLandscapeInfo() : nullptr;
	if (!Info)
	{
		return Result;
	}

	for (const FLandscapeInfoLayerSettings& Settings : Info->Layers)
	{
		FBridgeLandscapeLayerMapping Mapping;
		Mapping.LayerName = Settings.GetLayerName().ToString();
		if (const ULandscapeLayerInfoObject* LayerInfo = Settings.LayerInfoObj)
		{
			Mapping.LayerInfoPath = LayerInfo->GetPathName();
#if ENGINE_MAJOR_VERSION > 5 || ENGINE_MINOR_VERSION >= 7
			Mapping.bNoWeightBlend =
				LayerInfo->GetBlendMethod() == ELandscapeTargetLayerBlendMethod::None;
#else
			Mapping.bNoWeightBlend = LayerInfo->bNoWeightBlend;
#endif
			if (const UPhysicalMaterial* PhysicalMaterial =
				LayerInfo->GetPhysicalMaterial())
			{
				Mapping.PhysicalMaterialPath = PhysicalMaterial->GetPathName();
			}
			Mapping.bValid = true;
		}
		Result.Add(MoveTemp(Mapping));
	}
	Result.Sort([](const FBridgeLandscapeLayerMapping& Left, const FBridgeLandscapeLayerMapping& Right)
	{
		return Left.LayerName < Right.LayerName;
	});
	return Result;
}

TArray<FBridgeLandscapeLayerSample> UUnrealBridgeLandscapeLibrary::SampleLandscapeLayersBatch(
	const FString& LandscapeNameOrLabel,
	const TArray<FVector>& WorldLocations,
	const TArray<FString>& LayerNames)
{
	TArray<FBridgeLandscapeLayerSample> Result;
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	ULandscapeInfo* Info = nullptr;
	FIntRect Extent;
	if (!BridgeLandscapeImpl::GetExtent(Landscape, Info, Extent))
	{
		return Result;
	}

	TArray<FString> EffectiveLayerNames = LayerNames;
	if (EffectiveLayerNames.IsEmpty())
	{
		for (const FLandscapeInfoLayerSettings& Settings : Info->Layers)
		{
			EffectiveLayerNames.Add(Settings.GetLayerName().ToString());
		}
	}
	EffectiveLayerNames.Sort();
	EffectiveLayerNames.SetNum(Algo::Unique(EffectiveLayerNames));

	TArray<ULandscapeLayerInfoObject*> LayerInfos;
	LayerInfos.Reserve(EffectiveLayerNames.Num());
	for (const FString& LayerName : EffectiveLayerNames)
	{
		LayerInfos.Add(Info->GetLayerInfoByName(FName(*LayerName), Landscape));
	}

	FLandscapeEditDataInterface Edit(Info);
	const FTransform LandscapeToWorld = Landscape->LandscapeActorToWorld();
	const int32 SampleCount = FMath::Min(WorldLocations.Num(), 100000);
	Result.Reserve(SampleCount);
	for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
	{
		const FVector& WorldLocation = WorldLocations[SampleIndex];
		FBridgeLandscapeLayerSample Sample;
		Sample.WorldLocation = WorldLocation;
		const FVector Local = LandscapeToWorld.InverseTransformPosition(WorldLocation);
		const int32 X = FMath::RoundToInt(Local.X);
		const int32 Y = FMath::RoundToInt(Local.Y);
		Sample.bInsideExtent = X >= Extent.Min.X && X <= Extent.Max.X
			&& Y >= Extent.Min.Y && Y <= Extent.Max.Y;

		const TOptional<float> Height = Landscape->GetHeightAtLocation(WorldLocation);
		if (Height.IsSet())
		{
			Sample.Height = Height.GetValue();
			Sample.bHeightValid = true;
		}

		for (int32 LayerIndex = 0; LayerIndex < EffectiveLayerNames.Num(); ++LayerIndex)
		{
			uint8 Weight = 0;
			if (Sample.bInsideExtent && LayerInfos[LayerIndex])
			{
				Edit.GetWeightDataFast(LayerInfos[LayerIndex], X, Y, X, Y, &Weight, 1);
			}
			Sample.LayerWeights.Add(EffectiveLayerNames[LayerIndex], static_cast<float>(Weight) / 255.f);
		}
		Result.Add(MoveTemp(Sample));
	}
	return Result;
}

FBridgeLandscapeImportResult UUnrealBridgeLandscapeLibrary::LandscapeImportHeightmap(
	const FString& LandscapeNameOrLabel,
	const FString& HeightmapFilePath,
	bool bFlipYAxis,
	bool bResampleToFit,
	bool bUpdateCollision)
{
	FBridgeLandscapeImportResult Result;
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	ULandscapeInfo* Info = nullptr;
	FIntRect Extent;
	if (!BridgeLandscapeImpl::GetExtent(Landscape, Info, Extent))
	{
		Result.Error = TEXT("Landscape or LandscapeInfo was not found");
		return Result;
	}

	Result.Width = Extent.Width() + 1;
	Result.Height = Extent.Height() + 1;
	TArray<uint16> Data;
	if (!BridgeLandscapeImpl::LoadImportData<uint16>(
		HeightmapFilePath, NAME_None, Result.Width, Result.Height,
		bFlipYAxis, bResampleToFit, Data, Result.Warning, Result.Error))
	{
		return Result;
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeImportLandscapeHeightmap", "Bridge: Import Landscape Heightmap"));
	BridgeLandscapeImpl::ModifyLandscapeAndComponents(Landscape);
	FLandscapeEditDataInterface Edit(Info);
	Edit.SetHeightData(
		Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y,
		Data.GetData(), Result.Width, true, nullptr, nullptr, nullptr,
		false, nullptr, nullptr, true, bUpdateCollision, true);
	Landscape->PostEditChange();
	Landscape->MarkPackageDirty();
	Result.ModifiedPackage = Landscape->GetOutermost()->GetName();
	Result.bSuccess = true;
	return Result;
}

FBridgeLandscapeImportResult UUnrealBridgeLandscapeLibrary::LandscapeImportWeightmaps(
	const FString& LandscapeNameOrLabel,
	const TMap<FString, FString>& LayerToFilePath,
	bool bFlipYAxis,
	bool bResampleToFit,
	bool bWeightAdjust)
{
	FBridgeLandscapeImportResult Result;
	ALandscapeProxy* Landscape = BridgeLandscapeImpl::FindLandscape(LandscapeNameOrLabel);
	ULandscapeInfo* Info = nullptr;
	FIntRect Extent;
	if (!BridgeLandscapeImpl::GetExtent(Landscape, Info, Extent))
	{
		Result.Error = TEXT("Landscape or LandscapeInfo was not found");
		return Result;
	}
	if (LayerToFilePath.IsEmpty())
	{
		Result.Error = TEXT("No weightmap files were supplied");
		return Result;
	}

	Result.Width = Extent.Width() + 1;
	Result.Height = Extent.Height() + 1;
	struct FPendingWeightmap
	{
		FString LayerName;
		ULandscapeLayerInfoObject* LayerInfo = nullptr;
		TArray<uint8> Data;
	};
	TArray<FString> SortedLayerNames;
	LayerToFilePath.GetKeys(SortedLayerNames);
	SortedLayerNames.Sort();
	TArray<FPendingWeightmap> Pending;
	Pending.Reserve(SortedLayerNames.Num());
	for (const FString& LayerName : SortedLayerNames)
	{
		FPendingWeightmap Item;
		Item.LayerName = LayerName;
		Item.LayerInfo = Info->GetLayerInfoByName(FName(*LayerName), Landscape);
		if (!Item.LayerInfo)
		{
			Result.Error = FString::Printf(TEXT("Landscape layer '%s' has no LayerInfo mapping"), *LayerName);
			return Result;
		}
		FString LayerWarning;
		if (!BridgeLandscapeImpl::LoadImportData<uint8>(
			LayerToFilePath.FindChecked(LayerName), FName(*LayerName),
			Result.Width, Result.Height, bFlipYAxis, bResampleToFit,
			Item.Data, LayerWarning, Result.Error))
		{
			return Result;
		}
		if (!LayerWarning.IsEmpty())
		{
			if (!Result.Warning.IsEmpty()) Result.Warning += TEXT("; ");
			Result.Warning += LayerName + TEXT(": ") + LayerWarning;
		}
		Pending.Add(MoveTemp(Item));
	}

	const FScopedTransaction Transaction(LOCTEXT("BridgeImportLandscapeWeightmaps", "Bridge: Import Landscape Weightmaps"));
	BridgeLandscapeImpl::ModifyLandscapeAndComponents(Landscape);
	FLandscapeEditDataInterface Edit(Info);
	for (FPendingWeightmap& Item : Pending)
	{
		Edit.SetAlphaData(
			Item.LayerInfo, Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y,
			Item.Data.GetData(), Result.Width, ELandscapeLayerPaintingRestriction::None,
			bWeightAdjust, false);
		Result.ImportedLayers.Add(Item.LayerName);
	}
	Landscape->PostEditChange();
	Landscape->MarkPackageDirty();
	Result.ModifiedPackage = Landscape->GetOutermost()->GetName();
	Result.bSuccess = true;
	return Result;
}

#undef LOCTEXT_NAMESPACE
