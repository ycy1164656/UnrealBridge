#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace BridgeAudioRouting
{
	/** Dynamic optional-module lookup never enables a plugin or calls profile/config functions. */
	UClass* OptionalClass(const TCHAR* Name);
	TSharedRef<FJsonObject> Model(UObject* Object);
	bool Gain(const TSharedPtr<FJsonObject>& Input,float& Out);
	bool CallBusMix(const TCHAR* Function,const UObject* World,UObject* Mix,bool* Result=nullptr);
}
namespace BridgeAudioSession
{
	void Shutdown();
}
