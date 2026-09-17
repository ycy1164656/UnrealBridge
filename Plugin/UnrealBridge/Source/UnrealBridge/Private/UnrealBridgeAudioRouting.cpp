#include "UnrealBridgeAudioLibrary.h"
#include "UnrealBridgeAudioRoutingPrivate.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundCue.h"
#include "Modules/ModuleManager.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UnrealType.h"

namespace BridgeAudioRouting
{
	using namespace BridgeAuthoring;
	using BridgeAuthoring::Error;
	UClass* OptionalClass(const TCHAR* Name)
	{
		if (!FModuleManager::Get().IsModuleLoaded(TEXT("AudioModulation"))) return nullptr;
		return FindObject<UClass>(nullptr,*(FString(TEXT("/Script/AudioModulation."))+Name));
	}
	bool CallBusMix(const TCHAR* Function,const UObject* World,UObject* Mix,bool* Result)
	{
		const FString Op(Function);
		if (Op!=TEXT("ActivateBusMix") && Op!=TEXT("DeactivateBusMix") && Op!=TEXT("IsControlBusMixActive")) return false;
		auto* Class=OptionalClass(TEXT("AudioModulationStatics")); auto* MixClass=OptionalClass(TEXT("SoundControlBusMix"));
		if (!Class || !MixClass || !World || !Mix || !Mix->IsA(MixClass)) return false;
		auto* Fn=Class->FindFunctionByName(Function); if (!Fn) return false;
		FStructOnScope Params(Fn); auto* WorldProp=FindFProperty<FObjectPropertyBase>(Fn,TEXT("WorldContextObject"));
		auto* MixProp=FindFProperty<FObjectPropertyBase>(Fn,TEXT("Mix"));
		if (!WorldProp || !MixProp || !Mix->IsA(MixProp->PropertyClass)) return false;
		WorldProp->SetObjectPropertyValue_InContainer(Params.GetStructMemory(),const_cast<UObject*>(World));
		MixProp->SetObjectPropertyValue_InContainer(Params.GetStructMemory(),Mix);
		Class->GetDefaultObject()->ProcessEvent(Fn,Params.GetStructMemory());
		if (Result) { auto* Return=FindFProperty<FBoolProperty>(Fn,TEXT("ReturnValue")); if (!Return) return false; *Result=Return->GetPropertyValue_InContainer(Params.GetStructMemory()); }
		return true;
	}
	FString Path(UObject* Object) { return Object?(Object->GetOutermost()==GetTransientPackage()?Object->GetPathName():Object->GetOutermost()->GetName()):TEXT(""); }
	FString Kind(UObject* Object)
	{
		if (!Object) return TEXT("");
		if (Object->IsA<USoundClass>()) return TEXT("sound_class");
		if (Object->IsA<USoundMix>()) return TEXT("sound_mix");
		if (Object->GetClass()==USoundSubmix::StaticClass()) return TEXT("submix");
		if (Object->IsA<USoundCue>()) return TEXT("cue");
		if (Object->GetClass()==OptionalClass(TEXT("SoundControlBus"))) return TEXT("control_bus");
		if (Object->GetClass()==OptionalClass(TEXT("SoundControlBusMix"))) return TEXT("control_bus_mix");
		return TEXT("unsupported");
	}
	UClass* Class(const FString& K)
	{
		if (K==TEXT("sound_class")) return USoundClass::StaticClass();
		if (K==TEXT("sound_mix")) return USoundMix::StaticClass();
		if (K==TEXT("submix")) return USoundSubmix::StaticClass();
		if (K==TEXT("control_bus")) return OptionalClass(TEXT("SoundControlBus"));
		if (K==TEXT("control_bus_mix")) return OptionalClass(TEXT("SoundControlBusMix"));
		return nullptr;
	}
	UObject* ObjectValue(UObject* Object,const TCHAR* Name)
	{ const auto* P=FindFProperty<FObjectPropertyBase>(Object->GetClass(),Name); return P?P->GetObjectPropertyValue_InContainer(Object):nullptr; }
	bool Gain(const TSharedPtr<FJsonObject>& Input,float& Out)
	{
		if (!Fields(Input,{TEXT("unit"),TEXT("value")})) return false; double Value=0;
		if (!Number(Input,TEXT("value"),Value)) return false;
		const FString Unit=String(Input,TEXT("unit"));
		if (Unit==TEXT("linear")) { if (Value<0 || Value>4) return false; Out=Value; return true; }
		if (Unit==TEXT("db") && Value>=-96 && Value<=12.0411998) { Out=FMath::Pow(10.,Value/20.); return true; }
		return false;
	}
	TSharedRef<FJsonObject> Model(UObject* Object)
	{
		auto Value=MakeShared<FJsonObject>(); Value->SetStringField(TEXT("asset_path"),Path(Object)); Value->SetStringField(TEXT("kind"),Kind(Object));
		Value->SetStringField(TEXT("class_path"),Object?Object->GetClass()->GetPathName():TEXT(""));
		TArray<TSharedPtr<FJsonValue>> Children,Rows;
		if (auto* C=Cast<USoundClass>(Object))
		{
			Value->SetNumberField(TEXT("gain_linear"),C->Properties.Volume); Value->SetNumberField(TEXT("pitch_ratio"),C->Properties.Pitch);
			Value->SetStringField(TEXT("parent"),Path(C->ParentClass)); for (USoundClass* Child:C->ChildClasses) Children.Add(MakeShared<FJsonValueString>(Path(Child)));
			Value->SetArrayField(TEXT("children"),Children);
		}
		if (auto* M=Cast<USoundMix>(Object))
		{
			Value->SetNumberField(TEXT("fade_in_seconds"),M->FadeInTime); Value->SetNumberField(TEXT("fade_out_seconds"),M->FadeOutTime); Value->SetNumberField(TEXT("duration_seconds"),M->Duration);
			for (const auto& E:M->SoundClassEffects)
			{ auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("sound_class"),Path(E.SoundClassObject)); Row->SetNumberField(TEXT("gain_linear"),E.VolumeAdjuster); Row->SetNumberField(TEXT("pitch_ratio"),E.PitchAdjuster); Row->SetBoolField(TEXT("apply_to_children"),E.bApplyToChildren); Rows.Add(MakeShared<FJsonValueObject>(Row)); }
			Value->SetArrayField(TEXT("overrides"),Rows);
		}
		if (auto* S=Cast<USoundSubmix>(Object))
		{ Value->SetStringField(TEXT("parent"),Path(S->ParentSubmix)); for (USoundSubmixBase* Child:S->ChildSubmixes) Children.Add(MakeShared<FJsonValueString>(Path(Child))); Value->SetArrayField(TEXT("children"),Children); }
		if (auto* Cue=Cast<USoundCue>(Object))
		{
			Value->SetStringField(TEXT("sound_class"),Path(Cue->SoundClassObject)); Value->SetStringField(TEXT("base_submix"),Path(Cue->SoundSubmixObject));
			for (const auto& Send:Cue->SoundSubmixSends)
			{ auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("submix"),Path(Send.SoundSubmix)); Row->SetNumberField(TEXT("gain_linear"),Send.SendLevel); Row->SetBoolField(TEXT("manual"),Send.SendLevelControlMethod==ESendLevelControlMethod::Manual); Rows.Add(MakeShared<FJsonValueObject>(Row)); }
			Value->SetArrayField(TEXT("sends"),Rows);
		}
		if (Kind(Object)==TEXT("control_bus"))
		{
			Value->SetStringField(TEXT("parameter"),Path(ObjectValue(Object,TEXT("Parameter"))));
			if (auto* Bypass=FindFProperty<FBoolProperty>(Object->GetClass(),TEXT("bBypass"))) Value->SetBoolField(TEXT("bypass"),Bypass->GetPropertyValue_InContainer(Object));
		}
		if (Kind(Object)==TEXT("control_bus_mix"))
		{
			if (auto* P=FindFProperty<FArrayProperty>(Object->GetClass(),TEXT("MixStages"))) if (auto* Stage=CastField<FStructProperty>(P->Inner))
			{
				FScriptArrayHelper Helper(P,P->ContainerPtrToValuePtr<void>(Object)); auto* Bus=FindFProperty<FObjectPropertyBase>(Stage->Struct,TEXT("Bus")); auto* V=FindFProperty<FStructProperty>(Stage->Struct,TEXT("Value"));
				if (Bus && V) for (int32 I=0;I<Helper.Num() && I<64;++I)
				{
					auto Row=MakeShared<FJsonObject>(); Row->SetStringField(TEXT("bus"),Path(Bus->GetObjectPropertyValue_InContainer(Helper.GetRawPtr(I))));
					const void* Data=V->ContainerPtrToValuePtr<void>(Helper.GetRawPtr(I));
					for (const auto& Pair:TMap<FString,FString>{{TEXT("value_normalized"),TEXT("TargetValue")},{TEXT("attack_seconds"),TEXT("AttackTime")},{TEXT("release_seconds"),TEXT("ReleaseTime")}})
						if (const auto* F=FindFProperty<FFloatProperty>(V->Struct,*Pair.Value)) Row->SetNumberField(Pair.Key,F->GetPropertyValue_InContainer(Data));
					Rows.Add(MakeShared<FJsonValueObject>(Row));
				}
			}
			Value->SetArrayField(TEXT("stages"),Rows);
		}
		return Value;
	}
	struct FScope
	{
		TMap<FString,UObject*> Objects; TArray<TStrongObjectPtr<UObject>> Keep; TSet<FString> Created,Touched; bool Staged=false;
		~FScope()
		{
			// AudioDevice initialization enumerates ALL live submix UObjects,
			// including transient previews awaiting GC. A rejected staged cycle
			// must never remain visible to its unbounded IsDynamic ancestor walk.
			if (Staged) for (const auto& Pair:Objects)
			{
				if (auto* S=Cast<USoundSubmix>(Pair.Value)) { S->ParentSubmix=nullptr; S->ChildSubmixes.Reset(); }
				if (auto* C=Cast<USoundClass>(Pair.Value)) { C->ParentClass=nullptr; C->ChildClasses.Reset(); }
			}
		}
		FString Key(UObject* Object) const { for (const auto& Pair:Objects) if (Pair.Value==Object) return Pair.Key; return Path(Object); }
		UObject* Resolve(const FString& P) const { const FString K=FPackageName::ObjectPathToPackageName(P); if (const auto* O=Objects.Find(K)) return *O; return P.IsEmpty()?nullptr:LoadObject<UObject>(nullptr,*P); }
		UObject* Mapped(UObject* O) const { if (const auto* Value=Objects.Find(Path(O))) return *Value; return O; }
		void Remap()
		{
			for (const auto& Pair:Objects)
			{
				if (auto* C=Cast<USoundClass>(Pair.Value)) { C->ParentClass=Cast<USoundClass>(Mapped(C->ParentClass)); for (auto& Child:C->ChildClasses) Child=Cast<USoundClass>(Mapped(Child)); }
				if (auto* S=Cast<USoundSubmix>(Pair.Value)) { S->ParentSubmix=Cast<USoundSubmixBase>(Mapped(S->ParentSubmix)); for (auto& Child:S->ChildSubmixes) Child=Cast<USoundSubmixBase>(Mapped(Child)); }
				if (auto* M=Cast<USoundMix>(Pair.Value)) for (auto& Row:M->SoundClassEffects) Row.SoundClassObject=Cast<USoundClass>(Mapped(Row.SoundClassObject));
				if (auto* C=Cast<USoundCue>(Pair.Value))
				{ C->SoundClassObject=Cast<USoundClass>(Mapped(C->SoundClassObject)); C->SoundSubmixObject=Cast<USoundSubmixBase>(Mapped(C->SoundSubmixObject)); for (auto& Send:C->SoundSubmixSends) Send.SoundSubmix=Cast<USoundSubmixBase>(Mapped(Send.SoundSubmix)); }
				if (Kind(Pair.Value)==TEXT("control_bus_mix"))
				{
					auto* A=FindFProperty<FArrayProperty>(Pair.Value->GetClass(),TEXT("MixStages")); auto* S=A?CastField<FStructProperty>(A->Inner):nullptr;
					auto* B=S?FindFProperty<FObjectPropertyBase>(S->Struct,TEXT("Bus")):nullptr;
					if (B) { FScriptArrayHelper H(A,A->ContainerPtrToValuePtr<void>(Pair.Value)); for (int32 I=0;I<H.Num();++I) B->SetObjectPropertyValue_InContainer(H.GetRawPtr(I),Mapped(B->GetObjectPropertyValue_InContainer(H.GetRawPtr(I)))); }
				}
			}
		}
	};
	bool Seed(FScope& Scope,const FRequest& Request,FString& Message)
	{
		for (const FString& PathValue:Request.Targets)
		{
			const bool Exists=Request.Snapshot->GetObjectField(TEXT("targets"))->GetObjectField(PathValue)->GetBoolField(TEXT("exists"));
			UObject* Value=Exists?LoadObject<UObject>(nullptr,*PathValue):nullptr;
			if (Exists && (!Value || Kind(Value)==TEXT("unsupported"))) { Message=TEXT("Unsupported existing audio routing asset: ")+PathValue; return false; }
			if (Value && Scope.Staged)
			{
				// USoundSubmixWithParentBase::PostDuplicate(Normal) calls SetParentSubmix
				// and dirties the ORIGINAL parent. Preview only needs the typed route
				// fields, so never invoke that asset-duplication lifecycle here.
				if (auto* Original=Cast<USoundSubmix>(Value))
				{ auto* Copy=NewObject<USoundSubmix>(GetTransientPackage(),NAME_None,RF_Transient); Copy->ParentSubmix=Original->ParentSubmix; Copy->ChildSubmixes=Original->ChildSubmixes; Value=Copy; }
				else Value=DuplicateObject(Value,GetTransientPackage());
				Scope.Keep.Emplace(Value);
			}
			Scope.Objects.Add(PathValue,Value);
		}
		if (Scope.Staged) Scope.Remap(); return true;
	}
	bool MutableParent(const FScope& S,UObject* Parent) { return !Parent || S.Objects.Contains(S.Key(Parent)); }
	bool RouteCycles(FScope& Scope,FString& Message)
	{
		for (const auto& Pair:Scope.Objects)
		{
			TSet<FString> Seen; UObject* Cursor=Pair.Value;
			while (Cursor)
			{
				const FString Key=Scope.Key(Cursor); if (Seen.Contains(Key) || Seen.Num()>=128) { Message=TEXT("Routing parent cycle or chain budget"); return false; } Seen.Add(Key);
				if (auto* C=Cast<USoundClass>(Cursor)) Cursor=C->ParentClass;
				else if (auto* S=Cast<USoundSubmix>(Cursor)) Cursor=S->ParentSubmix;
				else break;
				Cursor=Scope.Mapped(Cursor);
			}
		}
		return true;
	}
	TSharedRef<FJsonObject> ScopeModel(FScope& S)
	{
		auto Value=MakeShared<FJsonObject>(); auto Assets=MakeShared<FJsonObject>();
		for (const auto& Pair:S.Objects)
		{
			FString Json=Encode(Model(Pair.Value));
			if (S.Staged) for (const auto& Object:S.Objects) if (Object.Value) Json.ReplaceInline(*(TEXT("\"")+Path(Object.Value)+TEXT("\"")),*(TEXT("\"")+Object.Key+TEXT("\"")),ESearchCase::CaseSensitive);
			auto M=Decode(Json); M->SetStringField(TEXT("asset_path"),Pair.Key); Assets->SetObjectField(Pair.Key,M);
		}
		Value->SetObjectField(TEXT("assets"),Assets); Value->SetBoolField(TEXT("audio_modulation_loaded"),OptionalClass(TEXT("SoundControlBusMix"))!=nullptr);
		return Value;
	}
	bool SetBusStage(UObject* Mix,UObject* Bus,const TSharedPtr<FJsonObject>& Op,FString& Message)
	{
		double Target=0,Attack=0,Release=0;
		if (!Number(Op,TEXT("value_normalized"),Target) || Target<0 || Target>1 || !Number(Op,TEXT("attack_seconds"),Attack) || Attack<0 || Attack>60 || !Number(Op,TEXT("release_seconds"),Release) || Release<0 || Release>60)
		{ Message=TEXT("Bus stage needs normalized 0..1 and attack/release seconds 0..60"); return false; }
		auto* A=FindFProperty<FArrayProperty>(Mix->GetClass(),TEXT("MixStages")); auto* Stage=A?CastField<FStructProperty>(A->Inner):nullptr;
		auto* BusProp=Stage?FindFProperty<FObjectPropertyBase>(Stage->Struct,TEXT("Bus")):nullptr; auto* V=Stage?FindFProperty<FStructProperty>(Stage->Struct,TEXT("Value")):nullptr;
		auto* TV=V?FindFProperty<FFloatProperty>(V->Struct,TEXT("TargetValue")):nullptr;
		auto* AT=V?FindFProperty<FFloatProperty>(V->Struct,TEXT("AttackTime")):nullptr;
		auto* RT=V?FindFProperty<FFloatProperty>(V->Struct,TEXT("ReleaseTime")):nullptr;
		if (!A || !Stage || !BusProp || !V || !TV || !AT || !RT || !Bus || !Bus->IsA(BusProp->PropertyClass)) { Message=TEXT("Loaded AudioModulation stage schema is unsupported"); return false; }
		FScriptArrayHelper H(A,A->ContainerPtrToValuePtr<void>(Mix)); int32 Index=INDEX_NONE;
		for (int32 I=0;I<H.Num();++I) if (BusProp->GetObjectPropertyValue_InContainer(H.GetRawPtr(I))==Bus) { if (Index!=INDEX_NONE) { Message=TEXT("Duplicate bus stages need explicit reconciliation"); return false; } Index=I; }
		if (Index==INDEX_NONE) { if (H.Num()>=32) { Message=TEXT("Bus stage budget reached"); return false; } Index=H.AddValue(); }
		void* Row=H.GetRawPtr(Index); BusProp->SetObjectPropertyValue_InContainer(Row,Bus); void* Value=V->ContainerPtrToValuePtr<void>(Row);
		TV->SetPropertyValue_InContainer(Value,Target); AT->SetPropertyValue_InContainer(Value,Attack); RT->SetPropertyValue_InContainer(Value,Release); return true;
	}
	bool Ops(FScope& S,const FRequest& Request,FString& Message)
	{
		for (const auto& Raw:Request.Operations)
		{
			const TSharedPtr<FJsonObject>* Ptr=nullptr; if (!Raw->TryGetObject(Ptr)) { Message=TEXT("Typed routing object required"); return false; }
			const auto& Op=*Ptr; const FString Action=String(Op,TEXT("op")),Target=String(Op,TEXT("target"));
			if (!S.Objects.Contains(Target)) { Message=TEXT("Operation target is outside declared target packages"); return false; }
			UObject* Object=S.Objects[Target];
			if (Action==TEXT("create_asset"))
			{
				UClass* C=Class(String(Op,TEXT("kind")));
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("kind")}) || !C || Object) { Message=TEXT("Supported new routing asset and absent declared target required"); return false; }
				Object=NewObject<UObject>(S.Staged?GetTransientPackage():CreatePackage(*Target),C,S.Staged?NAME_None:FName(*FPackageName::GetLongPackageAssetName(Target)),S.Staged?RF_Transient:RF_Public|RF_Standalone|RF_Transactional);
				S.Objects[Target]=Object; S.Keep.Emplace(Object); S.Created.Add(Target); S.Touched.Add(Target);
				if (!S.Staged) FAssetRegistryModule::AssetCreated(Object); continue;
			}
			if (!Object) { Message=TEXT("Create the declared asset before editing it"); return false; }
			if (!S.Staged) Object->Modify(); S.Touched.Add(Target);
			if (Action==TEXT("sound_class_defaults"))
			{
				auto* C=Cast<USoundClass>(Object); const TSharedPtr<FJsonObject>* G=nullptr; float GainValue=0; double Pitch=0;
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("gain"),TEXT("pitch_ratio")}) || !C || !Op->TryGetObjectField(TEXT("gain"),G) || !Gain(*G,GainValue) || !Number(Op,TEXT("pitch_ratio"),Pitch) || Pitch<.125 || Pitch>8) { Message=TEXT("SoundClass needs typed gain and pitch ratio .125..8"); return false; }
				C->Properties.Volume=GainValue; C->Properties.Pitch=Pitch; continue;
			}
			if (Action==TEXT("sound_class_parent") || Action==TEXT("submix_parent"))
			{
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("parent")})) { Message=TEXT("Exact parent operation required"); return false; }
				const FString ParentPath=String(Op,TEXT("parent")); UObject* Parent=S.Resolve(ParentPath); UObject* Old=nullptr;
				if (auto* C=Cast<USoundClass>(Object); C && Action==TEXT("sound_class_parent")) Old=C->ParentClass;
				else if (auto* Sub=Cast<USoundSubmix>(Object); Sub && Action==TEXT("submix_parent")) Old=Sub->ParentSubmix;
				else { Message=TEXT("Wrong routing parent family"); return false; }
				if ((!ParentPath.IsEmpty() && (!Parent || Kind(Parent)!=Kind(Object))) || !MutableParent(S,Old) || !MutableParent(S,Parent)) { Message=TEXT("Current/new parents must be matching classes and declared mutable targets"); return false; }
				if (Old) { S.Touched.Add(S.Key(Old)); if (!S.Staged) Old->Modify(); }
				if (Parent) { S.Touched.Add(S.Key(Parent)); if (!S.Staged) Parent->Modify(); }
				if (auto* C=Cast<USoundClass>(Object))
				{ if (C->ParentClass) C->ParentClass->ChildClasses.Remove(C); C->ParentClass=Cast<USoundClass>(Parent); if (C->ParentClass) C->ParentClass->ChildClasses.AddUnique(C); }
				else
				{ auto* Sub=CastChecked<USoundSubmix>(Object); if (Sub->ParentSubmix) Sub->ParentSubmix->ChildSubmixes.Remove(Sub); Sub->ParentSubmix=Cast<USoundSubmixBase>(Parent); if (Sub->ParentSubmix) Sub->ParentSubmix->ChildSubmixes.AddUnique(Sub); }
				continue;
			}
			if (Action==TEXT("sound_mix_override"))
			{
				auto* Mix=Cast<USoundMix>(Object); auto* SoundClass=Cast<USoundClass>(S.Resolve(String(Op,TEXT("sound_class"))));
				const TSharedPtr<FJsonObject>* G=nullptr; float GainValue=0; double Pitch=0;
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("sound_class"),TEXT("gain"),TEXT("pitch_ratio"),TEXT("apply_to_children")}) || !Mix || !SoundClass || !Op->TryGetObjectField(TEXT("gain"),G) || !Gain(*G,GainValue) || !Number(Op,TEXT("pitch_ratio"),Pitch) || Pitch<.125 || Pitch>8 || !Op->HasTypedField<EJson::Boolean>(TEXT("apply_to_children"))) { Message=TEXT("Typed SoundMix override required"); return false; }
				auto* E=Mix->SoundClassEffects.FindByPredicate([&](const auto& Row){return S.Key(Row.SoundClassObject)==S.Key(SoundClass);});
				if (!E) { if (Mix->SoundClassEffects.Num()>=32) { Message=TEXT("SoundMix override budget reached"); return false; } E=&Mix->SoundClassEffects.AddDefaulted_GetRef(); }
				E->SoundClassObject=SoundClass; E->VolumeAdjuster=GainValue; E->PitchAdjuster=Pitch; E->bApplyToChildren=Op->GetBoolField(TEXT("apply_to_children")); continue;
			}
			if (Action==TEXT("sound_mix_timing"))
			{
				auto* Mix=Cast<USoundMix>(Object); double In=0,Out=0,Duration=0;
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("fade_in_seconds"),TEXT("fade_out_seconds"),TEXT("duration_seconds")}) || !Mix || !Number(Op,TEXT("fade_in_seconds"),In) || !Number(Op,TEXT("fade_out_seconds"),Out) || !Number(Op,TEXT("duration_seconds"),Duration) || In<0 || In>60 || Out<0 || Out>60 || (Duration!=-1 && (Duration<0 || Duration>600))) { Message=TEXT("Mix timing requires seconds and duration -1 or 0..600"); return false; }
				Mix->FadeInTime=In; Mix->FadeOutTime=Out; Mix->Duration=Duration; continue;
			}
			if (Action==TEXT("cue_class") || Action==TEXT("cue_base_submix") || Action==TEXT("cue_submix_send"))
			{
				auto* Cue=Cast<USoundCue>(Object); const bool ClassOp=Action==TEXT("cue_class"),Send=Action==TEXT("cue_submix_send");
				const TCHAR* Field=ClassOp?TEXT("sound_class"):TEXT("submix"); UObject* Ref=S.Resolve(String(Op,Field));
				if (!Cue || !Ref || (ClassOp?!Ref->IsA<USoundClass>():!Ref->IsA<USoundSubmix>())) { Message=TEXT("Cue route reference has wrong asset class"); return false; }
				if (!Send)
				{
					if (!Fields(Op,{TEXT("op"),TEXT("target"),Field})) { Message=TEXT("Exact Cue route required"); return false; }
					if (ClassOp) Cue->SoundClassObject=CastChecked<USoundClass>(Ref); else { Cue->SoundSubmixObject=CastChecked<USoundSubmix>(Ref); Cue->bEnableBaseSubmix=true; } continue;
				}
				const TSharedPtr<FJsonObject>* G=nullptr; float GainValue=0;
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("submix"),TEXT("gain")}) || !Op->TryGetObjectField(TEXT("gain"),G) || !Gain(*G,GainValue) || GainValue>1) { Message=TEXT("Manual submix send must be within linear 0..1"); return false; }
				auto* Entry=Cue->SoundSubmixSends.FindByPredicate([&](const auto& V){return S.Key(V.SoundSubmix)==S.Key(Ref);});
				if (!Entry) { if (Cue->SoundSubmixSends.Num()>=16) { Message=TEXT("Submix send budget reached"); return false; } Entry=&Cue->SoundSubmixSends.AddDefaulted_GetRef(); }
				Entry->SoundSubmix=CastChecked<USoundSubmix>(Ref); Entry->SendLevel=GainValue; Entry->SendLevelControlMethod=ESendLevelControlMethod::Manual; Cue->bEnableSubmixSends=true; continue;
			}
			if (Action==TEXT("control_bus_parameter"))
			{
				auto* Param=S.Resolve(String(Op,TEXT("parameter"))); auto* ParamClass=OptionalClass(TEXT("SoundModulationParameter")); auto* ParamProp=FindFProperty<FObjectPropertyBase>(Object->GetClass(),TEXT("Parameter")); auto* Bypass=FindFProperty<FBoolProperty>(Object->GetClass(),TEXT("bBypass"));
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("parameter"),TEXT("bypass")}) || Kind(Object)!=TEXT("control_bus") || !Param || !ParamClass || !Param->IsA(ParamClass) || !ParamProp || !Bypass || !Op->HasTypedField<EJson::Boolean>(TEXT("bypass"))) { Message=TEXT("Verified ControlBus parameter and bypass required; optional plugin must already be loaded"); return false; }
				ParamProp->SetObjectPropertyValue_InContainer(Object,Param); Bypass->SetPropertyValue_InContainer(Object,Op->GetBoolField(TEXT("bypass"))); continue;
			}
			if (Action==TEXT("control_bus_mix_stage"))
			{
				UObject* Bus=S.Resolve(String(Op,TEXT("bus")));
				if (!Fields(Op,{TEXT("op"),TEXT("target"),TEXT("bus"),TEXT("value_normalized"),TEXT("attack_seconds"),TEXT("release_seconds")}) || Kind(Object)!=TEXT("control_bus_mix") || Kind(Bus)!=TEXT("control_bus") || !SetBusStage(Object,Bus,Op,Message)) { if (Message.IsEmpty()) Message=TEXT("Verified ControlBusMix and typed stage required"); return false; } continue;
			}
			Message=TEXT("Unsupported routing operation"); return false;
		}
		for (const auto& Pair:S.Objects)
		{
			if (!Pair.Value) { Message=TEXT("Declared absent target has no creation operation"); return false; }
			if (Kind(Pair.Value)==TEXT("control_bus") && !ObjectValue(Pair.Value,TEXT("Parameter"))) { Message=TEXT("ControlBus requires an explicit existing modulation parameter"); return false; }
		}
		return RouteCycles(S,Message);
	}
	FString Execute(const FString& Text,bool PreviewOnly)
	{
		FRequest Request; FString Reply; if (!Parse(Text,TEXT("audio.routing"),Request,Reply)) return Reply;
		if (Request.DryRun!=PreviewOnly) return Error(TEXT("ScopeViolation"),TEXT("Preview/apply mismatch"));
		for (const auto& Raw:Request.Operations)
		{
			const TSharedPtr<FJsonObject>* Op=nullptr; if (!Raw->TryGetObject(Op)) continue;
			if ((String(*Op,TEXT("op")).StartsWith(TEXT("control_bus")) || String(*Op,TEXT("kind")).StartsWith(TEXT("control_bus"))) && !OptionalClass(TEXT("SoundControlBusMix")))
				return Error(TEXT("UnsupportedCapability"),TEXT("AudioModulation must already be installed, enabled and loaded"));
		}
		FScope Staged; Staged.Staged=true; FString Message;
		if (!Seed(Staged,Request,Message) || !Ops(Staged,Request,Message)) return Error(TEXT("ValidationFailed"),Message);
		if (PreviewOnly) return Preview(Request,ScopeModel(Staged));
		FScope Actual; if (!Seed(Actual,Request,Message)) return Error(TEXT("ValidationFailed"),Message);
		if (!Begin(Request,Reply)) return Reply;
		if (!Ops(Actual,Request,Message)) return Error(TEXT("NeedsReconciliation"),Message,TEXT("retained_unsaved"));
		for (const FString& Target:Actual.Touched) { Actual.Objects[Target]->PostEditChange(); Actual.Objects[Target]->MarkPackageDirty(); }
		return Finish(Request,ScopeModel(Actual));
	}
}

FString UUnrealBridgeAudioLibrary::GetAudioRoutingModel(const FString& AssetPathsJson)
{
	if (AssetPathsJson.Len()>65536) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Audio path request exceeds 64 KiB"));
	using namespace BridgeAuthoring; const auto Root=Decode(TEXT("{\"paths\":")+AssetPathsJson+TEXT("}")); const TArray<TSharedPtr<FJsonValue>>* Paths=nullptr;
	if (!Root || !Root->TryGetArrayField(TEXT("paths"),Paths) || Paths->Num()>16) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("At most 16 asset paths required"));
	BridgeAudioRouting::FScope Scope;
	for (const auto& P:*Paths) { FString Path; if (!P->TryGetString(Path) || Path.Len()>512) return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("Asset paths must be bounded strings")); auto* Object=LoadObject<UObject>(nullptr,*Path); if (!Object || BridgeAudioRouting::Kind(Object)==TEXT("unsupported")) return BridgeAuthoring::Error(TEXT("UnsupportedCapability"),TEXT("Unsupported routing asset: ")+Path); Scope.Objects.Add(FPackageName::ObjectPathToPackageName(Path),Object); }
	auto Value=BridgeAudioRouting::ScopeModel(Scope); Value->SetBoolField(TEXT("ok"),true); return Encode(Value);
}
FString UUnrealBridgeAudioLibrary::PreviewAudioRoutingOps(const FString& RequestJson) { return BridgeAudioRouting::Execute(RequestJson,true); }
FString UUnrealBridgeAudioLibrary::ApplyAudioRoutingOps(const FString& RequestJson) { return BridgeAudioRouting::Execute(RequestJson,false); }
