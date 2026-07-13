#include "UnrealBridgeChangeSetLibrary.h"

#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "FileHelpers.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

namespace BridgeChangeSetImpl
{
	struct FActiveChangeSet
	{
		FBridgeChangeSetInfo Info;
		TSet<FString> BaselineDirty;
		TSet<FString> Targets;
		FString TransactionTitle;
		int32 TransactionIndex = INDEX_NONE;
		bool bTransactionOpen = false;
		bool bExplicit = false;
		bool bCommitRequested = false;
		bool bRollbackRequested = false;
		bool bSaveRequested = false;
	};

	TUniquePtr<FActiveChangeSet> Active;
	TMap<FString, FBridgeChangeSetInfo> History;
	TMap<FString, FString> JobToChangeSet;
	TArray<FString> HistoryOrder;
	constexpr int32 MaxHistory = 128;

	FString NormalizePackageName(const FString& Input)
	{
		FString Result = Input.TrimStartAndEnd();
		if (Result.IsEmpty())
		{
			return Result;
		}
		if (FPackageName::IsValidObjectPath(Result))
		{
			Result = FPackageName::ObjectPathToPackageName(Result);
		}
		else
		{
			int32 DotIndex = INDEX_NONE;
			if (Result.FindLastChar(TEXT('.'), DotIndex) && Result.StartsWith(TEXT("/")))
			{
				Result.LeftInline(DotIndex);
			}
		}
		return Result;
	}

	TSet<FString> GatherDirtyPackages()
	{
		TSet<FString> Result;
		for (TObjectIterator<UPackage> It; It; ++It)
		{
			UPackage* Package = *It;
			if (!Package || !Package->IsDirty())
			{
				continue;
			}
			const FString Name = Package->GetName();
			if (!Name.StartsWith(TEXT("/Script/"))
				&& !Name.StartsWith(TEXT("/Temp/"))
				&& Name != TEXT("/Engine/Transient"))
			{
				Result.Add(Name);
			}
		}
		return Result;
	}

	TArray<FString> SortedArray(const TSet<FString>& Values)
	{
		TArray<FString> Result = Values.Array();
		Result.Sort();
		return Result;
	}

	FBridgeChangeSetInfo BuildPreview(const FActiveChangeSet& State)
	{
		FBridgeChangeSetInfo Result = State.Info;
		const TSet<FString> CurrentDirty = GatherDirtyPackages();
		TSet<FString> NewlyDirty = CurrentDirty.Difference(State.BaselineDirty);
		TSet<FString> OwnedDirty = NewlyDirty;
		TSet<FString> Unexpected;
		TSet<FString> Protected;

		if (State.bExplicit && !State.Targets.IsEmpty())
		{
			Unexpected = NewlyDirty.Difference(State.Targets);
			OwnedDirty = CurrentDirty.Intersect(State.Targets);
		}
		for (const FString& PackageName : State.BaselineDirty)
		{
			if (CurrentDirty.Contains(PackageName) && !State.Targets.Contains(PackageName))
			{
				Protected.Add(PackageName);
			}
		}

		Result.TargetPackages = SortedArray(State.Targets);
		Result.DirtyPackagesForJob = SortedArray(OwnedDirty);
		Result.ProtectedPreexistingDirtyPackages = SortedArray(Protected);
		Result.UnexpectedDirtyPackages = SortedArray(Unexpected);
		Result.bCanCommit = Unexpected.IsEmpty();
		Result.bSuccess = true;
		return Result;
	}

	void Remember(const FBridgeChangeSetInfo& Info)
	{
		History.Add(Info.ChangeSetId, Info);
		JobToChangeSet.Add(Info.JobId, Info.ChangeSetId);
		HistoryOrder.Add(Info.ChangeSetId);
		while (HistoryOrder.Num() > MaxHistory)
		{
			const FString Oldest = HistoryOrder[0];
			HistoryOrder.RemoveAt(0);
			if (const FBridgeChangeSetInfo* Existing = History.Find(Oldest))
			{
				if (JobToChangeSet.FindRef(Existing->JobId) == Oldest)
				{
					JobToChangeSet.Remove(Existing->JobId);
				}
			}
			History.Remove(Oldest);
		}
	}

	bool EndTransaction(FActiveChangeSet& State)
	{
		if (!State.bTransactionOpen || !GEditor)
		{
			return false;
		}
		GEditor->EndTransaction();
		State.bTransactionOpen = false;
		return true;
	}

	FBridgeChangeSetInfo RollbackActive(const FString& Reason)
	{
		if (!Active)
		{
			FBridgeChangeSetInfo Missing;
			Missing.Status = TEXT("Missing");
			Missing.Error = TEXT("No active change set");
			return Missing;
		}

		FBridgeChangeSetInfo Result = BuildPreview(*Active);
		const bool bHadAttributedPackages = !Result.DirtyPackagesForJob.IsEmpty()
			|| !Result.UnexpectedDirtyPackages.IsEmpty();
		EndTransaction(*Active);

		bool bUndoSucceeded = false;
		FText UndoReason;
		FText UndoTitle;
		const bool bCanUndo = GEditor && GEditor->Trans && GEditor->Trans->CanUndo(&UndoReason);
		if (bCanUndo)
		{
			UndoTitle = GEditor->Trans->GetUndoContext(false).Title;
		}
		const bool bOurTransactionIsTop = bCanUndo
			&& UndoTitle.ToString() == Active->TransactionTitle;
		if (bOurTransactionIsTop)
		{
			bUndoSucceeded = GEditor->UndoTransaction(false);
		}
		else if (!bHadAttributedPackages)
		{
			// Empty transactions are discarded by UTransBuffer::End.
			bUndoSucceeded = true;
		}

		Result.Status = bUndoSucceeded ? TEXT("RolledBack") : TEXT("RollbackFailed");
		Result.bSuccess = bUndoSucceeded;
		Result.bCanCommit = false;
		Result.Error = bUndoSucceeded ? Reason : FString::Printf(
			TEXT("Refused rollback because top Undo transaction '%s' is not owned by change set %s (%s)"),
			*UndoTitle.ToString(), *Active->Info.ChangeSetId, *UndoReason.ToString());
		Remember(Result);
		Active.Reset();
		return Result;
	}

	FBridgeChangeSetInfo CommitActive(bool bSave)
	{
		if (!Active)
		{
			FBridgeChangeSetInfo Missing;
			Missing.Status = TEXT("Missing");
			Missing.Error = TEXT("No active change set");
			return Missing;
		}

		FBridgeChangeSetInfo Result = BuildPreview(*Active);
		if (!Result.bCanCommit)
		{
			return RollbackActive(TEXT("Unexpected dirty packages caused automatic rollback"));
		}

		EndTransaction(*Active);
		Result.Status = TEXT("Committed");
		Result.bSuccess = true;
		if (bSave && !Result.DirtyPackagesForJob.IsEmpty())
		{
			TArray<UPackage*> Packages;
			for (const FString& PackageName : Result.DirtyPackagesForJob)
			{
				if (UPackage* Package = FindPackage(nullptr, *PackageName))
				{
					Packages.Add(Package);
				}
			}
			Result.bSaved = Packages.Num() == Result.DirtyPackagesForJob.Num()
				&& UEditorLoadingAndSavingUtils::SavePackages(Packages, true);
			if (!Result.bSaved)
			{
				Result.bSuccess = false;
				Result.Status = TEXT("CommitSaveFailed");
				Result.Error = TEXT("Transaction committed, but one or more owned packages failed to save");
			}
		}
		Remember(Result);
		Active.Reset();
		return Result;
	}
}

namespace BridgeChangeSetRuntime
{
	void BeginJob(const FString& JobId)
	{
		check(IsInGameThread());
		using namespace BridgeChangeSetImpl;
		if (Active)
		{
			RollbackActive(TEXT("Previous bridge Job left an active transaction"));
		}

		Active = MakeUnique<FActiveChangeSet>();
		Active->Info.ChangeSetId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		Active->Info.JobId = JobId;
		Active->Info.Name = TEXT("Implicit Job Transaction");
		Active->Info.Status = TEXT("Active");
		Active->Info.bSuccess = true;
		Active->BaselineDirty = GatherDirtyPackages();
		Active->TransactionTitle = FString::Printf(
			TEXT("UnrealBridge ChangeSet %s"), *Active->Info.ChangeSetId);
		if (GEditor)
		{
			Active->TransactionIndex = GEditor->BeginTransaction(FText::FromString(Active->TransactionTitle));
			Active->bTransactionOpen = Active->TransactionIndex != INDEX_NONE;
		}
	}

	void EndJob(const FString& JobId, bool bPythonSuccess)
	{
		check(IsInGameThread());
		using namespace BridgeChangeSetImpl;
		if (!Active || Active->Info.JobId != JobId)
		{
			return;
		}
		if (!bPythonSuccess)
		{
			RollbackActive(TEXT("Python execution failed; Job transaction rolled back"));
		}
		else if (Active->bRollbackRequested)
		{
			RollbackActive(TEXT("Rollback requested by caller"));
		}
		else if (Active->bCommitRequested)
		{
			CommitActive(Active->bSaveRequested);
		}
		else if (Active->bExplicit)
		{
			RollbackActive(TEXT("Explicit change set was not committed before Job completion"));
		}
		else
		{
			CommitActive(false);
		}
	}
}

FString UUnrealBridgeChangeSetLibrary::BeginChangeSet(
	const FString& Name,
	const TArray<FString>& TargetPackages)
{
	using namespace BridgeChangeSetImpl;
	if (!Active)
	{
		return FString();
	}
	Active->bExplicit = true;
	Active->Info.Name = Name.IsEmpty() ? TEXT("UnrealBridge Change Set") : Name;
	Active->Targets.Reset();
	for (const FString& Input : TargetPackages)
	{
		const FString PackageName = NormalizePackageName(Input);
		if (!PackageName.IsEmpty())
		{
			Active->Targets.Add(PackageName);
		}
	}
	return Active->Info.ChangeSetId;
}

FBridgeChangeSetInfo UUnrealBridgeChangeSetLibrary::PreviewChangeSet(const FString& ChangeSetId)
{
	using namespace BridgeChangeSetImpl;
	if (Active && Active->Info.ChangeSetId == ChangeSetId)
	{
		return BuildPreview(*Active);
	}
	if (const FBridgeChangeSetInfo* Existing = History.Find(ChangeSetId))
	{
		return *Existing;
	}
	FBridgeChangeSetInfo Missing;
	Missing.ChangeSetId = ChangeSetId;
	Missing.Status = TEXT("Missing");
	Missing.Error = TEXT("Unknown change set id");
	return Missing;
}

FBridgeChangeSetInfo UUnrealBridgeChangeSetLibrary::CommitChangeSet(
	const FString& ChangeSetId,
	bool bSave)
{
	using namespace BridgeChangeSetImpl;
	if (!Active || Active->Info.ChangeSetId != ChangeSetId)
	{
		return PreviewChangeSet(ChangeSetId);
	}
	FBridgeChangeSetInfo Result = BuildPreview(*Active);
	if (!Result.bCanCommit)
	{
		Active->bRollbackRequested = true;
		Result.Status = TEXT("RollbackPending");
		Result.bSuccess = false;
		Result.Error = TEXT("Unexpected dirty packages require automatic rollback when the Job completes");
		return Result;
	}
	Active->bCommitRequested = true;
	Active->bSaveRequested = bSave;
	Result.Status = TEXT("CommitPending");
	Result.bSuccess = true;
	return Result;
}

FBridgeChangeSetInfo UUnrealBridgeChangeSetLibrary::RollbackChangeSet(const FString& ChangeSetId)
{
	using namespace BridgeChangeSetImpl;
	if (!Active || Active->Info.ChangeSetId != ChangeSetId)
	{
		return PreviewChangeSet(ChangeSetId);
	}
	Active->bRollbackRequested = true;
	FBridgeChangeSetInfo Result = BuildPreview(*Active);
	Result.Status = TEXT("RollbackPending");
	Result.bSuccess = true;
	Result.bCanCommit = false;
	return Result;
}

TArray<FString> UUnrealBridgeChangeSetLibrary::GetDirtyPackagesForJob(const FString& JobId)
{
	using namespace BridgeChangeSetImpl;
	if (Active && Active->Info.JobId == JobId)
	{
		return BuildPreview(*Active).DirtyPackagesForJob;
	}
	if (const FString* ChangeSetId = JobToChangeSet.Find(JobId))
	{
		if (const FBridgeChangeSetInfo* Existing = History.Find(*ChangeSetId))
		{
			return Existing->DirtyPackagesForJob;
		}
	}
	return {};
}
