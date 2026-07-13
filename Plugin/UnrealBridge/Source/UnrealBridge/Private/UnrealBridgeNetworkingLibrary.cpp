#include "UnrealBridgeNetworkingLibrary.h"

#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "UnrealBridgeNetworkingLibrary"

namespace BridgeNetworkingImpl
{
	AActor* FindActor(const FString& NameOrLabel)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World || NameOrLabel.IsEmpty())
		{
			return nullptr;
		}
		const FName AsName(*NameOrLabel);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && (Actor->GetActorLabel() == NameOrLabel || Actor->GetFName() == AsName || Actor->GetPathName() == NameOrLabel))
			{
				return Actor;
			}
		}
		return nullptr;
	}

	FString DormancyToString(ENetDormancy Dormancy)
	{
		switch (Dormancy)
		{
		case DORM_Never: return TEXT("Never");
		case DORM_Awake: return TEXT("Awake");
		case DORM_DormantAll: return TEXT("DormantAll");
		case DORM_DormantPartial: return TEXT("DormantPartial");
		case DORM_Initial: return TEXT("Initial");
		default: return TEXT("Unknown");
		}
	}

	ENetDormancy ParseDormancy(const FString& Dormancy)
	{
		const FString Normalized = Dormancy.ToLower();
		if (Normalized == TEXT("never") || Normalized == TEXT("dorm_never")) return DORM_Never;
		if (Normalized == TEXT("dormantall") || Normalized == TEXT("dorm_all") || Normalized == TEXT("dorm_dormantall")) return DORM_DormantAll;
		if (Normalized == TEXT("dormantpartial") || Normalized == TEXT("partial") || Normalized == TEXT("dorm_dormantpartial")) return DORM_DormantPartial;
		if (Normalized == TEXT("initial") || Normalized == TEXT("dorm_initial")) return DORM_Initial;
		return DORM_Awake;
	}

	FBridgeActorNetworkingInfo MakeInfo(AActor* Actor)
	{
		FBridgeActorNetworkingInfo Info;
		if (!Actor)
		{
			return Info;
		}
		Info.ActorLabel = Actor->GetActorLabel();
		Info.ActorPath = Actor->GetPathName();
		Info.bReplicates = Actor->GetIsReplicated();
		Info.bReplicateMovement = Actor->IsReplicatingMovement();
		Info.bAlwaysRelevant = Actor->bAlwaysRelevant;
		Info.NetDormancy = DormancyToString(Actor->NetDormancy);
		Info.NetUpdateFrequency = Actor->NetUpdateFrequency;
		Info.MinNetUpdateFrequency = Actor->MinNetUpdateFrequency;
		Info.NetPriority = Actor->NetPriority;
		Info.NetCullDistanceSquared = Actor->NetCullDistanceSquared;
		return Info;
	}
}

FBridgeActorNetworkingInfo UUnrealBridgeNetworkingLibrary::GetActorNetworkingInfo(const FString& ActorNameOrLabel)
{
	return BridgeNetworkingImpl::MakeInfo(BridgeNetworkingImpl::FindActor(ActorNameOrLabel));
}

bool UUnrealBridgeNetworkingLibrary::SetActorReplicates(const FString& ActorNameOrLabel, bool bReplicates)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorReplicates", "Bridge: Set Actor Replicates"));
	Actor->Modify();
	Actor->SetReplicates(bReplicates);
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorReplicateMovement(const FString& ActorNameOrLabel, bool bReplicateMovement)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorReplicateMovement", "Bridge: Set Actor Replicate Movement"));
	Actor->Modify();
	Actor->SetReplicateMovement(bReplicateMovement);
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorAlwaysRelevant(const FString& ActorNameOrLabel, bool bAlwaysRelevant)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorAlwaysRelevant", "Bridge: Set Actor Always Relevant"));
	Actor->Modify();
	Actor->bAlwaysRelevant = bAlwaysRelevant;
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorNetDormancy(const FString& ActorNameOrLabel, const FString& Dormancy)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorDormancy", "Bridge: Set Actor Net Dormancy"));
	Actor->Modify();
	Actor->NetDormancy = BridgeNetworkingImpl::ParseDormancy(Dormancy);
	Actor->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeNetworkingLibrary::SetActorNetUpdateSettings(
	const FString& ActorNameOrLabel,
	float NetUpdateFrequency,
	float MinNetUpdateFrequency,
	float NetPriority,
	float NetCullDistanceSquared)
{
	AActor* Actor = BridgeNetworkingImpl::FindActor(ActorNameOrLabel);
	if (!Actor)
	{
		return false;
	}
	const FScopedTransaction Transaction(LOCTEXT("BridgeSetActorNetUpdate", "Bridge: Set Actor Net Update Settings"));
	Actor->Modify();
	if (NetUpdateFrequency >= 0.0f)
	{
		Actor->NetUpdateFrequency = NetUpdateFrequency;
	}
	if (MinNetUpdateFrequency >= 0.0f)
	{
		Actor->MinNetUpdateFrequency = MinNetUpdateFrequency;
	}
	if (NetPriority >= 0.0f)
	{
		Actor->NetPriority = NetPriority;
	}
	if (NetCullDistanceSquared >= 0.0f)
	{
		Actor->NetCullDistanceSquared = NetCullDistanceSquared;
	}
	Actor->MarkPackageDirty();
	return true;
}

#undef LOCTEXT_NAMESPACE
