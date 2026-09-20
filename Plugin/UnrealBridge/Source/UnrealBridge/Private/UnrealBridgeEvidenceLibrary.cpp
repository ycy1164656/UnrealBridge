#include "UnrealBridgeEvidenceLibrary.h"
#include "UnrealBridgeWorldLibrary.h"
#include "UnrealBridgeSandboxLibrary.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Dom/JsonObject.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UnrealClient.h"

namespace BridgeEvidenceCapture
{
	struct FWriteResult { bool bOk=false; int64 Bytes=0; };
	struct FCapture
	{
		FString Id, Owner, Root, StopReason;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UGameViewportClient> Client;
		FViewport* Viewport=nullptr;
		FIntPoint Size;
		double Start=0, Next=0, Duration=0;
		int32 Fps=30, Attempts=0, Dropped=0;
		int64 Bytes=0, MaxBytes=128*1024*1024;
		bool bStop=false;
		TFuture<FWriteResult> Pending;
		TSharedPtr<FJsonObject> PendingFrame, Identity;
		TArray<TSharedPtr<FJsonValue>> Frames;
	};
	TUniquePtr<FCapture> Current;
	FTSTicker::FDelegateHandle Ticker;
	FString Json(const TSharedPtr<FJsonObject>& Value)
	{
		FString Out; FJsonSerializer::Serialize(Value.ToSharedRef(),TJsonWriterFactory<>::Create(&Out)); return Out;
	}
	TSharedPtr<FJsonObject> Read(const FString& Text)
	{
		TSharedPtr<FJsonObject> Value; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Value); return Value;
	}
	FString Get(const TSharedPtr<FJsonObject>& Value,const TCHAR* Key)
	{
		FString Out; if(Value) Value->TryGetStringField(Key,Out); return Out;
	}
	FString Failure(const TCHAR* Message)
	{
		auto Out=MakeShared<FJsonObject>();Out->SetBoolField(TEXT("ok"),false);Out->SetStringField(TEXT("error"),Message);return Json(Out);
	}
	bool Identifier(const FString& Value)
	{
		if(Value.IsEmpty() || Value.Len()>96)return false;
		for(TCHAR C:Value) if(!FChar::IsAlnum(C) && C!=TEXT('-') && C!=TEXT('_')) return false;
		return true;
	}
	void Collect(FCapture& Capture)
	{
		if(!Capture.Pending.IsValid() || !Capture.Pending.IsReady())return;
		const auto Result=Capture.Pending.Get();Capture.Pending=TFuture<FWriteResult>();
		if(Result.bOk)
		{
			Capture.Bytes+=Result.Bytes;Capture.PendingFrame->SetNumberField(TEXT("bytes"),Result.Bytes);
			Capture.Frames.Add(MakeShared<FJsonValueObject>(Capture.PendingFrame));
		}
		else { ++Capture.Dropped;Capture.bStop=true;Capture.StopReason=TEXT("encode_or_write_failed"); }
		Capture.PendingFrame.Reset();
	}
	bool Tick(float)
	{
		if(!Current)return false;
		auto& C=*Current;Collect(C);
		if(C.bStop)return C.Pending.IsValid();
		const double Now=FPlatformTime::Seconds();
		if(!C.World.IsValid() || C.World->bIsTearingDown || !C.Client.IsValid() || C.Client->GetWorld()!=C.World.Get() || C.Client->Viewport!=C.Viewport)
		{ C.bStop=true;C.StopReason=TEXT("world_or_viewport_destroyed");return C.Pending.IsValid(); }
		if(Now-C.Start>=C.Duration) {C.bStop=true;C.StopReason=TEXT("duration_complete");return C.Pending.IsValid();}
		if(Now<C.Next)return true;
		const int32 Slots=FMath::Max(1,FMath::FloorToInt((Now-C.Next)*C.Fps)+1);
		C.Next+=double(Slots)/C.Fps;C.Dropped+=Slots-1;C.Attempts+=Slots;
		if(C.Pending.IsValid()){++C.Dropped;return true;}
		uint64 Total=0,Free=0;
		if(!FPlatformMisc::GetDiskTotalAndFreeSpace(C.Root,Total,Free) || Free<64*1024*1024 || C.Bytes+int64(C.Size.X)*C.Size.Y*4>C.MaxBytes)
		{C.bStop=true;C.StopReason=TEXT("disk_or_evidence_budget");return false;}
		if(C.Viewport->GetSizeXY()!=C.Size){C.bStop=true;C.StopReason=TEXT("viewport_resized");return false;}
		TArray<FColor> Pixels;FReadSurfaceDataFlags Flags;Flags.SetLinearToGamma(false);
		// GPU readback occurs on the game thread; JPEG compression and disk writes do not.
		if(!C.Viewport->ReadPixels(Pixels,Flags) || Pixels.Num()!=C.Size.X*C.Size.Y)
		{++C.Dropped;C.bStop=true;C.StopReason=TEXT("readback_failed");return false;}
		auto Frame=MakeShared<FJsonObject>();const int32 Index=C.Frames.Num();
		Frame->SetNumberField(TEXT("index"),Index);Frame->SetNumberField(TEXT("platform_seconds"),Now);
		Frame->SetNumberField(TEXT("world_seconds"),C.World->GetTimeSeconds());Frame->SetNumberField(TEXT("engine_frame"),double(GFrameCounter));
		const FString Path=C.Root/FString::Printf(TEXT("frame-%05d.jpg"),Index);Frame->SetStringField(TEXT("path"),Path);
		C.PendingFrame=Frame;const FIntPoint Size=C.Size;const FString Journal=C.Root/TEXT("frames.jsonl");
		const FString Record=Json(Frame).Replace(TEXT("\r"),TEXT("")).Replace(TEXT("\n"),TEXT(""))+TEXT("\n");
		auto* Module=&FModuleManager::GetModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		C.Pending=Async(EAsyncExecution::ThreadPool,[Pixels=MoveTemp(Pixels),Size,Path,Journal,Record,Module]() mutable
		{
			FWriteResult Result;auto Wrapper=Module->CreateImageWrapper(EImageFormat::JPEG);
			if(!Wrapper.IsValid() || !Wrapper->SetRaw(Pixels.GetData(),Pixels.Num()*sizeof(FColor),Size.X,Size.Y,ERGBFormat::BGRA,8))return Result;
			const TArray64<uint8>& Compressed=Wrapper->GetCompressed(85);
			Result.Bytes=Compressed.Num();Result.bOk=Result.Bytes>0 && FFileHelper::SaveArrayToFile(Compressed,*Path)
				&& FFileHelper::SaveStringToFile(Record,*Journal,FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,&IFileManager::Get(),FILEWRITE_Append);
			return Result;
		});
		return true;
	}
	void Shutdown()
	{
		if(Ticker.IsValid())FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		if(Current && Current->Pending.IsValid())Current->Pending.Wait();
		Current.Reset();Ticker.Reset();
	}
}

FString UUnrealBridgeEvidenceLibrary::BeginViewportCapture(const FString& RequestJson)
{
	using namespace BridgeEvidenceCapture;
	if(RequestJson.Len()>16384)return Failure(TEXT("Request too large"));
	auto Request=Read(RequestJson);
	const TSet<FString> Fields={TEXT("schema"),TEXT("capture_id"),TEXT("owner_id"),TEXT("editor_session_id"),TEXT("world_handle"),TEXT("view_identity"),TEXT("plan_hash"),TEXT("acceptance_hash"),TEXT("fps"),TEXT("duration_seconds"),TEXT("max_bytes"),TEXT("allow_capture")};
	if(!Request || Request->Values.Num()!=Fields.Num())return Failure(TEXT("Exact capture schema required"));
	for(const auto& Pair:Request->Values)if(!Fields.Contains(FString(Pair.Key)))return Failure(TEXT("Unknown capture field"));
	bool bAllowed=false;double Fps=0,Duration=0,MaxBytes=0;
	if(Get(Request,TEXT("schema"))!=TEXT("unrealbridge.capture.v1") || !Request->TryGetBoolField(TEXT("allow_capture"),bAllowed) || !bAllowed
		|| !Request->TryGetNumberField(TEXT("fps"),Fps) || !Request->TryGetNumberField(TEXT("duration_seconds"),Duration) || !Request->TryGetNumberField(TEXT("max_bytes"),MaxBytes)
		|| !FMath::IsFinite(Fps) || !FMath::IsFinite(Duration) || !FMath::IsFinite(MaxBytes) || Fps<1 || Fps>30 || Fps!=FMath::FloorToDouble(Fps) || Duration<1 || Duration>20 || MaxBytes<1024*1024 || MaxBytes>256*1024*1024)
		return Failure(TEXT("Explicit bounded capture permission required"));
	const FString Id=Get(Request,TEXT("capture_id")),Owner=Get(Request,TEXT("owner_id"));
	if(!Identifier(Id) || !Identifier(Owner))return Failure(TEXT("Invalid capture identity"));
	if(Current && Current->Id==Id)return Json(Current->Identity)==Json(Request)?GetViewportCapture(Id):Failure(TEXT("Capture identity reused with different request"));
	if(Current && (!Current->bStop || Current->Pending.IsValid()))return Failure(TEXT("An owned capture is already active"));
	auto Context=Read(UUnrealBridgeWorldLibrary::GetWorldContexts());auto Sandbox=Read(UUnrealBridgeSandboxLibrary::GetSandboxStatus());
	UWorld* World=BridgeWorldContext::GetScopedWorld();bool bExact=false;
	if(Context && World && World->WorldType==EWorldType::PIE && Get(Context,TEXT("editor_session_id"))==Get(Request,TEXT("editor_session_id")))
		for(const auto& Item:Context->GetArrayField(TEXT("worlds")))if(Get(Item->AsObject(),TEXT("world_handle"))==Get(Request,TEXT("world_handle")) && Get(Item->AsObject(),TEXT("world_path"))==World->GetPathName())bExact=true;
	const FString View=Get(Request,TEXT("view_identity"));
	const TSharedPtr<FJsonObject>* Lease=nullptr;FString ActualView=TEXT("main");
	if(Sandbox && Sandbox->TryGetObjectField(TEXT("lease_record"),Lease))ActualView=Get(*Lease,TEXT("lease_id"));
	if(!bExact || View!=ActualView || Get(Request,TEXT("plan_hash")).Len()!=64 || Get(Request,TEXT("acceptance_hash")).Len()!=64)return Failure(TEXT("Stale World, view or contract identity"));
	UGameViewportClient* Client=World->GetGameViewport();FViewport* Viewport=Client?Client->Viewport:nullptr;
	if(!Client || Client->GetWorld()!=World || !Viewport)return Failure(TEXT("Exact scoped PIE viewport unavailable; no fallback"));
	const FIntPoint Size=Viewport->GetSizeXY();
	if(Size.X<=0 || Size.Y<=0 || Size.X>1280 || Size.Y>720)return Failure(TEXT("Viewport must be positive and at most 1280x720"));
	const FString Root=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("UnrealBridge/Artifacts/captures")/Id);
	if(IFileManager::Get().DirectoryExists(*Root))return Failure(TEXT("Existing capture output must not be overwritten"));
	if(!IFileManager::Get().MakeDirectory(*Root,true))return Failure(TEXT("Capture directory unavailable"));
	FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	if(Ticker.IsValid())FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
	Current=MakeUnique<FCapture>();auto& C=*Current;C.Id=Id;C.Owner=Owner;C.Root=Root;C.World=World;C.Client=Client;C.Viewport=Viewport;C.Size=Size;
	C.Start=FPlatformTime::Seconds();C.Next=C.Start;C.Fps=int32(Fps);C.Duration=Duration;C.MaxBytes=int64(MaxBytes);C.Identity=Request;
	Ticker=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick));
	return GetViewportCapture(Id);
}

FString UUnrealBridgeEvidenceLibrary::GetViewportCapture(const FString& CaptureId)
{
	using namespace BridgeEvidenceCapture;
	if(!Current || Current->Id!=CaptureId)return Failure(TEXT("Unknown capture; reconcile persisted frames after restart"));
	auto& C=*Current;Collect(C);auto Out=MakeShared<FJsonObject>();Out->SetBoolField(TEXT("ok"),true);
	Out->SetStringField(TEXT("schema"),TEXT("unrealbridge.capture.v1"));Out->SetObjectField(TEXT("identity"),C.Identity);
	Out->SetStringField(TEXT("status"),!C.bStop?TEXT("capturing"):C.Pending.IsValid()?TEXT("draining"):TEXT("frames_complete"));
	Out->SetStringField(TEXT("stop_reason"),C.StopReason);Out->SetStringField(TEXT("root"),C.Root);Out->SetStringField(TEXT("backend"),TEXT("scoped_pie_readback_async_jpeg"));
	Out->SetStringField(TEXT("clock"),TEXT("FPlatformTime::Seconds; world_seconds is same-World game clock"));
	Out->SetBoolField(TEXT("audio_recorded"),false);Out->SetBoolField(TEXT("video_encoded"),false);
	Out->SetNumberField(TEXT("width"),C.Size.X);Out->SetNumberField(TEXT("height"),C.Size.Y);Out->SetNumberField(TEXT("requested_fps"),C.Fps);
	Out->SetNumberField(TEXT("dropped_frames"),C.Dropped);Out->SetNumberField(TEXT("bytes"),C.Bytes);Out->SetArrayField(TEXT("frames"),C.Frames);
	Out->SetBoolField(TEXT("partial"),C.Dropped>0 || (C.bStop && C.StopReason!=TEXT("duration_complete")));
	return Json(Out);
}

FString UUnrealBridgeEvidenceLibrary::StopViewportCapture(const FString& CaptureId,const FString& OwnerId)
{
	using namespace BridgeEvidenceCapture;
	if(!Current || Current->Id!=CaptureId || Current->Owner!=OwnerId)return Failure(TEXT("Capture owner mismatch"));
	Current->bStop=true;Current->StopReason=TEXT("stopped_by_owner");return GetViewportCapture(CaptureId);
}
