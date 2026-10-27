#include "UnrealBridgeAudioLibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "UnrealBridgeWorldLibrary.h"
#include "Async/Async.h"
#include "AudioDevice.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "ISubmixBufferListener.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Sound/SoundSubmix.h"

namespace BridgeAudioCapture
{
	using namespace BridgeAuthoring;
	struct FListener final : ISubmixBufferListener
	{
		FCriticalSection Lock;
		TArray<int16> PCM;
		int32 Rate=0, Channels=0, MaxSamples=0;
		double Duration=0, FirstPlatform=0, FirstClock=0, LastClock=0;
		float Peak=0;
		bool Done=false;
		FString Reason;
		virtual void OnNewSubmixBuffer(const USoundSubmix*,float* Data,int32 Count,int32 NumChannels,const int32 SampleRate,double Clock) override
		{
			FScopeLock Guard(&Lock);
			if (Done) return;
			if (NumChannels<1 || NumChannels>8 || SampleRate<8000 || SampleRate>192000 || Count<0 || Count%NumChannels)
			{ Done=true; Reason=TEXT("unsupported_audio_format"); return; }
			if (!Rate) { Rate=SampleRate; Channels=NumChannels; FirstPlatform=FPlatformTime::Seconds(); FirstClock=Clock; }
			if (Rate!=SampleRate || Channels!=NumChannels) { Done=true; Reason=TEXT("audio_format_changed"); return; }
			const int32 Limit=FMath::Min(MaxSamples/Channels*Channels,int32(Duration*Rate)*Channels);
			const int32 Take=FMath::Min(Count,Limit-PCM.Num());
			for (int32 I=0;I<Take;++I)
			{
				if (!FMath::IsFinite(Data[I])) { Done=true; Reason=TEXT("nonfinite_audio"); return; }
				Peak=FMath::Max(Peak,FMath::Abs(Data[I]));
				PCM.Add(int16(FMath::RoundToInt(FMath::Clamp(Data[I],-1.f,1.f)*32767.f)));
			}
			LastClock=Clock;
			if (PCM.Num()>=Limit) { Done=true; Reason=Limit<Duration*Rate*Channels?TEXT("byte_limit"):TEXT("duration_limit"); }
		}
		virtual bool IsRenderingAudio() const override { return true; }
	};
	struct FCapture
	{
		FString Id,Owner,WorldHandle,Path,Status=TEXT("recording"),Reason;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<USoundSubmix> Submix;
		FAudioDeviceHandle Device;
		TSharedPtr<FListener,ESPMode::ThreadSafe> Listener;
		TFuture<bool> Write;
		double Start=0,StartWorld=0,End=0,Duration=0;
		int32 Frames=0,Bytes=0;
		TArray<TSharedPtr<FJsonValue>> DeviceWorlds;
	};
	TUniquePtr<FCapture> Current;
	FTSTicker::FDelegateHandle Ticker;
	TSharedRef<FJsonObject> Model();
	void Stop(const TCHAR* Reason)
	{
		if (!Current || Current->Status!=TEXT("recording")) return;
		auto& C=*Current;
		C.End=FPlatformTime::Seconds(); C.Reason=Reason; C.Status=TEXT("writing");
		TArray<int16> PCM;
		int32 Rate=0,Channels=0;
		{
			FScopeLock Guard(&C.Listener->Lock);
			C.Listener->Done=true;
			Rate=C.Listener->Rate; Channels=C.Listener->Channels;
			PCM=MoveTemp(C.Listener->PCM);
		}
		if (C.Device.IsValid() && C.Submix.IsValid()) C.Device->UnregisterSubmixBufferListener(C.Listener.ToSharedRef(),*C.Submix.Get());
		C.Device=FAudioDeviceHandle();
		C.Frames=Channels?PCM.Num()/Channels:0; C.Bytes=PCM.Num()*2+44;
		const FString Path=C.Path;
		C.Write=Async(EAsyncExecution::ThreadPool,[Path,Rate,Channels,PCM=MoveTemp(PCM)]()
		{
			if (!Rate || !Channels || PCM.IsEmpty()) return false;
			TArray<uint8> Wave;
			auto U16=[&](uint16 V){ Wave.Add(V&255); Wave.Add(V>>8); };
			auto U32=[&](uint32 V){ U16(V&65535); U16(V>>16); };
			auto Tag=[&](const ANSICHAR* V){ for(int32 I=0;I<4;++I) Wave.Add(V[I]); };
			Tag("RIFF"); U32(36+PCM.Num()*2); Tag("WAVE"); Tag("fmt "); U32(16); U16(1); U16(Channels);
			U32(Rate); U32(Rate*Channels*2); U16(Channels*2); U16(16); Tag("data"); U32(PCM.Num()*2);
			Wave.Append(reinterpret_cast<const uint8*>(PCM.GetData()),PCM.Num()*2);
			return FFileHelper::SaveArrayToFile(Wave,*Path);
		});
	}
	void Refresh()
	{
		if (!Current) return;
		auto& C=*Current;
		if (C.Status==TEXT("recording"))
		{
			FString Reason;
			{ FScopeLock Guard(&C.Listener->Lock); if (C.Listener->Done) Reason=C.Listener->Reason; }
			if (!C.World.IsValid() || C.World->bIsTearingDown) Reason=TEXT("world_cleanup");
			else if (C.World->GetAudioDevice().GetDeviceID()!=C.Device.GetDeviceID()) Reason=TEXT("audio_device_changed");
			else if (FPlatformTime::Seconds()>C.Start+C.Duration+1) Reason=TEXT("wall_time_limit");
			if (!Reason.IsEmpty()) Stop(*Reason);
		}
		if (C.Status==TEXT("writing") && C.Write.IsReady())
		{
			C.Status=C.Write.Get()?TEXT("completed"):TEXT("failed");
			if (!FFileHelper::SaveStringToFile(Encode(Model()),*(FPaths::GetPath(C.Path)/TEXT("capture.json")),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) C.Status=TEXT("failed");
		}
	}
	TSharedRef<FJsonObject> Model()
	{
		auto& C=*Current; auto J=MakeShared<FJsonObject>();
		J->SetBoolField(TEXT("ok"),C.Status!=TEXT("failed")); J->SetStringField(TEXT("schema"),TEXT("unrealbridge.audio_capture.v1"));
		J->SetStringField(TEXT("capture_id"),C.Id); J->SetStringField(TEXT("status"),C.Status); J->SetStringField(TEXT("stop_reason"),C.Reason);
		J->SetStringField(TEXT("world_handle"),C.WorldHandle); J->SetStringField(TEXT("capture_scope"),TEXT("audio_device_main_submix"));
		J->SetArrayField(TEXT("device_worlds_at_start"),C.DeviceWorlds); J->SetStringField(TEXT("wav_path"),C.Status==TEXT("completed")?C.Path:TEXT(""));
		J->SetNumberField(TEXT("start_platform_seconds"),C.Start); J->SetNumberField(TEXT("start_world_seconds"),C.StartWorld); J->SetNumberField(TEXT("end_platform_seconds"),C.End);
		J->SetNumberField(TEXT("frames"),C.Frames); J->SetNumberField(TEXT("bytes"),C.Bytes);
		FScopeLock Guard(&C.Listener->Lock);
		J->SetNumberField(TEXT("sample_rate"),C.Listener->Rate); J->SetNumberField(TEXT("channels"),C.Listener->Channels);
		J->SetNumberField(TEXT("first_buffer_platform_seconds"),C.Listener->FirstPlatform); J->SetNumberField(TEXT("first_audio_clock"),C.Listener->FirstClock);
		J->SetNumberField(TEXT("last_audio_clock"),C.Listener->LastClock); J->SetNumberField(TEXT("peak_linear"),C.Listener->Peak);
		J->SetBoolField(TEXT("audio_recorded"),C.Status==TEXT("completed") && C.Frames>0);
		J->SetStringField(TEXT("audible_acceptance"),TEXT("requires_user_listening"));
		J->SetStringField(TEXT("clock_alignment"),TEXT("first-buffer callback clocks; device latency and event sample accuracy are not certified"));
		return J;
	}
	void Shutdown()
	{
		if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker); Ticker.Reset();
		Stop(TEXT("module_shutdown"));
		if (Current && Current->Write.IsValid()) Current->Write.Wait();
		Refresh(); Current.Reset();
	}
}

FString UUnrealBridgeAudioLibrary::BeginAudioOutputCapture(const FString& RequestJson)
{
	using namespace BridgeAudioCapture;
	const auto Request=Decode(RequestJson);
	const FString WorldHandle=String(Request,TEXT("world_handle")),Owner=String(Request,TEXT("owner_token"));
	double Duration=0,MaxBytes=32*1024*1024;
	if (!Request || Owner.Len()<8 || Owner.Len()>128 || !Request->TryGetNumberField(TEXT("duration_seconds"),Duration) || !FMath::IsFinite(Duration) || Duration<.1 || Duration>90)
		return BridgeAuthoring::Error(TEXT("InvalidRequest"),TEXT("owner_token (8..128) and finite duration_seconds (.1..90) required; byte budget is enforced independently"));
	Request->TryGetNumberField(TEXT("max_bytes"),MaxBytes);
	if (!FMath::IsFinite(MaxBytes) || MaxBytes<4096 || MaxBytes>64*1024*1024) return BridgeAuthoring::Error(TEXT("InvalidRequest"),TEXT("max_bytes must be 4096..67108864"));
	Refresh();
	if (Current && (Current->Status==TEXT("recording") || Current->Status==TEXT("writing"))) return BridgeAuthoring::Error(TEXT("Busy"),TEXT("An audio capture is already active"));
	const auto Catalog=Decode(UUnrealBridgeWorldLibrary::GetWorldContexts(64)); const TArray<TSharedPtr<FJsonValue>>* Worlds=nullptr;
	UWorld* World=nullptr;
	if (Catalog && Catalog->TryGetArrayField(TEXT("worlds"),Worlds)) for(const auto& V:*Worlds)
	{ const auto O=V->AsObject(); if (String(O,TEXT("world_handle"))==WorldHandle) World=FindObject<UWorld>(nullptr,*String(O,TEXT("world_path"))); }
	if (!World || (World->WorldType!=EWorldType::PIE && World->WorldType!=EWorldType::Game) || World->GetNetMode()==NM_DedicatedServer)
		return BridgeAuthoring::Error(TEXT("ScopeViolation"),TEXT("Exact playable WorldHandle required"));
	auto Device=World->GetAudioDevice();
	if (!Device.IsValid() || !Device->bAudioMixerModuleLoaded) return BridgeAuthoring::Error(TEXT("UnsupportedCapability"),TEXT("Audio mixer device required"));
	TArray<TSharedPtr<FJsonValue>> DeviceWorlds;
	for (const auto& Context:GEngine->GetWorldContexts()) if (auto* W=Context.World())
		if (W->GetAudioDevice().IsValid() && W->GetAudioDevice().GetDeviceID()==Device.GetDeviceID()) DeviceWorlds.Add(MakeShared<FJsonValueString>(W->GetPathName()));
	bool Shared=false; Request->TryGetBoolField(TEXT("allow_shared_device"),Shared);
	if (DeviceWorlds.Num()>1 && !Shared) return BridgeAuthoring::Error(TEXT("SharedDevice"),TEXT("Device mixes multiple Worlds; explicit allow_shared_device required"));
	Current=MakeUnique<FCapture>(); auto& C=*Current;
	C.Id=FGuid::NewGuid().ToString(EGuidFormats::Digits); C.Owner=Owner; C.World=World; C.WorldHandle=WorldHandle; C.Device=Device;
	C.Submix=&Device->GetMainSubmixObject(); C.Duration=Duration; C.Start=FPlatformTime::Seconds(); C.StartWorld=World->GetTimeSeconds(); C.DeviceWorlds=MoveTemp(DeviceWorlds);
	C.Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("UnrealBridge/AudioCaptures")/C.Id/TEXT("audio.wav"));
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(C.Path),true)) { Current.Reset(); return BridgeAuthoring::Error(TEXT("IOError"),TEXT("Cannot create capture directory")); }
	C.Listener=MakeShared<BridgeAudioCapture::FListener,ESPMode::ThreadSafe>(); C.Listener->Duration=Duration; C.Listener->MaxSamples=int32(MaxBytes-44)/2;
	C.Listener->PCM.Reserve(C.Listener->MaxSamples);
	Device->RegisterSubmixBufferListener(C.Listener.ToSharedRef(),*C.Submix.Get());
	if (!Ticker.IsValid()) Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float){ Refresh(); return true; }),.05f);
	return Encode(Model());
}
FString UUnrealBridgeAudioLibrary::GetAudioOutputCapture(const FString& CaptureId,const FString& OwnerToken)
{
	using namespace BridgeAudioCapture;
	if (!Current || Current->Id!=CaptureId || Current->Owner!=OwnerToken) return BridgeAuthoring::Error(TEXT("OwnerMismatch"),TEXT("Exact capture ID and owner token required"));
	Refresh(); return Encode(Model());
}
FString UUnrealBridgeAudioLibrary::EndAudioOutputCapture(const FString& CaptureId,const FString& OwnerToken)
{
	using namespace BridgeAudioCapture;
	if (!Current || Current->Id!=CaptureId || Current->Owner!=OwnerToken) return BridgeAuthoring::Error(TEXT("OwnerMismatch"),TEXT("Exact capture ID and owner token required"));
	Stop(TEXT("caller_stopped")); return Encode(Model());
}
