#include "UnrealBridgeSequencerLibrary.h"

#include "Animation/AnimSequenceBase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "EngineUtils.h"
#include "LevelSequence.h"
#include "Misc/PackageName.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "ScopedTransaction.h"
#include "Channels/MovieSceneStringChannel.h"
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneBoolTrack.h"
#include "Tracks/MovieSceneByteTrack.h"
#include "Tracks/MovieSceneColorTrack.h"
#include "Tracks/MovieSceneDoubleTrack.h"
#include "Tracks/MovieSceneFloatTrack.h"
#include "Tracks/MovieSceneIntegerTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieSceneStringTrack.h"
#include "Tracks/MovieSceneVectorTrack.h"
#include "UObject/Package.h"
#include "Variants/MovieSceneTimeWarpVariant.h"

namespace BridgeSequencerImpl
{
	FString PackageNameFor(const FString& Path, const FString& Name)
	{
		const FString EffectivePath = Path.IsEmpty() ? TEXT("/Game") : Path;
		return EffectivePath / Name;
	}

	FString ObjectPathForPackage(const FString& PackageName)
	{
		return FString::Printf(TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName));
	}

	ULevelSequence* LoadSequence(const FString& SequencePath)
	{
		if (SequencePath.IsEmpty())
		{
			return nullptr;
		}
		if (ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *SequencePath))
		{
			return Sequence;
		}
		if (FPackageName::IsValidLongPackageName(SequencePath))
		{
			return LoadObject<ULevelSequence>(nullptr, *ObjectPathForPackage(SequencePath));
		}
		return nullptr;
	}

	void SaveSequenceIfRequested(ULevelSequence* Sequence, bool bSave)
	{
		if (Sequence && bSave)
		{
			UEditorAssetLibrary::SaveAsset(Sequence->GetPathName(), false);
		}
	}

	FFrameNumber EndFrameAfter(FFrameNumber StartFrame, FFrameNumber CandidateEnd)
	{
		return CandidateEnd <= StartFrame ? StartFrame + 1 : CandidateEnd;
	}

	ERichCurveInterpMode ParseInterpolation(const FString& Interpolation)
	{
		const FString Normalized = Interpolation.ToLower();
		if (Normalized == TEXT("constant"))
		{
			return RCIM_Constant;
		}
		if (Normalized == TEXT("linear"))
		{
			return RCIM_Linear;
		}
		return RCIM_Cubic;
	}

	AActor* FindActorByLabel(const FString& ActorLabel)
	{
		if (!GEditor)
		{
			return nullptr;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return nullptr;
		}
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor && Actor->GetActorLabel() == ActorLabel)
			{
				return Actor;
			}
		}
		return nullptr;
	}

	FGuid FindOrAddActorBinding(ULevelSequence* Sequence, UMovieScene* MovieScene, AActor* Actor, bool bCreateIfMissing)
	{
		if (!Sequence || !MovieScene || !Actor)
		{
			return FGuid();
		}
		for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
		{
			if (Binding.GetName() == Actor->GetActorLabel())
			{
				return Binding.GetObjectGuid();
			}
		}
		if (!bCreateIfMissing)
		{
			return FGuid();
		}
		Sequence->Modify();
		MovieScene->Modify();
		const FGuid BindingId = MovieScene->AddPossessable(Actor->GetActorLabel(), Actor->GetClass());
		Sequence->BindPossessableObject(BindingId, *Actor, Actor->GetWorld());
		return BindingId;
	}

	UMovieScene3DTransformTrack* FindOrAddTransformTrack(UMovieScene* MovieScene, const FGuid& BindingId, bool bCreateIfMissing)
	{
		if (!MovieScene || !BindingId.IsValid())
		{
			return nullptr;
		}
		if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingId))
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track))
				{
					return TransformTrack;
				}
			}
		}
		return bCreateIfMissing ? MovieScene->AddTrack<UMovieScene3DTransformTrack>(BindingId) : nullptr;
	}

	UMovieScene3DTransformSection* FindOrAddTransformSection(UMovieScene3DTransformTrack* Track, FFrameNumber Frame)
	{
		if (!Track)
		{
			return nullptr;
		}
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Section);
			if (TransformSection && TransformSection->GetRange().Contains(Frame))
			{
				return TransformSection;
			}
		}
		UMovieScene3DTransformSection* NewSection = Cast<UMovieScene3DTransformSection>(Track->CreateNewSection());
		if (NewSection)
		{
			const FFrameNumber Start = Frame - FFrameNumber(1);
			const FFrameNumber End = Frame + FFrameNumber(1);
			NewSection->SetRange(TRange<FFrameNumber>(Start, End));
			Track->AddSection(*NewSection);
		}
		return NewSection;
	}

	void AddDoubleKey(FMovieSceneDoubleChannel* Channel, FFrameNumber Frame, double Value, const FString& Interpolation)
	{
		if (!Channel)
		{
			return;
		}
		FMovieSceneDoubleValue KeyValue(Value);
		KeyValue.InterpMode = ParseInterpolation(Interpolation);
		KeyValue.TangentMode = RCTM_Auto;
		Channel->GetData().UpdateOrAddKey(Frame, KeyValue);
	}

	void AddFloatKey(FMovieSceneFloatChannel* Channel, FFrameNumber Frame, float Value, const FString& Interpolation)
	{
		if (!Channel)
		{
			return;
		}
		FMovieSceneFloatValue KeyValue(Value);
		KeyValue.InterpMode = ParseInterpolation(Interpolation);
		KeyValue.TangentMode = RCTM_Auto;
		Channel->GetData().UpdateOrAddKey(Frame, KeyValue);
	}

	UClass* PropertyTrackClass(EBridgeSequencerPropertyType PropertyType)
	{
		switch (PropertyType)
		{
		case EBridgeSequencerPropertyType::Bool: return UMovieSceneBoolTrack::StaticClass();
		case EBridgeSequencerPropertyType::Byte: return UMovieSceneByteTrack::StaticClass();
		case EBridgeSequencerPropertyType::Integer: return UMovieSceneIntegerTrack::StaticClass();
		case EBridgeSequencerPropertyType::Float: return UMovieSceneFloatTrack::StaticClass();
		case EBridgeSequencerPropertyType::Double: return UMovieSceneDoubleTrack::StaticClass();
		case EBridgeSequencerPropertyType::String: return UMovieSceneStringTrack::StaticClass();
		case EBridgeSequencerPropertyType::Vector2:
		case EBridgeSequencerPropertyType::Vector3:
		case EBridgeSequencerPropertyType::Vector4:
			return UMovieSceneDoubleVectorTrack::StaticClass();
		case EBridgeSequencerPropertyType::Color: return UMovieSceneColorTrack::StaticClass();
		default: return nullptr;
		}
	}

	UMovieScenePropertyTrack* FindOrAddPropertyTrack(
		UMovieScene* MovieScene,
		const FGuid& BindingId,
		const FString& PropertyName,
		const FString& PropertyPath,
		EBridgeSequencerPropertyType PropertyType,
		bool bCreateIfMissing)
	{
		if (!MovieScene || !BindingId.IsValid() || PropertyName.IsEmpty())
		{
			return nullptr;
		}
		const FString EffectivePath = PropertyPath.IsEmpty() ? PropertyName : PropertyPath;
		UClass* DesiredClass = PropertyTrackClass(PropertyType);
		if (!DesiredClass)
		{
			return nullptr;
		}
		if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingId))
		{
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(Track);
				if (!PropertyTrack || PropertyTrack->GetPropertyName() != FName(*PropertyName))
				{
					continue;
				}
				if (PropertyTrack->GetClass() == DesiredClass && PropertyTrack->GetPropertyPath() == FName(*EffectivePath))
				{
					return PropertyTrack;
				}
				return nullptr;
			}
		}
		if (!bCreateIfMissing)
		{
			return nullptr;
		}
		MovieScene->Modify();
		UMovieScenePropertyTrack* Track = Cast<UMovieScenePropertyTrack>(MovieScene->AddTrack(DesiredClass, BindingId));
		if (!Track)
		{
			return nullptr;
		}
		Track->SetPropertyNameAndPath(FName(*PropertyName), EffectivePath);
		if (UMovieSceneDoubleVectorTrack* VectorTrack = Cast<UMovieSceneDoubleVectorTrack>(Track))
		{
			const int32 NumChannels = PropertyType == EBridgeSequencerPropertyType::Vector2 ? 2
				: PropertyType == EBridgeSequencerPropertyType::Vector3 ? 3 : 4;
			VectorTrack->SetNumChannelsUsed(NumChannels);
		}
		return Track;
	}

	UMovieSceneSection* FindOrAddPropertySection(
		UMovieScenePropertyTrack* Track,
		FFrameNumber StartFrame,
		FFrameNumber EndFrame,
		bool bCreateIfMissing)
	{
		if (!Track)
		{
			return nullptr;
		}
		EndFrame = EndFrameAfter(StartFrame, EndFrame);
		const TRange<FFrameNumber> DesiredRange(StartFrame, EndFrame);
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (!Section)
			{
				continue;
			}
			const TRange<FFrameNumber> ExistingRange = Section->GetRange();
			if (ExistingRange.Contains(StartFrame) || ExistingRange.Overlaps(DesiredRange))
			{
				Section->Modify();
				const FFrameNumber NewStart = ExistingRange.HasLowerBound()
					? FMath::Min(ExistingRange.GetLowerBoundValue(), StartFrame) : StartFrame;
				const FFrameNumber NewEnd = ExistingRange.HasUpperBound()
					? FMath::Max(ExistingRange.GetUpperBoundValue(), EndFrame) : EndFrame;
				Section->SetRange(TRange<FFrameNumber>(NewStart, NewEnd));
				return Section;
			}
		}
		if (!bCreateIfMissing)
		{
			return nullptr;
		}
		Track->Modify();
		UMovieSceneSection* Section = Track->CreateNewSection();
		if (!Section)
		{
			return nullptr;
		}
		Section->SetRange(DesiredRange);
		Track->AddSection(*Section);
		return Section;
	}

	void AppendTrackInfo(
		TArray<FBridgeSequencerTrackInfo>& Result,
		UMovieSceneTrack* Track,
		const FString& BindingName,
		const FString& BindingId)
	{
		if (!Track)
		{
			return;
		}
		FBridgeSequencerTrackInfo Info;
		Info.BindingName = BindingName;
		Info.BindingId = BindingId;
		Info.TrackName = Track->GetName();
		Info.TrackClass = Track->GetClass()->GetName();
		Result.Add(Info);
	}

	void AppendSectionInfo(
		TArray<FBridgeSequencerSectionInfo>& Result,
		UMovieSceneTrack* Track,
		const FString& BindingName,
		const FString& BindingId,
		const FFrameRate& TickResolution)
	{
		if (!Track)
		{
			return;
		}
		const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();
		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			UMovieSceneSection* Section = Sections[SectionIndex];
			if (!Section)
			{
				continue;
			}
			FBridgeSequencerSectionInfo Info;
			Info.BindingName = BindingName;
			Info.BindingId = BindingId;
			Info.TrackName = Track->GetName();
			Info.TrackClass = Track->GetClass()->GetName();
			Info.SectionClass = Section->GetClass()->GetName();
			Info.SectionIndex = SectionIndex;
			const TRange<FFrameNumber> Range = Section->GetRange();
			Info.bHasStart = Range.HasLowerBound();
			Info.bHasEnd = Range.HasUpperBound();
			if (Info.bHasStart)
			{
				Info.StartSeconds = static_cast<float>(TickResolution.AsSeconds(Range.GetLowerBoundValue()));
			}
			if (Info.bHasEnd)
			{
				Info.EndSeconds = static_cast<float>(TickResolution.AsSeconds(Range.GetUpperBoundValue()));
			}
			const FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
			for (const FMovieSceneChannelEntry& Entry : Proxy.GetAllEntries())
			{
				for (FMovieSceneChannel* Channel : Entry.GetChannels())
				{
					if (Channel)
					{
						++Info.ChannelCount;
						Info.KeyCount += Channel->GetNumKeys();
					}
				}
			}
			Result.Add(Info);
		}
	}
}

FString UUnrealBridgeSequencerLibrary::CreateLevelSequence(const FString& Path, const FString& Name, bool bSave)
{
	if (Name.IsEmpty())
	{
		return FString();
	}
	const FString PackageName = BridgeSequencerImpl::PackageNameFor(Path, Name);
	if (!FPackageName::IsValidLongPackageName(PackageName))
	{
		return FString();
	}
	if (ULevelSequence* Existing = BridgeSequencerImpl::LoadSequence(PackageName))
	{
		BridgeSequencerImpl::SaveSequenceIfRequested(Existing, bSave);
		return Existing->GetOutermost()->GetName();
	}
	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		return FString();
	}
	Package->FullyLoad();
	ULevelSequence* Sequence = NewObject<ULevelSequence>(Package, FName(*Name), RF_Public | RF_Standalone | RF_Transactional);
	if (!Sequence)
	{
		return FString();
	}
	Sequence->Initialize();
	FAssetRegistryModule::AssetCreated(Sequence);
	Sequence->MarkPackageDirty();
	BridgeSequencerImpl::SaveSequenceIfRequested(Sequence, bSave);
	return PackageName;
}

FBridgeSequencerBindingInfo UUnrealBridgeSequencerLibrary::AddActorBinding(
	const FString& SequencePath,
	const FString& ActorLabel)
{
	FBridgeSequencerBindingInfo Info;
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	AActor* Actor = BridgeSequencerImpl::FindActorByLabel(ActorLabel);
	if (!Sequence || !Actor)
	{
		return Info;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return Info;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge", "AddActorBinding", "UnrealBridge Add Actor Binding"));
	const FGuid BindingId = BridgeSequencerImpl::FindOrAddActorBinding(Sequence, MovieScene, Actor, true);
	if (!BindingId.IsValid())
	{
		return Info;
	}
	if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingId))
	{
		Info.BindingName = Binding->GetName();
	}
	else
	{
		Info.BindingName = Actor->GetActorLabel();
	}
	Info.BindingId = BindingId.ToString();
	Sequence->MarkPackageDirty();
	return Info;
}

FString UUnrealBridgeSequencerLibrary::AddTransformTrack(
	const FString& SequencePath,
	const FString& ActorLabel,
	float StartSeconds,
	float EndSeconds)
{
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	AActor* Actor = BridgeSequencerImpl::FindActorByLabel(ActorLabel);
	if (!Sequence || !Actor)
	{
		return FString();
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FString();
	}
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge", "AddTransformTrack", "UnrealBridge Add Transform Track"));
	Sequence->Modify();
	MovieScene->Modify();
	const FGuid BindingId = BridgeSequencerImpl::FindOrAddActorBinding(Sequence, MovieScene, Actor, true);
	UMovieScene3DTransformTrack* Track = BridgeSequencerImpl::FindOrAddTransformTrack(MovieScene, BindingId, true);
	if (!Track)
	{
		return FString();
	}
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameNumber StartFrame = TickResolution.AsFrameNumber(StartSeconds);
	const FFrameNumber EndFrame = BridgeSequencerImpl::EndFrameAfter(
		StartFrame, TickResolution.AsFrameNumber(FMath::Max(StartSeconds, EndSeconds)));
	UMovieSceneSection* Section = nullptr;
	for (UMovieSceneSection* ExistingSection : Track->GetAllSections())
	{
		if (ExistingSection && ExistingSection->GetRange().Overlaps(TRange<FFrameNumber>(StartFrame, EndFrame)))
		{
			Section = ExistingSection;
			break;
		}
	}
	if (!Section)
	{
		Track->Modify();
		Section = Track->CreateNewSection();
	}
	if (Section)
	{
		Section->Modify();
		Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
		if (!Track->HasSection(*Section))
		{
			Track->AddSection(*Section);
		}
	}
	Sequence->MarkPackageDirty();
	return Track->GetName();
}

TArray<FBridgeSequencerTrackInfo> UUnrealBridgeSequencerLibrary::ListSequenceTracks(const FString& SequencePath)
{
	TArray<FBridgeSequencerTrackInfo> Result;
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	if (!Sequence)
	{
		return Result;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return Result;
	}
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			if (!Track)
			{
				continue;
			}
			BridgeSequencerImpl::AppendTrackInfo(
				Result, Track, Binding.GetName(), Binding.GetObjectGuid().ToString());
		}
	}
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		BridgeSequencerImpl::AppendTrackInfo(Result, Track, TEXT("<Master>"), FString());
	}
	return Result;
}

bool UUnrealBridgeSequencerLibrary::SetPlaybackRange(
	const FString& SequencePath,
	float StartSeconds,
	float EndSeconds)
{
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	if (!Sequence)
	{
		return false;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameNumber StartFrame = TickResolution.AsFrameNumber(StartSeconds);
	const FFrameNumber EndFrame = BridgeSequencerImpl::EndFrameAfter(
		StartFrame, TickResolution.AsFrameNumber(FMath::Max(StartSeconds, EndSeconds)));
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge", "SetPlaybackRange", "UnrealBridge Set Playback Range"));
	Sequence->Modify();
	MovieScene->Modify();
	MovieScene->SetPlaybackRange(StartFrame, FMath::Max(1, (EndFrame - StartFrame).Value));
	Sequence->MarkPackageDirty();
	return true;
}

bool UUnrealBridgeSequencerLibrary::AddTransformKey(
	const FString& SequencePath,
	const FString& ActorLabel,
	float Seconds,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale,
	bool bCreateTrackIfMissing,
	const FString& Interpolation)
{
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	AActor* Actor = BridgeSequencerImpl::FindActorByLabel(ActorLabel);
	if (!Sequence || !Actor)
	{
		return false;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return false;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge", "AddTransformKey", "UnrealBridge Add Transform Key"));
	Sequence->Modify();
	MovieScene->Modify();
	const FGuid BindingId = BridgeSequencerImpl::FindOrAddActorBinding(Sequence, MovieScene, Actor, bCreateTrackIfMissing);
	UMovieScene3DTransformTrack* Track = BridgeSequencerImpl::FindOrAddTransformTrack(MovieScene, BindingId, bCreateTrackIfMissing);
	const FFrameNumber Frame = MovieScene->GetTickResolution().AsFrameNumber(Seconds);
	UMovieScene3DTransformSection* Section = BridgeSequencerImpl::FindOrAddTransformSection(Track, Frame);
	if (!Section)
	{
		return false;
	}
	Section->Modify();
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Location.X").Get(), Frame, Location.X, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Location.Y").Get(), Frame, Location.Y, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Location.Z").Get(), Frame, Location.Z, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Rotation.X").Get(), Frame, Rotation.Roll, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Rotation.Y").Get(), Frame, Rotation.Pitch, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Rotation.Z").Get(), Frame, Rotation.Yaw, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Scale.X").Get(), Frame, Scale.X, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Scale.Y").Get(), Frame, Scale.Y, Interpolation);
	BridgeSequencerImpl::AddDoubleKey(Proxy.GetChannelByName<FMovieSceneDoubleChannel>("Scale.Z").Get(), Frame, Scale.Z, Interpolation);
	Sequence->MarkPackageDirty();
	return true;
}

TArray<FBridgeSequencerSectionInfo> UUnrealBridgeSequencerLibrary::ListSequenceSections(const FString& SequencePath)
{
	TArray<FBridgeSequencerSectionInfo> Result;
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!MovieScene)
	{
		return Result;
	}
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			BridgeSequencerImpl::AppendSectionInfo(
				Result, Track, Binding.GetName(), Binding.GetObjectGuid().ToString(), TickResolution);
		}
	}
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		BridgeSequencerImpl::AppendSectionInfo(Result, Track, TEXT("<Master>"), FString(), TickResolution);
	}
	return Result;
}

FBridgeSequencerValidationResult UUnrealBridgeSequencerLibrary::ValidateLevelSequence(const FString& SequencePath)
{
	FBridgeSequencerValidationResult Result;
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	if (!Sequence)
	{
		++Result.ErrorCount;
		Result.Messages.Add(FString::Printf(TEXT("ERROR: LevelSequence not found: %s"), *SequencePath));
		return Result;
	}
	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		++Result.ErrorCount;
		Result.Messages.Add(TEXT("ERROR: LevelSequence has no MovieScene."));
		return Result;
	}

	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	if (!PlaybackRange.HasLowerBound() || !PlaybackRange.HasUpperBound()
		|| PlaybackRange.GetLowerBoundValue() >= PlaybackRange.GetUpperBoundValue())
	{
		++Result.ErrorCount;
		Result.Messages.Add(TEXT("ERROR: Playback range is missing or empty."));
	}

	TSet<FString> BindingNames;
	int32 TrackCount = 0;
	auto ValidateTrack = [&Result, &TrackCount](UMovieSceneTrack* Track, const FString& OwnerName)
	{
		if (!Track)
		{
			++Result.ErrorCount;
			Result.Messages.Add(FString::Printf(TEXT("ERROR: %s contains a null track."), *OwnerName));
			return;
		}
		++TrackCount;
		if (Track->GetAllSections().IsEmpty())
		{
			++Result.WarningCount;
			Result.Messages.Add(FString::Printf(
				TEXT("WARNING: Track %s on %s has no sections."), *Track->GetName(), *OwnerName));
		}
		for (UMovieSceneSection* Section : Track->GetAllSections())
		{
			if (!Section)
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(TEXT("ERROR: Track %s contains a null section."), *Track->GetName()));
				continue;
			}
			const TRange<FFrameNumber> Range = Section->GetRange();
			if (Range.HasLowerBound() && Range.HasUpperBound()
				&& Range.GetLowerBoundValue() >= Range.GetUpperBoundValue())
			{
				++Result.ErrorCount;
				Result.Messages.Add(FString::Printf(
					TEXT("ERROR: Section %s on track %s has an empty range."), *Section->GetName(), *Track->GetName()));
			}
		}
	};

	for (const FMovieSceneBinding& Binding : MovieScene->GetBindings())
	{
		if (BindingNames.Contains(Binding.GetName()))
		{
			++Result.WarningCount;
			Result.Messages.Add(FString::Printf(TEXT("WARNING: Duplicate binding name: %s"), *Binding.GetName()));
		}
		BindingNames.Add(Binding.GetName());
		for (UMovieSceneTrack* Track : Binding.GetTracks())
		{
			ValidateTrack(Track, Binding.GetName());
		}
	}
	for (UMovieSceneTrack* Track : MovieScene->GetTracks())
	{
		ValidateTrack(Track, TEXT("<Master>"));
	}
	if (TrackCount == 0)
	{
		++Result.WarningCount;
		Result.Messages.Add(TEXT("WARNING: LevelSequence contains no tracks."));
	}
	Result.bSuccess = Result.ErrorCount == 0;
	if (Result.bSuccess && Result.WarningCount == 0)
	{
		Result.Messages.Add(TEXT("OK: LevelSequence structure is valid."));
	}
	return Result;
}

FString UUnrealBridgeSequencerLibrary::AddPropertyTrack(
	const FString& SequencePath,
	const FString& ActorLabel,
	const FString& PropertyName,
	const FString& PropertyPath,
	EBridgeSequencerPropertyType PropertyType,
	float StartSeconds,
	float EndSeconds,
	bool bSave)
{
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	AActor* Actor = BridgeSequencerImpl::FindActorByLabel(ActorLabel);
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Sequence || !Actor || !MovieScene || PropertyName.IsEmpty())
	{
		return FString();
	}
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameNumber StartFrame = TickResolution.AsFrameNumber(StartSeconds);
	const FFrameNumber EndFrame = BridgeSequencerImpl::EndFrameAfter(
		StartFrame, TickResolution.AsFrameNumber(FMath::Max(StartSeconds, EndSeconds)));

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge", "AddPropertyTrack", "UnrealBridge Add Property Track"));
	Sequence->Modify();
	MovieScene->Modify();
	const FGuid BindingId = BridgeSequencerImpl::FindOrAddActorBinding(Sequence, MovieScene, Actor, true);
	UMovieScenePropertyTrack* Track = BridgeSequencerImpl::FindOrAddPropertyTrack(
		MovieScene, BindingId, PropertyName, PropertyPath, PropertyType, true);
	UMovieSceneSection* Section = BridgeSequencerImpl::FindOrAddPropertySection(Track, StartFrame, EndFrame, true);
	if (!Track || !Section)
	{
		return FString();
	}
	Sequence->MarkPackageDirty();
	BridgeSequencerImpl::SaveSequenceIfRequested(Sequence, bSave);
	return Track->GetName();
}

bool UUnrealBridgeSequencerLibrary::AddPropertyKey(
	const FString& SequencePath,
	const FString& ActorLabel,
	const FString& PropertyName,
	const FString& PropertyPath,
	EBridgeSequencerPropertyType PropertyType,
	float Seconds,
	const FString& ValueExportText,
	bool bCreateTrackIfMissing,
	const FString& Interpolation,
	bool bSave)
{
	bool BoolValue = false;
	uint8 ByteValue = 0;
	int32 IntegerValue = 0;
	float FloatValue = 0.0f;
	double DoubleValue = 0.0;
	FString StringValue = ValueExportText;
	FVector4 VectorValue(0.0, 0.0, 0.0, 0.0);
	FLinearColor ColorValue = FLinearColor::Black;
	const FString TrimmedValue = ValueExportText.TrimStartAndEnd();

	switch (PropertyType)
	{
	case EBridgeSequencerPropertyType::Bool:
		if (TrimmedValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || TrimmedValue == TEXT("1"))
		{
			BoolValue = true;
		}
		else if (TrimmedValue.Equals(TEXT("false"), ESearchCase::IgnoreCase) || TrimmedValue == TEXT("0"))
		{
			BoolValue = false;
		}
		else
		{
			return false;
		}
		break;
	case EBridgeSequencerPropertyType::Byte:
	{
		int32 Parsed = 0;
		if (!LexTryParseString(Parsed, *TrimmedValue) || Parsed < 0 || Parsed > 255)
		{
			return false;
		}
		ByteValue = static_cast<uint8>(Parsed);
		break;
	}
	case EBridgeSequencerPropertyType::Integer:
		if (!LexTryParseString(IntegerValue, *TrimmedValue)) return false;
		break;
	case EBridgeSequencerPropertyType::Float:
		if (!LexTryParseString(FloatValue, *TrimmedValue)) return false;
		break;
	case EBridgeSequencerPropertyType::Double:
		if (!LexTryParseString(DoubleValue, *TrimmedValue)) return false;
		break;
	case EBridgeSequencerPropertyType::String:
		if (StringValue.Len() >= 2 && StringValue.StartsWith(TEXT("\"")) && StringValue.EndsWith(TEXT("\"")))
		{
			StringValue = StringValue.Mid(1, StringValue.Len() - 2);
		}
		break;
	case EBridgeSequencerPropertyType::Vector2:
	{
		FVector2D Parsed;
		if (!Parsed.InitFromString(TrimmedValue)) return false;
		VectorValue = FVector4(Parsed.X, Parsed.Y, 0.0, 0.0);
		break;
	}
	case EBridgeSequencerPropertyType::Vector3:
	{
		FVector Parsed;
		if (!Parsed.InitFromString(TrimmedValue)) return false;
		VectorValue = FVector4(Parsed.X, Parsed.Y, Parsed.Z, 0.0);
		break;
	}
	case EBridgeSequencerPropertyType::Vector4:
		if (!VectorValue.InitFromString(TrimmedValue)) return false;
		break;
	case EBridgeSequencerPropertyType::Color:
		if (!ColorValue.InitFromString(TrimmedValue)) return false;
		break;
	default:
		return false;
	}

	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	AActor* Actor = BridgeSequencerImpl::FindActorByLabel(ActorLabel);
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Sequence || !Actor || !MovieScene || PropertyName.IsEmpty())
	{
		return false;
	}

	const FScopedTransaction Transaction(NSLOCTEXT("UnrealBridge", "AddPropertyKey", "UnrealBridge Add Property Key"));
	Sequence->Modify();
	MovieScene->Modify();
	const FGuid BindingId = BridgeSequencerImpl::FindOrAddActorBinding(
		Sequence, MovieScene, Actor, bCreateTrackIfMissing);
	UMovieScenePropertyTrack* Track = BridgeSequencerImpl::FindOrAddPropertyTrack(
		MovieScene, BindingId, PropertyName, PropertyPath, PropertyType, bCreateTrackIfMissing);
	const FFrameNumber Frame = MovieScene->GetTickResolution().AsFrameNumber(Seconds);
	UMovieSceneSection* Section = BridgeSequencerImpl::FindOrAddPropertySection(
		Track, Frame, Frame + 1, bCreateTrackIfMissing);
	if (!Track || !Section)
	{
		return false;
	}
	Track->Modify();
	Section->Modify();
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	bool bKeyAdded = true;
	switch (PropertyType)
	{
	case EBridgeSequencerPropertyType::Bool:
		if (FMovieSceneBoolChannel* Channel = Proxy.GetChannel<FMovieSceneBoolChannel>(0))
		{
			Channel->GetData().UpdateOrAddKey(Frame, BoolValue);
		}
		else bKeyAdded = false;
		break;
	case EBridgeSequencerPropertyType::Byte:
		if (FMovieSceneByteChannel* Channel = Proxy.GetChannel<FMovieSceneByteChannel>(0))
		{
			Channel->GetData().UpdateOrAddKey(Frame, ByteValue);
		}
		else bKeyAdded = false;
		break;
	case EBridgeSequencerPropertyType::Integer:
		if (FMovieSceneIntegerChannel* Channel = Proxy.GetChannel<FMovieSceneIntegerChannel>(0))
		{
			Channel->GetData().UpdateOrAddKey(Frame, IntegerValue);
		}
		else bKeyAdded = false;
		break;
	case EBridgeSequencerPropertyType::Float:
		if (FMovieSceneFloatChannel* Channel = Proxy.GetChannel<FMovieSceneFloatChannel>(0))
		{
			BridgeSequencerImpl::AddFloatKey(Channel, Frame, FloatValue, Interpolation);
		}
		else bKeyAdded = false;
		break;
	case EBridgeSequencerPropertyType::Double:
		if (FMovieSceneDoubleChannel* Channel = Proxy.GetChannel<FMovieSceneDoubleChannel>(0))
		{
			BridgeSequencerImpl::AddDoubleKey(Channel, Frame, DoubleValue, Interpolation);
		}
		else bKeyAdded = false;
		break;
	case EBridgeSequencerPropertyType::String:
		if (FMovieSceneStringChannel* Channel = Proxy.GetChannel<FMovieSceneStringChannel>(0))
		{
			Channel->GetData().UpdateOrAddKey(Frame, StringValue);
		}
		else bKeyAdded = false;
		break;
	case EBridgeSequencerPropertyType::Vector2:
	case EBridgeSequencerPropertyType::Vector3:
	case EBridgeSequencerPropertyType::Vector4:
	{
		TArrayView<FMovieSceneDoubleChannel*> Channels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
		const int32 RequiredChannels = PropertyType == EBridgeSequencerPropertyType::Vector2 ? 2
			: PropertyType == EBridgeSequencerPropertyType::Vector3 ? 3 : 4;
		if (Channels.Num() < RequiredChannels)
		{
			bKeyAdded = false;
			break;
		}
		const double Values[4] = { VectorValue.X, VectorValue.Y, VectorValue.Z, VectorValue.W };
		for (int32 Index = 0; Index < RequiredChannels; ++Index)
		{
			BridgeSequencerImpl::AddDoubleKey(Channels[Index], Frame, Values[Index], Interpolation);
		}
		break;
	}
	case EBridgeSequencerPropertyType::Color:
	{
		TArrayView<FMovieSceneFloatChannel*> Channels = Proxy.GetChannels<FMovieSceneFloatChannel>();
		if (Channels.Num() < 4)
		{
			bKeyAdded = false;
			break;
		}
		const float Values[4] = { ColorValue.R, ColorValue.G, ColorValue.B, ColorValue.A };
		for (int32 Index = 0; Index < 4; ++Index)
		{
			BridgeSequencerImpl::AddFloatKey(Channels[Index], Frame, Values[Index], Interpolation);
		}
		break;
	}
	default:
		bKeyAdded = false;
		break;
	}
	if (!bKeyAdded)
	{
		return false;
	}
	Sequence->MarkPackageDirty();
	BridgeSequencerImpl::SaveSequenceIfRequested(Sequence, bSave);
	return true;
}

FString UUnrealBridgeSequencerLibrary::AddSkeletalAnimationSection(
	const FString& SequencePath,
	const FString& ActorLabel,
	const FString& AnimationPath,
	float StartSeconds,
	float EndSeconds,
	float PlayRate,
	bool bSave)
{
	ULevelSequence* Sequence = BridgeSequencerImpl::LoadSequence(SequencePath);
	AActor* Actor = BridgeSequencerImpl::FindActorByLabel(ActorLabel);
	UAnimSequenceBase* Animation = LoadObject<UAnimSequenceBase>(nullptr, *AnimationPath);
	UMovieScene* MovieScene = Sequence ? Sequence->GetMovieScene() : nullptr;
	if (!Sequence || !Actor || !Animation || !MovieScene)
	{
		return FString();
	}
	const double EffectivePlayRate = FMath::IsNearlyZero(PlayRate) ? 1.0 : FMath::Abs(PlayRate);
	const double EffectiveEndSeconds = EndSeconds > StartSeconds
		? EndSeconds : StartSeconds + Animation->GetPlayLength() / EffectivePlayRate;
	const FFrameRate TickResolution = MovieScene->GetTickResolution();
	const FFrameNumber StartFrame = TickResolution.AsFrameNumber(StartSeconds);
	const FFrameNumber EndFrame = BridgeSequencerImpl::EndFrameAfter(
		StartFrame, TickResolution.AsFrameNumber(EffectiveEndSeconds));

	const FScopedTransaction Transaction(NSLOCTEXT(
		"UnrealBridge", "AddSkeletalAnimationSection", "UnrealBridge Add Skeletal Animation Section"));
	Sequence->Modify();
	MovieScene->Modify();
	const FGuid BindingId = BridgeSequencerImpl::FindOrAddActorBinding(Sequence, MovieScene, Actor, true);
	UMovieSceneSkeletalAnimationTrack* Track = nullptr;
	if (const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingId))
	{
		for (UMovieSceneTrack* Candidate : Binding->GetTracks())
		{
			if (UMovieSceneSkeletalAnimationTrack* Existing = Cast<UMovieSceneSkeletalAnimationTrack>(Candidate))
			{
				Track = Existing;
				break;
			}
		}
	}
	if (!Track)
	{
		Track = MovieScene->AddTrack<UMovieSceneSkeletalAnimationTrack>(BindingId);
	}
	if (!Track)
	{
		return FString();
	}
	Track->Modify();
	UMovieSceneSkeletalAnimationSection* Section = nullptr;
	for (UMovieSceneSection* Candidate : Track->GetAllSections())
	{
		UMovieSceneSkeletalAnimationSection* Existing = Cast<UMovieSceneSkeletalAnimationSection>(Candidate);
		if (Existing && Existing->GetAnimation() == Animation
			&& Existing->GetRange().HasLowerBound()
			&& Existing->GetRange().GetLowerBoundValue() == StartFrame)
		{
			Section = Existing;
			break;
		}
	}
	if (!Section)
	{
		Section = Cast<UMovieSceneSkeletalAnimationSection>(Track->CreateNewSection());
	}
	if (!Section)
	{
		return FString();
	}
	Section->Modify();
	Section->Params.Animation = Animation;
	Section->Params.PlayRate = FMovieSceneTimeWarpVariant(EffectivePlayRate);
	Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
	if (!Track->HasSection(*Section))
	{
		Track->AddSection(*Section);
	}
	Sequence->MarkPackageDirty();
	BridgeSequencerImpl::SaveSequenceIfRequested(Sequence, bSave);
	return Section->GetPathName();
}
