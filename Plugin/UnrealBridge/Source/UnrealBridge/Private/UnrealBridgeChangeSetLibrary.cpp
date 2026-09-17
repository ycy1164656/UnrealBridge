#include "UnrealBridgeChangeSetLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "FileHelpers.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "PackageTools.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

namespace BridgeChangeSetImpl
{
	struct FActiveChangeSet
	{
		FBridgeChangeSetInfo Info;
		TSet<FString> BaselineDirty;
		TSet<FString> Targets;
		// Only packages that had neither a disk file nor a loaded package at
		// BeginChangeSet are eligible for explicit new-asset cleanup.  This
		// prevents rollback from ever deleting a pre-existing unsaved asset.
		TSet<FString> NewPackageTargets;
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
	TMap<FString, FString> CommittedTransactionTitles;
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

	TSet<FString> GatherAssetPaths(const TSet<FString>& PackageNames)
	{
		TSet<FString> Result;
		for (const FString& PackageName : PackageNames)
		{
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				continue;
			}
			ForEachObjectWithPackage(
				Package,
				[&Result](UObject* Object)
				{
					if (Object
						&& Object->IsAsset()
						&& !Object->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject))
					{
						Result.Add(Object->GetPathName());
					}
					return true;
				},
				EGetObjectsFlags::IncludeNestedObjects,
				RF_NoFlags,
				EInternalObjectFlags::Garbage);
		}
		return Result;
	}

	int32 CaptureExistingTargetObjectsForUndo(FActiveChangeSet& State)
	{
		int32 CapturedCount = 0;
		for (const FString& PackageName : State.Targets)
		{
			if (State.NewPackageTargets.Contains(PackageName))
			{
				continue;
			}

			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package && FPackageName::DoesPackageExist(PackageName))
			{
				Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			}
			if (!Package)
			{
				continue;
			}

			TArray<UObject*> TransactionalObjects;
			ForEachObjectWithPackage(
				Package,
				[&TransactionalObjects](UObject* Object)
				{
					if (Object
						&& Object->HasAnyFlags(RF_Transactional)
						&& !Object->HasAnyFlags(RF_Transient | RF_ClassDefaultObject | RF_ArchetypeObject))
					{
						TransactionalObjects.Add(Object);
					}
					return true;
				},
				EGetObjectsFlags::IncludeNestedObjects,
				RF_NoFlags,
				EInternalObjectFlags::Garbage);
			TransactionalObjects.Sort(
				[](const UObject& A, const UObject& B)
				{
					return A.GetPathName() < B.GetPathName();
				});
			for (UObject* Object : TransactionalObjects)
			{
				// bAlwaysMarkDirty=false records a pre-image in the already-open
				// bridge transaction without manufacturing a Dirty package before
				// the provider has actually changed anything.
				if (Object->Modify(false))
				{
					++CapturedCount;
				}
			}
		}
		return CapturedCount;
	}

	TSet<FString> PackageNamesForAssets(const TArray<FString>& AssetPaths)
	{
		TSet<FString> Result;
		for (const FString& AssetPath : AssetPaths)
		{
			if (FPackageName::IsValidObjectPath(AssetPath))
			{
				Result.Add(FPackageName::ObjectPathToPackageName(AssetPath));
			}
		}
		return Result;
	}

	bool RemoveUnsavedCreatedAssets(
		const TSet<FString>& EligibleNewPackages,
		const TArray<FString>& AssetPaths,
		TArray<FString>& OutRemovedAssets,
		FString& OutError)
	{
		OutRemovedAssets.Reset();
		OutError.Reset();
		if (AssetPaths.IsEmpty())
		{
			return true;
		}

		for (const FString& PackageName : EligibleNewPackages)
		{
			if (FPackageName::DoesPackageExist(PackageName))
			{
				OutError = FString::Printf(
					TEXT("Refused rollback cleanup because newly-created package '%s' unexpectedly exists on disk"),
					*PackageName);
				return false;
			}
		}

		IAssetRegistry& AssetRegistry =
			FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
		TArray<UObject*> CreatedObjectsToDelete;
		TArray<FString> CreatedObjectPathsToDelete;
		for (const FString& AssetPath : AssetPaths)
		{
			if (!FPackageName::IsValidObjectPath(AssetPath))
			{
				OutError = FString::Printf(TEXT("Invalid created asset path '%s'"), *AssetPath);
				return false;
			}
			const FString PackageName = FPackageName::ObjectPathToPackageName(AssetPath);
			if (!EligibleNewPackages.Contains(PackageName))
			{
				OutError = FString::Printf(
					TEXT("Refused rollback cleanup for '%s' because its package was not new at change-set start"),
					*AssetPath);
				return false;
			}

			UObject* Asset = FindObject<UObject>(nullptr, *AssetPath);
			if (!Asset)
			{
				// A provider may have fully destroyed its object during Undo.  The
				// registry verification below remains authoritative.
				OutRemovedAssets.Add(AssetPath);
				continue;
			}
			if (Asset->GetOutermost()->GetName() != PackageName)
			{
				OutError = FString::Printf(
					TEXT("Created asset '%s' resolved outside its declared target package"),
					*AssetPath);
				return false;
			}
			CreatedObjectsToDelete.Add(Asset);
			CreatedObjectPathsToDelete.Add(AssetPath);
		}

		// DeleteSingleObject without a reference check is the Editor's own
		// in-memory asset cleanup path.  It emits all registry/deletion delegates
		// and is safe here because every object belongs to a package proven absent
		// both on disk and in memory at ChangeSet start.
		if (!CreatedObjectsToDelete.IsEmpty())
		{
			const int32 DeletedCount = ObjectTools::DeleteObjectsUnchecked(CreatedObjectsToDelete);
			if (DeletedCount != CreatedObjectsToDelete.Num())
			{
				OutError = FString::Printf(
					TEXT("Deleted %d of %d newly-created rollback assets"),
					DeletedCount,
					CreatedObjectsToDelete.Num());
				return false;
			}
			OutRemovedAssets.Append(CreatedObjectPathsToDelete);
		}

		TArray<UPackage*> PackagesToUnload;
		for (const FString& PackageName : EligibleNewPackages)
		{
			if (UPackage* Package = FindPackage(nullptr, *PackageName))
			{
				Package->SetDirtyFlag(false);
				PackagesToUnload.Add(Package);
			}
		}
		// Graph compilers/builders may retain strong references after Undo.  A
		// second registry notification is not sufficient in that case: unload the
		// package that provably did not exist when the ChangeSet began.  This is
		// deliberately limited to EligibleNewPackages, so rollback can never
		// unload a pre-existing user package.
		if (!PackagesToUnload.IsEmpty())
		{
			FText UnloadError;
			if (!UPackageTools::UnloadPackages(PackagesToUnload, UnloadError, true))
			{
				OutError = FString::Printf(
					TEXT("Failed to unload one or more newly-created rollback packages: %s"),
					*UnloadError.ToString());
				return false;
			}
		}

		// Some graph compilers publish a late AssetCreated notification while
		// their strong reference is being released.  Reconcile that stale entry
		// after package unload, again only for assets in provably new packages.
		bool bNeedsFinalGarbageCollection = false;
		for (const FString& AssetPath : AssetPaths)
		{
			if (!AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid())
			{
				continue;
			}
			if (UObject* LingeringAsset = FindObject<UObject>(nullptr, *AssetPath))
			{
				FAssetRegistryModule::AssetDeleted(LingeringAsset);
				LingeringAsset->ClearFlags(RF_Public | RF_Standalone);
				const FName TombstoneName = MakeUniqueObjectName(
					GetTransientPackage(),
					LingeringAsset->GetClass(),
					TEXT("UnrealBridgeRollback"));
				if (!LingeringAsset->Rename(
					*TombstoneName.ToString(),
					GetTransientPackage(),
					REN_DontCreateRedirectors | REN_NonTransactional | REN_DoNotDirty))
				{
					OutError = FString::Printf(
						TEXT("Failed to detach lingering rollback asset '%s' from its new package"),
						*AssetPath);
					return false;
				}
				LingeringAsset->MarkAsGarbage();
				bNeedsFinalGarbageCollection = true;
			}
		}
		if (bNeedsFinalGarbageCollection)
		{
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}

		for (const FString& AssetPath : AssetPaths)
		{
			if (AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(AssetPath)).IsValid())
			{
				OutError = FString::Printf(
					TEXT("Asset Registry still contains '%s' after rollback cleanup"),
					*AssetPath);
				return false;
			}
		}
		return true;
	}

	bool ReloadCleanPackagesAfterUndo(
		const TArray<FString>& PackageNames,
		TArray<FString>& OutReloadedPackages,
		FString& OutError)
	{
		OutReloadedPackages.Reset();
		OutError.Reset();
		if (PackageNames.IsEmpty())
		{
			return true;
		}

		TArray<UPackage*> Packages;
		for (const FString& PackageName : PackageNames)
		{
			if (!FPackageName::DoesPackageExist(PackageName))
			{
				OutError = FString::Printf(
					TEXT("Refused rollback reload because target package '%s' no longer exists on disk"),
					*PackageName);
				return false;
			}
			UPackage* Package = FindPackage(nullptr, *PackageName);
			if (!Package)
			{
				Package = LoadPackage(nullptr, *PackageName, LOAD_None);
			}
			if (!Package)
			{
				OutError = FString::Printf(
					TEXT("Failed to load rollback target package '%s'"), *PackageName);
				return false;
			}
			Packages.Add(Package);
		}

		if (!UPackageTools::ReloadPackages(Packages))
		{
			OutError = TEXT("Editor failed to reload one or more clean rollback target packages");
			return false;
		}
		for (const FString& PackageName : PackageNames)
		{
			UPackage* Reloaded = FindPackage(nullptr, *PackageName);
			if (!Reloaded || Reloaded->IsDirty())
			{
				OutError = FString::Printf(
					TEXT("Rollback target package '%s' was missing or Dirty after reload"),
					*PackageName);
				return false;
			}
			OutReloadedPackages.Add(PackageName);
		}
		return true;
	}

	bool VerifyImmediateRollback(const FActiveChangeSet& State, FString& OutError)
	{
		const TSet<FString> CurrentDirty = GatherDirtyPackages();
		if (!CurrentDirty.Difference(State.BaselineDirty).IsEmpty()
			|| !State.BaselineDirty.Difference(CurrentDirty).IsEmpty())
		{
			const TArray<FString> NewlyDirty = SortedArray(CurrentDirty.Difference(State.BaselineDirty));
			const TArray<FString> NoLongerDirty = SortedArray(State.BaselineDirty.Difference(CurrentDirty));
			OutError = FString::Printf(
				TEXT("Dirty state differs after rollback (new=%s, cleared=%s)"),
				*FString::Join(NewlyDirty, TEXT(", ")),
				*FString::Join(NoLongerDirty, TEXT(", ")));
			return false;
		}
		return true;
	}

	bool VerifyHistoricalTargetRollback(
		const FBridgeChangeSetInfo& State,
		FString& OutError)
	{
		const TSet<FString> CurrentDirty = GatherDirtyPackages();
		TSet<FString> TargetSet;
		TargetSet.Append(State.TargetPackages);
		TSet<FString> ExpectedDirty;
		ExpectedDirty.Append(State.TargetPackagesDirtyAtBegin);
		const TSet<FString> ActualDirty = CurrentDirty.Intersect(TargetSet);
		if (!ActualDirty.Difference(ExpectedDirty).IsEmpty()
			|| !ExpectedDirty.Difference(ActualDirty).IsEmpty())
		{
			OutError = FString::Printf(
				TEXT("Target Dirty state differs after committed rollback (actual=%s, expected=%s)"),
				*FString::Join(SortedArray(ActualDirty), TEXT(", ")),
				*FString::Join(SortedArray(ExpectedDirty), TEXT(", ")));
			return false;
		}
		return true;
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
		Result.TargetPackagesDirtyAtBegin =
			SortedArray(State.BaselineDirty.Intersect(State.Targets));
		Result.DirtyPackagesForJob = SortedArray(OwnedDirty);
		Result.ProtectedPreexistingDirtyPackages = SortedArray(Protected);
		Result.UnexpectedDirtyPackages = SortedArray(Unexpected);
		Result.CreatedAssetsForJob = SortedArray(GatherAssetPaths(State.NewPackageTargets));
		Result.bCanCommit = Unexpected.IsEmpty();
		Result.bSuccess = true;
		return Result;
	}

	void Remember(const FBridgeChangeSetInfo& Info, const FString& CommittedTransactionTitle = FString())
	{
		History.Add(Info.ChangeSetId, Info);
		if (!CommittedTransactionTitle.IsEmpty())
		{
			CommittedTransactionTitles.Add(Info.ChangeSetId, CommittedTransactionTitle);
		}
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
			CommittedTransactionTitles.Remove(Oldest);
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

	FBridgeChangeSetInfo RetainFailedActive(const FString& Reason)
	{
		FBridgeChangeSetInfo Result = BuildPreview(*Active);
		EndTransaction(*Active);
		Result.Status = TEXT("NeedsReconciliation");
		Result.bSuccess = false;
		Result.bCanCommit = false;
		Result.bSaved = false;
		Result.bRollbackVerified = false;
		Result.Error = Reason + TEXT("; changes retained unsaved, no Undo/reload/delete performed");
		Remember(Result);
		Active.Reset();
		return Result;
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

		// Guarded authoring never inherits an implicit recovery action from a
		// caller error, forgotten commit, commit refusal or next-job cleanup.
		if (Active->Info.bRetainOnFailure)
		{
			return RetainFailedActive(Reason);
		}
		FBridgeChangeSetInfo Result = BuildPreview(*Active);
		const bool bHadAttributedPackages = !Result.DirtyPackagesForJob.IsEmpty()
			|| !Result.UnexpectedDirtyPackages.IsEmpty();
		bool bOnlyNewPackageMutations =
			!Result.CreatedAssetsForJob.IsEmpty()
			&& Result.UnexpectedDirtyPackages.IsEmpty();
		for (const FString& PackageName : Result.DirtyPackagesForJob)
		{
			bOnlyNewPackageMutations &= Active->NewPackageTargets.Contains(PackageName);
		}

		// Undoing the creation transaction of some editor assets (notably
		// MetaSound) first makes the UObject transient and then invokes its
		// PostEditUndo callback. UE 5.8's callback attempts to open a non-
		// transactional document builder and asserts because the object is no
		// longer an asset. For a ChangeSet containing only packages proven absent
		// at BeginChangeSet, discard the transaction record and use the explicit,
		// ownership-checked cleanup below instead of routing PostEditUndo.
		bool bDiscardedNewPackageTransaction = false;
		if (bOnlyNewPackageMutations && Active->bTransactionOpen && GEditor)
		{
			GEditor->CancelTransaction(Active->TransactionIndex);
			Active->bTransactionOpen = false;
			bDiscardedNewPackageTransaction = true;
		}
		else
		{
			EndTransaction(*Active);
		}

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
		// Some official asset factories do not record creation in the Editor
		// transaction buffer.  A fallback is safe only for packages proven not
		// to exist in memory or on disk when BeginChangeSet ran, with no Dirty
		// package outside that set.  Existing-package mutations still require
		// strict top-Undo ownership.
		const bool bCanUseExplicitNewPackageCleanup =
			!bUndoSucceeded && bOnlyNewPackageMutations
			&& (bDiscardedNewPackageTransaction || !Active->bTransactionOpen);

		bool bCleanupSucceeded = true;
		bool bReloadSucceeded = true;
		bool bStateVerified = false;
		FString CleanupError;
		FString ReloadError;
		FString VerificationError;
		if (bUndoSucceeded || bCanUseExplicitNewPackageCleanup)
		{
			bCleanupSucceeded = RemoveUnsavedCreatedAssets(
				Active->NewPackageTargets,
				Result.CreatedAssetsForJob,
				Result.RemovedCreatedAssetsDuringRollback,
				CleanupError);
			if (bCleanupSucceeded && bUndoSucceeded)
			{
				bReloadSucceeded = ReloadCleanPackagesAfterUndo(
					Result.ReloadPackagesOnRollback,
					Result.ReloadedPackagesDuringRollback,
					ReloadError);
			}
			if (bCleanupSucceeded && bReloadSucceeded)
			{
				bStateVerified = VerifyImmediateRollback(*Active, VerificationError);
			}
		}

		const bool bRollbackSucceeded =
			(bUndoSucceeded || bCanUseExplicitNewPackageCleanup)
			&& bCleanupSucceeded
			&& bReloadSucceeded
			&& bStateVerified;
		Result.Status = bRollbackSucceeded ? TEXT("RolledBack") : TEXT("RollbackFailed");
		Result.bSuccess = bRollbackSucceeded;
		Result.bCanCommit = false;
		Result.bRollbackVerified = bRollbackSucceeded;
		if (bRollbackSucceeded)
		{
			Result.Error = Reason;
		}
		else if (!bUndoSucceeded && !bCanUseExplicitNewPackageCleanup)
		{
			Result.Error = FString::Printf(
				TEXT("Refused rollback because top Undo transaction '%s' is not owned by change set %s (%s)"),
				*UndoTitle.ToString(), *Active->Info.ChangeSetId, *UndoReason.ToString());
		}
		else if (!bCleanupSucceeded)
		{
			Result.Error = CleanupError;
		}
		else if (!bReloadSucceeded)
		{
			Result.Error = ReloadError;
		}
		else
		{
			Result.Error = VerificationError;
		}
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
			return RollbackActive(Active->Info.bRetainOnFailure
				? TEXT("Unexpected dirty packages require reconciliation")
				: TEXT("Unexpected dirty packages caused automatic rollback"));
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
		Remember(Result, Active->TransactionTitle);
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
			RollbackActive(Active->Info.bRetainOnFailure
				? TEXT("Python execution failed; guarded Job needs reconciliation")
				: TEXT("Python execution failed; Job transaction rolled back"));
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

	FBridgeChangeSetInfo RollbackNow(
		const FString& ChangeSetId,
		const FString& Reason)
	{
		check(IsInGameThread());
		using namespace BridgeChangeSetImpl;
		if (!Active || Active->Info.ChangeSetId != ChangeSetId)
		{
			return UUnrealBridgeChangeSetLibrary::PreviewChangeSet(ChangeSetId);
		}
		return RollbackActive(
			Reason.IsEmpty() ? TEXT("Immediate rollback requested") : Reason);
	}

	FBridgeChangeSetInfo CommitNow(const FString& ChangeSetId, bool bSave)
	{
		check(IsInGameThread());
		using namespace BridgeChangeSetImpl;
		if (!Active || Active->Info.ChangeSetId != ChangeSetId)
		{
			return UUnrealBridgeChangeSetLibrary::PreviewChangeSet(ChangeSetId);
		}
		return CommitActive(bSave);
	}
}

FString UUnrealBridgeChangeSetLibrary::BeginChangeSet(
	const FString& Name,
	const TArray<FString>& TargetPackages,
	bool bReloadCleanTargetsOnRollback)
{
	using namespace BridgeChangeSetImpl;
	if (!Active || (Active->bExplicit && Active->Info.bRetainOnFailure))
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
			if (!FPackageName::DoesPackageExist(PackageName)
				&& FindPackage(nullptr, *PackageName) == nullptr)
			{
				Active->NewPackageTargets.Add(PackageName);
			}
		}
	}
	Active->Info.CapturedObjectsForUndo = CaptureExistingTargetObjectsForUndo(*Active);
	if (bReloadCleanTargetsOnRollback)
	{
		for (const FString& PackageName : Active->Targets)
		{
			if (!Active->NewPackageTargets.Contains(PackageName)
				&& !Active->BaselineDirty.Contains(PackageName)
				&& FPackageName::DoesPackageExist(PackageName))
			{
				Active->Info.ReloadPackagesOnRollback.Add(PackageName);
			}
		}
		Active->Info.ReloadPackagesOnRollback.Sort();
	}
	return Active->Info.ChangeSetId;
}

FString UUnrealBridgeChangeSetLibrary::BeginGuardedChangeSet(
	const FString& Name, const TArray<FString>& TargetPackages)
{
	using namespace BridgeChangeSetImpl;
	if (!IsInGameThread() || !Active || Active->bExplicit || TargetPackages.IsEmpty() || TargetPackages.Num() > 16)
	{
		return FString();
	}
	TSet<FString> UniqueTargets;
	for (const FString& Target : TargetPackages)
	{
		if (!FPackageName::IsValidLongPackageName(Target) || Target.Contains(TEXT("."))
			|| Target.Contains(TEXT(":")) || Target.Contains(TEXT("\\"))
			|| Target.StartsWith(TEXT("/Engine/"), ESearchCase::IgnoreCase)
			|| Target.StartsWith(TEXT("/Script/"), ESearchCase::IgnoreCase)
			|| Target.StartsWith(TEXT("/Temp/"), ESearchCase::IgnoreCase)
			|| Target.StartsWith(TEXT("/Transient/"), ESearchCase::IgnoreCase)
			|| Target.StartsWith(TEXT("/Memory/"), ESearchCase::IgnoreCase)
			|| UniqueTargets.Contains(Target.ToLower()) || Active->BaselineDirty.Contains(Target))
		{
			return FString();
		}
		UniqueTargets.Add(Target.ToLower());
	}
	Active->Info.bRetainOnFailure = true;
	return BeginChangeSet(Name, TargetPackages, false);
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
		Result.Status = Active->Info.bRetainOnFailure ? TEXT("RetainPending") : TEXT("RollbackPending");
		Result.bSuccess = false;
		Result.Error = Active->Info.bRetainOnFailure
			? TEXT("Unexpected dirty packages will be retained unsaved for reconciliation")
			: TEXT("Unexpected dirty packages require automatic rollback when the Job completes");
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

FBridgeChangeSetInfo UUnrealBridgeChangeSetLibrary::FinalizeChangeSet(
	const FString& ChangeSetId,
	bool bApply)
{
	using namespace BridgeChangeSetImpl;
	if (!Active || Active->Info.ChangeSetId != ChangeSetId)
	{
		return PreviewChangeSet(ChangeSetId);
	}
	return bApply
		? CommitActive(false)
		: RollbackActive(TEXT("Immediate preview finalization requested"));
}

FBridgeChangeSetInfo UUnrealBridgeChangeSetLibrary::RollbackCommittedChangeSet(
	const FString& ChangeSetId)
{
	using namespace BridgeChangeSetImpl;
	FBridgeChangeSetInfo* Existing = History.Find(ChangeSetId);
	const FString* TransactionTitle = CommittedTransactionTitles.Find(ChangeSetId);
	if (!Existing || !TransactionTitle)
	{
		FBridgeChangeSetInfo Missing;
		Missing.ChangeSetId = ChangeSetId;
		Missing.Status = TEXT("Missing");
		Missing.Error = TEXT("Unknown or non-committed change set id");
		return Missing;
	}

	FBridgeChangeSetInfo Result = *Existing;
	if (Result.Status != TEXT("Committed"))
	{
		Result.bSuccess = false;
		Result.Error = FString::Printf(
			TEXT("Change set status '%s' cannot be rolled back after commit"), *Result.Status);
		return Result;
	}
	if (Result.bSaved)
	{
		Result.bSuccess = false;
		Result.Error = TEXT("Refused rollback because this change set was already saved to disk");
		return Result;
	}

	// This function is normally called from a fresh bridge Job. Close only that
	// empty transaction so the committed transaction can be verified at the top
	// of the undo stack; any attributed mutation makes the request unsafe.
	if (Active)
	{
		const FBridgeChangeSetInfo ActivePreview = BuildPreview(*Active);
		if (!ActivePreview.DirtyPackagesForJob.IsEmpty()
			|| !ActivePreview.UnexpectedDirtyPackages.IsEmpty())
		{
			Result.bSuccess = false;
			Result.Error = TEXT("Refused rollback because the rollback Job already owns mutations");
			return Result;
		}
		EndTransaction(*Active);
	}

	FText UndoReason;
	FText UndoTitle;
	const bool bCanUndo = GEditor && GEditor->Trans && GEditor->Trans->CanUndo(&UndoReason);
	if (bCanUndo)
	{
		UndoTitle = GEditor->Trans->GetUndoContext(false).Title;
	}
	if (!bCanUndo || UndoTitle.ToString() != *TransactionTitle)
	{
		Result.bSuccess = false;
		Result.Error = FString::Printf(
			TEXT("Refused rollback because top Undo transaction '%s' is not owned by change set %s (%s)"),
			*UndoTitle.ToString(), *ChangeSetId, *UndoReason.ToString());
		return Result;
	}

	const bool bUndoSucceeded = GEditor->UndoTransaction(false);
	bool bCleanupSucceeded = true;
	bool bReloadSucceeded = true;
	bool bStateVerified = false;
	FString CleanupError;
	FString ReloadError;
	FString VerificationError;
	if (bUndoSucceeded)
	{
		const TSet<FString> EligibleNewPackages =
			PackageNamesForAssets(Result.CreatedAssetsForJob);
		bCleanupSucceeded = RemoveUnsavedCreatedAssets(
			EligibleNewPackages,
			Result.CreatedAssetsForJob,
			Result.RemovedCreatedAssetsDuringRollback,
			CleanupError);
		if (bCleanupSucceeded)
		{
			bReloadSucceeded = ReloadCleanPackagesAfterUndo(
				Result.ReloadPackagesOnRollback,
				Result.ReloadedPackagesDuringRollback,
				ReloadError);
		}
		if (bCleanupSucceeded && bReloadSucceeded)
		{
			bStateVerified = VerifyHistoricalTargetRollback(Result, VerificationError);
		}
	}
	const bool bRollbackSucceeded =
		bUndoSucceeded && bCleanupSucceeded && bReloadSucceeded && bStateVerified;
	Result.Status = bRollbackSucceeded ? TEXT("RolledBackAfterCommit") : TEXT("RollbackFailed");
	Result.bSuccess = bRollbackSucceeded;
	Result.bCanCommit = false;
	Result.bRollbackVerified = bRollbackSucceeded;
	if (bRollbackSucceeded)
	{
		Result.Error = TEXT("Committed transaction rolled back; no packages were saved");
	}
	else if (!bUndoSucceeded)
	{
		Result.Error = TEXT("Editor failed to undo the committed bridge transaction");
	}
	else if (!bCleanupSucceeded)
	{
		Result.Error = CleanupError;
	}
	else if (!bReloadSucceeded)
	{
		Result.Error = ReloadError;
	}
	else
	{
		Result.Error = VerificationError;
	}
	History.Add(ChangeSetId, Result);
	if (bUndoSucceeded)
	{
		CommittedTransactionTitles.Remove(ChangeSetId);
	}
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
