#include "UnrealBridgeSlateInputLibrary.h"
#include "UnrealBridgeWorldLibrary.h"
#include "UnrealBridgeNetworkSessionLibrary.h"
#include "Engine/GameViewportClient.h"
#include "Interfaces/IMainFrameModule.h"
#include "Modules/ModuleManager.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Dom/JsonObject.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/SlateUser.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/SWindow.h"

namespace BridgeSlateInput
{
	struct FGeometryRef
	{
		FString Handle,WorldHandle,Generation,WindowId,Revision;
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<UWidget> Widget;
		TWeakObjectPtr<ULocalPlayer> Player;
		TWeakPtr<SWidget> Slate;
		TWeakPtr<SWindow> Window;
		int32 LocalIndex=0,UserIndex=0;
		double Created=0;
	};
	struct FPointerStep { FString Type; FVector2D Local; double At=0; };
	struct FOperation
	{
		FGeometryRef Ref;
		FVector2D InitialLocalSize=FVector2D::ZeroVector;
		FString Id,RequestId,InputHash,Status=TEXT("queued"),Error;
		TArray<FPointerStep> Events;
		TArray<TSharedPtr<FJsonValue>> Evidence;
		FModifierKeysState Modifiers;
		FKey Button;
		TSet<FKey> Pressed;
		FVector2D LastPosition=FVector2D::ZeroVector;
		TWeakPtr<SWidget> OwnedCaptor;
		FWeakWidgetPath LastPath;
		int32 Next=0;
		double Started=0;
		bool Cancel=false;
	};
	TMap<FString,FGeometryRef> Refs;
	TMap<FString,FOperation> Operations;
	FString Active;
	FDelegateHandle TickHandle;
	const uint32 PointerIndex=FSlateApplication::CursorPointerIndex;
	FString Encode(const TSharedRef<FJsonObject>& Object)
	{ FString Text; FJsonSerializer::Serialize(Object,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text)); return Text; }
	TSharedPtr<FJsonObject> Decode(const FString& Text)
	{ TSharedPtr<FJsonObject> Object; if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Object)) return nullptr; return Object; }
	FString String(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key)
	{ FString Result; if (Object) Object->TryGetStringField(Key,Result); return Result; }
	FString Failure(const TCHAR* Code,const FString& Message=FString())
	{ auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),false); Value->SetStringField(TEXT("error_code"),Code); Value->SetStringField(TEXT("error"),Message); return Encode(Value); }
	FString Hash(const FString& Text)
	{ FTCHARToUTF8 Bytes(*Text); return FSHA1::HashBuffer(Bytes.Get(),Bytes.Length()).ToString().ToLower(); }
	bool Number(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double& Value)
	{ const auto* Field=Object?Object->Values.Find(Key):nullptr; return Field && (*Field)->Type==EJson::Number && (*Field)->TryGetNumber(Value) && FMath::IsFinite(Value); }
	UWorld* ResolveWorld(const FString& Handle)
	{
		const auto Catalog=Decode(UUnrealBridgeWorldLibrary::GetWorldContexts(64));
		const TArray<TSharedPtr<FJsonValue>>* Worlds=nullptr;
		if (!Catalog || !Catalog->TryGetArrayField(TEXT("worlds"),Worlds)) return nullptr;
		for (const auto& Entry:*Worlds)
		{
			const auto* Object=static_cast<const TSharedPtr<FJsonObject>*>(nullptr);
			if (Entry->TryGetObject(Object) && String(*Object,TEXT("world_handle"))==Handle)
				return FindObject<UWorld>(nullptr,*String(*Object,TEXT("world_path")));
		}
		return nullptr;
	}
	FString GeometryRevision(const FGeometryRef& Ref,const FGeometry& Geometry)
	{
		const FVector2D A=Geometry.LocalToAbsolute(FVector2D::ZeroVector),B=Geometry.LocalToAbsolute(FVector2D(1,0)),C=Geometry.LocalToAbsolute(FVector2D(0,1)),Size=Geometry.GetLocalSize();
		return Hash(FString::Printf(TEXT("%s|%p|%.6f,%.6f|%.6f,%.6f|%.6f,%.6f|%.6f,%.6f"),*Ref.WindowId,Ref.Slate.Pin().Get(),A.X,A.Y,B.X,B.Y,C.X,C.Y,Size.X,Size.Y));
	}
	bool ResolveGeometry(const FGeometryRef& Ref,FGeometry& Geometry,FWidgetPath& Path,FString& Error)
	{
		if (!FSlateApplication::IsInitialized() || !Ref.World.IsValid() || ResolveWorld(Ref.WorldHandle)!=Ref.World.Get()
			|| !Ref.Widget.IsValid() || Ref.Widget->GetWorld()!=Ref.World.Get() || !Ref.Player.IsValid()
			|| Ref.Player->GetWorld()!=Ref.World.Get() || FPlatformTime::Seconds()-Ref.Created>300)
		{ Error=TEXT("StaleWorldOrWidget"); return false; }
		auto Slate=Ref.Widget->GetCachedWidget(); auto Window=Ref.Window.Pin();
		if (!Slate || Slate!=Ref.Slate.Pin() || !Window || !Slate->IsEnabled() || !Slate->GetVisibility().IsVisible()
			|| !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Slate.ToSharedRef(),Path) || Path.GetWindow()!=Window.ToSharedRef())
		{ Error=TEXT("WidgetNotVisibleInWindow"); return false; }
		Geometry=Ref.Widget->GetCachedGeometry(); const FVector2D Size=Geometry.GetLocalSize();
		if (Size.X<=0 || Size.Y<=0) { Error=TEXT("EmptyGeometry"); return false; }
		if (!Ref.Revision.IsEmpty() && GeometryRevision(Ref,Geometry)!=Ref.Revision)
		{
			const FVector2D Origin=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
			Error=FString::Printf(TEXT("StaleGeometry current_origin=%.6f,%.6f current_size=%.6f,%.6f"),Origin.X,Origin.Y,Size.X,Size.Y); return false;
		}
		return true;
	}
	FString State(const FOperation& Operation)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),Operation.Error.IsEmpty());
		Value->SetStringField(TEXT("schema"),TEXT("unrealbridge.pointer.state.v1")); Value->SetStringField(TEXT("operation_id"),Operation.Id);
		Value->SetStringField(TEXT("world_handle"),Operation.Ref.WorldHandle); Value->SetStringField(TEXT("status"),Operation.Status);
		Value->SetStringField(TEXT("error"),Operation.Error); Value->SetNumberField(TEXT("events_completed"),Operation.Next);
		Value->SetBoolField(TEXT("pressed"),Operation.Pressed.Num()>0); Value->SetArrayField(TEXT("events"),Operation.Evidence);
		Value->SetStringField(TEXT("window_id"),Operation.Ref.WindowId); return Encode(Value);
	}
	void ReleaseOwned(FOperation& Operation)
	{
		if (!FSlateApplication::IsInitialized()) { Operation.Pressed.Empty(); return; }
		auto& App=FSlateApplication::Get(); auto User=App.GetUser(Operation.Ref.UserIndex);
		const auto Captor=User?User->GetPointerCaptor(PointerIndex):nullptr;
		if (User && Captor && Captor==Operation.OwnedCaptor.Pin())
		{
			const FWidgetPath Path=User->GetCaptorPath(PointerIndex);
			if (Path.IsValid() && Operation.Pressed.Num())
			{
				Operation.Pressed.Empty(); FPointerEvent Up(Operation.Ref.UserIndex,PointerIndex,Operation.LastPosition,Operation.LastPosition,Operation.Pressed,Operation.Button,0,Operation.Modifiers);
				App.RoutePointerUpEvent(Path,Up);
			}
			if (User->GetPointerCaptor(PointerIndex)==Captor) User->ReleaseCapture(PointerIndex);
		}
		else if (User && !Captor && Operation.Pressed.Num() && Operation.Ref.World.IsValid())
		{
			// A control can accept a down without capturing. Release only its
			// original still-live path, never hit-test a replacement control.
			const FWidgetPath Path=Operation.LastPath.ToWidgetPath(FWeakWidgetPath::EInterruptedPathHandling::ReturnInvalid);
			if (Path.IsValid() && Path.GetWindow()==Operation.Ref.Window.Pin() && Path.ContainsWidget(Operation.Ref.Slate.Pin().Get()))
			{
				Operation.Pressed.Empty(); FPointerEvent Up(Operation.Ref.UserIndex,PointerIndex,Operation.LastPosition,Operation.LastPosition,Operation.Pressed,Operation.Button,0,Operation.Modifiers);
				App.RoutePointerUpEvent(Path,Up);
			}
		}
		Operation.Pressed.Empty(); Operation.OwnedCaptor.Reset();
	}
	void Tick(float Delta)
	{
		// Python may pump Slate during an editor script. That guard forces actor
		// RPCs local; runtime pointer routing must wait until it has unwound.
		if (GAllowActorScriptExecutionInEditor) return;
		auto* Operation=Operations.Find(Active); if (!Operation) return;
		if (Operation->Cancel || FPlatformTime::Seconds()-Operation->Started>12)
		{
			ReleaseOwned(*Operation); Operation->Status=Operation->Cancel?TEXT("cancelled"):TEXT("failed");
			if (!Operation->Cancel) Operation->Error=TEXT("SequenceDeadline"); Active.Empty(); return;
		}
		if (Operation->Next>=Operation->Events.Num()) { ReleaseOwned(*Operation); Operation->Status=TEXT("succeeded"); Active.Empty(); return; }
		const auto& Event=Operation->Events[Operation->Next];
		if (FPlatformTime::Seconds()-Operation->Started<Event.At) return;
		FGeometry Geometry; FWidgetPath GeometryPath; FString Error;
		FGeometryRef CurrentRef=Operation->Ref; CurrentRef.Revision.Empty();
		if (!ResolveGeometry(CurrentRef,Geometry,GeometryPath,Error))
		{ Operation->Error=Error; ReleaseOwned(*Operation); Operation->Status=TEXT("failed"); Active.Empty(); return; }
		const FVector2D CurrentSize=Geometry.GetLocalSize();
		if (!CurrentSize.Equals(Operation->InitialLocalSize,.001))
		{ Operation->Error=TEXT("GeometrySizeChanged"); ReleaseOwned(*Operation); Operation->Status=TEXT("failed"); Active.Empty(); return; }
		const FString PreviousRevision=Operation->Ref.Revision;
		Operation->Ref.Revision=GeometryRevision(Operation->Ref,Geometry);
		auto& App=FSlateApplication::Get(); auto User=App.GetUser(Operation->Ref.UserIndex);
		const auto Captor=User?User->GetPointerCaptor(PointerIndex):nullptr;
		if (!User || (Captor && Captor!=Operation->OwnedCaptor.Pin()))
		{ Operation->Error=TEXT("ForeignPointerCapture"); ReleaseOwned(*Operation); Operation->Status=TEXT("failed"); Active.Empty(); return; }
		const FVector2D Absolute=Geometry.LocalToAbsolute(Event.Local);
		// Never activate desktop windows on behalf of an input request.
		// Losing the exact active window releases only this operation's state.
		if (!App.IsActive() || !Operation->Ref.Window.Pin()->IsActive())
		{
			Operation->Error=TEXT("WindowNotActive"); ReleaseOwned(*Operation); Operation->Status=TEXT("failed"); Active.Empty(); return;
		}
		FWidgetPath Hit=App.LocateWindowUnderMouse(Absolute,App.GetInteractiveTopLevelWindows(),false,Operation->Ref.UserIndex);
		if (!Hit.IsValid() || Hit.GetWindow()!=Operation->Ref.Window.Pin().ToSharedRef() || !Hit.ContainsWidget(Operation->Ref.Slate.Pin().Get()))
		{ Operation->Error=TEXT("ClippedOrOccluded"); ReleaseOwned(*Operation); Operation->Status=TEXT("failed"); Active.Empty(); return; }
		FWidgetPath Route=Captor?User->GetCaptorPath(PointerIndex):Hit;
		if (Event.Type==TEXT("down")) Operation->Pressed.Add(Operation->Button);
		if (Event.Type==TEXT("up")) Operation->Pressed.Remove(Operation->Button);
		FPointerEvent Pointer(Operation->Ref.UserIndex,PointerIndex,Absolute,Operation->Next?Operation->LastPosition:Absolute,
			Operation->Pressed,Event.Type==TEXT("move")?EKeys::Invalid:Operation->Button,0,Operation->Modifiers);
		bool Handled=false;
		if (Event.Type==TEXT("down")) Handled=App.RoutePointerDownEvent(Route,Pointer).IsEventHandled();
		else if (Event.Type==TEXT("up")) Handled=App.RoutePointerUpEvent(Route,Pointer).IsEventHandled();
		// This is an explicit input sequence, not Slate's synthetic hover
		// refresh: synthetic moves deliberately skip a captor's OnMouseMove.
		else Handled=App.RoutePointerMoveEvent(Route,Pointer,false);
		if (!Captor && User->GetPointerCaptor(PointerIndex)) Operation->OwnedCaptor=User->GetPointerCaptor(PointerIndex);
		Operation->LastPosition=Absolute; Operation->LastPath=FWeakWidgetPath(Hit);
		auto Evidence=MakeShared<FJsonObject>(); Evidence->SetStringField(TEXT("type"),Event.Type);
		Evidence->SetBoolField(TEXT("geometry_reprojected"),PreviousRevision!=Operation->Ref.Revision);
		Evidence->SetStringField(TEXT("geometry_revision"),Operation->Ref.Revision);
		Evidence->SetNumberField(TEXT("local_x"),Event.Local.X); Evidence->SetNumberField(TEXT("local_y"),Event.Local.Y);
		Evidence->SetNumberField(TEXT("absolute_x"),Absolute.X); Evidence->SetNumberField(TEXT("absolute_y"),Absolute.Y);
		Evidence->SetBoolField(TEXT("handled"),Handled); Evidence->SetBoolField(TEXT("captured"),User->GetPointerCaptor(PointerIndex).IsValid());
		Evidence->SetStringField(TEXT("hit_widget_type"),Hit.Widgets.Last().Widget->GetTypeAsString());
		Evidence->SetNumberField(TEXT("frame"),GFrameCounter); Operation->Evidence.Add(MakeShared<FJsonValueObject>(Evidence));
		++Operation->Next; Operation->Status=TEXT("running");
	}
	void Shutdown()
	{
		if (auto* Operation=Operations.Find(Active)) ReleaseOwned(*Operation);
		if (FSlateApplication::IsInitialized() && TickHandle.IsValid()) FSlateApplication::Get().OnPreTick().Remove(TickHandle);
		TickHandle.Reset(); Active.Empty(); Operations.Empty(); Refs.Empty();
	}
}

namespace BridgeSlateWindow
{
	using namespace BridgeSlateInput;
	TMap<FString,FString> Requests;
	TSharedPtr<SWindow> Resolve(const FString& RunId,const FString& WorldHandle,UWorld*& World)
	{
		World=ResolveWorld(WorldHandle); if (!World || World->WorldType!=EWorldType::PIE || !World->GetGameViewport()) return nullptr;
		const auto Lease=Decode(UUnrealBridgeNetworkSessionLibrary::GetNetworkSessionState(RunId)); const TArray<TSharedPtr<FJsonValue>>* Worlds=nullptr;
		if (!Lease || !Lease->HasTypedField<EJson::Boolean>(TEXT("ok")) || !Lease->GetBoolField(TEXT("ok")) || String(Lease,TEXT("status"))!=TEXT("ready") || !Lease->TryGetArrayField(TEXT("network_worlds"),Worlds)) return nullptr;
		bool Owned=false; for (const auto& Entry:*Worlds) { const TSharedPtr<FJsonObject>* O=nullptr; if (Entry->TryGetObject(O)) Owned|=String(*O,TEXT("world_path"))==World->GetPathName(); }
		auto Window=World->GetGameViewport()->GetWindow(); if (!Owned || !Window || !Window->IsRegularWindow()) return nullptr;
		if (auto* Main=FModuleManager::GetModulePtr<IMainFrameModule>(TEXT("MainFrame")); Main && Main->GetParentWindow()==Window) return nullptr;
		return Window;
	}
	FString Model(UWorld* World,const TSharedPtr<SWindow>& Window,const FString& Handle)
	{
		auto V=MakeShared<FJsonObject>(); V->SetBoolField(TEXT("ok"),true); V->SetStringField(TEXT("world_handle"),Handle);
		V->SetStringField(TEXT("window_id"),FString::Printf(TEXT("window:%p"),Window.Get()));
		const FVector2D Size=Window->GetClientSizeInScreen(); FVector2D Viewport; World->GetGameViewport()->GetViewportSize(Viewport);
		V->SetNumberField(TEXT("client_width_px"),Size.X); V->SetNumberField(TEXT("client_height_px"),Size.Y);
		V->SetNumberField(TEXT("viewport_width_px"),Viewport.X); V->SetNumberField(TEXT("viewport_height_px"),Viewport.Y);
		V->SetNumberField(TEXT("dpi_scale"),Window->GetDPIScaleFactor());
		V->SetBoolField(TEXT("application_active"),FSlateApplication::Get().IsActive()); return Encode(V);
	}
}

FString UUnrealBridgeSlateInputLibrary::GetOwnedPIEWindowGeometry(const FString& RunId,const FString& WorldHandle)
{
	using namespace BridgeSlateWindow; UWorld* World=nullptr; const auto Window=Resolve(RunId,WorldHandle,World);
	return Window?Model(World,Window,WorldHandle):BridgeSlateInput::Failure(TEXT("ScopeViolation"),TEXT("Exact owned PIE window required; embedded Editor windows cannot be resized"));
}
FString UUnrealBridgeSlateInputLibrary::CaptureOwnedPIEWindow(const FString& RunId,const FString& WorldHandle)
{
	using namespace BridgeSlateWindow;
	if (!IsInGameThread() || !FSlateApplication::IsInitialized()) return Failure(TEXT("GameThreadRequired"));
	UWorld* World=nullptr; const auto Window=Resolve(RunId,WorldHandle,World);
	if (!Window) return Failure(TEXT("ScopeViolation"),TEXT("Exact owned PIE window required"));
	const FVector2D Bounds=Window->GetClientSizeInScreen();
	if (Bounds.X<1 || Bounds.Y<1 || Bounds.X>2560 || Bounds.Y>1440) return Failure(TEXT("CaptureBudget"));
	TArray<FColor> Pixels; FIntVector Size;
	if (!FSlateApplication::Get().TakeScreenshot(Window->GetContent(),Pixels,Size) || Pixels.Num()!=Size.X*Size.Y)
		return Failure(TEXT("CaptureFailed"));
	TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(Size.X,Size.Y,MakeArrayView(Pixels),PNG);
	// The single native PIE lease owns this stable diagnostic output. The
	// caller cannot supply arbitrary filesystem or Content destinations.
	const FString Directory=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("UnrealBridge/Captures/PointerWindow"));
	const FString File=Directory/TEXT("latest.png");
	if (PNG.IsEmpty() || !IFileManager::Get().MakeDirectory(*Directory,true) || !FFileHelper::SaveArrayToFile(PNG,*File)) return Failure(TEXT("CaptureWriteFailed"));
	auto Result=Decode(Model(World,Window,WorldHandle)); Result->SetStringField(TEXT("run_id"),RunId); Result->SetStringField(TEXT("file_path"),File);
	Result->SetNumberField(TEXT("width"),Size.X); Result->SetNumberField(TEXT("height"),Size.Y); Result->SetBoolField(TEXT("includes_slate_ui"),true);
	return Encode(Result.ToSharedRef());
}
FString UUnrealBridgeSlateInputLibrary::SetOwnedPIEWindowGeometry(const FString& RequestJson)
{
	using namespace BridgeSlateWindow;
	if (!IsInGameThread() || RequestJson.Len()>8192) return Failure(TEXT("InvalidRequest"));
	const auto R=Decode(RequestJson); const TSet<FString> Fields={TEXT("schema"),TEXT("request_id"),TEXT("run_id"),TEXT("world_handle"),TEXT("window_id"),TEXT("width_px"),TEXT("height_px"),TEXT("dpi_scale")};
	if (!R || R->Values.Num()!=Fields.Num() || String(R,TEXT("schema"))!=TEXT("unrealbridge.pie_window.v1")) return Failure(TEXT("InvalidSchema"));
	for (const auto& Pair:R->Values) if (!Fields.Contains(FString(Pair.Key))) return Failure(TEXT("UnexpectedField"));
	const FString Id=String(R,TEXT("request_id")),Handle=String(R,TEXT("world_handle")),Digest=Hash(RequestJson); double Width=0,Height=0,DPI=0;
	if (Id.IsEmpty() || Id.Len()>96 || !Number(R,TEXT("width_px"),Width) || !Number(R,TEXT("height_px"),Height) || !Number(R,TEXT("dpi_scale"),DPI)
		|| Width<640 || Width>2560 || Height<480 || Height>1440 || Width!=FMath::FloorToDouble(Width) || Height!=FMath::FloorToDouble(Height) || DPI<1 || DPI>2) return Failure(TEXT("ValidationFailed"));
	UWorld* World=nullptr; const auto Window=Resolve(String(R,TEXT("run_id")),Handle,World);
	if (!Window || String(R,TEXT("window_id"))!=FString::Printf(TEXT("window:%p"),Window.Get())) return Failure(TEXT("ScopeViolation"));
	if (!BridgeSlateInput::Active.IsEmpty()) return Failure(TEXT("PointerSequenceActive"));
	// Windows ReshapeWindow restores a maximized window with SW_RESTORE,
	// which may activate it. Leave minimized/maximized user layouts alone.
	if (Window->IsWindowMaximized() || Window->IsWindowMinimized())
		return Failure(TEXT("WindowLayoutUnavailable"),TEXT("Restore the owned window explicitly before changing its size; the bridge never restores or activates windows."));
	// The native network lease permits one owned PIE run. Receipts from a
	// previous run cannot be used with its destroyed World/window and must
	// not consume the next run's budget.
	static FString ReceiptRunId;
	const FString RunId=String(R,TEXT("run_id"));
	if (ReceiptRunId!=RunId) { Requests.Empty(); ReceiptRunId=RunId; }
	if (const auto* Prior=Requests.Find(Id)) return *Prior==Digest?Model(World,Window,Handle):Failure(TEXT("IdempotencyConflict"));
	if (Requests.Num()>=64) return Failure(TEXT("ReferenceCapacity")); Requests.Add(Id,Digest);
	// Layout changes do not authorize foreground activation or focus changes.
	Window->SetDPIScaleFactor(DPI); Window->Resize(FVector2D(Width,Height));
	return Model(World,Window,Handle); // asynchronous Slate/layout settlement is read back by the caller
}

FString UUnrealBridgeSlateInputLibrary::GetWidgetInputGeometry(const FString& WorldHandle,const FString& WidgetPath,int32 LocalPlayerIndex)
{
	using namespace BridgeSlateInput;
	if (!IsInGameThread() || !FSlateApplication::IsInitialized()) return Failure(TEXT("GameThreadRequired"));
	UWorld* World=ResolveWorld(WorldHandle);
	auto* Widget=FindObject<UWidget>(nullptr,*WidgetPath);
	if (!World || (World->WorldType!=EWorldType::PIE && World->WorldType!=EWorldType::Game) || !Widget || Widget->GetWorld()!=World
		|| !World->GetGameInstance() || !World->GetGameInstance()->GetLocalPlayers().IsValidIndex(LocalPlayerIndex)) return Failure(TEXT("ScopeViolation"));
	auto* Player=World->GetGameInstance()->GetLocalPlayerByIndex(LocalPlayerIndex);
	const UUserWidget* Owner=Cast<UUserWidget>(Widget); if (!Owner) Owner=Widget->GetTypedOuter<UUserWidget>();
	if (!Owner || Owner->GetOwningLocalPlayer()!=Player || !Player->GetSlateUser()) return Failure(TEXT("PlayerWidgetMismatch"));
	auto Slate=Widget->GetCachedWidget(); FWidgetPath Path;
	if (!Slate || !FSlateApplication::Get().GeneratePathToWidgetUnchecked(Slate.ToSharedRef(),Path)) return Failure(TEXT("WidgetNotArranged"));
	FGeometryRef Ref; Ref.Handle=TEXT("ubr:widget-input:")+FGuid::NewGuid().ToString(EGuidFormats::Digits); Ref.World=World;
	Ref.WorldHandle=WorldHandle; Ref.Widget=Widget; Ref.Slate=Slate; Ref.Player=Player; Ref.LocalIndex=LocalPlayerIndex;
	Ref.UserIndex=Player->GetSlateUser()->GetUserIndex(); Ref.Window=Path.GetWindow(); Ref.WindowId=FString::Printf(TEXT("window:%p"),Ref.Window.Pin().Get());
	Ref.Generation=FGuid::NewGuid().ToString(EGuidFormats::Digits); Ref.Created=FPlatformTime::Seconds();
	FGeometry Geometry; FString Error;
	if (!ResolveGeometry(Ref,Geometry,Path,Error)) return Failure(TEXT("GeometryUnavailable"),Error);
	Ref.Revision=GeometryRevision(Ref,Geometry);
	for (auto It=Refs.CreateIterator();It;++It) if (!It.Value().Widget.IsValid() || FPlatformTime::Seconds()-It.Value().Created>300) It.RemoveCurrent();
	if (Refs.Num()>=128) return Failure(TEXT("ReferenceCapacity")); Refs.Add(Ref.Handle,Ref);
	auto Value=MakeShared<FJsonObject>(); Value->SetBoolField(TEXT("ok"),true); Value->SetStringField(TEXT("schema"),TEXT("unrealbridge.widget_input_geometry.v1"));
	Value->SetStringField(TEXT("world_handle"),WorldHandle); Value->SetStringField(TEXT("widget_path"),WidgetPath);
	Value->SetStringField(TEXT("widget_handle"),Ref.Handle); Value->SetStringField(TEXT("generation"),Ref.Generation);
	Value->SetStringField(TEXT("geometry_revision"),Ref.Revision); Value->SetStringField(TEXT("window_id"),Ref.WindowId);
	Value->SetNumberField(TEXT("local_player_index"),LocalPlayerIndex); Value->SetNumberField(TEXT("slate_user_index"),Ref.UserIndex);
	Value->SetNumberField(TEXT("width"),Geometry.GetLocalSize().X); Value->SetNumberField(TEXT("height"),Geometry.GetLocalSize().Y);
	Value->SetNumberField(TEXT("window_dpi_scale"),Ref.Window.Pin()->GetDPIScaleFactor());
	const FVector2D Origin=Geometry.LocalToAbsolute(FVector2D::ZeroVector);
	Value->SetNumberField(TEXT("absolute_x"),Origin.X); Value->SetNumberField(TEXT("absolute_y"),Origin.Y); return Encode(Value);
}

FString UUnrealBridgeSlateInputLibrary::SubmitPointerSequence(const FString& RequestJson)
{
	using namespace BridgeSlateInput;
	if (!IsInGameThread() || !FSlateApplication::IsInitialized()) return Failure(TEXT("GameThreadRequired"));
	if (RequestJson.Len()>32768) return Failure(TEXT("RequestTooLarge"));
	const auto Request=Decode(RequestJson);
	const TSet<FString> Fields={TEXT("schema"),TEXT("request_id"),TEXT("world_handle"),TEXT("widget_handle"),TEXT("generation"),TEXT("window_id"),TEXT("geometry_revision"),TEXT("local_player_index"),TEXT("coordinate_space"),TEXT("button"),TEXT("modifiers"),TEXT("events")};
	if (!Request || Request->Values.Num()!=Fields.Num() || String(Request,TEXT("schema"))!=TEXT("unrealbridge.pointer.v1")) return Failure(TEXT("InvalidSchema"));
	for (const auto& Pair:Request->Values) if (!Fields.Contains(FString(Pair.Key))) return Failure(TEXT("UnexpectedField"));
	const FString RequestId=String(Request,TEXT("request_id")),InputHash=Hash(RequestJson);
	if (RequestId.IsEmpty() || RequestId.Len()>128) return Failure(TEXT("RequestIdRequired"));
	for (auto It=Operations.CreateIterator();It;++It)
	{
		const auto& Prior=It.Value();
		const bool Terminal=Prior.Status!=TEXT("queued") && Prior.Status!=TEXT("running");
		if (Terminal && It.Key()!=Active && (!Prior.Ref.World.IsValid() || FPlatformTime::Seconds()-Prior.Ref.Created>300))
			It.RemoveCurrent();
	}
	for (const auto& Pair:Operations) if (Pair.Value.RequestId==RequestId)
		return Pair.Value.InputHash==InputHash?State(Pair.Value):Failure(TEXT("IdempotencyConflict"));
	if (!Active.IsEmpty()) return Failure(TEXT("PointerBusy"));
	const auto* Found=Refs.Find(String(Request,TEXT("widget_handle")));
	double LocalIndex=0;
	if (!Found || String(Request,TEXT("world_handle"))!=Found->WorldHandle || String(Request,TEXT("generation"))!=Found->Generation
		|| String(Request,TEXT("window_id"))!=Found->WindowId || String(Request,TEXT("geometry_revision"))!=Found->Revision
		|| !Number(Request,TEXT("local_player_index"),LocalIndex) || LocalIndex!=Found->LocalIndex) return Failure(TEXT("ScopeViolation"));
	FGeometry Geometry; FWidgetPath Path; FString Error;
	if (!ResolveGeometry(*Found,Geometry,Path,Error)) return Failure(TEXT("StaleGeometry"),Error);
	if (!FSlateApplication::Get().IsActive() || !Found->Window.Pin()->IsActive())
		return Failure(TEXT("WindowNotActive"),TEXT("Focus the owned PIE window before submitting pointer input; the bridge never raises windows."));
	auto User=FSlateApplication::Get().GetUser(Found->UserIndex);
	if (!User || User->GetPointerCaptor(PointerIndex) || User->IsDragDropping() || FSlateApplication::Get().GetPressedMouseButtons().Num()) return Failure(TEXT("ForeignPointerCapture"));
	FOperation Operation; Operation.Ref=*Found; Operation.Id=FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Operation.InitialLocalSize=Geometry.GetLocalSize();
	Operation.RequestId=RequestId; Operation.InputHash=InputHash; Operation.Started=FPlatformTime::Seconds();
	const FString Button=String(Request,TEXT("button"));
	if (Button==TEXT("left")) Operation.Button=EKeys::LeftMouseButton;
	else if (Button==TEXT("right")) Operation.Button=EKeys::RightMouseButton;
	else if (Button==TEXT("middle")) Operation.Button=EKeys::MiddleMouseButton;
	else return Failure(TEXT("InvalidButton"));
	const TArray<TSharedPtr<FJsonValue>>* Modifiers=nullptr; TSet<FString> Keys;
	if (!Request->TryGetArrayField(TEXT("modifiers"),Modifiers) || Modifiers->Num()>4) return Failure(TEXT("InvalidModifiers"));
	for (const auto& Item:*Modifiers)
	{
		FString Key; if (!Item->TryGetString(Key) || Keys.Contains(Key) || (Key!=TEXT("shift") && Key!=TEXT("ctrl") && Key!=TEXT("alt") && Key!=TEXT("cmd"))) return Failure(TEXT("InvalidModifiers")); Keys.Add(Key);
	}
	Operation.Modifiers=FModifierKeysState(Keys.Contains(TEXT("shift")),false,Keys.Contains(TEXT("ctrl")),false,Keys.Contains(TEXT("alt")),false,Keys.Contains(TEXT("cmd")),false,false);
	const FString Space=String(Request,TEXT("coordinate_space"));
	if (Space!=TEXT("widget_local") && Space!=TEXT("widget_normalized")) return Failure(TEXT("InvalidCoordinateSpace"));
	const TArray<TSharedPtr<FJsonValue>>* Events=nullptr;
	if (!Request->TryGetArrayField(TEXT("events"),Events) || Events->Num()<1 || Events->Num()>120) return Failure(TEXT("EventBudget"));
	bool Pressed=false; double LastTime=-1;
	for (const auto& Value:*Events)
	{
		const TSharedPtr<FJsonObject>* Item=nullptr;
		if (!Value->TryGetObject(Item) || (*Item)->Values.Num()!=4) return Failure(TEXT("InvalidEvent"));
		FPointerStep Event; Event.Type=String(*Item,TEXT("type")); double X=0,Y=0;
		if (!Number(*Item,TEXT("x"),X) || !Number(*Item,TEXT("y"),Y) || !Number(*Item,TEXT("at_seconds"),Event.At)
			|| Event.At<0 || Event.At>10 || Event.At<LastTime || X<0 || Y<0) return Failure(TEXT("InvalidEvent"));
		if (Space==TEXT("widget_normalized")) { if (X>1 || Y>1) return Failure(TEXT("OutOfBounds")); X*=Geometry.GetLocalSize().X; Y*=Geometry.GetLocalSize().Y; }
		if (X>Geometry.GetLocalSize().X || Y>Geometry.GetLocalSize().Y) return Failure(TEXT("OutOfBounds"));
		if (Event.Type==TEXT("down")) { if (Pressed) return Failure(TEXT("UnbalancedButtons")); Pressed=true; }
		else if (Event.Type==TEXT("up")) { if (!Pressed) return Failure(TEXT("UnbalancedButtons")); Pressed=false; }
		else if (Event.Type!=TEXT("move")) return Failure(TEXT("InvalidEventType"));
		Event.Local=FVector2D(X,Y); LastTime=Event.At; Operation.Events.Add(Event);
	}
	if (Pressed || Operation.Events[0].Type!=TEXT("move")) return Failure(TEXT("UnbalancedButtons"));
	if (Operations.Num()>=128) return Failure(TEXT("OperationCapacity"));
	Active=Operation.Id; Operations.Add(Operation.Id,MoveTemp(Operation));
	if (!TickHandle.IsValid()) TickHandle=FSlateApplication::Get().OnPreTick().AddStatic(&Tick);
	return State(Operations.FindChecked(Active));
}
FString UUnrealBridgeSlateInputLibrary::GetPointerSequenceState(const FString& OperationId)
{
	if (const auto* Operation=BridgeSlateInput::Operations.Find(OperationId)) return BridgeSlateInput::State(*Operation);
	return BridgeSlateInput::Failure(TEXT("UnknownOperation"));
}
FString UUnrealBridgeSlateInputLibrary::CancelPointerSequence(const FString& OperationId,const FString& WorldHandle)
{
	auto* Operation=BridgeSlateInput::Operations.Find(OperationId);
	if (!Operation || Operation->Ref.WorldHandle!=WorldHandle) return BridgeSlateInput::Failure(TEXT("ScopeViolation"));
	Operation->Cancel=true; return BridgeSlateInput::State(*Operation);
}
