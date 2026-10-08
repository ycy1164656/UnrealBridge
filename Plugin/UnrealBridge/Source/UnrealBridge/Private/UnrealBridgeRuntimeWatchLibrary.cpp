#include "UnrealBridgeRuntimeWatchLibrary.h"

#include "UnrealBridgeWorldLibrary.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace BridgeWatch
{
	static FString Json(const TSharedRef<FJsonObject>& Object)
	{
		FString Out;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out));
		return Out;
	}
	static TSharedPtr<FJsonObject> Parse(const FString& Text)
	{
		TSharedPtr<FJsonObject> Out;
		FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Out);
		return Out;
	}
	static FString Error(const TCHAR* Code, const FString& Message)
	{
		auto Out = MakeShared<FJsonObject>();
		Out->SetBoolField(TEXT("ok"), false);
		Out->SetStringField(TEXT("error_code"), Code);
		Out->SetStringField(TEXT("error"), Message);
		return Json(Out);
	}
	static FString NetMode(ENetMode Mode)
	{
		return Mode == NM_ListenServer ? TEXT("ListenServer") : Mode == NM_Client ? TEXT("Client")
			: Mode == NM_DedicatedServer ? TEXT("DedicatedServer") : TEXT("Standalone");
	}
	static FString Role(ENetRole Value)
	{
		return Value == ROLE_Authority ? TEXT("Authority") : Value == ROLE_AutonomousProxy ? TEXT("AutonomousProxy")
			: Value == ROLE_SimulatedProxy ? TEXT("SimulatedProxy") : TEXT("None");
	}
	static UAbilitySystemComponent* ASC(AActor* Actor)
	{
		return Actor ? Actor->FindComponentByClass<UAbilitySystemComponent>() : nullptr;
	}
	static bool Property(AActor* Actor, const FString& Name, TSharedPtr<FJsonValue>& Out, FString& ErrorText)
	{
		if (Name.IsEmpty() || Name.Len() > 128 || Name.Contains(TEXT(".")))
		{
			ErrorText = TEXT("Only one direct property name is allowed"); return false;
		}
		FProperty* Field = Actor->GetClass()->FindPropertyByName(FName(*Name));
		if (!Field || Field->ArrayDim != 1 || Field->IsA<FArrayProperty>() || Field->IsA<FMapProperty>() || Field->IsA<FSetProperty>())
		{
			ErrorText = TEXT("Property absent or unsupported container"); return false;
		}
		const void* Value = Field->ContainerPtrToValuePtr<void>(Actor);
		if (const FBoolProperty* Bool = CastField<FBoolProperty>(Field))
			Out = MakeShared<FJsonValueBoolean>(Bool->GetPropertyValue(Value));
		else if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Field))
			Out = MakeShared<FJsonValueNumber>(Numeric->IsInteger() ? static_cast<double>(Numeric->GetSignedIntPropertyValue(Value))
				: Numeric->GetFloatingPointPropertyValue(Value));
		else if (const FObjectPropertyBase* Object = CastField<FObjectPropertyBase>(Field))
		{
			const UObject* Ref = Object->GetObjectPropertyValue(Value);
			Out = MakeShared<FJsonValueString>(Ref ? Ref->GetPathName() : FString());
		}
		else if (Field->IsA<FNameProperty>() || Field->IsA<FStrProperty>() || Field->IsA<FEnumProperty>()
			|| (Field->IsA<FStructProperty>() && (CastFieldChecked<FStructProperty>(Field)->Struct == TBaseStructure<FVector>::Get()
				|| CastFieldChecked<FStructProperty>(Field)->Struct == TBaseStructure<FVector2D>::Get()
				|| CastFieldChecked<FStructProperty>(Field)->Struct == TBaseStructure<FRotator>::Get()
				|| CastFieldChecked<FStructProperty>(Field)->Struct == FGameplayTag::StaticStruct())))
		{
			FString Text;
			Field->ExportTextItem_Direct(Text, Value, nullptr, Actor, PPF_None);
			if (Text.Len() > 4096) { ErrorText = TEXT("Property exceeds 4 KiB"); return false; }
			Out = MakeShared<FJsonValueString>(Text);
		}
		else { ErrorText = TEXT("Unsupported property type"); return false; }
		return true;
	}
	static bool Attribute(AActor* Actor, const FString& Name, const TSharedRef<FJsonObject>& Out)
	{
		UAbilitySystemComponent* Component = ASC(Actor);
		if (!Component || Name.IsEmpty() || Name.Len() > 128) return false;
		FProperty* Match = nullptr;
		for (const UAttributeSet* Set : Component->GetSpawnedAttributes())
		{
			if (!Set) continue;
			if (FProperty* Candidate = Set->GetClass()->FindPropertyByName(FName(*Name)))
			{
				if (Match && Match != Candidate) return false;
				Match = Candidate;
			}
		}
		if (!Match) return false;
		const FGameplayAttribute GameplayAttribute(Match);
		if (!GameplayAttribute.IsValid()) return false;
		Out->SetNumberField(TEXT("base_value"), Component->GetNumericAttributeBase(GameplayAttribute));
		Out->SetNumberField(TEXT("current_value"), Component->GetNumericAttribute(GameplayAttribute));
		return true;
	}
}

FString UUnrealBridgeRuntimeWatchLibrary::GetMultiWorldWatchSnapshot(const FString& RequestJson)
{
	using namespace BridgeWatch;
	if (!IsInGameThread() || RequestJson.Len() > 16384)
		return BridgeWatch::Error(TEXT("invalid_request"), TEXT("Bounded GameThread request required"));
	const TSharedPtr<FJsonObject> Request = Parse(RequestJson);
	const TSharedPtr<FJsonObject> Catalog = Parse(UUnrealBridgeWorldLibrary::GetWorldContexts(32));
	const TArray<TSharedPtr<FJsonValue>> *WorldHandles = nullptr, *Fields = nullptr;
	FString ActorName, ClassPath, PIESession;
	if (!Request || !Catalog || !Request->TryGetArrayField(TEXT("world_handles"), WorldHandles)
		|| !Request->TryGetArrayField(TEXT("fields"), Fields)
		|| !Request->TryGetStringField(TEXT("actor_name"), ActorName)
		|| !Request->TryGetStringField(TEXT("class_path"), ClassPath)
		|| !Request->TryGetStringField(TEXT("pie_session_id"), PIESession)
		|| PIESession.IsEmpty() || PIESession != Catalog->GetStringField(TEXT("pie_session_id"))
		|| WorldHandles->IsEmpty() || WorldHandles->Num() > 4 || Fields->IsEmpty() || Fields->Num() > 32)
		return BridgeWatch::Error(TEXT("invalid_scope"), TEXT("Exact PIE session, 1..4 world handles, actor and 1..32 fields required"));
	if (Catalog->GetBoolField(TEXT("truncated")))
		return BridgeWatch::Error(TEXT("world_catalog_truncated"), TEXT("Complete World catalog required"));
	TSet<FString> SeenWorlds;
	TArray<TSharedPtr<FJsonValue>> Snapshots;
	for (const TSharedPtr<FJsonValue>& WorldValue : *WorldHandles)
	{
		FString Handle;
		if (!WorldValue->TryGetString(Handle) || SeenWorlds.Contains(Handle))
			return BridgeWatch::Error(TEXT("invalid_world"), TEXT("Unique exact World handles required"));
		SeenWorlds.Add(Handle);
		UWorld* World = BridgeWorldContext::ResolveWorldHandle(Handle);
		if (!World || World->WorldType != EWorldType::PIE)
			return BridgeWatch::Error(TEXT("stale_world"), TEXT("World handle expired or not PIE"));
		AActor* Match = nullptr;
		int32 Matches = 0, Scanned = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (++Scanned > 50000) return BridgeWatch::Error(TEXT("actor_scan_limit"), TEXT("Actor scan exceeded 50000"));
			AActor* Candidate = *It;
			if (IsValid(Candidate) && Candidate->GetName() == ActorName && Candidate->GetClass()->GetPathName() == ClassPath)
			{
				Match = Candidate; ++Matches;
			}
		}
		if (Matches != 1) return BridgeWatch::Error(TEXT("ambiguous_actor_correlation"),
			FString::Printf(TEXT("Expected one actor in %s, found %d"), *Handle, Matches));
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("world_handle"), Handle);
		Row->SetStringField(TEXT("world_path"), World->GetPathName());
		Row->SetStringField(TEXT("net_mode"), NetMode(World->GetNetMode()));
		Row->SetStringField(TEXT("local_role"), Role(Match->GetLocalRole()));
		Row->SetStringField(TEXT("remote_role"), Role(Match->GetRemoteRole()));
		Row->SetNumberField(TEXT("world_time_seconds"), World->GetTimeSeconds());
		Row->SetNumberField(TEXT("engine_frame"), GFrameCounter);
		Row->SetNumberField(TEXT("host_monotonic_seconds"), FPlatformTime::Seconds());
		TArray<TSharedPtr<FJsonValue>> Values;
		for (const TSharedPtr<FJsonValue>& FieldValue : *Fields)
		{
			const TSharedPtr<FJsonObject> Field = FieldValue->AsObject();
			FString Kind, Name;
			if (!Field || !Field->TryGetStringField(TEXT("kind"), Kind) || !Field->TryGetStringField(TEXT("name"), Name))
				return BridgeWatch::Error(TEXT("invalid_field"), TEXT("Every field needs kind and name"));
			auto Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("kind"), Kind); Item->SetStringField(TEXT("name"), Name);
			if (Kind == TEXT("property"))
			{
				TSharedPtr<FJsonValue> Value;
				FString Why;
				if (!Property(Match, Name, Value, Why)) return BridgeWatch::Error(TEXT("unsupported_property"), Why);
				Item->SetField(TEXT("value"), Value);
			}
			else if (Kind == TEXT("gas_attribute"))
			{
				if (!Attribute(Match, Name, Item)) return BridgeWatch::Error(TEXT("unsupported_attribute"), TEXT("ASC numeric attribute absent or ambiguous"));
			}
			else if (Kind == TEXT("gameplay_tag"))
			{
				const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*Name), false);
				if (!Tag.IsValid() || !ASC(Match)) return BridgeWatch::Error(TEXT("unsupported_tag"), TEXT("Registered tag and ASC required"));
				Item->SetBoolField(TEXT("value"), ASC(Match)->HasMatchingGameplayTag(Tag));
			}
			else return BridgeWatch::Error(TEXT("invalid_field"), TEXT("Unsupported field kind"));
			Values.Add(MakeShared<FJsonValueObject>(Item));
		}
		Row->SetArrayField(TEXT("fields"), Values);
		Snapshots.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("ok"), true);
	Out->SetStringField(TEXT("schema"), TEXT("unrealbridge.runtime_watch_snapshot.v1"));
	Out->SetStringField(TEXT("editor_session_id"), Catalog->GetStringField(TEXT("editor_session_id")));
	Out->SetStringField(TEXT("pie_session_id"), PIESession);
	Out->SetStringField(TEXT("actor_name"), ActorName);
	Out->SetStringField(TEXT("class_path"), ClassPath);
	Out->SetArrayField(TEXT("worlds"), Snapshots);
	return Json(Out);
}
