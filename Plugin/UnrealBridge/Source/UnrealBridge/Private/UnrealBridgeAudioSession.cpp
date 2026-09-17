#include "UnrealBridgeAudioLibrary.h"
#include "UnrealBridgeAudioRoutingPrivate.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "UnrealBridgeWorldLibrary.h"
#include "AudioDevice.h"
#include "AudioDeviceManager.h"
#include "AudioThread.h"
#include "Components/AudioComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "IAudioModulation.h"
#include "Misc/ScopeLock.h"
#include "Misc/App.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace BridgeAudioSession
{
	using namespace BridgeAuthoring;
	using BridgeAuthoring::Error;
	struct FSource
	{
		TWeakObjectPtr<UAudioComponent> Component;
		TWeakObjectPtr<USoundModulatorBase> VolumeBus;
		FDelegateHandle EnvelopeHandle,PercentHandle;
		FString SoundPath,BusPath;
		uint64 ComponentId=0;
	};
	struct FMeter
	{
		uint32 EnvelopeSamples=0,PercentSamples=0;
		float PeakEnvelope=0,PlaybackPercent=0;
		bool AudioThreadActive=false;
		TSet<FString> WavePaths;
	};
	struct FSession
	{
		FString Id,RequestId,Digest,WorldHandle,Status=TEXT("active"),EndReason;
		FString SourceMixDigest,SourceBusMixDigest;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<AActor> Owner;
		TWeakObjectPtr<USoundMix> SourceMix;
		TWeakObjectPtr<UObject> SourceBusMix;
		TStrongObjectPtr<USoundMix> OwnedMix;
		TStrongObjectPtr<UObject> OwnedBusMix;
		FAudioDeviceHandle Device;
		uint32 DeviceId=0;
		double Expires=0;
		bool Pushed=false,BusActivated=false,SourceChanged=false;
		FCriticalSection MeterLock;
		TArray<FSource> Sources;
		TArray<FMeter> Meters;
		uint32 AudioSnapshots=0,MixRefs=0;
		bool MixPresent=false,DeviceMuted=false;
		float DevicePrimaryVolume=0,DeviceTransientVolume=0;
	};
	TMap<FString,TSharedPtr<FSession>> Sessions;
	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle CleanupHandle;
	UWorld* ResolveWorld(const FString& Handle)
	{
		const auto Catalog=Decode(UUnrealBridgeWorldLibrary::GetWorldContexts(64)); const TArray<TSharedPtr<FJsonValue>>* Worlds=nullptr;
		if (!Catalog || !Catalog->TryGetArrayField(TEXT("worlds"),Worlds)) return nullptr;
		for (const auto& Entry:*Worlds) { const TSharedPtr<FJsonObject>* O=nullptr; if (Entry->TryGetObject(O) && String(*O,TEXT("world_handle"))==Handle) return FindObject<UWorld>(nullptr,*String(*O,TEXT("world_path"))); }
		return nullptr;
	}
	float BusValue(UWorld* World,USoundModulatorBase* Bus)
	{
		auto* C=BridgeAudioRouting::OptionalClass(TEXT("AudioModulationStatics")); auto* Fn=C?C->FindFunctionByName(TEXT("GetModulatorValue")):nullptr;
		if (!Fn || !World || !Bus) return -1;
		auto* W=FindFProperty<FObjectPropertyBase>(Fn,TEXT("WorldContextObject")); auto* B=FindFProperty<FObjectPropertyBase>(Fn,TEXT("Modulator")); auto* R=FindFProperty<FFloatProperty>(Fn,TEXT("ReturnValue"));
		if (!W || !B || !R || !Bus->IsA(B->PropertyClass)) return -1;
		FStructOnScope Params(Fn); W->SetObjectPropertyValue_InContainer(Params.GetStructMemory(),World); B->SetObjectPropertyValue_InContainer(Params.GetStructMemory(),Bus);
		C->GetDefaultObject()->ProcessEvent(Fn,Params.GetStructMemory()); return R->GetPropertyValue_InContainer(Params.GetStructMemory());
	}
	void SampleAudioThread(const TSharedPtr<FSession>& Session)
	{
		if (!Session->Device.IsValid()) return;
		const TWeakPtr<FSession> Weak=Session; FAudioDeviceHandle Device=Session->Device;
		USoundMix* Mix=Session->OwnedMix.Get(); TArray<uint64> Ids; for (const auto& S:Session->Sources) Ids.Add(S.ComponentId);
		FAudioThread::RunCommandOnAudioThread([Weak,Device,Mix,Ids]() mutable
		{
			if (const auto S=Weak.Pin())
			{
				{
					FScopeLock Lock(&S->MeterLock); ++S->AudioSnapshots;
					const auto* State=Mix?Device->GetSoundMixModifiers().Find(Mix):nullptr;
					S->MixPresent=State!=nullptr; S->MixRefs=State?State->ActiveRefCount:0;
					S->DeviceMuted=Device->IsAudioDeviceMuted(); S->DevicePrimaryVolume=Device->GetPrimaryVolume(); S->DeviceTransientVolume=Device->GetTransientPrimaryVolume();
					for (auto& Meter:S->Meters) Meter.AudioThreadActive=false;
				}
				for (int32 I=0;I<Ids.Num();++I) Device->SendCommandToActiveSounds(Ids[I],[Weak,I](FActiveSound&)
				{ if (const auto V=Weak.Pin()) { FScopeLock Lock(&V->MeterLock); V->Meters[I].AudioThreadActive=true; } });
			}
		});
	}
	bool BusActive(const TSharedPtr<FSession>& S)
	{
		bool Active=false;
		if (S->OwnedBusMix.IsValid() && S->World.IsValid()) BridgeAudioRouting::CallBusMix(TEXT("IsControlBusMixActive"),S->World.Get(),S->OwnedBusMix.Get(),&Active);
		return Active;
	}
	void Stop(const TSharedPtr<FSession>& S,const TCHAR* Reason)
	{
		if (S->Status!=TEXT("active")) return;
		S->Status=TEXT("ending"); S->EndReason=Reason;
		for (auto& Source:S->Sources) if (auto* C=Source.Component.Get())
		{
			C->OnAudioSingleEnvelopeValueNative.Remove(Source.EnvelopeHandle); C->OnAudioPlaybackPercentNative.Remove(Source.PercentHandle);
			C->Stop(); C->DestroyComponent();
		}
		if (S->Pushed && S->Device.IsValid()) { S->Device->PopSoundMixModifier(S->OwnedMix.Get()); S->Pushed=false; }
		if (S->BusActivated && S->World.IsValid())
		{ BridgeAudioRouting::CallBusMix(TEXT("DeactivateBusMix"),S->World.Get(),S->OwnedBusMix.Get()); S->BusActivated=false; }
		if (auto* Owner=S->Owner.Get()) Owner->Destroy();
		SampleAudioThread(S);
	}
	void Refresh(const TSharedPtr<FSession>& S)
	{
		if (S->SourceMix.IsValid()) S->SourceChanged|=S->SourceMixDigest!=Hash(Encode(BridgeAudioRouting::Model(S->SourceMix.Get())));
		if (S->SourceBusMix.IsValid()) S->SourceChanged|=S->SourceBusMixDigest!=Hash(Encode(BridgeAudioRouting::Model(S->SourceBusMix.Get())));
		if (S->Status==TEXT("active"))
		{
			if (!S->World.IsValid() || !S->Device.IsValid() || !S->Owner.IsValid()) Stop(S,TEXT("owned_object_destroyed"));
			else if (S->World->GetAudioDevice().GetDeviceID()!=S->DeviceId) Stop(S,TEXT("audio_device_changed"));
			else if (FPlatformTime::Seconds()>=S->Expires) Stop(S,TEXT("lease_expired"));
		}
		if (S->Status==TEXT("ending"))
		{
			bool AudioClear=false;
			{ FScopeLock Lock(&S->MeterLock); AudioClear=S->AudioSnapshots>0 && !S->MixPresent; for (const auto& M:S->Meters) AudioClear&=!M.AudioThreadActive; }
			if (AudioClear && !BusActive(S)) { S->Status=TEXT("ended"); S->Device=FAudioDeviceHandle(); }
		}
		if (S->Status!=TEXT("ended")) SampleAudioThread(S);
	}
	void EnsureObservers()
	{
		if (!TickHandle.IsValid()) TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{ for (const auto& Pair:Sessions) Refresh(Pair.Value); return true; }),.1f);
		if (!CleanupHandle.IsValid()) CleanupHandle=FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World,bool,bool)
		{ for (const auto& Pair:Sessions) if (Pair.Value->World.Get()==World) Stop(Pair.Value,TEXT("world_cleanup")); });
	}
	TSharedRef<FJsonObject> Model(const TSharedPtr<FSession>& S)
	{
		Refresh(S); auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true);
		Value->SetStringField(TEXT("schema"),TEXT("unrealbridge.audio_session.v1")); Value->SetStringField(TEXT("session_id"),S->Id);
		Value->SetStringField(TEXT("world_handle"),S->WorldHandle); Value->SetNumberField(TEXT("audio_device_id"),S->DeviceId);
		Value->SetStringField(TEXT("status"),S->Status); Value->SetStringField(TEXT("end_reason"),S->EndReason);
		Value->SetBoolField(TEXT("source_asset_changed"),S->SourceChanged); Value->SetBoolField(TEXT("source_asset_restored"),false);
		Value->SetBoolField(TEXT("owned_actor_alive"),S->Owner.IsValid()); Value->SetBoolField(TEXT("bus_mix_active"),BusActive(S));
		Value->SetStringField(TEXT("owned_mix_path"),S->OwnedMix.IsValid()?S->OwnedMix->GetPathName():TEXT(""));
		Value->SetStringField(TEXT("owned_bus_mix_path"),S->OwnedBusMix.IsValid()?S->OwnedBusMix->GetPathName():TEXT(""));
		TArray<TSharedPtr<FJsonValue>> Sources;
		FScopeLock Lock(&S->MeterLock); Value->SetNumberField(TEXT("audio_thread_snapshots"),S->AudioSnapshots);
		Value->SetBoolField(TEXT("sound_mix_present"),S->MixPresent); Value->SetNumberField(TEXT("sound_mix_active_refs"),S->MixRefs);
		Value->SetBoolField(TEXT("audio_thread_device_muted"),S->DeviceMuted);
		Value->SetNumberField(TEXT("device_primary_volume"),S->DevicePrimaryVolume); Value->SetNumberField(TEXT("device_transient_volume"),S->DeviceTransientVolume);
		Value->SetNumberField(TEXT("app_volume_multiplier"),FApp::GetVolumeMultiplier()); Value->SetNumberField(TEXT("unfocused_volume_multiplier"),FApp::GetUnfocusedVolumeMultiplier());
		for (int32 I=0;I<S->Sources.Num();++I)
		{
			const auto& Source=S->Sources[I]; const auto& M=S->Meters[I]; auto Row=MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("sound_path"),Source.SoundPath); Row->SetStringField(TEXT("volume_bus_path"),Source.BusPath);
			Row->SetStringField(TEXT("component_id"),LexToString(Source.ComponentId)); Row->SetBoolField(TEXT("audio_thread_active"),M.AudioThreadActive);
			Row->SetNumberField(TEXT("envelope_samples"),M.EnvelopeSamples); Row->SetNumberField(TEXT("peak_envelope"),M.PeakEnvelope);
			Row->SetNumberField(TEXT("playback_percent_samples"),M.PercentSamples); Row->SetNumberField(TEXT("playback_percent"),M.PlaybackPercent);
			Row->SetBoolField(TEXT("component_playing"),Source.Component.IsValid() && Source.Component->IsPlaying());
			Row->SetBoolField(TEXT("virtualized"),Source.Component.IsValid() && Source.Component->IsVirtualized());
			Row->SetNumberField(TEXT("volume_bus_value_normalized"),BusValue(S->World.Get(),Source.VolumeBus.Get()));
			TArray<TSharedPtr<FJsonValue>> Waves; for (const FString& Path:M.WavePaths) Waves.Add(MakeShared<FJsonValueString>(Path)); Row->SetArrayField(TEXT("observed_wave_paths"),Waves);
			Sources.Add(MakeShared<FJsonValueObject>(Row));
		}
		Value->SetArrayField(TEXT("sources"),Sources); Value->SetStringField(TEXT("audible_acceptance"),TEXT("requires_user_listening"));
		return Value;
	}
	void Shutdown()
	{
		if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
		FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle); CleanupHandle.Reset();
		for (const auto& Pair:Sessions) Stop(Pair.Value,TEXT("module_shutdown"));
		FAudioCommandFence Fence; Fence.BeginFence(); Fence.Wait(); Sessions.Empty();
	}
}

FString UUnrealBridgeAudioLibrary::GetAudioMixContext(const FString& WorldHandle)
{
	using namespace BridgeAudioSession; using BridgeAuthoring::Error;
	UWorld* World=ResolveWorld(WorldHandle);
	if (!World || (World->WorldType!=EWorldType::Game && World->WorldType!=EWorldType::PIE) || World->GetNetMode()==NM_DedicatedServer)
		return Error(TEXT("ScopeViolation"),TEXT("Exact playable World required"));
	const auto Device=World->GetAudioDevice();
	if (!Device.IsValid()) return Error(TEXT("UnsupportedCapability"),TEXT("Selected World has no audio device"));
	auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true); Value->SetStringField(TEXT("world_handle"),WorldHandle);
	Value->SetNumberField(TEXT("audio_device_id"),Device.GetDeviceID()); Value->SetBoolField(TEXT("audio_modulation_loaded"),BridgeAudioRouting::OptionalClass(TEXT("SoundControlBusMix"))!=nullptr);
	const auto Active=GEngine && GEngine->GetAudioDeviceManager()?GEngine->GetAudioDeviceManager()->GetActiveAudioDevice():FAudioDeviceHandle();
	Value->SetNumberField(TEXT("active_audio_device_id"),Active.IsValid()?double(Active.GetDeviceID()):-1);
	Value->SetNumberField(TEXT("app_volume_multiplier"),FApp::GetVolumeMultiplier()); Value->SetNumberField(TEXT("unfocused_volume_multiplier"),FApp::GetUnfocusedVolumeMultiplier());
	return Encode(Value);
}

FString UUnrealBridgeAudioLibrary::BeginAudioMixSession(const FString& RequestJson)
{
	using namespace BridgeAudioSession;
	using BridgeAuthoring::Error;
	if (!IsInGameThread() || RequestJson.Len()>65536) return Error(TEXT("ValidationFailed"),TEXT("Bounded game-thread audio request required"));
	const auto R=Decode(RequestJson);
	if (!Fields(R,{TEXT("schema"),TEXT("request_id"),TEXT("world_handle"),TEXT("audio_device_id"),TEXT("lease_seconds"),TEXT("sound_mix_path"),TEXT("control_bus_mix_path"),TEXT("sources")}) || String(R,TEXT("schema"))!=TEXT("unrealbridge.audio_session.v1"))
		return Error(TEXT("ValidationFailed"),TEXT("Exact audio session v1 fields required"));
	const FString RequestId=String(R,TEXT("request_id")),Handle=String(R,TEXT("world_handle")),Digest=Hash(RequestJson);
	if (RequestId.IsEmpty() || RequestId.Len()>96 || Handle.Len()>1024) return Error(TEXT("ValidationFailed"),TEXT("Bounded request and World identities required"));
	for (const auto& Pair:Sessions) if (Pair.Value->RequestId==RequestId)
		return Pair.Value->Digest==Digest?Encode(Model(Pair.Value)):Error(TEXT("IdempotencyConflict"),TEXT("Audio request ID already has different content"));
	double DeviceId=0,Lease=0; UWorld* World=ResolveWorld(Handle);
	if (!World || (World->WorldType!=EWorldType::PIE && World->WorldType!=EWorldType::Game) || World->GetNetMode()==NM_DedicatedServer) return Error(TEXT("ScopeViolation"),TEXT("Exact playable non-dedicated World required"));
	auto Device=World->GetAudioDevice();
	if (!Device.IsValid()) return Error(TEXT("UnsupportedCapability"),TEXT("Selected World has no audio device"));
	if (!Number(R,TEXT("audio_device_id"),DeviceId) || DeviceId<0 || DeviceId>MAX_uint32 || DeviceId!=FMath::FloorToDouble(DeviceId) || uint32(DeviceId)!=Device.GetDeviceID()) return Error(TEXT("ScopeViolation"),TEXT("AudioDevice does not match the selected World"));
	if (!Number(R,TEXT("lease_seconds"),Lease) || Lease<5 || Lease>300) return Error(TEXT("ValidationFailed"),TEXT("Audio lease must be 5..300 seconds"));
	APlayerController* Player=nullptr; for (auto It=World->GetPlayerControllerIterator();It;++It) if (It->IsValid() && It->Get()->IsLocalController()) { Player=It->Get(); break; }
	if (!Player) return Error(TEXT("ScopeViolation"),TEXT("Selected World has no local listener player"));
	const FString MixPath=String(R,TEXT("sound_mix_path")),BusMixPath=String(R,TEXT("control_bus_mix_path"));
	if (MixPath.Len()>512 || BusMixPath.Len()>512) return Error(TEXT("ValidationFailed"),TEXT("Bounded asset paths required"));
	USoundMix* SourceMix=MixPath.IsEmpty()?nullptr:LoadObject<USoundMix>(nullptr,*MixPath);
	if (!MixPath.IsEmpty() && !SourceMix) return Error(TEXT("ValidationFailed"),TEXT("Existing SoundMix required"));
	UObject* SourceBusMix=nullptr;
	if (!BusMixPath.IsEmpty())
	{
		auto* C=BridgeAudioRouting::OptionalClass(TEXT("SoundControlBusMix")); if (!C) return Error(TEXT("UnsupportedCapability"),TEXT("AudioModulation is not already loaded"));
		SourceBusMix=LoadObject<UObject>(nullptr,*BusMixPath); if (!SourceBusMix || !SourceBusMix->IsA(C)) return Error(TEXT("ValidationFailed"),TEXT("Existing ControlBusMix required"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
	if (!R->TryGetArrayField(TEXT("sources"),Rows) || Rows->Num()<1 || Rows->Num()>3) return Error(TEXT("ValidationFailed"),TEXT("One to three real audio sources required"));
	struct FInput { USoundBase* Sound=nullptr; USoundModulatorBase* Bus=nullptr; FVector Position; float Gain=1,Pitch=1; FString Path,BusPath; };
	TArray<FInput> Inputs;
	for (const auto& Raw:*Rows)
	{
		const TSharedPtr<FJsonObject>* Row=nullptr; if (!Raw->TryGetObject(Row) || !Fields(*Row,{TEXT("sound_path"),TEXT("relative_location_cm"),TEXT("volume_gain"),TEXT("pitch_ratio"),TEXT("volume_bus_path")})) return Error(TEXT("ValidationFailed"),TEXT("Exact typed source fields required"));
		FInput Input; Input.Path=String(*Row,TEXT("sound_path")); Input.BusPath=String(*Row,TEXT("volume_bus_path"));
		if (Input.Path.IsEmpty() || Input.Path.Len()>512 || Input.BusPath.Len()>512) return Error(TEXT("ValidationFailed"),TEXT("Bounded real sound paths required"));
		Input.Sound=LoadObject<USoundBase>(nullptr,*Input.Path); double Pitch=0,X=0,Y=0,Z=0; const TSharedPtr<FJsonObject>* G=nullptr; const TSharedPtr<FJsonObject>* P=nullptr;
		if (!Input.Sound || (!Input.Sound->IsA<USoundCue>() && !Input.Sound->IsA<USoundWave>()) || Input.Sound->GetDuration()<=0 || !(*Row)->TryGetObjectField(TEXT("volume_gain"),G) || !BridgeAudioRouting::Gain(*G,Input.Gain)
			|| !Number(*Row,TEXT("pitch_ratio"),Pitch) || Pitch<.125 || Pitch>8 || !(*Row)->TryGetObjectField(TEXT("relative_location_cm"),P) || !Fields(*P,{TEXT("x"),TEXT("y"),TEXT("z")})
			|| !Number(*P,TEXT("x"),X) || !Number(*P,TEXT("y"),Y) || !Number(*P,TEXT("z"),Z) || FMath::Abs(X)>5000 || FMath::Abs(Y)>5000 || FMath::Abs(Z)>5000)
			return Error(TEXT("ValidationFailed"),TEXT("Existing Wave/Cue, gain, pitch ratio and bounded relative centimetres required"));
		Input.Pitch=Pitch; Input.Position=FVector(X,Y,Z);
		if (!Input.BusPath.IsEmpty())
		{
			auto* C=BridgeAudioRouting::OptionalClass(TEXT("SoundControlBus")); if (!C) return Error(TEXT("UnsupportedCapability"),TEXT("AudioModulation is not loaded"));
			Input.Bus=LoadObject<USoundModulatorBase>(nullptr,*Input.BusPath);
			auto* Prop=Input.Bus?FindFProperty<FObjectPropertyBase>(Input.Bus->GetClass(),TEXT("Parameter")):nullptr;
			auto* Param=Prop?Prop->GetObjectPropertyValue_InContainer(Input.Bus):nullptr; auto* Volume=BridgeAudioRouting::OptionalClass(TEXT("SoundModulationParameterVolume"));
			if (!Input.Bus || !Input.Bus->IsA(C) || !Volume || !Param || !Param->IsA(Volume)) return Error(TEXT("ValidationFailed"),TEXT("Volume routing requires a ControlBus with a Volume parameter"));
		}
		Inputs.Add(Input);
	}
	int32 Active=0; for (const auto& Pair:Sessions) if (Pair.Value->Status!=TEXT("ended")) ++Active;
	if (Active>=4 || Sessions.Num()>=64) return Error(TEXT("ResourceBudgetExceeded"),TEXT("At most four active sessions and 64 receipts per Editor session"));
	auto S=MakeShared<FSession>(); S->Id=FGuid::NewGuid().ToString(EGuidFormats::Digits); S->RequestId=RequestId; S->Digest=Digest; S->WorldHandle=Handle; S->World=World;
	S->Device=Device; S->DeviceId=Device.GetDeviceID(); S->Expires=FPlatformTime::Seconds()+Lease;
	S->SourceMix=SourceMix; S->SourceBusMix=SourceBusMix;
	if (SourceMix) { S->SourceMixDigest=Hash(Encode(BridgeAudioRouting::Model(SourceMix))); S->OwnedMix.Reset(DuplicateObject(SourceMix,GetTransientPackage())); }
	if (SourceBusMix) { S->SourceBusMixDigest=Hash(Encode(BridgeAudioRouting::Model(SourceBusMix))); S->OwnedBusMix.Reset(DuplicateObject(SourceBusMix,GetTransientPackage())); }
	FActorSpawnParameters Spawn; Spawn.ObjectFlags=RF_Transient; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Owner=World->SpawnActor<AActor>(AActor::StaticClass(),FTransform::Identity,Spawn);
	if (!Owner) return Error(TEXT("ExecutionFailed"),TEXT("Could not create an owned transient audio actor"));
	Owner->Tags.Add(FName(*(TEXT("UnrealBridge.Audio.")+S->Id))); S->Owner=Owner;
	Sessions.Add(S->Id,S); EnsureObservers(); FVector Listener; FRotator Rotation; Player->GetPlayerViewPoint(Listener,Rotation);
	S->Meters.SetNum(Inputs.Num());
	for (int32 I=0;I<Inputs.Num();++I)
	{
		const auto& Input=Inputs[I]; auto* C=NewObject<UAudioComponent>(Owner,NAME_None,RF_Transient);
		C->bAutoActivate=false; C->bAutoDestroy=false; C->bStopWhenOwnerDestroyed=true; C->bAllowSpatialization=true; C->EnvelopeFollowerAttackTime=5; C->EnvelopeFollowerReleaseTime=50;
		Owner->AddInstanceComponent(C); C->RegisterComponentWithWorld(World); C->SetWorldLocation(Listener+Rotation.RotateVector(Input.Position));
		C->SetSound(Input.Sound); C->SetVolumeMultiplier(Input.Gain); C->SetPitchMultiplier(Input.Pitch);
		if (Input.Bus) C->SetModulationRouting(TSet<USoundModulatorBase*>{Input.Bus},EModulationDestination::Volume,EModulationRouting::Override);
		FSource Source; Source.Component=C; Source.ComponentId=C->GetAudioComponentID(); Source.SoundPath=Input.Path; Source.BusPath=Input.BusPath; Source.VolumeBus=Input.Bus;
		const TWeakPtr<FSession> Weak=S;
		Source.EnvelopeHandle=C->OnAudioSingleEnvelopeValueNative.AddLambda([Weak,I](const UAudioComponent*,const USoundWave* Wave,float Level)
		{ if (const auto V=Weak.Pin()) { FScopeLock Lock(&V->MeterLock); auto& M=V->Meters[I]; ++M.EnvelopeSamples; M.PeakEnvelope=FMath::Max(M.PeakEnvelope,Level); if (Wave && M.WavePaths.Num()<32) M.WavePaths.Add(Wave->GetPathName()); } });
		Source.PercentHandle=C->OnAudioPlaybackPercentNative.AddLambda([Weak,I](const UAudioComponent*,const USoundWave* Wave,float Percent)
		{ if (const auto V=Weak.Pin()) { FScopeLock Lock(&V->MeterLock); auto& M=V->Meters[I]; ++M.PercentSamples; M.PlaybackPercent=Percent; if (Wave && M.WavePaths.Num()<32) M.WavePaths.Add(Wave->GetPathName()); } });
		S->Sources.Add(Source);
	}
	if (S->OwnedMix.IsValid()) { Device->PushSoundMixModifier(S->OwnedMix.Get()); S->Pushed=true; }
	if (S->OwnedBusMix.IsValid())
	{
		if (!BridgeAudioRouting::CallBusMix(TEXT("ActivateBusMix"),World,S->OwnedBusMix.Get())) { Stop(S,TEXT("activation_failed")); return Error(TEXT("UnsupportedCapability"),TEXT("Loaded AudioModulation activation API is incompatible"),TEXT("cleanup_requested")); }
		S->BusActivated=true;
	}
	for (auto& Source:S->Sources) Source.Component->Play(); SampleAudioThread(S); return Encode(Model(S));
}

FString UUnrealBridgeAudioLibrary::GetAudioMixSession(const FString& SessionId,const FString& WorldHandle)
{
	using namespace BridgeAudioSession; using BridgeAuthoring::Error; const auto* S=Sessions.Find(SessionId);
	if (!S || (*S)->WorldHandle!=WorldHandle) return Error(TEXT("ScopeViolation"),TEXT("Exact audio session and original World handle required"));
	return Encode(Model(*S));
}
FString UUnrealBridgeAudioLibrary::EndAudioMixSession(const FString& SessionId,const FString& WorldHandle)
{
	using namespace BridgeAudioSession; using BridgeAuthoring::Error; const auto* S=Sessions.Find(SessionId);
	if (!S || (*S)->WorldHandle!=WorldHandle) return Error(TEXT("ScopeViolation"),TEXT("Exact audio session and original World handle required"));
	Stop(*S,TEXT("explicit_end")); return Encode(Model(*S));
}
