#include "UnrealBridgeNiagaraLibrary.h"
#include "UnrealBridgeAuthoringCommon.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceEmitterBinding.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraScriptSource.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraCommon.h"
#include "EdGraph/EdGraphPin.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"

namespace BridgeNiagaraTransplant
{
	using namespace BridgeAuthoring;
	FString Normalize(FString Text, const UNiagaraSystem* System, const FNiagaraEmitterHandle& Handle)
	{
		if (Handle.GetInstance().Emitter) Text.ReplaceInline(*Handle.GetInstance().Emitter->GetPathName(), TEXT("{Emitter}"));
		Text.ReplaceInline(*System->GetPathName(), TEXT("{System}"));
		// Do not replace a display name inside user parameters or module-local pin names.
		// Those are preserved verbatim by Niagara, even when the emitter is renamed.
		if (Handle.GetInstance().Emitter) Text.ReplaceInline(*(Handle.GetInstance().Emitter->GetName()+TEXT(".")), TEXT("{Emitter}."));
		Text.ReplaceInline(*(System->GetName()+TEXT(":")), TEXT("{System}:"));
		Text.ReplaceInline(*Handle.GetId().ToString(), TEXT("{EmitterId}"));
		return Text;
	}
	TSharedRef<FJsonObject> Snapshot(const UNiagaraSystem* System, const FNiagaraEmitterHandle& Handle, bool bEvidence=false,
		const TMap<FGuid,FGuid>* ExternalIds=nullptr,const TMap<FString,FString>* ExternalNames=nullptr)
	{
		auto J=MakeShared<FJsonObject>();
		const auto* Data=Handle.GetEmitterData();
		const auto* Source=Data?Cast<UNiagaraScriptSource>(Data->GraphSource):nullptr;
		const auto* Graph=Source?Source->NodeGraph.Get():nullptr;
		TArray<FString> Nodes,Renderers,RendererRuntime,Events,Interfaces,Rapid;
		int32 Pins=0,Links=0,DynamicInputs=0;
		if (Graph) for (const UEdGraphNode* Node:Graph->Nodes)
		{
			if (!Node) continue;
			FString Row=Node->NodeGuid.ToString()+TEXT("|")+Node->GetClass()->GetPathName()+TEXT("|")+FString::FromInt(int32(Node->GetDesiredEnabledState()));
			if (const auto* Call=Cast<UNiagaraNodeFunctionCall>(Node))
			{
				Row+=TEXT("|")+(Call->FunctionScript?Call->FunctionScript->GetPathName():TEXT(""));
				if (Call->FunctionScript && Call->FunctionScript->GetUsage()==ENiagaraScriptUsage::DynamicInput) ++DynamicInputs;
			}
			for (const UEdGraphPin* Pin:Node->Pins)
			{
				if (!Pin) continue; ++Pins;
				Row+=TEXT("|")+Pin->PinId.ToString()+TEXT(":")+Pin->PinName.ToString()+TEXT(":")+Pin->DefaultValue;
				if (Pin->DefaultObject) Row+=Pin->DefaultObject->GetPathName();
				TArray<FString> Targets;
				for (const UEdGraphPin* Linked:Pin->LinkedTo) if (Linked)
				{ ++Links; Targets.Add(Linked->GetOwningNode()->NodeGuid.ToString()+TEXT(":")+Linked->PinId.ToString()); }
				Targets.Sort(); Row+=FString::Join(Targets,TEXT(","));
			}
			Nodes.Add(Normalize(Row,System,Handle));
		}
		if (Graph)
		{
			TArray<UObject*> Objects; GetObjectsWithOuter(Graph,Objects,EGetObjectsFlags::IncludeNestedObjects);
			for (UObject* Object:Objects) if (auto* DI=Cast<UNiagaraDataInterface>(Object))
			{
				FString Row=DI->GetClass()->GetPathName();
				for (TFieldIterator<FProperty> P(DI->GetClass());P;++P)
				{
					if (P->HasAnyPropertyFlags(CPF_Transient)) continue;
					FString Text;
					const auto* StructP=CastField<FStructProperty>(*P);
					if (ExternalNames && StructP && StructP->Struct==FNiagaraDataInterfaceEmitterBinding::StaticStruct())
					{
						auto Binding=*StructP->ContainerPtrToValuePtr<FNiagaraDataInterfaceEmitterBinding>(DI);
						if (const auto* Name=ExternalNames->Find(Binding.EmitterName.ToString())) Binding.EmitterName=FName(**Name);
						P->ExportText_Direct(Text,&Binding,nullptr,DI,PPF_None);
					}
					else P->ExportText_InContainer(0,Text,DI,DI,DI,PPF_None);
					Row+=TEXT("|")+P->GetName()+TEXT("=")+Text;
				}
				Interfaces.Add(Normalize(Row,System,Handle));
			}
		}
		if (Data)
		{
			for (const auto* Renderer:Data->GetRenderers()) if (Renderer)
			{
				FString Row=Renderer->GetClass()->GetPathName();
				FString RuntimeRow=Row;
				for (TFieldIterator<FProperty> Property(Renderer->GetClass());Property;++Property)
				{
					if (Property->HasAnyPropertyFlags(CPF_Transient)) continue;
					FString Text; Property->ExportText_InContainer(0,Text,Renderer,Renderer,const_cast<UNiagaraRendererProperties*>(Renderer),PPF_None);
					RuntimeRow+=TEXT("|")+Property->GetName()+TEXT("=")+Text;
					const auto* StructProperty=CastField<FStructProperty>(*Property);
					if (StructProperty && StructProperty->Struct==FNiagaraVariableAttributeBinding::StaticStruct())
					{
						// CacheValues changes these serialized implementation flags during compile/load.
						// Keep their actual values in the runtime evidence, not the authored-config hash.
						auto Binding=*StructProperty->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Renderer);
						for (const FName Flag:{FName(TEXT("bBindingExistsOnSource")),FName(TEXT("bIsCachedParticleValue"))})
							if (const auto* P=FindFProperty<FBoolProperty>(FNiagaraVariableAttributeBinding::StaticStruct(),Flag)) P->SetPropertyValue_InContainer(&Binding,false);
						Text.Reset(); StructProperty->ExportText_Direct(Text,&Binding,nullptr,const_cast<UNiagaraRendererProperties*>(Renderer),PPF_None);
					}
					Row+=TEXT("|")+Property->GetName()+TEXT("=")+Text;
				}
				Renderers.Add(Normalize(Row,System,Handle));
				RendererRuntime.Add(Normalize(RuntimeRow,System,Handle));
			}
			for (const auto& Event:Data->GetEventHandlers())
			{
				FString EventId=Event.SourceEmitterID==Handle.GetId()?TEXT("{EmitterId}"):Event.SourceEmitterID.ToString();
				if (Event.SourceEmitterID!=Handle.GetId() && ExternalIds) if (const auto* Mapped=ExternalIds->Find(Event.SourceEmitterID)) EventId=Mapped->ToString();
				Events.Add(EventId+TEXT(":")+Event.SourceEventName.ToString());
			}
			TArray<UNiagaraScript*> Scripts; Data->GetScripts(Scripts,false,false);
			for (const auto* Script:Scripts) if (Script)
			{
				TArray<FNiagaraVariable> Variables; Script->RapidIterationParameters.GetParameters(Variables);
				for (const auto& V:Variables)
				{
					FString Name=V.GetName().ToString();
					const FString Prefix=TEXT("Constants.")+Handle.GetUniqueInstanceName()+TEXT(".");
					if (Name.StartsWith(Prefix)) Name=TEXT("Constants.{EmitterName}.")+Name.Mid(Prefix.Len());
					FString Value;
					if (!V.IsDataInterface() && !V.IsUObject())
					{ TArray<uint8> Bytes; Bytes.SetNumZeroed(V.GetSizeInBytes()); Script->RapidIterationParameters.CopyParameterData(V,Bytes.GetData()); Value=BytesToHex(Bytes.GetData(),Bytes.Num()); }
					else if (V.IsUObject()) { const UObject* O=Script->RapidIterationParameters.GetUObject(V).Get(); Value=O?O->GetPathName():TEXT("None"); }
					else { const auto* DI=Script->RapidIterationParameters.GetDataInterface(V); Value=DI?DI->GetClass()->GetPathName():TEXT("None"); }
					Rapid.Add(FString::FromInt(int32(Script->GetUsage()))+TEXT(":")+Name+TEXT(":")+V.GetType().GetName()+TEXT(":")+Normalize(Value,System,Handle));
				}
			}
		}
		Nodes.Sort(); Renderers.Sort(); RendererRuntime.Sort(); Events.Sort(); Interfaces.Sort(); Rapid.Sort();
		J->SetNumberField(TEXT("nodes"),Nodes.Num()); J->SetNumberField(TEXT("pins"),Pins); J->SetNumberField(TEXT("links"),Links);
		J->SetNumberField(TEXT("dynamic_inputs"),DynamicInputs); J->SetNumberField(TEXT("renderers"),Renderers.Num()); J->SetNumberField(TEXT("event_handlers"),Events.Num());
		J->SetStringField(TEXT("graph_hash"),Hash(FString::Join(Nodes,TEXT("\n"))));
		J->SetStringField(TEXT("renderer_hash"),Hash(FString::Join(Renderers,TEXT("\n"))));
		J->SetStringField(TEXT("renderer_runtime_hash"),Hash(FString::Join(RendererRuntime,TEXT("\n"))));
		J->SetStringField(TEXT("renderer_hash_scope"),TEXT("renderer properties excluding Niagara attribute-binding CacheValues flags; full values retained in renderer_runtime_hash/evidence"));
		J->SetStringField(TEXT("event_hash"),Hash(FString::Join(Events,TEXT("\n"))));
		J->SetStringField(TEXT("data_interface_hash"),Hash(FString::Join(Interfaces,TEXT("\n"))));
		J->SetStringField(TEXT("rapid_iteration_hash"),Hash(FString::Join(Rapid,TEXT("\n"))));
		J->SetNumberField(TEXT("data_interfaces"),Interfaces.Num()); J->SetNumberField(TEXT("rapid_iteration_parameters"),Rapid.Num());
		J->SetStringField(TEXT("sim_target"),Data && Data->SimTarget==ENiagaraSimTarget::GPUComputeSim?TEXT("GPU"):TEXT("CPU"));
		if (bEvidence)
		{
			auto Rows=[&](const TCHAR* Key,const TArray<FString>& Values)
			{ TArray<TSharedPtr<FJsonValue>> Out; for (int32 I=0;I<FMath::Min(Values.Num(),256);++I) Out.Add(MakeShared<FJsonValueString>(Values[I].Left(16384))); J->SetArrayField(Key,Out); };
			Rows(TEXT("graph_evidence"),Nodes); Rows(TEXT("renderer_evidence"),Renderers); Rows(TEXT("data_interface_evidence"),Interfaces); Rows(TEXT("rapid_iteration_evidence"),Rapid);
			Rows(TEXT("renderer_runtime_evidence"),RendererRuntime);
		}
		return J;
	}
	TArray<UNiagaraDataInterface*> OwnedInterfaces(const FNiagaraEmitterHandle& Handle)
	{
		TArray<UNiagaraDataInterface*> Result; TArray<UObject*> Objects;
		GetObjectsWithOuter(Handle.GetInstance().Emitter,Objects,EGetObjectsFlags::IncludeNestedObjects);
		for (UObject* Object:Objects) if (auto* DI=Cast<UNiagaraDataInterface>(Object)) Result.Add(DI);
		return Result;
	}
	bool EqualParameter(const FNiagaraParameterStore& A,const FNiagaraParameterStore& B,const FNiagaraVariable& Variable)
	{
		if (Variable.IsDataInterface())
		{
			const auto* Left=A.GetDataInterface(Variable); const auto* Right=B.GetDataInterface(Variable);
			return Left==Right || (Left && Right && Left->GetClass()==Right->GetClass() && Left->Equals(Right));
		}
		if (Variable.IsUObject()) return A.GetUObject(Variable)==B.GetUObject(Variable);
		TArray<uint8> Left,Right; Left.SetNumZeroed(Variable.GetSizeInBytes()); Right.SetNumZeroed(Variable.GetSizeInBytes());
		return A.CopyParameterData(Variable,Left.GetData()) && B.CopyParameterData(Variable,Right.GetData()) && Left==Right;
	}
}

FString UUnrealBridgeNiagaraLibrary::InspectNiagaraEmitterInstance(const FString& SystemPath,const FString& EmitterHandleId,
	const FString& EmitterName,bool bIncludeEvidence)
{
	using namespace BridgeNiagaraTransplant;
	auto* System=LoadObject<UNiagaraSystem>(nullptr,*SystemPath); FGuid Id;
	if (!System || !FGuid::Parse(EmitterHandleId,Id)) return BridgeAuthoring::Error(TEXT("InvalidIdentity"),TEXT("Exact System and emitter identity required"));
	for (const auto& H:System->GetEmitterHandles()) if (H.GetId()==Id && H.GetName().ToString()==EmitterName)
	{
		auto J=Snapshot(System,H,bIncludeEvidence); J->SetBoolField(TEXT("ok"),true);
		J->SetStringField(TEXT("system_path"),System->GetPathName()); J->SetStringField(TEXT("handle_id"),H.GetId().ToString()); J->SetStringField(TEXT("name"),H.GetName().ToString());
		TArray<FString> Refs; for (const auto* DI:OwnedInterfaces(H)) DI->GetEmitterReferencesByName(Refs);
		TArray<TSharedPtr<FJsonValue>> Names; for (const auto& Ref:Refs) Names.Add(MakeShared<FJsonValueString>(Ref)); J->SetArrayField(TEXT("data_interface_emitter_references"),Names);
		return Encode(J);
	}
	return BridgeAuthoring::Error(TEXT("EmitterIdentityMismatch"),TEXT("Handle and name do not resolve to the same emitter"));
}

FString UUnrealBridgeNiagaraLibrary::TransplantNiagaraEmitterInstance(const FString& SourceSystemPath,
	const FString& SourceEmitterHandleId,const FString& SourceEmitterName,const FString& DestinationSystemPath,
	const FString& DestinationEmitterName,bool bCopyUserParameters,bool bRequestCompile,const FString& DependencyMapJson)
{
	using namespace BridgeNiagaraTransplant;
	auto* Source=LoadObject<UNiagaraSystem>(nullptr,*SourceSystemPath);
	auto* Destination=LoadObject<UNiagaraSystem>(nullptr,*DestinationSystemPath);
	if (!Source || !Destination || Source==Destination) return BridgeAuthoring::Error(TEXT("InvalidSystems"),TEXT("Distinct existing source and destination Systems are required"));
	FGuid Id; if (!FGuid::Parse(SourceEmitterHandleId,Id) || SourceEmitterName.IsEmpty() || DestinationEmitterName.IsEmpty())
		return BridgeAuthoring::Error(TEXT("InvalidIdentity"),TEXT("Exact emitter handle, source name and destination name required"));
	const FNiagaraEmitterHandle* Found=nullptr; int32 NameMatches=0;
	for (const auto& Handle:Source->GetEmitterHandles())
	{ if (Handle.GetName().ToString()==SourceEmitterName) ++NameMatches; if (Handle.GetId()==Id) Found=&Handle; }
	if (!Found || NameMatches!=1 || Found->GetName().ToString()!=SourceEmitterName)
		return BridgeAuthoring::Error(TEXT("EmitterIdentityMismatch"),TEXT("Handle and unique display name must resolve to the same emitter"));
	for (const auto& Handle:Destination->GetEmitterHandles()) if (Handle.GetName()==FName(*DestinationEmitterName))
		return BridgeAuthoring::Error(TEXT("DestinationNameConflict"),TEXT("Destination emitter name already exists; no automatic suffix or overwrite"));
	const auto* Data=Found->GetEmitterData();
	if (!Data || Found->GetEmitterMode()!=ENiagaraEmitterMode::Standard)
		return BridgeAuthoring::Error(TEXT("UnsupportedEmitter"),TEXT("Versioned standard emitter instance required"));
	TSharedPtr<FJsonObject> DependencyMap;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DependencyMapJson),DependencyMap) || !DependencyMap.IsValid())
		return BridgeAuthoring::Error(TEXT("InvalidDependencyMap"),TEXT("Object mapping exact source handle IDs to destination handle IDs required"));
	TMap<FGuid,FGuid> IdMap; TMap<FString,FString> NameMap;
	for (const auto& Pair:DependencyMap->Values)
	{
		FGuid From,To; FString ToText;
		if (!Pair.Value->TryGetString(ToText) || !FGuid::Parse(Pair.Key,From) || !FGuid::Parse(ToText,To) || From==Id)
			return BridgeAuthoring::Error(TEXT("InvalidDependencyMap"),TEXT("Dependency IDs must be valid; the copied emitter maps automatically"));
		const auto* A=Source->GetEmitterHandles().FindByPredicate([&](const auto& H){return H.GetId()==From;});
		const auto* B=Destination->GetEmitterHandles().FindByPredicate([&](const auto& H){return H.GetId()==To;});
		if (!A || !B) return BridgeAuthoring::Error(TEXT("DependencyIdentityMismatch"),TEXT("Dependency map references a missing handle"));
		IdMap.Add(From,To); NameMap.Add(A->GetName().ToString(),B->GetName().ToString());
	}
	NameMap.Add(SourceEmitterName,DestinationEmitterName);
	for (const auto& Event:Data->GetEventHandlers()) if (Event.SourceEmitterID.IsValid() && Event.SourceEmitterID!=Id && !IdMap.Contains(Event.SourceEmitterID))
		return BridgeAuthoring::Error(TEXT("ExternalEmitterDependency"),TEXT("Event source needs an explicit source-to-destination handle mapping"));
	for (const auto* DI:OwnedInterfaces(*Found))
	{
		TArray<FString> Refs; DI->GetEmitterReferencesByName(Refs);
		for (const auto& Ref:Refs) if (!NameMap.Contains(Ref))
			return BridgeAuthoring::Error(TEXT("ExternalEmitterDependency"),TEXT("Data interface source needs an explicit handle mapping: ")+Ref);
	}
	TArray<FNiagaraVariable> Parameters; Source->GetExposedParameters().GetParameters(Parameters);
	TArray<TSharedPtr<FJsonValue>> Reused,Added;
	for (const auto& Variable:Parameters)
	{
		const auto* Existing=Destination->GetExposedParameters().FindParameterVariable(Variable,true);
		if (Existing)
		{
			if (Existing->GetType()!=Variable.GetType() || !EqualParameter(Source->GetExposedParameters(),Destination->GetExposedParameters(),Variable))
				return BridgeAuthoring::Error(TEXT("UserParameterConflict"),TEXT("Destination has a different type/value for ")+Variable.GetName().ToString());
			Reused.Add(MakeShared<FJsonValueString>(Variable.GetName().ToString()));
		}
		else
		{
			if (!bCopyUserParameters) return BridgeAuthoring::Error(TEXT("MissingUserParameter"),Variable.GetName().ToString());
			Added.Add(MakeShared<FJsonValueString>(Variable.GetName().ToString()));
		}
	}
	const auto Before=Snapshot(Source,*Found); const FString BeforeText=Encode(Before);
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge","TransplantNiagaraEmitter","UnrealBridge Transplant Emitter Instance"));
	Destination->Modify();
	const FNiagaraEmitterHandle SourceHandle=*Found;
	// The editor utility also wires the System spawn/update graphs. A bare DuplicateEmitterHandle
	// copies the emitter data but can leave it disconnected from System execution.
	const FGuid NewId=FNiagaraEditorUtilities::AddEmitterToSystem(*Destination,*SourceHandle.GetInstance().Emitter,SourceHandle.GetInstance().Version,true);
	auto* AppliedHandle=Destination->GetEmitterHandles().FindByPredicate([&](const auto& H){return H.GetId()==NewId;});
	if (AppliedHandle) AppliedHandle->SetName(FName(*DestinationEmitterName),*Destination);
	FNiagaraEmitterHandle Applied=AppliedHandle?*AppliedHandle:FNiagaraEmitterHandle::InvalidHandle;
	// The source is a private System-owned instance, not an emitter asset. Keep its
	// copied content while removing the newly created cross-package parent link.
	if (auto* AppliedData=Applied.GetEmitterData()) AppliedData->RemoveParent();
	auto Receipt=MakeShared<FJsonObject>();
	Receipt->SetStringField(TEXT("schema"),TEXT("unrealbridge.niagara_emitter_transplant.v1"));
	Receipt->SetStringField(TEXT("source_system"),Source->GetPathName()); Receipt->SetStringField(TEXT("destination_system"),Destination->GetPathName());
	Receipt->SetStringField(TEXT("requested_handle_id"),SourceEmitterHandleId); Receipt->SetStringField(TEXT("resolved_source_handle_id"),SourceHandle.GetId().ToString());
	Receipt->SetStringField(TEXT("requested_name"),SourceEmitterName); Receipt->SetStringField(TEXT("requested_destination_name"),DestinationEmitterName);
	Receipt->SetBoolField(TEXT("saved"),false); Receipt->SetStringField(TEXT("runtime_validation"),TEXT("not_run"));
	if (!Applied.IsValid())
	{
		Receipt->SetBoolField(TEXT("ok"),false); Receipt->SetStringField(TEXT("status"),TEXT("needs_reconciliation"));
		Receipt->SetStringField(TEXT("side_effect_state"),TEXT("retained_unsaved")); return Encode(Receipt);
	}
	if (bCopyUserParameters) Source->GetExposedParameters().CopyParametersTo(Destination->GetExposedParameters(),true,FNiagaraParameterStore::EDataInterfaceCopyMethod::Value);
	// Niagara's duplicate path remaps owned event IDs; enforce the self-reference explicitly.
	IdMap.Add(SourceHandle.GetId(),Applied.GetId());
	if (auto* AppliedData=Applied.GetEmitterData()) for (auto& Event:AppliedData->EventHandlerScriptProps)
		if (const auto* Mapped=IdMap.Find(Event.SourceEmitterID)) Event.SourceEmitterID=*Mapped;
	for (auto* DI:OwnedInterfaces(Applied))
	{
		for (TFieldIterator<FStructProperty> P(DI->GetClass());P;++P)
			if (P->Struct==FNiagaraDataInterfaceEmitterBinding::StaticStruct())
			{
				auto* Binding=P->ContainerPtrToValuePtr<FNiagaraDataInterfaceEmitterBinding>(DI);
				if (const auto* Mapped=NameMap.Find(Binding->EmitterName.ToString())) Binding->EmitterName=FName(**Mapped);
			}
	}
	Destination->MarkPackageDirty();
	const auto After=Snapshot(Destination,Applied);
	const auto Expected=Snapshot(Source,SourceHandle,false,&IdMap,&NameMap);
	const bool bSourcePreserved=Encode(Snapshot(Source,SourceHandle))==BeforeText;
	const bool bEquivalent=String(Expected,TEXT("graph_hash"))==String(After,TEXT("graph_hash")) &&
		String(Expected,TEXT("renderer_hash"))==String(After,TEXT("renderer_hash")) && String(Expected,TEXT("event_hash"))==String(After,TEXT("event_hash")) &&
		String(Expected,TEXT("data_interface_hash"))==String(After,TEXT("data_interface_hash")) && String(Expected,TEXT("rapid_iteration_hash"))==String(After,TEXT("rapid_iteration_hash"));
	const bool bNameMatches=Applied.GetName().ToString()==DestinationEmitterName;
	Receipt->SetBoolField(TEXT("ok"),bSourcePreserved && bEquivalent && bNameMatches);
	Receipt->SetStringField(TEXT("status"),bSourcePreserved && bEquivalent && bNameMatches?TEXT("applied"):TEXT("needs_reconciliation"));
	Receipt->SetStringField(TEXT("side_effect_state"),TEXT("retained_unsaved"));
	Receipt->SetStringField(TEXT("applied_handle_id"),Applied.GetId().ToString()); Receipt->SetStringField(TEXT("applied_name"),Applied.GetName().ToString());
	Receipt->SetObjectField(TEXT("source_snapshot"),Before); Receipt->SetObjectField(TEXT("readback_snapshot"),After);
	Receipt->SetObjectField(TEXT("expected_after_dependency_remap"),Expected);
	Receipt->SetBoolField(TEXT("source_preserved"),bSourcePreserved); Receipt->SetBoolField(TEXT("instance_equivalent"),bEquivalent);
	Receipt->SetArrayField(TEXT("added_user_parameters"),Added); Receipt->SetArrayField(TEXT("reused_user_parameters"),Reused);
	Receipt->SetObjectField(TEXT("dependency_map"),DependencyMap);
	Receipt->SetStringField(TEXT("parameter_scope"),TEXT("all source exposed parameters; same-name differences reject before mutation"));
	Receipt->SetBoolField(TEXT("compile_requested"),bRequestCompile);
	if (bRequestCompile) Destination->RequestCompile(false);
	return Encode(Receipt);
}
