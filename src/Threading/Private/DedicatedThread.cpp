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

	void PopAndExecute(DedicatedThreadData *DedicatedThread)
	{
		UniqueMovableLock Lock(DedicatedThread->ItemsLock);
		DedicatedThread->WakeUp->wait(Lock, [DedicatedThread]() {
			return DedicatedThread->ThreadShouldStop || !DedicatedThread->WorkItems.empty();
		});

		if (DedicatedThread->WorkItems.empty())
		{
			return;
		}

		WorkItem Item = MOVE(DedicatedThread->WorkItems.front());
		DedicatedThread->WorkItems.pop();
		Lock.unlock();
		ExecuteItem(Item);
	}

	void DedicatedThreadProc(DedicatedThreadData *DedicatedThread, String ThreadName)
	{
		tracy::SetThreadName(ThreadName.data());
		while (!DedicatedThread->ThreadShouldStop)
		{
			PopAndExecute(DedicatedThread);
		}
	}
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

void StartDedicatedThread(DedicatedThreadData* DedicatedThread, const String& ThreadName)
{
	DedicatedThread->ActualThread = Thread(DedicatedThreadProc, DedicatedThread, ThreadName);
}

void StopDedicatedThread(DedicatedThreadData* DedicatedThread)
{
	DedicatedThread->ThreadShouldStop = true;
	DedicatedThread->WakeUp->notify_all();
	if (DedicatedThread->ActualThread.joinable())
		DedicatedThread->ActualThread.join();
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
		if (!StealWork())
		{
			//::SleepEx(1, true);
		}
	}
}

Ticket EnqueueWork(DedicatedThreadData *DedicatedThread, TFunction<void(void)>&& Work)
{
	ZoneScoped;
	uint64_t TicketID = -1;
	{
		ScopedLock TicketAutoLock(gTicketsLock);
		TicketID = gCurrentTicketId.fetch_add(1);
	}
	{
		ScopedMovableLock ItemsAutoLock(DedicatedThread->ItemsLock);
		DedicatedThread->WorkItems.push({ MOVE(Work), Ticket {TicketID} });
	}
	DedicatedThread->WakeUp->notify_one();

	ScopedLock TicketAutoLock(gTicketsLock);
	gDedicatedThreadData.emplace(TicketID);
	return Ticket{ TicketID };
}

void   ExecutePendingWork(DedicatedThreadData* DedicatedThread)
{
	while(!DedicatedThread->WorkItems.empty())
	{
		UniqueMovableLock ItemsAutoLock(DedicatedThread->ItemsLock);
		WorkItem Work = MOVE(DedicatedThread->WorkItems.front());
		DedicatedThread->WorkItems.pop();
		ItemsAutoLock.unlock();
		ExecuteItem(Work);
	}
}

