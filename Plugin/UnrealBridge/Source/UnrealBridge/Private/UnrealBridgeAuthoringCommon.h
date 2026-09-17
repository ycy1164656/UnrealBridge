#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Shared native preflight/receipt/guarded-save layer for incremental authoring. */
namespace BridgeAuthoring
{
	struct FRequest
	{
		TSharedPtr<FJsonObject> Json,Context,Snapshot;
		TArray<TSharedPtr<FJsonValue>> Operations;
		TArray<FString> Targets;
		FString Target,Operation,RequestId,ReceiptKey,Digest,ChangeSet;
		bool DryRun=true,Save=false;
	};
	FString Encode(const TSharedRef<FJsonObject>& Object);
	TSharedPtr<FJsonObject> Decode(const FString& Text);
	FString String(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key);
	bool Number(const TSharedPtr<FJsonObject>& Object,const TCHAR* Key,double& Out);
	FString Hash(const FString& Text);
	FString Error(const TCHAR* Code,const FString& Message,const TCHAR* SideEffect=TEXT("none"));
	bool Fields(const TSharedPtr<FJsonObject>& Object,std::initializer_list<const TCHAR*> Names);
	/** Returns false with a terminal rejection or an idempotent cached reply. */
	bool Parse(const FString& Text,const TCHAR* Operation,FRequest& Out,FString& Reply);
	bool Begin(FRequest& Request,FString& Reply);
	FString Finish(FRequest& Request,const TSharedRef<FJsonObject>& Model);
	FString Preview(FRequest& Request,const TSharedRef<FJsonObject>& Model);
}
