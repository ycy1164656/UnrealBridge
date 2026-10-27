#include "UnrealBridgeNiagaraLibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "UnrealBridgeWorldLibrary.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraSimCache.h"
#include "Serialization/ArchiveCountMem.h"
#include "UObject/StrongObjectPtr.h"

namespace BridgeNiagaraCapture
{
	using namespace BridgeAuthoring;
	struct FEmitter { FString Id,Name; bool GPU=false,Enabled=false; };
	struct FCapture
	{
		FString Id,Owner,WorldHandle,ComponentPath,SystemPath,Status=TEXT("recording"),Reason;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UNiagaraComponent> Component;
		TStrongObjectPtr<UNiagaraSimCache> Cache;
		TArray<FEmitter> Emitters;
		TArray<double> PlatformTimes,WorldTimes;
		double Start=0,End=0,MaxSeconds=0;
		int32 RequestedFrames=0,MaxBytes=0;
		uint64 Bytes=0;
	};
	TMap<FString,TUniquePtr<FCapture>> Captures;
	FTSTicker::FDelegateHandle Ticker;
	void Finish(FCapture& Capture,const FString& Reason)
	{
		if (Capture.Status!=TEXT("recording")) return;
		Capture.End=FPlatformTime::Seconds(); Capture.Reason=Reason;
		const bool Good=Capture.Cache.IsValid() && Capture.Cache->EndWrite(false);
		Capture.Status=Good && Capture.Cache->GetNumFrames()>0?TEXT("completed"):TEXT("capture_failed");
		if (Capture.Cache.IsValid())
		{
			FArchiveCountMem Count(Capture.Cache.Get()); Capture.Bytes=Count.GetMax();
			// Include packed particle buffers even when a bulk serializer does not count them.
			uint64 ParticleBytes=0;
			for (int32 E=0;E<Capture.Cache->GetNumEmitters();++E)
			{
				uint64 Stride=0;
				Capture.Cache->ForEachEmitterAttribute(E,[&](const FNiagaraSimCacheVariable& V){ Stride+=V.FloatCount*4+V.HalfCount*2+V.Int32Count*4; return true; });
				for (int32 F=0;F<Capture.Cache->GetNumFrames();++F) ParticleBytes+=Stride*Capture.Cache->GetEmitterNumInstances(E,F);
			}
			Capture.Bytes=FMath::Max(Capture.Bytes,ParticleBytes);
			if (Capture.Bytes>uint64(Capture.MaxBytes))
			{ Capture.Status=TEXT("byte_limit"); Capture.Reason=TEXT("retained_cache_budget_exceeded"); Capture.Cache.Reset(); }
		}
	}
	void Tick()
	{
		const double Now=FPlatformTime::Seconds();
		TArray<FString> Expired;
		for (auto& Pair:Captures)
		{
			auto& C=*Pair.Value;
			if (C.Status!=TEXT("recording"))
			{ if (C.End>0 && Now-C.End>120) Expired.Add(Pair.Key); continue; }
			if (!C.World.IsValid() || C.World->bIsTearingDown || !C.Component.IsValid()) { Finish(C,TEXT("world_or_component_expired")); continue; }
			if (C.Component->GetWorld()!=C.World.Get() || !C.Component->GetAsset() || C.Component->GetAsset()->GetPathName()!=C.SystemPath)
			{ Finish(C,TEXT("component_identity_changed")); continue; }
			if (Now-C.Start>C.MaxSeconds) { Finish(C,TEXT("time_limit")); continue; }
			if (!C.Component->IsActive()) { Finish(C,TEXT("inactive")); continue; }
			const int32 Before=C.Cache->GetNumFrames();
			if (!C.Cache->WriteFrame(C.Component.Get())) { Finish(C,TEXT("write_frame_failed")); continue; }
			if (C.Cache->GetNumFrames()>Before) { C.PlatformTimes.Add(Now); C.WorldTimes.Add(C.World->GetTimeSeconds()); }
			FArchiveCountMem Count(C.Cache.Get());
			if (Count.GetMax()>uint64(C.MaxBytes)) { Finish(C,TEXT("byte_limit")); continue; }
			if (C.Cache->GetNumFrames()>=C.RequestedFrames) Finish(C,TEXT("frame_limit"));
		}
		for (const auto& Id:Expired) Captures.Remove(Id);
	}
	TSharedRef<FJsonObject> Model(const FCapture& C)
	{
		auto J=MakeShared<FJsonObject>();
		J->SetStringField(TEXT("schema"),TEXT("unrealbridge.niagara_simcache.v1"));
		J->SetBoolField(TEXT("ok"),C.Status==TEXT("recording") || C.Status==TEXT("completed"));
		J->SetStringField(TEXT("status"),C.Status); J->SetStringField(TEXT("stop_reason"),C.Reason);
		J->SetStringField(TEXT("capture_id"),C.Id); J->SetStringField(TEXT("world_handle"),C.WorldHandle);
		J->SetStringField(TEXT("component_path"),C.ComponentPath); J->SetStringField(TEXT("system_path"),C.SystemPath);
		J->SetNumberField(TEXT("frames"),C.Cache.IsValid()?C.Cache->GetNumFrames():0); J->SetNumberField(TEXT("requested_frames"),C.RequestedFrames);
		J->SetNumberField(TEXT("measured_retained_bytes"),double(C.Bytes)); J->SetNumberField(TEXT("max_bytes"),C.MaxBytes);
		J->SetNumberField(TEXT("start_platform_seconds"),C.Start); J->SetNumberField(TEXT("end_platform_seconds"),C.End);
		J->SetNumberField(TEXT("max_seconds"),C.MaxSeconds); J->SetBoolField(TEXT("simulation_advanced"),false);
		J->SetStringField(TEXT("budget_scope"),TEXT("retained cache; one native capture/readback allocation may transiently exceed the byte budget"));
		J->SetBoolField(TEXT("data_interfaces_captured"),false); J->SetNumberField(TEXT("expires_after_completion_seconds"),120);
		TArray<TSharedPtr<FJsonValue>> Emitters,Frames;
		for (const auto& E:C.Emitters)
		{ auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("handle_id"),E.Id); O->SetStringField(TEXT("name"),E.Name); O->SetBoolField(TEXT("gpu"),E.GPU); O->SetBoolField(TEXT("enabled"),E.Enabled); Emitters.Add(MakeShared<FJsonValueObject>(O)); }
		for (int32 I=0;I<C.PlatformTimes.Num();++I)
		{ auto O=MakeShared<FJsonObject>(); O->SetNumberField(TEXT("frame"),I); O->SetNumberField(TEXT("platform_seconds"),C.PlatformTimes[I]); O->SetNumberField(TEXT("world_seconds"),C.WorldTimes[I]); Frames.Add(MakeShared<FJsonValueObject>(O)); }
		J->SetArrayField(TEXT("emitters"),Emitters); J->SetArrayField(TEXT("frame_times"),Frames); return J;
	}
	FCapture* Find(const FString& Id,const FString& Owner,FString& Failure)
	{
		auto* Entry=Captures.Find(Id);
		if (!Entry || Owner.IsEmpty() || (*Entry)->Owner!=Owner) { Failure=BridgeAuthoring::Error(TEXT("CaptureIdentityMismatch"),TEXT("Exact current capture and owner token required")); return nullptr; }
		return Entry->Get();
	}
	int32 Emitter(FCapture& C,const FString& Id,const FString& Name,int32 Frame,FString& Failure)
	{
		if (C.Status==TEXT("recording")) { Failure=BridgeAuthoring::Error(TEXT("NotReady"),TEXT("Capture is still recording")); return INDEX_NONE; }
		if (C.Status!=TEXT("completed") || !C.Cache.IsValid()) { Failure=BridgeAuthoring::Error(TEXT("CaptureFailed"),C.Status+TEXT(": ")+C.Reason); return INDEX_NONE; }
		if (Frame<0 || Frame>=C.Cache->GetNumFrames()) { Failure=BridgeAuthoring::Error(TEXT("FrameOutOfRange"),TEXT("Exact captured frame required")); return INDEX_NONE; }
		const FEmitter* Matched=nullptr; int32 Names=0;
		for (const auto& E:C.Emitters) { if (E.Name==Name) ++Names; if (E.Id==Id && E.Name==Name) Matched=&E; }
		if (!Matched || Names!=1) { Failure=BridgeAuthoring::Error(TEXT("EmitterIdentityMismatch"),TEXT("Exact captured handle and unique name required")); return INDEX_NONE; }
		if (!Matched->Enabled) { Failure=BridgeAuthoring::Error(TEXT("InactiveEmitter"),TEXT("Emitter was disabled when capture began")); return INDEX_NONE; }
		const int32 Index=C.Cache->GetEmitterIndex(FName(*Name));
		if (Index==INDEX_NONE) Failure=BridgeAuthoring::Error(TEXT("EmitterNotCaptured"),TEXT("Named emitter has no captured layout"));
		return Index;
	}
	void Shutdown()
	{
		if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		Ticker.Reset(); for (auto& Pair:Captures) Finish(*Pair.Value,TEXT("module_shutdown")); Captures.Empty();
	}
}

FString UUnrealBridgeNiagaraLibrary::StartNiagaraSimCacheCapture(const FString& WorldHandle,const FString& ComponentPath,
	const FString& OwnerToken,int32 NumFrames,int32 MaxBytes,float MaxSeconds)
{
	using namespace BridgeNiagaraCapture;
	if (OwnerToken.Len()<8 || OwnerToken.Len()>128 || NumFrames<1 || NumFrames>32 || MaxBytes<65536 || MaxBytes>67108864 || !FMath::IsFinite(MaxSeconds) || MaxSeconds<=0 || MaxSeconds>15)
		return BridgeAuthoring::Error(TEXT("InvalidCaptureBudget"),TEXT("Owner 8..128 chars; frames 1..32; bytes 64KiB..64MiB; seconds (0,15]"));
	if (Captures.Num()>=4) return BridgeAuthoring::Error(TEXT("CaptureCapacity"),TEXT("Release an owned capture before starting another"));
	UWorld* World=BridgeWorldContext::ResolveWorldHandle(WorldHandle);
	auto* Component=FindObject<UNiagaraComponent>(nullptr,*ComponentPath);
	if (!World || !Component || Component->GetWorld()!=World || World->bIsTearingDown)
		return BridgeAuthoring::Error(TEXT("ComponentIdentityMismatch"),TEXT("Exact live component in the provided world required"));
	if (!Component->IsActive()) return BridgeAuthoring::Error(TEXT("InactiveComponent"),TEXT("Component is inactive; this API does not activate it"));
	auto* System=Component->GetAsset();
	if (!System || !System->IsReadyToRun()) return BridgeAuthoring::Error(TEXT("NotReady"),TEXT("Niagara system is not ready to run"));
	auto Capture=MakeUnique<FCapture>(); auto& C=*Capture;
	C.Id=FGuid::NewGuid().ToString(EGuidFormats::Digits); C.Owner=OwnerToken; C.WorldHandle=WorldHandle; C.World=World;
	C.Component=Component; C.ComponentPath=ComponentPath; C.SystemPath=System->GetPathName(); C.Start=FPlatformTime::Seconds();
	C.RequestedFrames=NumFrames; C.MaxBytes=MaxBytes; C.MaxSeconds=MaxSeconds;
	for (const auto& Handle:System->GetEmitterHandles())
	{ const auto* Data=Handle.GetEmitterData(); C.Emitters.Add({Handle.GetId().ToString(),Handle.GetName().ToString(),Data && Data->SimTarget==ENiagaraSimTarget::GPUComputeSim,Handle.GetIsEnabled()}); }
	C.Cache.Reset(NewObject<UNiagaraSimCache>(GetTransientPackage()));
	FNiagaraSimCacheCreateParameters Params; Params.AttributeCaptureMode=ENiagaraSimCacheAttributeCaptureMode::All;
	Params.bAllowDataInterfaceCaching=false; Params.bAllowInterpolation=false; Params.bIncludeDebugData=false;
	if (!C.Cache->BeginWrite(Params,Component)) return BridgeAuthoring::Error(TEXT("CaptureFailed"),TEXT("Niagara SimCache BeginWrite rejected the current simulation"));
	const FString Result=Encode(Model(C)); Captures.Add(C.Id,MoveTemp(Capture));
	if (!Ticker.IsValid()) Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){ Tick(); return true; }));
	return Result;
}

FString UUnrealBridgeNiagaraLibrary::PollNiagaraSimCacheCapture(const FString& CaptureId,const FString& OwnerToken)
{
	using namespace BridgeNiagaraCapture; FString Failure; const auto* C=Find(CaptureId,OwnerToken,Failure); return C?Encode(Model(*C)):Failure;
}

FString UUnrealBridgeNiagaraLibrary::ListNiagaraSimCacheAttributes(const FString& CaptureId,const FString& OwnerToken,
	const FString& EmitterHandleId,const FString& EmitterName,int32 FrameIndex)
{
	using namespace BridgeNiagaraCapture; FString Failure; auto* C=Find(CaptureId,OwnerToken,Failure); if (!C) return Failure;
	const int32 Index=Emitter(*C,EmitterHandleId,EmitterName,FrameIndex,Failure); if (Index==INDEX_NONE) return Failure;
	auto J=Model(*C); TArray<TSharedPtr<FJsonValue>> Attributes; int32 Total=0;
	C->Cache->ForEachEmitterAttribute(Index,[&](const FNiagaraSimCacheVariable& V)
	{
		++Total; if (Attributes.Num()>=256) return true;
		auto O=MakeShared<FJsonObject>(); O->SetStringField(TEXT("name"),V.Variable.GetName().ToString()); O->SetStringField(TEXT("type"),V.Variable.GetType().GetName());
		O->SetNumberField(TEXT("float_components"),V.FloatCount); O->SetNumberField(TEXT("half_components"),V.HalfCount); O->SetNumberField(TEXT("int_components"),V.Int32Count);
		Attributes.Add(MakeShared<FJsonValueObject>(O)); return true;
	});
	const int32 Instances=C->Cache->GetEmitterNumInstances(Index,FrameIndex);
	J->SetStringField(TEXT("sample_state"),Instances?TEXT("captured"):TEXT("zero_particles")); J->SetNumberField(TEXT("instances"),Instances);
	J->SetStringField(TEXT("emitter_name"),EmitterName); J->SetStringField(TEXT("emitter_handle_id"),EmitterHandleId); J->SetNumberField(TEXT("frame"),FrameIndex);
	J->SetArrayField(TEXT("attributes"),Attributes); J->SetNumberField(TEXT("total_attributes"),Total); J->SetBoolField(TEXT("truncated"),Total>256); return Encode(J);
}

FString UUnrealBridgeNiagaraLibrary::ReadNiagaraSimCacheAttribute(const FString& CaptureId,const FString& OwnerToken,
	const FString& EmitterHandleId,const FString& EmitterName,const FString& AttributeName,int32 FrameIndex,int32 MaxSamples)
{
	using namespace BridgeNiagaraCapture; FString Failure; auto* C=Find(CaptureId,OwnerToken,Failure); if (!C) return Failure;
	if (MaxSamples<1 || MaxSamples>64) return BridgeAuthoring::Error(TEXT("InvalidSampleBudget"),TEXT("MaxSamples must be 1..64"));
	const int32 Index=Emitter(*C,EmitterHandleId,EmitterName,FrameIndex,Failure); if (Index==INDEX_NONE) return Failure;
	FNiagaraSimCacheVariable Attribute; bool Found=false;
	C->Cache->ForEachEmitterAttribute(Index,[&](const FNiagaraSimCacheVariable& V){ if (V.Variable.GetName().ToString()==AttributeName) { Attribute=V; Found=true; return false; } return true; });
	if (!Found) return BridgeAuthoring::Error(TEXT("AttributeMissing"),TEXT("Attribute is not in the captured layout; use ListNiagaraSimCacheAttributes"));
	const int32 Instances=C->Cache->GetEmitterNumInstances(Index,FrameIndex);
	auto J=MakeShared<FJsonObject>(); J->SetBoolField(TEXT("ok"),true); J->SetStringField(TEXT("capture_id"),CaptureId);
	J->SetStringField(TEXT("status"),Instances?TEXT("captured"):TEXT("zero_particles")); J->SetStringField(TEXT("attribute"),AttributeName);
	J->SetStringField(TEXT("type"),Attribute.Variable.GetType().GetName()); J->SetStringField(TEXT("emitter_name"),EmitterName);
	J->SetStringField(TEXT("emitter_handle_id"),EmitterHandleId); J->SetNumberField(TEXT("frame"),FrameIndex); J->SetNumberField(TEXT("instances"),Instances);
	J->SetStringField(TEXT("space"),TEXT("raw cached simulation space; no world rebase"));
	TArray<float> Floats; TArray<FFloat16> Halfs; TArray<int32> Ints;
	C->Cache->ReadAttribute(Floats,Halfs,Ints,FName(*AttributeName),FName(*EmitterName),FrameIndex);
	if (Floats.Num()!=Instances*Attribute.FloatCount || Halfs.Num()!=Instances*Attribute.HalfCount || Ints.Num()!=Instances*Attribute.Int32Count)
		return BridgeAuthoring::Error(TEXT("ReadbackSizeMismatch"),TEXT("Captured layout and returned values differ"));
	TArray<TSharedPtr<FJsonValue>> Statistics,Samples;
	auto Summarize=[&](const TCHAR* Storage,int32 Width,auto& Values)
	{
		for (int32 Column=0;Column<Width;++Column)
		{
			double Sum=0,Minimum=TNumericLimits<double>::Max(),Maximum=TNumericLimits<double>::Lowest(); int32 Finite=0;
			for (int32 Row=0;Row<Instances;++Row)
			{ const double Value=double(Values[Row*Width+Column]); if (FMath::IsFinite(Value)) { Sum+=Value; Minimum=FMath::Min(Minimum,Value); Maximum=FMath::Max(Maximum,Value); ++Finite; } }
			auto S=MakeShared<FJsonObject>(); S->SetStringField(TEXT("storage"),Storage); S->SetNumberField(TEXT("component"),Column); S->SetNumberField(TEXT("finite_count"),Finite);
			S->SetNumberField(TEXT("nonfinite_count"),Instances-Finite);
			if (Finite) { S->SetNumberField(TEXT("min"),Minimum); S->SetNumberField(TEXT("max"),Maximum); S->SetNumberField(TEXT("mean"),Sum/Finite); }
			Statistics.Add(MakeShared<FJsonValueObject>(S));
		}
	};
	Summarize(TEXT("float"),Attribute.FloatCount,Floats); Summarize(TEXT("half"),Attribute.HalfCount,Halfs); Summarize(TEXT("int"),Attribute.Int32Count,Ints);
	for (int32 Row=0;Row<FMath::Min(Instances,MaxSamples);++Row)
	{
		auto S=MakeShared<FJsonObject>(); S->SetNumberField(TEXT("instance"),Row);
		auto Add=[&](const TCHAR* Key,int32 Width,auto& Values)
		{ TArray<TSharedPtr<FJsonValue>> Data; for (int32 Col=0;Col<Width;++Col) { double V=double(Values[Row*Width+Col]); if (FMath::IsFinite(V)) Data.Add(MakeShared<FJsonValueNumber>(V)); else Data.Add(MakeShared<FJsonValueNull>()); } S->SetArrayField(Key,Data); };
		Add(TEXT("floats"),Attribute.FloatCount,Floats); Add(TEXT("halfs"),Attribute.HalfCount,Halfs); Add(TEXT("ints"),Attribute.Int32Count,Ints); Samples.Add(MakeShared<FJsonValueObject>(S));
	}
	J->SetArrayField(TEXT("statistics"),Statistics); J->SetArrayField(TEXT("samples"),Samples); J->SetBoolField(TEXT("samples_truncated"),Instances>MaxSamples); return Encode(J);
}

FString UUnrealBridgeNiagaraLibrary::ReleaseNiagaraSimCacheCapture(const FString& CaptureId,const FString& OwnerToken)
{
	using namespace BridgeNiagaraCapture; FString Failure; auto* C=Find(CaptureId,OwnerToken,Failure); if (!C) return Failure;
	Finish(*C,TEXT("owner_release")); auto J=Model(*C); J->SetBoolField(TEXT("released"),true); Captures.Remove(CaptureId); return Encode(J);
}
