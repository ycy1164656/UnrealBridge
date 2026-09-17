#include "UnrealBridgeAudioLibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundNodeWavePlayer.h"
#include "Sound/SoundNodeMixer.h"
#include "Sound/SoundNodeRandom.h"
#include "Sound/SoundNodeModulator.h"
#include "Sound/SoundNodeAttenuation.h"
#include "SoundCueGraph/SoundCueGraphNode.h"
#include "SoundCueGraph/SoundCueGraphNode_Root.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace BridgeCueAuthoring
{
	using namespace BridgeAuthoring;
	using BridgeAuthoring::Error;
	using FNodes=TMap<FString,USoundCueGraphNode*>;
	FString Guid(const UEdGraphNode* Node) { return Node?Node->NodeGuid.ToString(EGuidFormats::Digits):TEXT(""); }
	UClass* Class(const FString& Kind)
	{
		if (Kind==TEXT("wave")) return USoundNodeWavePlayer::StaticClass();
		if (Kind==TEXT("mixer")) return USoundNodeMixer::StaticClass();
		if (Kind==TEXT("random")) return USoundNodeRandom::StaticClass();
		if (Kind==TEXT("modulator")) return USoundNodeModulator::StaticClass();
		if (Kind==TEXT("attenuation")) return USoundNodeAttenuation::StaticClass();
		return nullptr;
	}
	FString Kind(const USoundNode* Node)
	{
		for (const TCHAR* Name:{TEXT("wave"),TEXT("mixer"),TEXT("random"),TEXT("modulator"),TEXT("attenuation")}) if (Node && Node->GetClass()==Class(Name)) return Name;
		return TEXT("unsupported");
	}
	bool Collect(USoundCue* Cue,FNodes& Nodes,USoundCueGraphNode_Root*& Root,FString& Message)
	{
		if (!Cue || !Cue->GetGraph() || Cue->AllNodes.Num()>128) { Message=TEXT("Existing bounded SoundCue graph required"); return false; }
		for (UEdGraphNode* Raw:Cue->GetGraph()->Nodes)
		{
			if (auto* R=Cast<USoundCueGraphNode_Root>(Raw)) { if (Root) { Message=TEXT("Multiple SoundCue outputs"); return false; } Root=R; continue; }
			auto* Node=Cast<USoundCueGraphNode>(Raw);
			if (!Node || !Node->NodeGuid.IsValid() || Nodes.Contains(Guid(Node)) || !Node->SoundNode || Node->SoundNode->GetOuter()!=Cue || Kind(Node->SoundNode)==TEXT("unsupported"))
			{ Message=TEXT("Unsupported SoundNode class, duplicate GUID or foreign Outer"); return false; }
			Nodes.Add(Guid(Node),Node);
		}
		if (!Root || Nodes.Num()!=Cue->AllNodes.Num()) { Message=TEXT("Graph/AllNodes membership mismatch"); return false; }
		TSet<USoundNode*> Members; for (const auto& Pair:Nodes) Members.Add(Pair.Value->SoundNode);
		for (USoundNode* Member:Cue->AllNodes)
			if (!Member || Member->GetOuter()!=Cue || !Members.Remove(Member))
			{ Message=TEXT("Graph/AllNodes contains foreign or duplicate runtime members"); return false; }
		return true;
	}
	TSharedRef<FJsonObject> Properties(USoundNode* Node)
	{
		auto P=MakeShared<FJsonObject>();
		if (auto* Wave=Cast<USoundNodeWavePlayer>(Node)) { P->SetStringField(TEXT("wave_path"),GetPathNameSafe(Wave->GetSoundWave())); P->SetBoolField(TEXT("looping"),Wave->bLooping); }
		if (auto* Mix=Cast<USoundNodeMixer>(Node)) { TArray<TSharedPtr<FJsonValue>> A; for (float Gain:Mix->InputVolume) A.Add(MakeShared<FJsonValueNumber>(Gain)); P->SetArrayField(TEXT("input_gains_linear"),A); }
		if (auto* Random=Cast<USoundNodeRandom>(Node))
		{ TArray<TSharedPtr<FJsonValue>> A; for (float Weight:Random->Weights) A.Add(MakeShared<FJsonValueNumber>(Weight)); P->SetArrayField(TEXT("weights"),A); P->SetBoolField(TEXT("without_replacement"),Random->bRandomizeWithoutReplacement); }
		if (auto* Mod=Cast<USoundNodeModulator>(Node))
		{ P->SetNumberField(TEXT("pitch_min_ratio"),Mod->PitchMin); P->SetNumberField(TEXT("pitch_max_ratio"),Mod->PitchMax); P->SetNumberField(TEXT("volume_min_linear"),Mod->VolumeMin); P->SetNumberField(TEXT("volume_max_linear"),Mod->VolumeMax); }
		if (auto* Att=Cast<USoundNodeAttenuation>(Node))
		{ P->SetNumberField(TEXT("inner_radius_cm"),Att->AttenuationOverrides.AttenuationShapeExtents.X); P->SetNumberField(TEXT("falloff_cm"),Att->AttenuationOverrides.FalloffDistance); P->SetBoolField(TEXT("spatialize"),Att->AttenuationOverrides.bSpatialize); P->SetBoolField(TEXT("uses_inline_override"),Att->bOverrideAttenuation); }
		return P;
	}
	bool SetProperties(USoundNode* Node,const TSharedPtr<FJsonObject>& P,int32 Inputs,FString& Message)
	{
		bool Flag=false;
		if (auto* Wave=Cast<USoundNodeWavePlayer>(Node))
		{
			auto* Source=LoadObject<USoundWave>(nullptr,*String(P,TEXT("wave_path")));
			if (!Fields(P,{TEXT("wave_path"),TEXT("looping")}) || !Source || Source->GetDuration()<=0 || !P->HasTypedField<EJson::Boolean>(TEXT("looping")) || !P->TryGetBoolField(TEXT("looping"),Flag)) { Message=TEXT("Real SoundWave and explicit looping required"); return false; }
			Wave->SetSoundWave(Source); Wave->bLooping=Flag; return true;
		}
		if (Cast<USoundNodeMixer>(Node) || Cast<USoundNodeRandom>(Node))
		{
			const bool Random=Cast<USoundNodeRandom>(Node)!=nullptr; const TCHAR* Field=Random?TEXT("weights"):TEXT("input_gains_linear"); const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;
			if ((Random?!Fields(P,{TEXT("weights"),TEXT("without_replacement")}):!Fields(P,{TEXT("input_gains_linear")})) || !P->TryGetArrayField(Field,Values) || Values->Num()!=Inputs || Inputs<1 || Inputs>16) { Message=TEXT("One bounded gain/weight per input required"); return false; }
			TArray<float> Floats; float Total=0;
			for (const auto& Value:*Values) { double N=0; if (Value->Type!=EJson::Number || !Value->TryGetNumber(N) || !FMath::IsFinite(N) || N<0 || N>(Random?1000:4)) { Message=TEXT("Finite nonnegative input gains/weights required"); return false; } Floats.Add(N); Total+=N; }
			if (Total<=0) { Message=TEXT("At least one input must contribute"); return false; }
			if (auto* R=Cast<USoundNodeRandom>(Node))
			{ if (!P->HasTypedField<EJson::Boolean>(TEXT("without_replacement"))) { Message=TEXT("Explicit random replacement policy required"); return false; } R->Weights=Floats; R->bRandomizeWithoutReplacement=P->GetBoolField(TEXT("without_replacement")); R->PreselectAtLevelLoad=0; }
			else CastChecked<USoundNodeMixer>(Node)->InputVolume=Floats;
			return true;
		}
		if (auto* Mod=Cast<USoundNodeModulator>(Node))
		{
			double PMin=0,PMax=0,VMin=0,VMax=0;
			if (!Fields(P,{TEXT("pitch_min_ratio"),TEXT("pitch_max_ratio"),TEXT("volume_min_linear"),TEXT("volume_max_linear")}) || !Number(P,TEXT("pitch_min_ratio"),PMin) || !Number(P,TEXT("pitch_max_ratio"),PMax) || !Number(P,TEXT("volume_min_linear"),VMin) || !Number(P,TEXT("volume_max_linear"),VMax) || PMin<.125 || PMax>PMin+8 || PMax>8 || PMax<PMin || VMin<0 || VMax<VMin || VMax>4)
			{ Message=TEXT("Ordered pitch ratio .125..8 and linear gain 0..4 required"); return false; }
			Mod->PitchMin=PMin; Mod->PitchMax=PMax; Mod->VolumeMin=VMin; Mod->VolumeMax=VMax; return true;
		}
		if (auto* Att=Cast<USoundNodeAttenuation>(Node))
		{
			double Radius=0,Falloff=0;
			if (!Fields(P,{TEXT("inner_radius_cm"),TEXT("falloff_cm"),TEXT("spatialize")}) || !Number(P,TEXT("inner_radius_cm"),Radius) || !Number(P,TEXT("falloff_cm"),Falloff) || Radius<0 || Radius>100000 || Falloff<1 || Falloff>100000 || !P->HasTypedField<EJson::Boolean>(TEXT("spatialize")))
			{ Message=TEXT("Sphere attenuation requires bounded centimetres and spatialize flag"); return false; }
			Att->bOverrideAttenuation=true; Att->AttenuationOverrides.bAttenuate=true; Att->AttenuationOverrides.bSpatialize=P->GetBoolField(TEXT("spatialize"));
			Att->AttenuationOverrides.AttenuationShape=EAttenuationShape::Sphere; Att->AttenuationOverrides.DistanceAlgorithm=EAttenuationDistanceModel::Linear;
			Att->AttenuationOverrides.AttenuationShapeExtents=FVector(Radius,0,0); Att->AttenuationOverrides.FalloffDistance=Falloff; return true;
		}
		Message=TEXT("Unsupported node property family"); return false;
	}
	TSharedRef<FJsonObject> Model(USoundCue* Cue)
	{
		auto Value=MakeShared<FJsonObject>(); FNodes Nodes; USoundCueGraphNode_Root* Root=nullptr; FString Message;
		Value->SetBoolField(TEXT("ok"),Collect(Cue,Nodes,Root,Message)); Value->SetStringField(TEXT("error"),Message); Value->SetStringField(TEXT("asset_path"),Cue->GetPathName());
		Value->SetStringField(TEXT("output_node_guid"),Cue->FirstNode?Guid(Cue->FirstNode->GraphNode):TEXT(""));
		TArray<FString> Keys; Nodes.GetKeys(Keys); Keys.Sort(); TArray<TSharedPtr<FJsonValue>> Items;
		for (const FString& Key:Keys)
		{
			auto* Node=Nodes[Key]; auto Item=MakeShared<FJsonObject>(); Item->SetStringField(TEXT("node_guid"),Key); Item->SetStringField(TEXT("kind"),Kind(Node->SoundNode));
			Item->SetStringField(TEXT("runtime_class"),Node->SoundNode->GetClass()->GetPathName()); Item->SetNumberField(TEXT("x"),Node->NodePosX); Item->SetNumberField(TEXT("y"),Node->NodePosY);
			TArray<TSharedPtr<FJsonValue>> Children; for (USoundNode* Child:Node->SoundNode->ChildNodes) Children.Add(MakeShared<FJsonValueString>(Child?Guid(Child->GraphNode):TEXT("")));
			Item->SetArrayField(TEXT("inputs"),Children); Item->SetObjectField(TEXT("properties"),Properties(Node->SoundNode)); Items.Add(MakeShared<FJsonValueObject>(Item));
		}
		Value->SetArrayField(TEXT("nodes"),Items); Value->SetStringField(TEXT("structure_revision"),Hash(Encode(Value))); return Value;
	}
	bool Validate(USoundCue* Cue,FString& Message,bool Runtime)
	{
		FNodes Nodes; USoundCueGraphNode_Root* Root=nullptr; if (!Collect(Cue,Nodes,Root,Message)) return false;
		if (!Root->GetInputPin(0) || Root->GetInputPin(0)->LinkedTo.Num()!=1) { Message=TEXT("Exactly one connected output is required"); return false; }
		auto* First=Cast<USoundCueGraphNode>(Root->GetInputPin(0)->LinkedTo[0]->GetOwningNode());
		if (!First || (Runtime && Cue->FirstNode!=First->SoundNode)) { Message=TEXT("Graph/runtime output differs"); return false; }
		TSet<FString> Seen,Active;
		for (const auto& Pair:Nodes)
		{
			USoundNode* Node=Pair.Value->SoundNode; const int32 Inputs=Pair.Value->GetInputCount();
			if (auto* W=Cast<USoundNodeWavePlayer>(Node); W && (!W->GetSoundWave() || W->GetSoundWave()->GetDuration()<=0)) { Message=TEXT("Existing wave source is unresolved or empty"); return false; }
			const TArray<float>* Levels=nullptr; float Maximum=4;
			if (auto* M=Cast<USoundNodeMixer>(Node)) Levels=&M->InputVolume;
			if (auto* R=Cast<USoundNodeRandom>(Node)) { Levels=&R->Weights; Maximum=1000; }
			if (Levels)
			{
				if (Levels->Num()!=Inputs) { Message=TEXT("Existing gain/weight input count mismatch"); return false; } float Total=0;
				for (float N:*Levels) { if (!FMath::IsFinite(N) || N<0 || N>Maximum) { Message=TEXT("Existing gain/weight is invalid"); return false; } Total+=N; }
				if (Total<=0) { Message=TEXT("Existing inputs have no contribution"); return false; }
			}
			if (auto* M=Cast<USoundNodeModulator>(Node); M && (!FMath::IsFinite(M->PitchMin) || !FMath::IsFinite(M->PitchMax) || !FMath::IsFinite(M->VolumeMin) || !FMath::IsFinite(M->VolumeMax)
				|| M->PitchMin<.125 || M->PitchMax>8 || M->PitchMin>M->PitchMax || M->VolumeMin<0 || M->VolumeMax>4 || M->VolumeMin>M->VolumeMax)) { Message=TEXT("Existing modulator is outside verified bounds"); return false; }
		}
		TFunction<bool(USoundCueGraphNode*)> Visit=[&](USoundCueGraphNode* Node)
		{
			if (Active.Contains(Guid(Node))) { Message=TEXT("Sound graph cycle"); return false; } if (Seen.Contains(Guid(Node))) return true;
			Active.Add(Guid(Node)); const int32 Count=Node->GetInputCount();
			if (Count<Node->SoundNode->GetMinChildNodes() || Count>Node->SoundNode->GetMaxChildNodes() || Count>16) { Message=TEXT("SoundNode input arity invalid"); return false; }
			if (Runtime && Count!=Node->SoundNode->ChildNodes.Num()) { Message=TEXT("Graph/runtime input count differs"); return false; }
			for (int32 I=0;I<Count;++I)
			{
				auto* Pin=Node->GetInputPin(I); if (!Pin || Pin->LinkedTo.Num()!=1) { Message=TEXT("Every declared node input must connect exactly one source"); return false; }
				auto* Child=Cast<USoundCueGraphNode>(Pin->LinkedTo[0]->GetOwningNode());
				if (!Child || (Runtime && Node->SoundNode->ChildNodes[I]!=Child->SoundNode)) { Message=TEXT("Graph/runtime input connection differs"); return false; }
				if (!Visit(Child)) return false;
			}
			Active.Remove(Guid(Node)); Seen.Add(Guid(Node)); return true;
		};
		if (!Visit(First)) return false;
		if (Seen.Num()!=Nodes.Num()) { Message=TEXT("Disconnected nodes are retained and rejected, never silently dropped"); return false; }
		return true;
	}
	bool Ops(USoundCue* Cue,const TArray<TSharedPtr<FJsonValue>>& Operations,bool Creating,FString& Message)
	{
		if (Creating) Cue->CreateGraph();
		FNodes Nodes; USoundCueGraphNode_Root* Root=nullptr; if (!Collect(Cue,Nodes,Root,Message)) return false;
		for (int32 I=0;I<Operations.Num();++I)
		{
			const TSharedPtr<FJsonObject>* Ptr=nullptr; if (!Operations[I]->TryGetObject(Ptr)) return false; const auto& Op=*Ptr; const FString Action=String(Op,TEXT("op"));
			if (Action==TEXT("create_cue")) { if (!Creating || I!=0 || !Fields(Op,{TEXT("op")})) { Message=TEXT("create_cue only on an absent target"); return false; } continue; }
			if (Action==TEXT("add_node"))
			{
				FGuid Id; double Inputs=0,X=0,Y=0; UClass* NodeClass=Class(String(Op,TEXT("kind"))); const TSharedPtr<FJsonObject>* Props=nullptr;
				if (!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("kind"),TEXT("input_count"),TEXT("x"),TEXT("y"),TEXT("properties")}) || !NodeClass || !FGuid::ParseExact(String(Op,TEXT("node_guid")),EGuidFormats::Digits,Id) || !Id.IsValid() || Nodes.Contains(Id.ToString(EGuidFormats::Digits)) || Nodes.Num()>=128 || !Number(Op,TEXT("input_count"),Inputs) || Inputs<0 || Inputs>16 || Inputs!=FMath::FloorToDouble(Inputs) || !Number(Op,TEXT("x"),X) || !Number(Op,TEXT("y"),Y) || FMath::Abs(X)>100000 || FMath::Abs(Y)>100000 || X!=FMath::FloorToDouble(X) || Y!=FMath::FloorToDouble(Y) || !Op->TryGetObjectField(TEXT("properties"),Props))
				{ Message=TEXT("Valid GUID, supported node kind and bounded exact creation fields required"); return false; }
				auto* Runtime=NewObject<USoundNode>(Cue,NodeClass,NAME_None,RF_Transactional);
				if (Inputs<Runtime->GetMinChildNodes() || Inputs>Runtime->GetMaxChildNodes()) { Message=TEXT("Node input count unsupported"); return false; }
				TArray<USoundNode*> Empty; Empty.SetNumZeroed(Inputs); Runtime->SetChildNodes(Empty);
				if (!SetProperties(Runtime,*Props,Inputs,Message)) return false;
				Cue->AllNodes.Add(Runtime); Cue->SetupSoundNode(Runtime,false); auto* Node=Cast<USoundCueGraphNode>(Runtime->GraphNode);
				if (!Node) { Message=TEXT("Native graph node setup failed"); return false; }
				Node->NodeGuid=Id; Node->NodePosX=X; Node->NodePosY=Y; Nodes.Add(Guid(Node),Node); continue;
			}
			auto* Node=Nodes.FindRef(String(Op,TEXT("node_guid")));
			if (Action==TEXT("set_property"))
			{
				const TSharedPtr<FJsonObject>* Props=nullptr;
				if (!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("properties")}) || !Node || !Op->TryGetObjectField(TEXT("properties"),Props)) { Message=TEXT("Typed property operation required"); return false; }
				Node->SoundNode->Modify(); if (!SetProperties(Node->SoundNode,*Props,Node->GetInputCount(),Message)) return false; continue;
			}
			if (Action==TEXT("set_location"))
			{
				double X=0,Y=0; if (!Fields(Op,{TEXT("op"),TEXT("node_guid"),TEXT("x"),TEXT("y")}) || !Node || !Number(Op,TEXT("x"),X) || !Number(Op,TEXT("y"),Y) || FMath::Abs(X)>100000 || FMath::Abs(Y)>100000 || X!=FMath::FloorToDouble(X) || Y!=FMath::FloorToDouble(Y)) { Message=TEXT("Bounded location required"); return false; }
				Node->Modify(); Node->NodePosX=X; Node->NodePosY=Y; continue;
			}
			if (Action==TEXT("connect") || Action==TEXT("set_output"))
			{
				auto* Source=Nodes.FindRef(String(Op,TEXT("source_guid"))); USoundCueGraphNode_Base* Destination=Root; double Input=0;
				if (Action==TEXT("connect"))
				{
					Destination=Nodes.FindRef(String(Op,TEXT("destination_guid")));
					if (!Fields(Op,{TEXT("op"),TEXT("source_guid"),TEXT("destination_guid"),TEXT("input_index")}) || !Number(Op,TEXT("input_index"),Input) || Input<0 || Input>15 || Input!=FMath::FloorToDouble(Input)) { Message=TEXT("Exact indexed connection required"); return false; }
				}
				else if (!Fields(Op,{TEXT("op"),TEXT("source_guid")})) { Message=TEXT("Explicit connected output required"); return false; }
				if (!Source || !Destination || !Destination->GetInputPin(Input) || !Cue->GetGraph()->GetSchema()->TryCreateConnection(Source->GetOutputPin(),Destination->GetInputPin(Input))) { Message=TEXT("Native audio graph schema rejected connection"); return false; }
				continue;
			}
			Message=TEXT("Unsupported SoundCue operation; deletion or type replacement is forbidden"); return false;
		}
		if (!Validate(Cue,Message,false)) return false;
		Cue->CompileSoundNodesFromGraphNodes(); Cue->LinkGraphNodesFromSoundNodes();
		return Validate(Cue,Message,true);
	}
	FString Execute(const FString& Text,bool PreviewOnly)
	{
		FRequest Request; FString Reply; if (!Parse(Text,TEXT("audio.sound_cue"),Request,Reply)) return Reply;
		if (Request.DryRun!=PreviewOnly) return Error(TEXT("ScopeViolation"),TEXT("Preview/apply mismatch"));
		if (!USoundCue::GetSoundCueAudioEditor().IsValid()) return Error(TEXT("UnsupportedCapability"),TEXT("AudioEditor SoundCue adapter unavailable"));
		const bool Exists=Request.Snapshot->GetObjectField(TEXT("targets"))->GetObjectField(Request.Target)->GetBoolField(TEXT("exists"));
		auto* Existing=Exists?LoadObject<USoundCue>(nullptr,*Request.Target):nullptr;
		if (Exists && !Existing) return Error(TEXT("ValidationFailed"),TEXT("Target is not SoundCue"));
		TStrongObjectPtr<USoundCue> Staged(Existing?DuplicateObject<USoundCue>(Existing,GetTransientPackage()):NewObject<USoundCue>(GetTransientPackage(),NAME_None,RF_Transient));
		struct FPreviewCleanup
		{
			USoundCue* Cue;
			~FPreviewCleanup() { Cue->FirstNode=nullptr; for (USoundNode* Node:Cue->AllNodes) if (Node && Node->GetOuter()==Cue) Node->ChildNodes.Reset(); }
		} PreviewCleanup{Staged.Get()};
		// SoundCue graph nodes deliberately regenerate GUIDs on duplication. A transient
		// validation copy must keep the original identities used by incremental requests.
		if (Existing && Existing->GetGraph() && Staged->GetGraph())
		{
			TMap<FName,FGuid> OriginalIds;
			for (UEdGraphNode* Node:Existing->GetGraph()->Nodes) OriginalIds.Add(Node->GetFName(),Node->NodeGuid);
			for (UEdGraphNode* Node:Staged->GetGraph()->Nodes)
			{
				const FGuid* Id=OriginalIds.Find(Node->GetFName());
				if (!Id) return Error(TEXT("NeedsReconciliation"),TEXT("Transient graph copy lost an original node"));
				Node->NodeGuid=*Id;
			}
		}
		FString Message; if (!Ops(Staged.Get(),Request.Operations,!Exists,Message)) return Error(TEXT("ValidationFailed"),Message);
		auto Planned=Model(Staged.Get()); Planned->SetStringField(TEXT("asset_path"),Request.Target); if (Existing) Planned->SetObjectField(TEXT("previous_model"),Model(Existing));
		if (PreviewOnly) return Preview(Request,Planned);
		if (!Begin(Request,Reply)) return Reply;
		auto* Cue=Existing;
		if (!Cue) { Cue=NewObject<USoundCue>(CreatePackage(*Request.Target),*FPackageName::GetLongPackageAssetName(Request.Target),RF_Public|RF_Standalone|RF_Transactional); FAssetRegistryModule::AssetCreated(Cue); }
		Cue->Modify(); if (Cue->GetGraph()) Cue->GetGraph()->Modify();
		if (!Ops(Cue,Request.Operations,!Exists,Message)) return Error(TEXT("NeedsReconciliation"),Message,TEXT("retained_unsaved"));
		Cue->PostEditChange(); Cue->MarkPackageDirty(); return Finish(Request,Model(Cue));
	}
}

FString UUnrealBridgeAudioLibrary::GetSoundCueEditModel(const FString& SoundCuePath)
{
	if (auto* Cue=LoadObject<USoundCue>(nullptr,*SoundCuePath)) return BridgeAuthoring::Encode(BridgeCueAuthoring::Model(Cue));
	return BridgeAuthoring::Error(TEXT("ValidationFailed"),TEXT("SoundCue unavailable"));
}
FString UUnrealBridgeAudioLibrary::PreviewSoundCueOps(const FString& RequestJson) { return BridgeCueAuthoring::Execute(RequestJson,true); }
FString UUnrealBridgeAudioLibrary::ApplySoundCueOps(const FString& RequestJson) { return BridgeCueAuthoring::Execute(RequestJson,false); }
FString UUnrealBridgeAudioLibrary::ValidateSoundCueAsset(const FString& SoundCuePath)
{
	auto* Cue=LoadObject<USoundCue>(nullptr,*SoundCuePath); FString Message;
	if (!Cue || !BridgeCueAuthoring::Validate(Cue,Message,true)) return BridgeAuthoring::Error(TEXT("ValidationFailed"),Cue?Message:TEXT("SoundCue unavailable"));
	return BridgeAuthoring::Encode(BridgeCueAuthoring::Model(Cue));
}
