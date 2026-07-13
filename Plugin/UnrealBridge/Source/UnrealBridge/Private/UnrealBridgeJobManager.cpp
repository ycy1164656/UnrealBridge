#include "UnrealBridgeJobManager.h"

#include "HAL/PlatformProcess.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"

namespace
{
	FBridgeJobResult MakeLifecycleError(
		const TCHAR* Message,
		const TCHAR* Code,
		const TCHAR* Phase,
		const TCHAR* SideEffectState = TEXT("none"))
	{
		FBridgeJobResult Result;
		Result.bSuccess = false;
		Result.Error = Message;
		Result.ErrorCode = Code;
		Result.Phase = Phase;
		Result.bRetryable = false;
		Result.SideEffectState = SideEffectState;
		return Result;
	}
}

const TCHAR* LexToString(EBridgeJobState State)
{
	switch (State)
	{
	case EBridgeJobState::Queued: return TEXT("queued");
	case EBridgeJobState::Running: return TEXT("running");
	case EBridgeJobState::Succeeded: return TEXT("succeeded");
	case EBridgeJobState::Failed: return TEXT("failed");
	case EBridgeJobState::Cancelled: return TEXT("cancelled");
	case EBridgeJobState::Expired: return TEXT("expired");
	case EBridgeJobState::Aborted: return TEXT("aborted");
	default: return TEXT("unknown");
	}
}

bool IsBridgeJobTerminal(EBridgeJobState State)
{
	return State == EBridgeJobState::Succeeded
		|| State == EBridgeJobState::Failed
		|| State == EBridgeJobState::Cancelled
		|| State == EBridgeJobState::Expired
		|| State == EBridgeJobState::Aborted;
}

FBridgeJob::FBridgeJob()
	: CompletionEvent(FPlatformProcess::GetSynchEventFromPool(true))
{
}

FBridgeJob::~FBridgeJob()
{
	if (CompletionEvent)
	{
		FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);
		CompletionEvent = nullptr;
	}
}

FBridgeJobManager::~FBridgeJobManager()
{
	Shutdown();
}

FBridgeJobSubmitResult FBridgeJobManager::Submit(
	const FString& Script,
	const FString& RequestId,
	double QueueDeadlineSecondsFromNow,
	const FString& IdempotencyKey,
	const FString& PollScript,
	double PollIntervalSeconds,
	double RunTimeoutSeconds)
{
	FBridgeJobSubmitResult SubmitResult;
	const double Now = FPlatformTime::Seconds();
	const FString HashSource = FString::Printf(
		TEXT("%s\n--UNREALBRIDGE-POLL--\n%s\n%.6f\n%.6f"),
		*Script, *PollScript, PollIntervalSeconds, RunTimeoutSeconds);
	const FString ScriptHash = FMD5::HashAnsiString(*HashSource);

	{
		FScopeLock Lock(&JobsLock);
		if (bShuttingDown)
		{
			SubmitResult.Error = TEXT("job manager is shutting down");
			return SubmitResult;
		}

		CleanupLocked(Now);

		if (!IdempotencyKey.IsEmpty())
		{
			if (const FString* ExistingId = IdempotencyToJobId.Find(IdempotencyKey))
			{
				if (const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>* Existing = Jobs.Find(*ExistingId))
				{
					if ((*Existing)->ScriptHash != ScriptHash)
					{
						SubmitResult.Error = FString::Printf(
							TEXT("idempotency key '%s' was already used with a different script"),
							*IdempotencyKey);
						return SubmitResult;
					}
					++TotalDeduplicated;
					SubmitResult.Job = *Existing;
					SubmitResult.bDeduplicated = true;
					return SubmitResult;
				}
				IdempotencyToJobId.Remove(IdempotencyKey);
			}
		}

		TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Job = MakeShared<FBridgeJob, ESPMode::ThreadSafe>();
		Job->JobId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		Job->TraceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower);
		Job->RequestId = RequestId;
		Job->Script = Script;
		Job->ScriptHash = ScriptHash;
		Job->IdempotencyKey = IdempotencyKey;
		Job->CreatedSeconds = Now;
		Job->DeadlineSeconds = Now + FMath::Max(0.01, QueueDeadlineSecondsFromNow);
		Job->PollScript = PollScript;
		Job->PollIntervalSeconds = FMath::Clamp(PollIntervalSeconds, 0.0, 60.0);
		Job->RunTimeoutSeconds = FMath::Clamp(RunTimeoutSeconds, 0.1, 86400.0);

		Jobs.Add(Job->JobId, Job);
		if (!IdempotencyKey.IsEmpty())
		{
			IdempotencyToJobId.Add(IdempotencyKey, Job->JobId);
		}
		++TotalSubmitted;
		SubmitResult.Job = Job;
	}

	Queue.Enqueue(SubmitResult.Job);
	QueueDepth.Increment();
	return SubmitResult;
}

TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> FBridgeJobManager::ClaimNextJob()
{
	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Job;
	while (Queue.Dequeue(Job))
	{
		QueueDepth.Decrement();
		if (!Job.IsValid())
		{
			continue;
		}

		FScopeLock Lock(&JobsLock);
		if (Job->State != EBridgeJobState::Queued)
		{
			continue;
		}

		const double Now = FPlatformTime::Seconds();
		if (bShuttingDown)
		{
			FinishLocked(Job, EBridgeJobState::Aborted,
				MakeLifecycleError(TEXT("server shutting down"), TEXT("SERVER_SHUTDOWN"), TEXT("queue")));
			continue;
		}
		if (Job->bCancelRequested)
		{
			FinishLocked(Job, EBridgeJobState::Cancelled,
				MakeLifecycleError(TEXT("job cancelled before execution"), TEXT("JOB_CANCELLED"), TEXT("queue")));
			continue;
		}
		if (Now >= Job->DeadlineSeconds)
		{
			FinishLocked(Job, EBridgeJobState::Expired,
				MakeLifecycleError(TEXT("job queue deadline expired before execution"), TEXT("QUEUE_DEADLINE_EXCEEDED"), TEXT("queue")));
			continue;
		}

		Job->State = EBridgeJobState::Running;
		Job->StartedSeconds = Now;
		Job->RunDeadlineSeconds = Now + Job->RunTimeoutSeconds;
		Job->bStepInFlight = true;
		++Job->StepCount;
		RunningJobId = Job->JobId;
		return Job;
	}

	// Polling Jobs remain Running between short steps. Scan the bounded result
	// store for one due poll instead of occupying the normal submission queue.
	{
		FScopeLock Lock(&JobsLock);
		const double Now = FPlatformTime::Seconds();
		for (const TPair<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>>& Pair : Jobs)
		{
			Job = Pair.Value;
			if (!Job.IsValid() || Job->State != EBridgeJobState::Running
				|| !Job->IsPolling() || Job->bStepInFlight)
			{
				continue;
			}

			if (Job->bCancelRequested)
			{
				FinishLocked(Job, EBridgeJobState::Cancelled,
					MakeLifecycleError(TEXT("polling job cancelled"), TEXT("JOB_CANCELLED"), TEXT("poll"), TEXT("unknown")));
				continue;
			}
			if (Now >= Job->RunDeadlineSeconds)
			{
				FinishLocked(Job, EBridgeJobState::Failed,
					MakeLifecycleError(TEXT("polling job run deadline exceeded"), TEXT("RUN_DEADLINE_EXCEEDED"), TEXT("poll"), TEXT("unknown")));
				continue;
			}
			if (Now < Job->NextPollSeconds)
			{
				continue;
			}

			Job->bStepInFlight = true;
			++Job->StepCount;
			RunningJobId = Job->JobId;
			return Job;
		}
	}

	return nullptr;
}

void FBridgeJobManager::Complete(
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job,
	FBridgeJobResult&& Result)
{
	if (!Job.IsValid())
	{
		return;
	}

	FScopeLock Lock(&JobsLock);
	if (Job->State != EBridgeJobState::Running)
	{
		return;
	}

	Job->bStepInFlight = false;
	const EBridgeJobState FinalState = Result.bSuccess
		? EBridgeJobState::Succeeded
		: EBridgeJobState::Failed;
	FinishLocked(Job, FinalState, MoveTemp(Result));
	if (RunningJobId == Job->JobId)
	{
		RunningJobId.Reset();
	}
}

void FBridgeJobManager::CompletePollingStep(
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job,
	FBridgeJobResult&& Result,
	bool bPollComplete)
{
	if (!Job.IsValid())
	{
		return;
	}

	FScopeLock Lock(&JobsLock);
	if (Job->State != EBridgeJobState::Running)
	{
		return;
	}

	Job->bStepInFlight = false;
	if (RunningJobId == Job->JobId)
	{
		RunningJobId.Reset();
	}

	if (Job->bCancelRequested)
	{
		Result.bSuccess = false;
		Result.Error = TEXT("polling job cancelled");
		Result.ErrorCode = TEXT("JOB_CANCELLED");
		Result.Phase = TEXT("poll");
		FinishLocked(Job, EBridgeJobState::Cancelled, MoveTemp(Result));
		return;
	}
	if (!Result.bSuccess)
	{
		FinishLocked(Job, EBridgeJobState::Failed, MoveTemp(Result));
		return;
	}

	const double Now = FPlatformTime::Seconds();
	if (!Job->bStartStepCompleted)
	{
		Job->bStartStepCompleted = true;
		Job->Result = MoveTemp(Result);
		Job->Result.Phase = TEXT("poll_wait");
		Job->NextPollSeconds = Now + Job->PollIntervalSeconds;
		return;
	}

	if (bPollComplete)
	{
		Result.Phase = TEXT("poll_complete");
		FinishLocked(Job, EBridgeJobState::Succeeded, MoveTemp(Result));
		return;
	}
	if (Now >= Job->RunDeadlineSeconds)
	{
		FinishLocked(Job, EBridgeJobState::Failed,
			MakeLifecycleError(TEXT("polling job run deadline exceeded"), TEXT("RUN_DEADLINE_EXCEEDED"), TEXT("poll"), TEXT("unknown")));
		return;
	}

	Job->Result = MoveTemp(Result);
	Job->Result.Phase = TEXT("poll_wait");
	Job->NextPollSeconds = Now + Job->PollIntervalSeconds;
}

bool FBridgeJobManager::Cancel(
	const FString& JobId,
	FBridgeJobSnapshot& OutSnapshot,
	FString& OutError)
{
	FScopeLock Lock(&JobsLock);
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>* Found = Jobs.Find(JobId);
	if (!Found || !Found->IsValid())
	{
		OutError = FString::Printf(TEXT("unknown job '%s'"), *JobId);
		return false;
	}

	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job = *Found;
	if (Job->State == EBridgeJobState::Queued)
	{
		Job->bCancelRequested = true;
		FinishLocked(Job, EBridgeJobState::Cancelled,
			MakeLifecycleError(TEXT("job cancelled before execution"), TEXT("JOB_CANCELLED"), TEXT("queue")));
	}
	else if (Job->State == EBridgeJobState::Running)
	{
		Job->bCancelRequested = true;
		if (Job->IsPolling() && !Job->bStepInFlight)
		{
			FinishLocked(Job, EBridgeJobState::Cancelled,
				MakeLifecycleError(TEXT("polling job cancelled"), TEXT("JOB_CANCELLED"), TEXT("poll"), TEXT("unknown")));
		}
	}

	OutSnapshot = MakeSnapshotLocked(Job);
	return true;
}

bool FBridgeJobManager::Wait(
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job,
	double WaitSeconds) const
{
	if (!Job.IsValid() || !Job->CompletionEvent)
	{
		return false;
	}
	if (WaitSeconds <= 0.0)
	{
		FScopeLock Lock(&JobsLock);
		return IsBridgeJobTerminal(Job->State);
	}
	return Job->CompletionEvent->Wait(FTimespan::FromSeconds(WaitSeconds));
}

bool FBridgeJobManager::WaitForJob(
	const FString& JobId,
	double WaitSeconds,
	FBridgeJobSnapshot& OutSnapshot) const
{
	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Job;
	{
		FScopeLock Lock(&JobsLock);
		const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>* Found = Jobs.Find(JobId);
		if (!Found || !Found->IsValid())
		{
			return false;
		}
		Job = *Found;
	}
	Wait(Job, WaitSeconds);
	return GetSnapshot(JobId, OutSnapshot);
}

bool FBridgeJobManager::GetSnapshot(const FString& JobId, FBridgeJobSnapshot& OutSnapshot) const
{
	FScopeLock Lock(&JobsLock);
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>* Found = Jobs.Find(JobId);
	if (!Found || !Found->IsValid())
	{
		return false;
	}
	OutSnapshot = MakeSnapshotLocked(*Found);
	return true;
}

TArray<FBridgeJobSnapshot> FBridgeJobManager::ListSnapshots(int32 Limit) const
{
	TArray<FBridgeJobSnapshot> Result;
	FScopeLock Lock(&JobsLock);
	Result.Reserve(FMath::Min(Limit, Jobs.Num()));
	for (const TPair<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>>& Pair : Jobs)
	{
		if (Pair.Value.IsValid())
		{
			Result.Add(MakeSnapshotLocked(Pair.Value));
		}
	}
	Result.Sort([](const FBridgeJobSnapshot& A, const FBridgeJobSnapshot& B)
	{
		return A.CreatedSeconds > B.CreatedSeconds;
	});
	if (Result.Num() > Limit)
	{
		Result.SetNum(Limit);
	}
	return Result;
}

FBridgeJobMetrics FBridgeJobManager::GetMetrics() const
{
	FBridgeJobMetrics Metrics;
	Metrics.QueueDepth = QueueDepth.GetValue();
	const double Now = FPlatformTime::Seconds();

	FScopeLock Lock(&JobsLock);
	Metrics.TrackedJobs = Jobs.Num();
	Metrics.TotalSubmitted = TotalSubmitted;
	Metrics.TotalDeduplicated = TotalDeduplicated;
	Metrics.TotalSucceeded = TotalSucceeded;
	Metrics.TotalFailed = TotalFailed;
	Metrics.TotalCancelled = TotalCancelled;
	Metrics.TotalExpired = TotalExpired;
	Metrics.TotalAborted = TotalAborted;

	double OldestQueued = 0.0;
	double OldestRunningStart = TNumericLimits<double>::Max();
	for (const TPair<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>>& Pair : Jobs)
	{
		const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job = Pair.Value;
		if (!Job.IsValid())
		{
			continue;
		}
		if (Job->State == EBridgeJobState::Queued)
		{
			OldestQueued = FMath::Max(OldestQueued, Now - Job->CreatedSeconds);
		}
		if (Job->State == EBridgeJobState::Running && Job->StartedSeconds > 0.0)
		{
			if (Job->StartedSeconds < OldestRunningStart)
			{
				OldestRunningStart = Job->StartedSeconds;
				Metrics.RunningJobId = Job->JobId;
				Metrics.RunningMilliseconds = (Now - Job->StartedSeconds) * 1000.0;
			}
		}
	}
	Metrics.OldestQueuedMilliseconds = OldestQueued * 1000.0;
	return Metrics;
}

void FBridgeJobManager::Shutdown()
{
	TArray<FEvent*> EventsToTrigger;
	{
		FScopeLock Lock(&JobsLock);
		if (bShuttingDown)
		{
			return;
		}
		bShuttingDown = true;
		for (const TPair<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>>& Pair : Jobs)
		{
			const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job = Pair.Value;
			if (Job.IsValid() && !IsBridgeJobTerminal(Job->State))
			{
				FinishLocked(Job, EBridgeJobState::Aborted,
					MakeLifecycleError(TEXT("server shutting down"), TEXT("SERVER_SHUTDOWN"), TEXT("shutdown"), TEXT("unknown")));
			}
		}
		RunningJobId.Reset();
	}

	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Discarded;
	while (Queue.Dequeue(Discarded))
	{
		QueueDepth.Decrement();
	}
}

FBridgeJobSnapshot FBridgeJobManager::MakeSnapshotLocked(
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job) const
{
	FBridgeJobSnapshot Snapshot;
	Snapshot.JobId = Job->JobId;
	Snapshot.TraceId = Job->TraceId;
	Snapshot.RequestId = Job->RequestId;
	Snapshot.IdempotencyKey = Job->IdempotencyKey;
	Snapshot.State = Job->State;
	Snapshot.bCancelRequested = Job->bCancelRequested;
	Snapshot.bPolling = Job->IsPolling();
	Snapshot.StepCount = Job->StepCount;
	Snapshot.CreatedSeconds = Job->CreatedSeconds;
	Snapshot.DeadlineSeconds = Job->DeadlineSeconds;
	Snapshot.RunDeadlineSeconds = Job->RunDeadlineSeconds;
	Snapshot.NextPollSeconds = Job->NextPollSeconds;
	Snapshot.StartedSeconds = Job->StartedSeconds;
	Snapshot.FinishedSeconds = Job->FinishedSeconds;
	Snapshot.Result = Job->Result;
	return Snapshot;
}

void FBridgeJobManager::FinishLocked(
	const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job,
	EBridgeJobState State,
	FBridgeJobResult&& Result)
{
	Job->State = State;
	Job->bStepInFlight = false;
	Job->FinishedSeconds = FPlatformTime::Seconds();
	Job->Result = MoveTemp(Result);
	if (RunningJobId == Job->JobId)
	{
		RunningJobId.Reset();
	}

	switch (State)
	{
	case EBridgeJobState::Succeeded: ++TotalSucceeded; break;
	case EBridgeJobState::Failed: ++TotalFailed; break;
	case EBridgeJobState::Cancelled: ++TotalCancelled; break;
	case EBridgeJobState::Expired: ++TotalExpired; break;
	case EBridgeJobState::Aborted: ++TotalAborted; break;
	default: break;
	}

	if (Job->CompletionEvent)
	{
		Job->CompletionEvent->Trigger();
	}
}

void FBridgeJobManager::CleanupLocked(double NowSeconds)
{
	if (Jobs.Num() <= MaxTrackedJobs)
	{
		bool bHasExpiredRecord = false;
		for (const TPair<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>>& Pair : Jobs)
		{
			const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job = Pair.Value;
			if (Job.IsValid() && IsBridgeJobTerminal(Job->State)
				&& Job->FinishedSeconds > 0.0
				&& NowSeconds - Job->FinishedSeconds > CompletedJobTtlSeconds)
			{
				bHasExpiredRecord = true;
				break;
			}
		}
		if (!bHasExpiredRecord)
		{
			return;
		}
	}

	TArray<TPair<double, FString>> Removable;
	for (const TPair<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>>& Pair : Jobs)
	{
		const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job = Pair.Value;
		if (Job.IsValid() && IsBridgeJobTerminal(Job->State))
		{
			Removable.Emplace(Job->FinishedSeconds, Pair.Key);
		}
	}
	Removable.Sort([](const TPair<double, FString>& A, const TPair<double, FString>& B)
	{
		return A.Key < B.Key;
	});

	for (const TPair<double, FString>& Candidate : Removable)
	{
		const bool bExpired = Candidate.Key > 0.0
			&& NowSeconds - Candidate.Key > CompletedJobTtlSeconds;
		if (!bExpired && Jobs.Num() <= MaxTrackedJobs)
		{
			break;
		}
		if (const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>* Job = Jobs.Find(Candidate.Value))
		{
			if (Job->IsValid() && !(*Job)->IdempotencyKey.IsEmpty())
			{
				IdempotencyToJobId.Remove((*Job)->IdempotencyKey);
			}
		}
		Jobs.Remove(Candidate.Value);
	}
}
