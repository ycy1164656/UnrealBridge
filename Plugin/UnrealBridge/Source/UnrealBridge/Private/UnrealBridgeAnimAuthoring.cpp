#include "UnrealBridgeAnimLibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace BridgeAnimAuthoring
{
	using namespace BridgeAuthoring;
	using BridgeAuthoring::Error;
	struct FSegment { FString Stable; FAnimSegment Value; };
	struct FSlot { FName Name; TArray<FSegment> Segments; };
	struct FPlan { UAnimMontage* Existing=nullptr; USkeleton* Skeleton=nullptr; TArray<FSlot> Slots; float Length=0; bool Create=false; };
	TSharedRef<FJsonObject> SegmentModel(const FAnimSegment& Segment)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("sequence_path"),GetPathNameSafe(Segment.GetAnimReference()));
		Value->SetNumberField(TEXT("source_start_seconds"),Segment.AnimStartTime); Value->SetNumberField(TEXT("source_end_seconds"),Segment.AnimEndTime);
		Value->SetNumberField(TEXT("montage_start_seconds"),Segment.StartPos); Value->SetNumberField(TEXT("play_rate"),Segment.AnimPlayRate);
		Value->SetNumberField(TEXT("loop_count"),Segment.LoopingCount); Value->SetNumberField(TEXT("length_seconds"),Segment.GetLength());
		Value->SetNumberField(TEXT("source_rate_scale"),Segment.GetAnimReference()?Segment.GetAnimReference()->RateScale:0);
		return Value;
	}
	FString Fingerprint(const FAnimSegment& Segment) { return Hash(Encode(SegmentModel(Segment))); }
	FString Stable(const FName& Slot,int32 Index,const FAnimSegment& Segment)
	{ return Slot.ToString()+TEXT("|")+FString::FromInt(Index)+TEXT("|")+Fingerprint(Segment); }
	FPlan FromMontage(UAnimMontage* Montage)
	{
		FPlan Plan; Plan.Existing=Montage; Plan.Skeleton=Montage?Montage->GetSkeleton():nullptr;
		if (Montage) for (const auto& Track:Montage->SlotAnimTracks)
		{
			FSlot Slot; Slot.Name=Track.SlotName;
			for (int32 Index=0;Index<Track.AnimTrack.AnimSegments.Num();++Index)
			{ const auto& Segment=Track.AnimTrack.AnimSegments[Index]; Slot.Segments.Add({Stable(Slot.Name,Index,Segment),Segment}); }
			Plan.Slots.Add(MoveTemp(Slot));
		}
		Plan.Length=Montage?Montage->GetPlayLength():0; return Plan;
	}
	TSharedRef<FJsonObject> Model(const FString& Target,const FPlan& Plan)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true); Value->SetStringField(TEXT("asset_path"),Target);
		Value->SetStringField(TEXT("skeleton"),GetPathNameSafe(Plan.Skeleton)); Value->SetNumberField(TEXT("length_seconds"),Plan.Length);
		TArray<TSharedPtr<FJsonValue>> Slots;
		for (const auto& Slot:Plan.Slots)
		{
			auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("slot_name"),Slot.Name.ToString());
			Item->SetStringField(TEXT("group"),Plan.Skeleton?Plan.Skeleton->GetSlotGroupName(Slot.Name).ToString():TEXT(""));
			TArray<TSharedPtr<FJsonValue>> Segments;
			for (int32 Index=0;Index<Slot.Segments.Num();++Index)
			{
				const auto& Segment=Slot.Segments[Index].Value; auto Entry=SegmentModel(Segment);
				Entry->SetNumberField(TEXT("index"),Index); Entry->SetStringField(TEXT("fingerprint"),Fingerprint(Segment));
				Segments.Add(MakeShared<FJsonValueObject>(Entry));
			}
			Item->SetArrayField(TEXT("segments"),Segments); Slots.Add(MakeShared<FJsonValueObject>(Item));
		}
		Value->SetArrayField(TEXT("slots"),Slots);
		TArray<TSharedPtr<FJsonValue>> Sections;
		if (Plan.Existing) for (const auto& Section:Plan.Existing->CompositeSections)
		{
			auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("name"),Section.SectionName.ToString());
			Item->SetNumberField(TEXT("time_seconds"),Section.GetTime()); Item->SetStringField(TEXT("next"),Section.NextSectionName.ToString());
			Item->SetNumberField(TEXT("link_method"),Section.GetLinkMethod()); Sections.Add(MakeShared<FJsonValueObject>(Item));
		}
		Value->SetArrayField(TEXT("sections"),Sections); Value->SetBoolField(TEXT("root_motion"),Plan.Existing && Plan.Existing->HasRootMotion());
		Value->SetStringField(TEXT("structure_revision"),Hash(Encode(Value))); return Value;
	}
	bool ReadSegment(const TSharedPtr<FJsonObject>& Input,USkeleton* Skeleton,FAnimSegment& Segment,FString& Message)
	{
		const FString Path=String(Input,TEXT("sequence_path"));
		auto* Sequence=LoadObject<UAnimSequence>(nullptr,*Path); double Start=0,End=0,MontageStart=0,Rate=0,Loops=0;
		if (!Sequence || Sequence->GetSkeleton()!=Skeleton || !Number(Input,TEXT("source_start_seconds"),Start)
			|| !Number(Input,TEXT("source_end_seconds"),End) || !Number(Input,TEXT("montage_start_seconds"),MontageStart)
			|| !Number(Input,TEXT("play_rate"),Rate) || !Number(Input,TEXT("loop_count"),Loops)
			|| Start<0 || End<=Start || End>Sequence->GetPlayLength()+UE_KINDA_SMALL_NUMBER || MontageStart<0 || MontageStart>3600
			|| Rate<0.01 || Rate>100 || !FMath::IsFinite(Sequence->RateScale) || Sequence->RateScale<=0
			|| Loops<1 || Loops>100 || Loops!=FMath::FloorToDouble(Loops))
		{ Message=TEXT("Sequence, skeleton, positive source/rate range or integer loop count invalid"); return false; }
		Segment.SetAnimReference(Sequence,false); Segment.AnimStartTime=Start; Segment.AnimEndTime=End;
		Segment.StartPos=MontageStart; Segment.AnimPlayRate=Rate; Segment.LoopingCount=static_cast<int32>(Loops); Segment.UpdateCachedPlayLength();
		if (!FMath::IsFinite(Segment.GetLength()) || Segment.GetLength()<=0 || Segment.GetEndPos()>3600)
		{ Message=TEXT("Calculated engine segment duration exceeds budget"); return false; }
		return true;
	}
	bool Build(FRequest& Request,FPlan& Plan,FString& Message)
	{
		const auto Snapshot=Request.Snapshot->GetObjectField(TEXT("targets"))->GetObjectField(Request.Target);
		Plan=FromMontage(Snapshot->GetBoolField(TEXT("exists"))?LoadObject<UAnimMontage>(nullptr,*Request.Target):nullptr);
		if (Snapshot->GetBoolField(TEXT("exists")) && !Plan.Existing) { Message=TEXT("Target is not a Montage"); return false; }
		if (Plan.Existing && (Plan.Existing->HasParentAsset() || Plan.Existing->ChildrenAssets.Num())) { Message=TEXT("Parent/child propagation requires a separate explicit target plan"); return false; }
		for (const auto& Raw:Request.Operations)
		{
			const TSharedPtr<FJsonObject>* Object=nullptr; if (!Raw->TryGetObject(Object)) { Message=TEXT("Operation must be an object"); return false; }
			const auto& Op=*Object; const FString Kind=String(Op,TEXT("op"));
			if (Kind==TEXT("create_montage"))
			{
				if (!Fields(Op,{TEXT("op"),TEXT("skeleton_path")}) || Plan.Existing || Plan.Create || Plan.Slots.Num()) { Message=TEXT("Create only accepts an absent target as the first operation"); return false; }
				Plan.Skeleton=LoadObject<USkeleton>(nullptr,*String(Op,TEXT("skeleton_path")));
				if (!Plan.Skeleton) { Message=TEXT("Existing source skeleton required"); return false; } Plan.Create=true; continue;
			}
			if (!Plan.Skeleton) { Message=TEXT("Target or create_montage must supply an existing skeleton"); return false; }
			if (Kind==TEXT("add_slot"))
			{
				const FName Name(*String(Op,TEXT("slot_name")));
				if (!Fields(Op,{TEXT("op"),TEXT("slot_name")}) || !Plan.Skeleton->ContainsSlotName(Name) || Plan.Slots.ContainsByPredicate([&](const FSlot& Slot){return Slot.Name==Name;}) || Plan.Slots.Num()>=8)
				{ Message=TEXT("Slot must already exist in source skeleton and be unique in this montage"); return false; }
				Plan.Slots.Add({Name,{}}); continue;
			}
			if (Kind==TEXT("add_segment"))
			{
				if (!Fields(Op,{TEXT("op"),TEXT("slot_name"),TEXT("sequence_path"),TEXT("source_start_seconds"),TEXT("source_end_seconds"),TEXT("montage_start_seconds"),TEXT("play_rate"),TEXT("loop_count")})) { Message=TEXT("Exact add_segment fields required"); return false; }
				auto* Slot=Plan.Slots.FindByPredicate([&](const FSlot& Value){return Value.Name.ToString()==String(Op,TEXT("slot_name"));});
				FAnimSegment Segment; if (!Slot || !ReadSegment(Op,Plan.Skeleton,Segment,Message)) return false;
				Slot->Segments.Add({FGuid::NewGuid().ToString(),Segment}); continue;
			}
			if (Kind!=TEXT("update_segment") && Kind!=TEXT("move_segment")) { Message=TEXT("Unsupported segment operation; removal is never implicit"); return false; }
			const TSharedPtr<FJsonObject>* Target=nullptr; double Index=0;
			if (!Op->TryGetObjectField(TEXT("segment"),Target) || !Fields(*Target,{TEXT("slot_name"),TEXT("index"),TEXT("fingerprint")})
				|| !Number(*Target,TEXT("index"),Index) || Index<0 || Index>128 || FMath::FloorToDouble(Index)!=Index)
			{ Message=TEXT("Original slot/index/fingerprint segment identity required"); return false; }
			const FString Key=String(*Target,TEXT("slot_name"))+TEXT("|")+FString::FromInt(Index)+TEXT("|")+String(*Target,TEXT("fingerprint"));
			FSlot* FoundSlot=nullptr; int32 FoundIndex=INDEX_NONE;
			for (auto& Slot:Plan.Slots) for (int32 Item=0;Item<Slot.Segments.Num();++Item) if (Slot.Segments[Item].Stable==Key) { FoundSlot=&Slot; FoundIndex=Item; }
			if (!FoundSlot) { Message=TEXT("Segment fingerprint or original index is stale"); return false; }
			if (Kind==TEXT("update_segment"))
			{
				if (!Fields(Op,{TEXT("op"),TEXT("segment"),TEXT("sequence_path"),TEXT("source_start_seconds"),TEXT("source_end_seconds"),TEXT("montage_start_seconds"),TEXT("play_rate"),TEXT("loop_count")})) { Message=TEXT("Exact update_segment fields required"); return false; }
				if (!ReadSegment(Op,Plan.Skeleton,FoundSlot->Segments[FoundIndex].Value,Message)) return false;
			}
			else
			{
				double Start=0; auto* Destination=Plan.Slots.FindByPredicate([&](const FSlot& Slot){return Slot.Name.ToString()==String(Op,TEXT("slot_name"));});
				if (!Fields(Op,{TEXT("op"),TEXT("segment"),TEXT("slot_name"),TEXT("montage_start_seconds")}) || !Number(Op,TEXT("montage_start_seconds"),Start) || Start<0 || Start>3600 || !Destination)
				{ Message=TEXT("Invalid move destination"); return false; }
				FSegment Moved=FoundSlot->Segments[FoundIndex]; Moved.Value.StartPos=Start;
				FoundSlot->Segments.RemoveAt(FoundIndex); Destination->Segments.Add(MoveTemp(Moved));
			}
		}
		FName Group=NAME_None; int32 SegmentCount=0; Plan.Length=0;
		for (auto& Slot:Plan.Slots)
		{
			const FName ThisGroup=Plan.Skeleton->GetSlotGroupName(Slot.Name);
			if (ThisGroup.IsNone() || (!Group.IsNone() && Group!=ThisGroup)) { Message=TEXT("All slots must belong to the same existing skeleton group"); return false; } Group=ThisGroup;
			Slot.Segments.Sort([](const FSegment& A,const FSegment& B){return A.Value.StartPos<B.Value.StartPos;}); float End=0;
			for (const auto& Segment:Slot.Segments)
			{
				if (++SegmentCount>128 || !FMath::IsNearlyEqual(Segment.Value.StartPos,End,0.0001f)) { Message=TEXT("Gaps, overlaps, or segment budget violation"); return false; }
				End=Segment.Value.GetEndPos();
			}
			Plan.Length=FMath::Max(Plan.Length,End);
		}
		if (SegmentCount==0 || Plan.Length<=0 || Plan.Length>3600) { Message=TEXT("At least one valid segment is required"); return false; }
		if (Plan.Existing)
		{
			for (const auto& Section:Plan.Existing->CompositeSections)
				if (Section.GetTime()<0 || Section.GetTime()>=Plan.Length || Section.GetLinkMethod()!=EAnimLinkMethod::Absolute)
				{ Message=TEXT("Existing section would be out of bounds or requires explicit relative retiming"); return false; }
			for (const auto& Notify:Plan.Existing->Notifies)
				if (Notify.GetTime()<0 || Notify.GetTime()+Notify.GetDuration()>Plan.Length || Notify.GetLinkMethod()!=EAnimLinkMethod::Absolute)
				{ Message=TEXT("Existing notify would be out of bounds or requires explicit relative retiming"); return false; }
		}
		return true;
	}
	FString Execute(const FString& Text,bool PreviewOnly)
	{
		FRequest Request; FString Reply; if (!Parse(Text,TEXT("anim.montage_segments"),Request,Reply)) return Reply;
		if (Request.DryRun!=PreviewOnly) return Error(TEXT("ScopeViolation"),TEXT("Preview/apply entry must match explicit dry_run"));
		FPlan Plan; FString Message;
		if (!Build(Request,Plan,Message)) return Error(TEXT("ValidationFailed"),Message);
		if (PreviewOnly) return Preview(Request,Model(Request.Target,Plan));
		if (!Begin(Request,Reply)) return Reply;
		UAnimMontage* Montage=Plan.Existing;
		if (Plan.Create)
		{
			UPackage* Package=CreatePackage(*Request.Target);
			Montage=NewObject<UAnimMontage>(Package,*FPackageName::GetLongPackageAssetName(Request.Target),RF_Public|RF_Standalone|RF_Transactional);
			Montage->SetSkeleton(Plan.Skeleton); FAssetRegistryModule::AssetCreated(Montage);
		}
		Montage->Modify();
		TArray<float> SectionTimes,NotifyTimes,NotifyDurations;
		for (const auto& Section:Montage->CompositeSections) SectionTimes.Add(Section.GetTime());
		for (const auto& Notify:Montage->Notifies) { NotifyTimes.Add(Notify.GetTime()); NotifyDurations.Add(Notify.GetDuration()); }
		Montage->SlotAnimTracks.Reset();
		for (const auto& Slot:Plan.Slots)
		{
			FSlotAnimationTrack Track; Track.SlotName=Slot.Name;
			for (const auto& Segment:Slot.Segments) Track.AnimTrack.AnimSegments.Add(Segment.Value);
			Montage->SlotAnimTracks.Add(MoveTemp(Track));
		}
		Montage->SetCompositeLength(Montage->CalculateSequenceLength()); Montage->InvalidateRecursiveAsset();
		if (Plan.Create) Montage->AddAnimCompositeSection(TEXT("Default"),0);
		for (int32 Index=0;Index<SectionTimes.Num();++Index) Montage->CompositeSections[Index].Link(Montage,SectionTimes[Index],Montage->CompositeSections[Index].GetSlotIndex());
		for (int32 Index=0;Index<NotifyTimes.Num();++Index)
		{
			auto& Notify=Montage->Notifies[Index]; Notify.Link(Montage,NotifyTimes[Index],Notify.GetSlotIndex());
			Notify.SetDuration(NotifyDurations[Index]); Notify.EndLink.Link(Montage,NotifyTimes[Index]+NotifyDurations[Index],Notify.GetSlotIndex());
		}
		Montage->UpdateLinkableElements(); Montage->RefreshCacheData(); Montage->PostEditChange(); Montage->MarkPackageDirty();
		const auto Actual=FromMontage(Montage);
		if (!FMath::IsNearlyEqual(Actual.Length,Plan.Length,0.0001f)) return Error(TEXT("NeedsReconciliation"),TEXT("Native derived length differs; retained unsaved"),TEXT("retained_unsaved"));
		return Finish(Request,Model(Request.Target,Actual));
	}
}

FString UUnrealBridgeAnimLibrary::GetMontageEditModel(const FString& MontagePath)
{
	if (auto* Montage=LoadObject<UAnimMontage>(nullptr,*MontagePath)) return BridgeAuthoring::Encode(BridgeAnimAuthoring::Model(MontagePath,BridgeAnimAuthoring::FromMontage(Montage)));
	return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Montage unavailable"));
}
FString UUnrealBridgeAnimLibrary::PreviewMontageSegmentOps(const FString& RequestJson) { return BridgeAnimAuthoring::Execute(RequestJson,true); }
FString UUnrealBridgeAnimLibrary::ApplyMontageSegmentOps(const FString& RequestJson) { return BridgeAnimAuthoring::Execute(RequestJson,false); }
