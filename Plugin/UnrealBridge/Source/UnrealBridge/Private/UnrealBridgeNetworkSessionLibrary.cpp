#include "UnrealBridgeNetworkSessionLibrary.h"
#include "UnrealBridgeEditorLibrary.h"
#include "UnrealBridgeWorldLibrary.h"
#include "Dom/JsonObject.h"
#include "Editor.h"
#include "Engine/GameInstance.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Misc/SecureHash.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "FileHelpers.h"

namespace BridgeNetworkSession
{
	struct FSession
	{
		FString RunId,Input,EditorSession,PIESession,Topology,Status;
		int32 ExpectedRemotes=0;
		TMap<FString,FString> Receipts;
	};
	TMap<FString,FSession> Sessions;
	FString ActiveRun;
	FString Json(const TSharedRef<FJsonObject>& Object)
	{ FString Value; FJsonSerializer::Serialize(Object,TJsonWriterFactory<TCHAR,TCondensedJsonPrintPolicy<TCHAR>>::Create(&Value)); return Value; }
	TSharedPtr<FJsonObject> Read(const FString& Value)
	{ TSharedPtr<FJsonObject> Result; FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Value),Result); return Result; }
	FString NetworkError(const TCHAR* Code,const TCHAR* Message)
	{
		auto Result=MakeShared<FJsonObject>(); Result->SetBoolField(TEXT("ok"),false);
		Result->SetStringField(TEXT("error_code"),Code); Result->SetStringField(TEXT("error"),Message);
		Result->SetBoolField(TEXT("retryable"),false); return Json(Result);
	}
	bool Identifier(const FString& Value)
	{
		if (Value.IsEmpty() || Value.Len()>128) return false;
		for (TCHAR C:Value) if (!FChar::IsAlnum(C) && C!='-' && C!='_' && C!='.') return false;
		return true;
	}
	bool String(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,FString& Value)
	{ return Object && Object->HasTypedField<EJson::String>(Key) && Object->TryGetStringField(Key,Value); }
	bool Integer(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,int32 Min,int32 Max,int32& Value)
	{
		double Number=0;
		if (!Object || !Object->HasTypedField<EJson::Number>(Key) || !Object->TryGetNumberField(Key,Number)
			|| !FMath::IsFinite(Number) || Number<Min || Number>Max || Number!=FMath::FloorToDouble(Number)) return false;
		Value=static_cast<int32>(Number); return true;
	}
	bool HasPIE()
	{
		if (!GEditor || GEditor->IsPlaySessionInProgress()) return true;
		for (const auto& Context:GEngine->GetWorldContexts()) if (Context.WorldType==EWorldType::PIE) return true;
		return false;
	}
	bool Dirty()
	{ TArray<UPackage*> Packages; FEditorFileUtils::GetDirtyContentPackages(Packages); FEditorFileUtils::GetDirtyWorldPackages(Packages); return Packages.Num()>0; }
	FString State(FSession& Session)
	{
		auto Result=Read(UUnrealBridgeWorldLibrary::GetWorldContexts(32));
		if (!Result) return NetworkError(TEXT("Unavailable"),TEXT("World catalog unavailable"));
		const FString Current=Result->GetStringField(TEXT("pie_session_id"));
		const bool SameEditor=Result->GetStringField(TEXT("editor_session_id"))==Session.EditorSession;
		const bool SamePIE=Current==Session.PIESession;
		if (!SameEditor || (!Current.IsEmpty() && !Session.PIESession.IsEmpty() && !SamePIE)) Session.Status=TEXT("needs_reconciliation");
		TArray<TSharedPtr<FJsonValue>> Evidence;
		int32 Servers=0,Clients=0,Locals=0,Controllers=0,ReadyPawns=0;
		bool AllBegun=true,MapMatches=true;
		const UWorld* EditorWorld=GEditor?GEditor->GetEditorWorldContext().World():nullptr;
		const FString EditorMap=EditorWorld?EditorWorld->GetOutermost()->GetName():FString();
		if (SameEditor && SamePIE && !Current.IsEmpty())
			for (const auto& Context:GEngine->GetWorldContexts())
			{
				UWorld* World=Context.World(); if (!World || Context.WorldType!=EWorldType::PIE) continue;
				auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("world_path"),World->GetPathName());
				Row->SetNumberField(TEXT("pie_instance"),Context.PIEInstance);
				const ENetMode Mode=World->GetNetMode();
				Row->SetStringField(TEXT("net_mode"),Mode==NM_DedicatedServer?TEXT("DedicatedServer"):Mode==NM_ListenServer?TEXT("ListenServer"):Mode==NM_Client?TEXT("Client"):TEXT("Standalone"));
				const int32 LocalCount=World->GetGameInstance()?World->GetGameInstance()->GetLocalPlayers().Num():0;
				Row->SetNumberField(TEXT("local_player_count"),LocalCount); Locals+=LocalCount;
				Row->SetBoolField(TEXT("has_begun_play"),World->HasBegunPlay()); AllBegun&=World->HasBegunPlay();
				FString Map=World->GetOutermost()->GetName(); Map.ReplaceInline(*FString::Printf(TEXT("UEDPIE_%d_"),Context.PIEInstance),TEXT(""));
				MapMatches &= Map==EditorMap;
				if (Mode==NM_Client) ++Clients;
				else if ((Session.Topology==TEXT("dedicated") && Mode==NM_DedicatedServer && LocalCount==0)
					|| (Session.Topology==TEXT("listen") && Mode==NM_ListenServer && LocalCount==1)) ++Servers;
				TArray<TSharedPtr<FJsonValue>> Players;
				int32 WorldControllers=0,WorldPawns=0;
				for (auto It=World->GetPlayerControllerIterator();It;++It)
					if (auto* PC=It->Get(); PC && PC->Player)
					{
						++WorldControllers; WorldPawns+=IsValid(PC->GetPawn())?1:0;
						auto Player=MakeShared<FJsonObject>(); Player->SetStringField(TEXT("controller_path"),PC->GetPathName());
						Player->SetBoolField(TEXT("local"),PC->IsLocalController()); Player->SetBoolField(TEXT("pawn_ready"),IsValid(PC->GetPawn()));
						Player->SetStringField(TEXT("pawn_path"),IsValid(PC->GetPawn())?PC->GetPawn()->GetPathName():FString());
						Player->SetNumberField(TEXT("player_id"),PC->PlayerState?PC->PlayerState->GetPlayerId():-1);
						Player->SetNumberField(TEXT("observed_ping_ms"),PC->PlayerState?PC->PlayerState->GetPingInMilliseconds():-1);
						Players.Add(MakeShared<FJsonValueObject>(Player));
					}
				Row->SetArrayField(TEXT("players"),Players); Controllers+=WorldControllers; ReadyPawns+=WorldPawns;
				if (UNetDriver* Driver=World->GetNetDriver())
				{
					Row->SetStringField(TEXT("net_driver"),Driver->GetClass()->GetPathName());
#if DO_ENABLE_NET_TEST
					const auto& Simulation=Driver->PacketSimulationSettings;
					Row->SetNumberField(TEXT("out_lag_min_ms"),Simulation.PktLagMin); Row->SetNumberField(TEXT("out_lag_max_ms"),Simulation.PktLagMax);
					Row->SetNumberField(TEXT("out_loss_percent"),Simulation.PktLoss);
					Row->SetNumberField(TEXT("in_lag_min_ms"),Simulation.PktIncomingLagMin); Row->SetNumberField(TEXT("in_loss_percent"),Simulation.PktIncomingLoss);
#endif
				}
				Evidence.Add(MakeShared<FJsonValueObject>(Row));
			}
		const int32 ExpectedPlayers=Session.ExpectedRemotes+(Session.Topology==TEXT("listen")?1:0);
		const bool Ready=Servers==1 && Clients==Session.ExpectedRemotes && Locals==ExpectedPlayers && AllBegun && MapMatches
			&& Controllers==ExpectedPlayers+Session.ExpectedRemotes && ReadyPawns==Controllers;
		if (Ready && (Session.Status==TEXT("starting") || Session.Status==TEXT("joining"))) Session.Status=TEXT("ready");
		if (Current.IsEmpty() && !HasPIE() && !Session.PIESession.IsEmpty())
		{ Session.Status=TEXT("cleaned"); if (ActiveRun==Session.RunId) ActiveRun.Reset(); }
		Result->SetBoolField(TEXT("ok"),Session.Status!=TEXT("needs_reconciliation"));
		Result->SetStringField(TEXT("schema"),TEXT("unrealbridge.network_session.v2"));
		Result->SetStringField(TEXT("run_id"),Session.RunId); Result->SetStringField(TEXT("owned_pie_session_id"),Session.PIESession);
		Result->SetStringField(TEXT("topology"),Session.Topology); Result->SetStringField(TEXT("status"),Session.Status);
		Result->SetNumberField(TEXT("remote_client_count"),Session.ExpectedRemotes); Result->SetBoolField(TEXT("topology_ready"),Ready);
		Result->SetArrayField(TEXT("network_worlds"),Evidence); Result->SetBoolField(TEXT("dirty"),Dirty());
		return Json(Result.ToSharedRef());
	}
}

FString UUnrealBridgeNetworkSessionLibrary::StartNetworkSession(const FString& RequestJson)
{
	using namespace BridgeNetworkSession;
	if (!IsInGameThread() || !GEditor || RequestJson.Len()>8192) return NetworkError(TEXT("Unavailable"),TEXT("Editor GameThread and bounded request required"));
	auto Request=Read(RequestJson); FString Schema,Run,Editor,Topology; int32 Count=0,Lag=0,Loss=0;
	if (!String(Request,TEXT("schema"),Schema) || Schema!=TEXT("unrealbridge.network_session.v2")
		|| !String(Request,TEXT("run_id"),Run) || !Identifier(Run) || !String(Request,TEXT("editor_session_id"),Editor)
		|| !String(Request,TEXT("topology"),Topology) || (Topology!=TEXT("listen") && Topology!=TEXT("dedicated"))
		|| !Integer(Request,TEXT("remote_client_count"),1,4,Count) || !Integer(Request,TEXT("out_lag_ms"),0,500,Lag)
		|| !Integer(Request,TEXT("out_loss_percent"),0,10,Loss)) return NetworkError(TEXT("ValidationFailed"),TEXT("Invalid typed topology/count/network profile"));
	if (Request->Values.Num()!=7) return NetworkError(TEXT("ValidationFailed"),TEXT("Unknown fields are not accepted"));
	if (auto* Existing=Sessions.Find(Run)) return Existing->Input==RequestJson?State(*Existing):NetworkError(TEXT("ValidationFailed"),TEXT("Run input conflict"));
	auto Catalog=Read(UUnrealBridgeWorldLibrary::GetWorldContexts(32));
	if (!Catalog || Catalog->GetStringField(TEXT("editor_session_id"))!=Editor) return NetworkError(TEXT("StaleHandle"),TEXT("Editor session changed"));
	if (auto* Previous=Sessions.Find(ActiveRun)) State(*Previous);
	if (!ActiveRun.IsEmpty() || HasPIE() || Dirty()) return NetworkError(TEXT("ScopeViolation"),TEXT("Existing PIE, unresolved native lease or Dirty baseline"));
	if (Sessions.Num()>=64) return NetworkError(TEXT("CapacityExceeded"),TEXT("Session receipt budget reached"));
	auto* Settings=DuplicateObject<ULevelEditorPlaySettings>(GetDefault<ULevelEditorPlaySettings>(),GetTransientPackage());
	Settings->SetRunUnderOneProcess(true);
	Settings->SetPlayNetMode(Topology==TEXT("dedicated")?EPlayNetMode::PIE_Client:EPlayNetMode::PIE_ListenServer);
	Settings->SetPlayNumberOfClients(Count+(Topology==TEXT("listen")?1:0));
	Settings->bLaunchSeparateServer=Topology==TEXT("dedicated");
	Settings->NetworkEmulationSettings.bIsNetworkEmulationEnabled=Lag>0 || Loss>0;
	Settings->NetworkEmulationSettings.EmulationTarget=NetworkEmulationTarget::Any;
	Settings->NetworkEmulationSettings.CurrentProfile=TEXT("Custom");
	Settings->NetworkEmulationSettings.OutPackets.MinLatency=Lag;
	Settings->NetworkEmulationSettings.OutPackets.MaxLatency=Lag;
	Settings->NetworkEmulationSettings.OutPackets.PacketLossPercentage=Loss;
	Settings->NetworkEmulationSettings.InPackets=FNetworkEmulationPacketSettings();
	FSession Session; Session.RunId=Run; Session.Input=RequestJson; Session.EditorSession=Editor; Session.Topology=Topology;
	Session.ExpectedRemotes=Count; Session.Status=TEXT("starting"); Sessions.Add(Run,MoveTemp(Session)); ActiveRun=Run;
	FRequestPlaySessionParams Params; Params.WorldType=EPlaySessionWorldType::PlayInEditor; Params.EditorPlaySettings=Settings;
	GEditor->RequestPlaySession(Params); GEditor->StartQueuedPlaySessionRequest();
	// Claim only the nonce observed within this start call. A later unrelated
	// BeginPIE must never be adopted after an uncertain/failed startup.
	if (auto Started=Read(UUnrealBridgeWorldLibrary::GetWorldContexts(32)))
		Sessions.FindChecked(Run).PIESession=Started->GetStringField(TEXT("pie_session_id"));
	if (Sessions.FindChecked(Run).PIESession.IsEmpty()) Sessions.FindChecked(Run).Status=TEXT("needs_reconciliation");
	return State(Sessions.FindChecked(Run));
}

FString UUnrealBridgeNetworkSessionLibrary::GetNetworkSessionState(const FString& RunId)
{
	if (!IsInGameThread()) return BridgeNetworkSession::NetworkError(TEXT("Unavailable"),TEXT("GameThread required"));
	auto* Session=BridgeNetworkSession::Sessions.Find(RunId);
	return Session?BridgeNetworkSession::State(*Session):BridgeNetworkSession::NetworkError(TEXT("UnknownSession"),TEXT("No native ownership for this run"));
}

FString UUnrealBridgeNetworkSessionLibrary::JoinNetworkClient(const FString& RunId,const FString& ExpectedPIESession,const FString& RequestId,int32 RemoteClientOrdinal)
{
	using namespace BridgeNetworkSession;
	if (!IsInGameThread() || !GEditor || !Identifier(RequestId)) return NetworkError(TEXT("ValidationFailed"),TEXT("Invalid context or request ID"));
	auto* Session=Sessions.Find(RunId); if (!Session) return NetworkError(TEXT("UnknownSession"),TEXT("No native ownership"));
	const FString Fingerprint=TEXT("join:")+ExpectedPIESession+TEXT(":")+FString::FromInt(RemoteClientOrdinal);
	if (const auto* Receipt=Session->Receipts.Find(RequestId)) return *Receipt==Fingerprint?State(*Session):NetworkError(TEXT("ValidationFailed"),TEXT("Request input conflict"));
	State(*Session);
	if (ExpectedPIESession.IsEmpty() || ExpectedPIESession!=Session->PIESession || Session->Status!=TEXT("ready") || RunId!=ActiveRun
		|| RemoteClientOrdinal!=Session->ExpectedRemotes+1 || RemoteClientOrdinal>4 || Session->Receipts.Num()>=64)
		return NetworkError(TEXT("ScopeViolation"),TEXT("Join requires exact ready session and next client ordinal"));
	Session->Receipts.Add(RequestId,Fingerprint); Session->ExpectedRemotes=RemoteClientOrdinal; Session->Status=TEXT("joining");
	GEditor->RequestLateJoin(); return State(*Session);
}

FString UUnrealBridgeNetworkSessionLibrary::StopOwnedNetworkSession(const FString& RunId,const FString& ExpectedPIESession,const FString& RequestId)
{
	using namespace BridgeNetworkSession;
	if (!IsInGameThread() || !GEditor || !Identifier(RequestId)) return NetworkError(TEXT("ValidationFailed"),TEXT("Invalid context or request ID"));
	auto* Session=Sessions.Find(RunId); if (!Session) return NetworkError(TEXT("UnknownSession"),TEXT("No native ownership"));
	const FString Fingerprint=TEXT("stop:")+ExpectedPIESession;
	if (const auto* Receipt=Session->Receipts.Find(RequestId)) return *Receipt==Fingerprint?State(*Session):NetworkError(TEXT("ValidationFailed"),TEXT("Request input conflict"));
	State(*Session);
	if (ExpectedPIESession.IsEmpty() || ExpectedPIESession!=Session->PIESession || Session->Status==TEXT("needs_reconciliation"))
		return NetworkError(TEXT("ScopeViolation"),TEXT("PIE ownership changed; replacement is untouched"));
	if (Session->Status==TEXT("cleaned")) return State(*Session);
	if (ActiveRun!=RunId) return NetworkError(TEXT("ScopeViolation"),TEXT("Run no longer owns active PIE"));
	Session->Receipts.Add(RequestId,Fingerprint); Session->Status=TEXT("stopping"); GEditor->RequestEndPlayMap(); return State(*Session);
}
