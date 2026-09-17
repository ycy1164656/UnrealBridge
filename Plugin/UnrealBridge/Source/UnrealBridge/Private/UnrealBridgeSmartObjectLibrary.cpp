#include "UnrealBridgeSmartObjectLibrary.h"

#include "GameplayTagContainer.h"
#include "Engine/World.h"
#include "SmartObjectComponent.h"
#include "SmartObjectDefinition.h"
#include "SmartObjectRuntime.h"
#include "SmartObjectSubsystem.h"
#include "SmartObjectTypes.h"
#include "UObject/UObjectIterator.h"

namespace UnrealBridgeSmartObject
{
	bool IsRuntimeWorld(const UWorld* World)
	{
		return World
			&& (World->WorldType == EWorldType::PIE
				|| World->WorldType == EWorldType::Game
				|| World->WorldType == EWorldType::GamePreview);
	}

	FString RegistrationTypeToString(ESmartObjectRegistrationType Type)
	{
		switch (Type)
		{
		case ESmartObjectRegistrationType::NotRegistered: return TEXT("NotRegistered");
		case ESmartObjectRegistrationType::Dynamic: return TEXT("Dynamic");
		case ESmartObjectRegistrationType::BindToExistingInstance: return TEXT("BindToExistingInstance");
		default: return TEXT("Unknown");
		}
	}

	FString SlotStateToString(ESmartObjectSlotState State)
	{
		switch (State)
		{
		case ESmartObjectSlotState::Invalid: return TEXT("Invalid");
		case ESmartObjectSlotState::Free: return TEXT("Free");
		case ESmartObjectSlotState::Claimed: return TEXT("Claimed");
		case ESmartObjectSlotState::Occupied: return TEXT("Occupied");
		default: return TEXT("Unknown");
		}
	}
}

TArray<FBridgeSmartObjectRuntimeInfo> UUnrealBridgeSmartObjectLibrary::GetRuntimeSmartObjects(
	int32 MaxComponents,
	int32 MaxSlotsPerComponent,
	bool bRuntimeWorldsOnly)
{
	TArray<FBridgeSmartObjectRuntimeInfo> Result;
	MaxComponents = FMath::Clamp(MaxComponents, 1, 8192);
	MaxSlotsPerComponent = FMath::Clamp(MaxSlotsPerComponent, 0, 4096);

	for (TObjectIterator<USmartObjectComponent> It; It && Result.Num() < MaxComponents; ++It)
	{
		USmartObjectComponent* Component = *It;
		UWorld* World = IsValid(Component) ? Component->GetWorld() : nullptr;
		if (!IsValid(Component) || Component->IsTemplate() || !World
			|| (bRuntimeWorldsOnly && !UnrealBridgeSmartObject::IsRuntimeWorld(World)))
		{
			continue;
		}

		FBridgeSmartObjectRuntimeInfo Info;
		Info.World = World->GetPathName();
		Info.ComponentPath = Component->GetPathName();
		Info.OwnerPath = IsValid(Component->GetOwner()) ? Component->GetOwner()->GetPathName() : FString();
		if (const USmartObjectDefinition* Definition = Component->GetDefinition())
		{
			Info.DefinitionPath = Definition->GetPathName();
		}
		const FBox Bounds = Component->GetSmartObjectBounds();
		Info.BoundsCenter = Bounds.GetCenter();
		Info.BoundsExtent = Bounds.GetExtent();
		Info.RegistrationType = UnrealBridgeSmartObject::RegistrationTypeToString(Component->GetRegistrationType());
		Info.bBoundToSimulation = Component->IsBoundToSimulation();

		const FSmartObjectHandle Handle = Component->GetRegisteredHandle();
		Info.bRegistered = Handle.IsValid();
		Info.Handle = Info.bRegistered ? LexToString(Handle) : FString();
		Info.bEnabled = Info.bRegistered && Component->IsSmartObjectEnabled();
		if (Info.bRegistered)
		{
			if (const USmartObjectSubsystem* Subsystem = World->GetSubsystem<USmartObjectSubsystem>())
			{
				Info.InstanceTags = Subsystem->GetInstanceTags(Handle).ToStringSimple();
				TArray<FSmartObjectSlotHandle> Slots;
				Subsystem->GetAllSlots(Handle, Slots);
				Info.TotalSlotCount = Slots.Num();
				const int32 SlotCount = FMath::Min(Slots.Num(), MaxSlotsPerComponent);
				Info.Slots.Reserve(SlotCount);
				for (int32 Index = 0; Index < SlotCount; ++Index)
				{
					const FSmartObjectSlotHandle SlotHandle = Slots[Index];
					FBridgeSmartObjectSlotRuntimeInfo SlotInfo;
					SlotInfo.Handle = LexToString(SlotHandle);
					SlotInfo.State = UnrealBridgeSmartObject::SlotStateToString(Subsystem->GetSlotState(SlotHandle));
					const TOptional<FVector> Location = Subsystem->GetSlotLocation(SlotHandle);
					SlotInfo.bHasLocation = Location.IsSet();
					if (Location.IsSet())
					{
						SlotInfo.Location = Location.GetValue();
					}
					Info.Slots.Add(MoveTemp(SlotInfo));
				}
			}
		}
		Result.Add(MoveTemp(Info));
	}
	return Result;
}
