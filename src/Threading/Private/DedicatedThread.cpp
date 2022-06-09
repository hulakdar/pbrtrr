#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Containers/Map.h"
#include "Threading/Thread.h"
#include "Util/Util.h"
#include <windows.h>
#include <unordered_set>

TracyLockable(Mutex, gTicketsLock);
std::atomic<uint64_t> gCurrentTicketId;

TracyLockable(Mutex, gTicketsWakeupLock);
std::unordered_set<uint64_t> gDedicatedThreadData;

namespace {
	bool ThreadShouldExit(DedicatedThreadData *DedicatedThread)
	{
		return DedicatedThread->ThreadShouldStop && DedicatedThread->WorkItems.empty();
	}

	void ExecuteItem(WorkItem& Item)
	{
		{
			ZoneScopedN("Threaded work item");
			Item.Work();
		}
		ScopedLock AutoLock(gTicketsLock);
		gDedicatedThreadData.erase(Item.WorkDoneTicket.Value);
	}

	void PopAndExecute(DedicatedThreadData *DedicatedThread)
	{
		UniqueLock Lock(DedicatedThread->ItemsLock);
		DedicatedThread->WakeUp.wait(Lock, [DedicatedThread]() { return DedicatedThread->ThreadShouldStop || !DedicatedThread->WorkItems.empty(); });
		if (ThreadShouldExit(DedicatedThread))
		{
			return;
		}

		WorkItem Item = MOVE(DedicatedThread->WorkItems.front());
		DedicatedThread->WorkItems.pop();
		Lock.unlock();
		ExecuteItem(Item);
	}

	void DedicatedThreadProc(DedicatedThreadData *DedicatedThread, WString ThreadName)
	{
		SetThreadDescription(GetCurrentThread(), ThreadName.c_str());
		while (!DedicatedThread->ThreadShouldStop)
		{
			PopAndExecute(DedicatedThread);
		}
	}
}

bool TryPopAndExecute(DedicatedThreadData* DedicatedThread)
{
	UniqueLock Lock(DedicatedThread->ItemsLock);
	if (DedicatedThread->WorkItems.empty())
		return false;
	WorkItem Item = MOVE(DedicatedThread->WorkItems.front());
	DedicatedThread->WorkItems.pop();
	Lock.unlock();
	ExecuteItem(Item);
	return true;
}

Thread StartDedicatedThread(DedicatedThreadData* DedicatedThread, WString ThreadName)
{
	return Thread(DedicatedThreadProc, DedicatedThread, ThreadName);
}

void StopDedicatedThread(DedicatedThreadData* DedicatedThread)
{
	DedicatedThread->ThreadShouldStop = true;
	DedicatedThread->WakeUp.notify_all();
}

bool WorkIsDone(Ticket WorkDoneTicket)
{
	ScopedLock AutoLock(gTicketsLock);
	return gDedicatedThreadData.find(WorkDoneTicket.Value) == gDedicatedThreadData.end();
}

void WaitForCompletion(Ticket WorkDoneTicket)
{
	while (!WorkIsDone(WorkDoneTicket))
	{
		//ZoneScoped;
		if (!TryPopAndExecute(GetWorkerDedicatedThreadData()))
		{
			//ZoneScopedN("Sleep 1ms");
			//Sleep(1);
		}
	}
}

Ticket EnqueueWork(DedicatedThreadData *DedicatedThread, const TFunction<void(void)>& Work)
{
	ZoneScoped;
	ScopedLock TicketAutoLock(gTicketsLock);
	uint64_t TicketID = gCurrentTicketId.fetch_add(1);
	{
		ScopedLock ItemsAutoLock(DedicatedThread->ItemsLock);
		DedicatedThread->WorkItems.push({ MOVE(Work), Ticket {TicketID} });
	}
	DedicatedThread->WakeUp.notify_one();
	gDedicatedThreadData.emplace(TicketID);
	return Ticket{ TicketID };
}

