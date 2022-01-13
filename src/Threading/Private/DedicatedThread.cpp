#include "Threading/DedicatedThread.h"

namespace {
	void DedicatedThreadProc(DedicatedThreadData *DedicatedThread)
	{
		while (true)
		{
			std::unique_lock<LockableBase(Mutex)> Lock(DedicatedThread->ItemsLock);
			DedicatedThread->WakeUp.wait(Lock, [DedicatedThread]() { return DedicatedThread->ThreadShouldStop || !DedicatedThread->WorkItems.empty(); });
			if (DedicatedThread->ThreadShouldStop)
			{
				return;
			}

			eastl::function<void(void)> CurrentWorkItem = std::move(DedicatedThread->WorkItems.front());
			DedicatedThread->WorkItems.pop();
			Lock.unlock();
			CurrentWorkItem();
		}
	}
}

std::thread StartDedicatedThread(DedicatedThreadData* DedicatedThread)
{
	return std::thread(DedicatedThreadProc, DedicatedThread);
}

void StopDedicatedThread(DedicatedThreadData* DedicatedThread)
{
	DedicatedThread->ThreadShouldStop = true;
	DedicatedThread->WakeUp.notify_all();
}

void EnqueueWork(DedicatedThreadData *DedicatedThread, eastl::function<void(void)> Work)
{
	ZoneScoped;
	std::lock_guard AutoLock(DedicatedThread->ItemsLock);
	DedicatedThread->WorkItems.push(Work);
	DedicatedThread->WakeUp.notify_one();
}

