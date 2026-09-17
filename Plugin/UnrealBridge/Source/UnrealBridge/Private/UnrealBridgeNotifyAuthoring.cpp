#include "UnrealBridgeAnimLibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequenceBase.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimNotifies/AnimNotify_PlaySound.h"
#include "Sound/SoundBase.h"
#include "UObject/UnrealType.h"

namespace BridgeNotifyAuthoring
{
	using namespace BridgeAuthoring;
	using BridgeAuthoring::Error;
	bool Allowed(UClass* Class,FProperty* Property)
	{
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Edit) || Property->HasAnyPropertyFlags(CPF_Transient|CPF_Config|CPF_EditConst)) return false;
		if (Property->GetMetaData(TEXT("UnrealBridgeEditable"))==TEXT("true"))
			return CastField<FBoolProperty>(Property) || CastField<FFloatProperty>(Property) || CastField<FNameProperty>(Property);
		static const TSet<FName> SoundProperties={TEXT("Sound"),TEXT("VolumeMultiplier"),TEXT("PitchMultiplier"),TEXT("bFollow"),TEXT("AttachName")};
		return Class->IsChildOf(UAnimNotify_PlaySound::StaticClass()) && SoundProperties.Contains(Property->GetFName());
	}
	TSharedRef<FJsonObject> Properties(UObject* Object)
	{
		auto Values=MakeShared<FJsonObject>(); if (!Object) return Values;
		for (TFieldIterator<FProperty> It(Object->GetClass());It;++It)
		{
			FProperty* Property=*It; if (!Allowed(Object->GetClass(),Property)) continue;
			const void* Address=Property->ContainerPtrToValuePtr<void>(Object); auto Value=MakeShared<FJsonObject>();
			if (const auto* Bool=CastField<FBoolProperty>(Property)) { Value->SetStringField(TEXT("type"),TEXT("bool")); Value->SetBoolField(TEXT("value"),Bool->GetPropertyValue(Address)); }
			else if (const auto* Float=CastField<FFloatProperty>(Property)) { Value->SetStringField(TEXT("type"),TEXT("float")); Value->SetNumberField(TEXT("value"),Float->GetPropertyValue(Address)); }
			else if (const auto* Name=CastField<FNameProperty>(Property)) { Value->SetStringField(TEXT("type"),TEXT("name")); Value->SetStringField(TEXT("value"),Name->GetPropertyValue(Address).ToString()); }
			else if (const auto* Reference=CastField<FObjectPropertyBase>(Property)) { Value->SetStringField(TEXT("type"),TEXT("asset")); Value->SetStringField(TEXT("value"),GetPathNameSafe(Reference->GetObjectPropertyValue(Address))); }
			else continue;
			Values->SetObjectField(Property->GetName(),Value);
		}
		return Values;
	}
	TSharedRef<FJsonObject> EventModel(const FAnimNotifyEvent& Event)
	{
		UObject* Object=Event.Notify?static_cast<UObject*>(Event.Notify):static_cast<UObject*>(Event.NotifyStateClass);
		auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("notify_guid"),Event.Guid.ToString(EGuidFormats::Digits));
		Value->SetStringField(TEXT("kind"),Event.NotifyStateClass?TEXT("state"):Event.Notify?TEXT("notify"):TEXT("name_only"));
		Value->SetStringField(TEXT("class_path"),Object?Object->GetClass()->GetPathName():TEXT(""));
		Value->SetStringField(TEXT("object_path"),GetPathNameSafe(Object)); Value->SetStringField(TEXT("outer"),Object?Object->GetOuter()->GetPathName():TEXT(""));
		Value->SetNumberField(TEXT("time_seconds"),Event.GetTime()); Value->SetNumberField(TEXT("duration_seconds"),Event.GetDuration());
		Value->SetNumberField(TEXT("track_index"),Event.TrackIndex); Value->SetObjectField(TEXT("properties"),Properties(Object)); return Value;
	}
	TSharedRef<FJsonObject> Model(UAnimSequenceBase* Animation)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true); Value->SetStringField(TEXT("asset_path"),Animation->GetPathName());
		Value->SetNumberField(TEXT("length_seconds"),Animation->GetPlayLength()); TArray<TSharedPtr<FJsonValue>> Events,Tracks;
		for (const auto& Event:Animation->Notifies) Events.Add(MakeShared<FJsonValueObject>(EventModel(Event)));
		for (const auto& Track:Animation->AnimNotifyTracks) Tracks.Add(MakeShared<FJsonValueString>(Track.TrackName.ToString()));
		Value->SetArrayField(TEXT("notifies"),Events); Value->SetArrayField(TEXT("tracks"),Tracks); return Value;
	}
	struct FPropertyWrite { FProperty* Property=nullptr; TSharedPtr<FJsonObject> Value; UObject* Asset=nullptr; };
	struct FNotifyWrite
	{
		FGuid Guid; UClass* Class=nullptr; bool State=false; int32 ExistingIndex=INDEX_NONE,TrackIndex=0; float Time=0,Duration=0;
		TArray<FPropertyWrite> Properties; FString TrackName;
	};
	bool ParseProperty(UClass* Class,const FString& Key,const TSharedPtr<FJsonObject>& Value,FPropertyWrite& Write,FString& Message)
	{
		Write.Property=FindFProperty<FProperty>(Class,*Key); Write.Value=Value;
		if (!Allowed(Class,Write.Property) || !Fields(Value,{TEXT("type"),TEXT("value")})) { Message=TEXT("Property is not in the verified editable whitelist: ")+Key; return false; }
		const FString Type=String(Value,TEXT("type")); FProperty* Property=Write.Property;
		if (CastField<FBoolProperty>(Property))
		{ if (Type==TEXT("bool") && Value->HasTypedField<EJson::Boolean>(TEXT("value"))) return true; }
		else if (CastField<FFloatProperty>(Property))
		{
			double NumberValue=0;
			if (Type==TEXT("float") && Number(Value,TEXT("value"),NumberValue) && NumberValue>=0 && NumberValue<=4
				&& (Key!=TEXT("PitchMultiplier") || NumberValue>=0.01)) return true;
		}
		else if (CastField<FNameProperty>(Property))
		{
			const FString Name=String(Value,TEXT("value"));
			if (Type==TEXT("name") && Value->HasTypedField<EJson::String>(TEXT("value")) && Name.Len()<=64)
			{
				bool Valid=true; for (TCHAR C:Name) if (!(FChar::IsAlnum(C) || C==TEXT('_') || C==TEXT('.') || C==TEXT('-'))) Valid=false;
				if (Valid) return true;
			}
		}
		else if (CastField<FObjectPropertyBase>(Property) && Class->IsChildOf(UAnimNotify_PlaySound::StaticClass()) && Key==TEXT("Sound"))
		{
			if (Type==TEXT("asset")) { Write.Asset=LoadObject<USoundBase>(nullptr,*String(Value,TEXT("value"))); if (Write.Asset) return true; }
		}
		Message=TEXT("Property type or value outside verified bounds: ")+Key; return false;
	}
	void ApplyProperty(UObject* Object,const FPropertyWrite& Write)
	{
		void* Address=Write.Property->ContainerPtrToValuePtr<void>(Object);
		if (auto* Bool=CastField<FBoolProperty>(Write.Property)) Bool->SetPropertyValue(Address,Write.Value->GetBoolField(TEXT("value")));
		else if (auto* Float=CastField<FFloatProperty>(Write.Property)) Float->SetPropertyValue(Address,Write.Value->GetNumberField(TEXT("value")));
		else if (auto* Name=CastField<FNameProperty>(Write.Property)) Name->SetPropertyValue(Address,FName(*String(Write.Value,TEXT("value"))));
		else if (auto* Reference=CastField<FObjectPropertyBase>(Write.Property)) Reference->SetObjectPropertyValue(Address,Write.Asset);
	}
	FString Execute(const FString& Text,bool Update)
	{
		FRequest Request; FString Reply;
		if (!Parse(Text,Update?TEXT("anim.notify_update"):TEXT("anim.notify_add"),Request,Reply)) return Reply;
		auto* Animation=LoadObject<UAnimSequenceBase>(nullptr,*Request.Target);
		if (!Animation || Animation->HasParentAsset() || Animation->ChildrenAssets.Num()) return Error(TEXT("ValidationFailed"),TEXT("Editable existing animation without parent/child propagation required"));
		TArray<FNotifyWrite> Writes; TSet<FGuid> Seen; TArray<FName> Tracks;
		for (const auto& Track:Animation->AnimNotifyTracks) Tracks.Add(Track.TrackName);
		for (const auto& Raw:Request.Operations)
		{
			const TSharedPtr<FJsonObject>* Object=nullptr;
			if (!Raw->TryGetObject(Object) || !Fields(*Object,{TEXT("notify_guid"),TEXT("class_path"),TEXT("kind"),TEXT("track_index"),TEXT("track_name"),TEXT("time_seconds"),TEXT("duration_seconds"),TEXT("properties")}))
				return Error(TEXT("ValidationFailed"),TEXT("Exact typed notify fields required"));
			const auto& Op=*Object; FNotifyWrite Write; double Time=0,Duration=0,Track=0; const FString Kind=String(Op,TEXT("kind"));
			Write.State=Kind==TEXT("state"); Write.TrackName=String(Op,TEXT("track_name"));
			if ((Kind!=TEXT("notify") && Kind!=TEXT("state")) || !FGuid::ParseExact(String(Op,TEXT("notify_guid")),EGuidFormats::Digits,Write.Guid)
				|| !Write.Guid.IsValid() || Seen.Contains(Write.Guid) || !Number(Op,TEXT("time_seconds"),Time) || !Number(Op,TEXT("duration_seconds"),Duration)
				|| !Number(Op,TEXT("track_index"),Track) || Time<0 || Time>=Animation->GetPlayLength() || Duration<0 || Time+Duration>Animation->GetPlayLength()
				|| (Write.State?Duration<=0:Duration!=0) || Track<0 || Track>31 || Track!=FMath::FloorToDouble(Track) || Write.TrackName.IsEmpty() || Write.TrackName.Len()>64)
				return Error(TEXT("ValidationFailed"),TEXT("Invalid GUID, kind, track or notify time range"));
			Write.Time=Time; Write.Duration=Duration; Write.TrackIndex=Track; Seen.Add(Write.Guid);
			if (Write.TrackIndex==Tracks.Num()) Tracks.Add(FName(*Write.TrackName));
			if (!Tracks.IsValidIndex(Write.TrackIndex) || Tracks[Write.TrackIndex].ToString()!=Write.TrackName) return Error(TEXT("ValidationFailed"),TEXT("Track index/name mismatch; track gaps are forbidden"));
			Write.ExistingIndex=Animation->Notifies.IndexOfByPredicate([&](const FAnimNotifyEvent& Event){return Event.Guid==Write.Guid;});
			if (Update!=(Write.ExistingIndex!=INDEX_NONE)) return Error(TEXT("TargetRevisionMismatch"),TEXT("Add GUID already exists, or update GUID no longer exists"));
			Write.Class=LoadClass<UObject>(nullptr,*String(Op,TEXT("class_path")));
			if (!Write.Class || !Write.Class->IsChildOf(Write.State?UAnimNotifyState::StaticClass():UAnimNotify::StaticClass())
				|| Write.Class->HasAnyClassFlags(CLASS_Abstract|CLASS_Deprecated|CLASS_NewerVersionExists)) return Error(TEXT("ValidationFailed"),TEXT("Concrete current AnimNotify/AnimNotifyState subclass required"));
			UObject* ExistingObject=nullptr;
			if (Update)
			{
				const auto& Event=Animation->Notifies[Write.ExistingIndex]; ExistingObject=Write.State?static_cast<UObject*>(Event.NotifyStateClass):static_cast<UObject*>(Event.Notify);
				if (!ExistingObject || ExistingObject->GetClass()!=Write.Class || ExistingObject->GetOuter()!=Animation) return Error(TEXT("ScopeViolation"),TEXT("Update cannot replace class/kind or edit a foreign notify instance"));
			}
			const TSharedPtr<FJsonObject>* Props=nullptr;
			if (!Op->TryGetObjectField(TEXT("properties"),Props) || (*Props)->Values.Num()>16) return Error(TEXT("ValidationFailed"),TEXT("Bounded typed property map required"));
			for (const auto& Pair:(*Props)->Values)
			{
				const TSharedPtr<FJsonObject>* Value=nullptr; FPropertyWrite Property; FString Message;
				if (!Pair.Value->TryGetObject(Value) || !ParseProperty(Write.Class,FString(Pair.Key),*Value,Property,Message)) return Error(TEXT("ValidationFailed"),Message);
				Write.Properties.Add(MoveTemp(Property));
			}
			Writes.Add(MoveTemp(Write));
		}
		if (Animation->Notifies.Num()+(Update?0:Writes.Num())>512) return Error(TEXT("ValidationFailed"),TEXT("Notify budget exceeded"));
		if (Request.DryRun)
		{
			auto Result=Model(Animation); Result->SetArrayField(TEXT("planned_notifies"),Request.Operations); return Preview(Request,Result);
		}
		if (!Begin(Request,Reply)) return Reply;
		Animation->Modify();
		while (Animation->AnimNotifyTracks.Num()<Tracks.Num())
		{
			FAnimNotifyTrack Track; Track.TrackName=Tracks[Animation->AnimNotifyTracks.Num()]; Track.TrackColor=FLinearColor::White; Animation->AnimNotifyTracks.Add(Track);
		}
		for (const auto& Write:Writes)
		{
			const int32 Index=Update?Write.ExistingIndex:Animation->Notifies.AddDefaulted(); auto& Event=Animation->Notifies[Index];
			UObject* Instance=Update?(Write.State?static_cast<UObject*>(Event.NotifyStateClass):static_cast<UObject*>(Event.Notify)):
				NewObject<UObject>(Animation,Write.Class,NAME_None,RF_Transactional);
			Instance->Modify(); for (const auto& Property:Write.Properties) ApplyProperty(Instance,Property);
			Event.Guid=Write.Guid; Event.TrackIndex=Write.TrackIndex; Event.NotifyName=Write.Class->GetFName();
			if (Write.State) Event.NotifyStateClass=CastChecked<UAnimNotifyState>(Instance); else Event.Notify=CastChecked<UAnimNotify>(Instance);
			Event.Link(Animation,Write.Time); Event.ChangeLinkMethod(EAnimLinkMethod::Absolute);
			Event.SetDuration(Write.Duration); Event.EndLink.Link(Animation,Write.Time+Write.Duration); Event.EndLink.ChangeLinkMethod(EAnimLinkMethod::Absolute);
			Event.TriggerTimeOffset=GetTriggerTimeOffsetForType(Animation->CalculateOffsetForNotify(Write.Time));
			Event.EndTriggerTimeOffset=GetTriggerTimeOffsetForType(Animation->CalculateOffsetForNotify(Write.Time+Write.Duration));
			Event.bTriggerOnDedicatedServer=true;
		}
		Animation->Notifies.Sort([](const FAnimNotifyEvent& A,const FAnimNotifyEvent& B){return A.GetTime()<B.GetTime();});
		Animation->RefreshCacheData(); Animation->PostEditChange(); Animation->MarkPackageDirty();
		return Finish(Request,Model(Animation));
	}
}

FString UUnrealBridgeAnimLibrary::GetNotifyEditModel(const FString& AnimationPath)
{
	if (auto* Animation=LoadObject<UAnimSequenceBase>(nullptr,*AnimationPath)) return BridgeAuthoring::Encode(BridgeNotifyAuthoring::Model(Animation));
	return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Animation unavailable"));
}
FString UUnrealBridgeAnimLibrary::AddTypedAnimNotify(const FString& RequestJson) { return BridgeNotifyAuthoring::Execute(RequestJson,false); }
FString UUnrealBridgeAnimLibrary::UpdateTypedAnimNotify(const FString& RequestJson) { return BridgeNotifyAuthoring::Execute(RequestJson,true); }
