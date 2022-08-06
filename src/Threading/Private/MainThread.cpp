#include "Threading/MainThread.h"
#include "Threading/Worker.h"
#include "Render/RenderThread.h"
#include "Containers/Array.h"
#include "Util/Util.h"

DedicatedThreadData *gMainThreadData = new DedicatedThreadData();

void   InitializeMainThreadWork()
{
    gMainThreadData->ThreadName = "MainThread";
}

void EnqueueToMainThread(TFunction<void(void)>&& WorkItem)
{
    EnqueueWork(gMainThreadData, MOVE(WorkItem));
}

void   ExecuteMainThreadWork()
{
    ExecutePendingWork(gMainThreadData);
}

struct DelayedWork
{
	u64 SafeFenceValue;
	TFunction<void(void)> Work;
	TargetThread Thread;
};

TQueue<DelayedWork> gDelayedWork;
TracyLockable(Mutex, gDelayedWorkLock);
//Mutex gDelayedWorkLock;

void EnqueueDelayedWork(TFunction<void(void)>&& Work, u64 SafeFenceValue, TargetThread Thread)
{
	ScopedLock AutoLock(gDelayedWorkLock);

	gDelayedWork.push(
		DelayedWork {
			SafeFenceValue,
			MOVE(Work),
			Thread
		}
	);
}

void CheckForDelayedWork(u64 CurrentFenceValue)
{
	if (!gDelayedWork.empty())
	{
		TArray<DelayedWork> Work;
		{
			ScopedLock AutoLock(gDelayedWorkLock);
			while (!gDelayedWork.empty() && gDelayedWork.front().SafeFenceValue < CurrentFenceValue)
			{
				Work.push_back(MOVE(gDelayedWork.front()));
				gDelayedWork.pop();
			}
		}
		for (auto& Item : Work)
		{
			switch (Item.Thread)
			{
			break; case TargetThread::WorkerThread: EnqueueToWorker(MOVE(Item.Work));
			break; case TargetThread::MainThread: EnqueueToMainThread(MOVE(Item.Work));
			break; case TargetThread::RenderThread: EnqueueToRenderThread(MOVE(Item.Work));
			}
		}
	}
}

