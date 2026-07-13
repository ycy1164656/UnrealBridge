#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "HAL/Event.h"
#include "HAL/ThreadSafeCounter.h"

enum class EBridgeJobState : uint8
{
	Queued,
	Running,
	Succeeded,
	Failed,
	Cancelled,
	Expired,
	Aborted,
};

UNREALBRIDGE_API const TCHAR* LexToString(EBridgeJobState State);
UNREALBRIDGE_API bool IsBridgeJobTerminal(EBridgeJobState State);

struct FBridgeJobResult
{
	bool bSuccess = false;
	FString Output;
	FString Error;
	FString ErrorCode;
	FString Phase = TEXT("execute");
	bool bRetryable = false;
	FString SideEffectState = TEXT("unknown");
};

struct FBridgeJobSnapshot
{
	FString JobId;
	FString TraceId;
	FString RequestId;
	FString IdempotencyKey;
	EBridgeJobState State = EBridgeJobState::Queued;
	bool bCancelRequested = false;
	bool bPolling = false;
	int32 StepCount = 0;
	double CreatedSeconds = 0.0;
	double DeadlineSeconds = 0.0;
	double RunDeadlineSeconds = 0.0;
	double NextPollSeconds = 0.0;
	double StartedSeconds = 0.0;
	double FinishedSeconds = 0.0;
	FBridgeJobResult Result;
};

struct FBridgeJobMetrics
{
	int32 QueueDepth = 0;
	int32 TrackedJobs = 0;
	int64 TotalSubmitted = 0;
	int64 TotalDeduplicated = 0;
	int64 TotalSucceeded = 0;
	int64 TotalFailed = 0;
	int64 TotalCancelled = 0;
	int64 TotalExpired = 0;
	int64 TotalAborted = 0;
	FString RunningJobId;
	double RunningMilliseconds = 0.0;
	double OldestQueuedMilliseconds = 0.0;
};

class FBridgeJob : public TSharedFromThis<FBridgeJob, ESPMode::ThreadSafe>
{
public:
	FBridgeJob();
	~FBridgeJob();

	FString JobId;
	FString TraceId;
	FString RequestId;
	FString Script;
	FString ScriptHash;
	FString IdempotencyKey;
	double CreatedSeconds = 0.0;
	double DeadlineSeconds = 0.0;

	bool IsPolling() const { return !PollScript.IsEmpty(); }
	bool IsPollStep() const { return bStartStepCompleted; }
	const FString& GetExecutableScript() const { return bStartStepCompleted ? PollScript : Script; }

private:
	friend class FBridgeJobManager;

	EBridgeJobState State = EBridgeJobState::Queued;
	bool bCancelRequested = false;
	bool bStartStepCompleted = false;
	bool bStepInFlight = false;
	FString PollScript;
	double PollIntervalSeconds = 0.25;
	double RunTimeoutSeconds = 300.0;
	double RunDeadlineSeconds = 0.0;
	double NextPollSeconds = 0.0;
	int32 StepCount = 0;
	double StartedSeconds = 0.0;
	double FinishedSeconds = 0.0;
	FBridgeJobResult Result;
	FEvent* CompletionEvent = nullptr;
};

struct FBridgeJobSubmitResult
{
	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> Job;
	bool bDeduplicated = false;
	FString Error;
};

/**
 * Thread-safe lifecycle and result store for bridge Python jobs.
 *
 * Producers may submit and wait from socket/HTTP worker threads. The editor
 * GameThread claims at most one short step per tick. Polling jobs yield between
 * steps so Editor and transport ticks keep running. Queue deadlines are checked before a job is
 * allowed to execute, so a timed-out queued request can never mutate the editor
 * later.
 */
class UNREALBRIDGE_API FBridgeJobManager
{
public:
	FBridgeJobManager() = default;
	~FBridgeJobManager();

	FBridgeJobSubmitResult Submit(
		const FString& Script,
		const FString& RequestId,
		double QueueDeadlineSecondsFromNow,
		const FString& IdempotencyKey = FString(),
		const FString& PollScript = FString(),
		double PollIntervalSeconds = 0.25,
		double RunTimeoutSeconds = 300.0);

	/** Claim the next executable job and atomically move it to Running. */
	TSharedPtr<FBridgeJob, ESPMode::ThreadSafe> ClaimNextJob();

	/** Complete a job previously returned by ClaimNextJob. */
	void Complete(const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job, FBridgeJobResult&& Result);

	/** Complete one short step while keeping an incomplete polling Job Running. */
	void CompletePollingStep(
		const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job,
		FBridgeJobResult&& Result,
		bool bPollComplete);

	/** Cancel queued jobs immediately; running jobs receive a cooperative cancellation request. */
	bool Cancel(const FString& JobId, FBridgeJobSnapshot& OutSnapshot, FString& OutError);

	/** Wait only for the caller's requested interval; the Job lifetime is independent. */
	bool Wait(const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job, double WaitSeconds) const;
	bool WaitForJob(const FString& JobId, double WaitSeconds, FBridgeJobSnapshot& OutSnapshot) const;

	bool GetSnapshot(const FString& JobId, FBridgeJobSnapshot& OutSnapshot) const;
	TArray<FBridgeJobSnapshot> ListSnapshots(int32 Limit = 50) const;
	FBridgeJobMetrics GetMetrics() const;
	void Shutdown();

private:
	FBridgeJobSnapshot MakeSnapshotLocked(const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job) const;
	void FinishLocked(
		const TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>& Job,
		EBridgeJobState State,
		FBridgeJobResult&& Result);
	void CleanupLocked(double NowSeconds);

	mutable FCriticalSection JobsLock;
	TMap<FString, TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>> Jobs;
	TMap<FString, FString> IdempotencyToJobId;
	TQueue<TSharedPtr<FBridgeJob, ESPMode::ThreadSafe>, EQueueMode::Mpsc> Queue;
	FThreadSafeCounter QueueDepth;
	FString RunningJobId;

	int64 TotalSubmitted = 0;
	int64 TotalDeduplicated = 0;
	int64 TotalSucceeded = 0;
	int64 TotalFailed = 0;
	int64 TotalCancelled = 0;
	int64 TotalExpired = 0;
	int64 TotalAborted = 0;
	bool bShuttingDown = false;

	static constexpr int32 MaxTrackedJobs = 512;
	static constexpr double CompletedJobTtlSeconds = 15.0 * 60.0;
};
