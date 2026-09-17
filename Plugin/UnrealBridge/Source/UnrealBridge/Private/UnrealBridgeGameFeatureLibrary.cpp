#include "UnrealBridgeGameFeatureLibrary.h"

#include "GameFeatureAction.h"
#include "GameFeatureData.h"
#include "GameFeatureTypes.h"
#include "GameFeaturesSubsystem.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/DataValidation.h"

namespace UnrealBridgeGameFeature
{
	FString ValidationResultToString(EDataValidationResult Result)
	{
		switch (Result)
		{
		case EDataValidationResult::Valid: return TEXT("Valid");
		case EDataValidationResult::Invalid: return TEXT("Invalid");
		case EDataValidationResult::NotValidated: return TEXT("NotValidated");
		default: return TEXT("Unknown");
		}
	}

	FString SeverityToString(EMessageSeverity::Type Severity)
	{
		switch (Severity)
		{
		case EMessageSeverity::Error: return TEXT("Error");
		case EMessageSeverity::PerformanceWarning: return TEXT("PerformanceWarning");
		case EMessageSeverity::Warning: return TEXT("Warning");
		case EMessageSeverity::Info: return TEXT("Info");
		default: return TEXT("Other");
		}
	}

	FBridgeGameFeatureInfo BuildInfo(
		UGameFeaturesSubsystem& Subsystem,
		const TSharedRef<IPlugin>& Plugin,
		bool bRunDataValidation)
	{
		FBridgeGameFeatureInfo Info;
		Info.PluginName = Plugin->GetName();
		Info.DescriptorFile = Plugin->GetDescriptorFileName();
		Info.bEnabled = Plugin->IsEnabled();

		if (!Subsystem.GetPluginURLByName(Info.PluginName, Info.PluginUrl))
		{
			Info.State = TEXT("Discovered");
			Info.ValidationResult = TEXT("NotRegistered");
			return Info;
		}

		const EGameFeaturePluginState State = Subsystem.GetPluginState(Info.PluginUrl);
		Info.State = UE::GameFeatures::ToString(State);
		Info.bActive = State == EGameFeaturePluginState::Active;
		Info.bInErrorState = Subsystem.IsGameFeaturePluginInErrorState(Info.PluginUrl);

		FGameFeaturePluginDetails Details;
		Info.bDescriptorDetailsAvailable = Subsystem.GetGameFeaturePluginDetails(Info.PluginUrl, Details);
		if (Info.bDescriptorDetailsAvailable)
		{
			Info.Dependencies.Reserve(Details.PluginDependencies.Num());
			for (const FGameFeaturePluginReferenceDetails& Dependency : Details.PluginDependencies)
			{
				FBridgeGameFeatureDependencyInfo DependencyInfo;
				DependencyInfo.PluginName = FString(Dependency.PluginName);
				DependencyInfo.bShouldActivate = Dependency.bShouldActivate;
				DependencyInfo.bResolved = Subsystem.GetPluginURLByName(Dependency.PluginName, DependencyInfo.PluginUrl);
				if (DependencyInfo.bResolved)
				{
					DependencyInfo.State = UE::GameFeatures::ToString(Subsystem.GetPluginState(DependencyInfo.PluginUrl));
				}
				Info.Dependencies.Add(MoveTemp(DependencyInfo));
			}
		}

		const UGameFeatureData* Data = Subsystem.GetGameFeatureDataForRegisteredPluginByURL(Info.PluginUrl, true);
		if (!Data)
		{
			Info.ValidationResult = TEXT("GameFeatureDataUnavailable");
			return Info;
		}

		Info.GameFeatureDataPath = Data->GetPathName();
		const TArray<UGameFeatureAction*>& Actions = Data->GetActions();
		Info.Actions.Reserve(Actions.Num());
		for (int32 Index = 0; Index < Actions.Num(); ++Index)
		{
			FBridgeGameFeatureActionInfo ActionInfo;
			ActionInfo.Index = Index;
			if (const UGameFeatureAction* Action = Actions[Index])
			{
				ActionInfo.ObjectPath = Action->GetPathName();
				ActionInfo.ClassPath = Action->GetClass()->GetPathName();
			}
			Info.Actions.Add(MoveTemp(ActionInfo));
		}

		if (!bRunDataValidation)
		{
			Info.ValidationResult = TEXT("Skipped");
			return Info;
		}

		FDataValidationContext Context;
		Info.ValidationResult = ValidationResultToString(Data->IsDataValid(Context));
		for (const FDataValidationContext::FIssue& Issue : Context.GetIssues())
		{
			FBridgeGameFeatureValidationIssue ValidationIssue;
			ValidationIssue.Severity = SeverityToString(Issue.Severity);
			ValidationIssue.Message = Issue.TokenizedMessage.IsValid()
				? Issue.TokenizedMessage->ToText().ToString()
				: Issue.Message.ToString();
			Info.ValidationIssues.Add(MoveTemp(ValidationIssue));
		}
		return Info;
	}
}

TArray<FBridgeGameFeatureInfo> UUnrealBridgeGameFeatureLibrary::ListGameFeatures(
	bool bIncludeDisabled,
	bool bRunDataValidation,
	int32 MaxPlugins)
{
	TArray<FBridgeGameFeatureInfo> Result;
	MaxPlugins = FMath::Clamp(MaxPlugins, 1, 4096);
	UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();
	for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetDiscoveredPlugins())
	{
		if (Result.Num() >= MaxPlugins)
		{
			break;
		}
		if ((!bIncludeDisabled && !Plugin->IsEnabled()) || !UGameFeaturesSubsystem::IsGameFeaturePlugin(Plugin))
		{
			continue;
		}
		Result.Add(UnrealBridgeGameFeature::BuildInfo(Subsystem, Plugin, bRunDataValidation));
	}
	Result.Sort([](const FBridgeGameFeatureInfo& Left, const FBridgeGameFeatureInfo& Right)
	{
		return Left.PluginName < Right.PluginName;
	});
	return Result;
}

FBridgeGameFeatureInfo UUnrealBridgeGameFeatureLibrary::GetGameFeatureInfo(
	const FString& PluginNameOrUrl,
	bool bRunDataValidation)
{
	UGameFeaturesSubsystem& Subsystem = UGameFeaturesSubsystem::Get();
	FString PluginName = PluginNameOrUrl;
	if (PluginNameOrUrl.Contains(TEXT(":")))
	{
		PluginName.Reset();
		for (const TSharedRef<IPlugin>& Candidate : IPluginManager::Get().GetDiscoveredPlugins())
		{
			FString CandidateUrl;
			if (Subsystem.GetPluginURLByName(Candidate->GetName(), CandidateUrl)
				&& CandidateUrl == PluginNameOrUrl)
			{
				PluginName = Candidate->GetName();
				break;
			}
		}
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
	return Plugin.IsValid() && UGameFeaturesSubsystem::IsGameFeaturePlugin(Plugin)
		? UnrealBridgeGameFeature::BuildInfo(Subsystem, Plugin.ToSharedRef(), bRunDataValidation)
		: FBridgeGameFeatureInfo();
}
