#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Containers/Map.h"
#include "Threading/Thread.h"
#include "Util/Util.h"
#include <windows.h>
#include <unordered_set>
#include <Util/Debug.h>

DISABLE_OPTIMIZATION

std::atomic<u16> gCurrentTicketId;
std::atomic<u64> gSharedTickets[1024];

//TracyLockable(Mutex, gTicketsLock);
//std::unordered_set<uint64_t> gDedicatedThreadData;

namespace {
	bool ThreadShouldExit(DedicatedThreadData *DedicatedThread)
	{
		return DedicatedThread->ThreadShouldStop && DedicatedThread->WorkItems.empty();
	}

	void PopAndExecute(DedicatedThreadData *DedicatedThread)
	{
		UniqueMovableLock Lock(DedicatedThread->ItemsLock);
		while (true)
		{
			bool HasWork = DedicatedThread->WakeUp->wait_for(Lock, std::chrono::microseconds(DedicatedThread->SleepMicrosecondsWhenIdle), [DedicatedThread]() {
				return DedicatedThread->ThreadShouldStop || !DedicatedThread->WorkItems.empty();
			});

			if (DedicatedThread->ThreadShouldStop || HasWork)
			{
				break;
			}

			Lock.unlock();
			StealWork();
			Lock.lock();
		}

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
		tracy::SetThreadName(ThreadName.c_str());
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

	u64 TicketID = Item.WorkDoneTicket.Value;
	u64 Shift = TicketID % 64;
	u64 Mask = (1ULL << Shift);

	u64 Test = (gSharedTickets[TicketID / 64].load() & Mask);
	CHECK(Test == Mask, "Ticket already cleared?!");

	gSharedTickets[TicketID / 64] &= ~(Mask);
	//ScopedLock AutoLock(gTicketsLock);
	//CHECK(gDedicatedThreadData.erase(Item.WorkDoneTicket.Value) > 0, "didn't find the value");
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
	u64 Shift = WorkDoneTicket.Value % 64;
	u64 Mask = (1ULL << Shift);
	return (gSharedTickets[WorkDoneTicket.Value / 64] & Mask) == 0;
	//ScopedLock AutoLock(gTicketsLock);
	//return gDedicatedThreadData.find(WorkDoneTicket.Value) == gDedicatedThreadData.end();
}

void WaitForCompletion(Ticket WorkDoneTicket)
{
	while (!WorkIsDone(WorkDoneTicket))
	{
		if (!StealWork())
		{
			//std::this_thread::sleep_for(std::chrono::microseconds(10));
		}
	}
}

Ticket EnqueueWork(DedicatedThreadData *DedicatedThread, TFunction<void(void)>&& Work)
{
	ZoneScoped;
	uint64_t TicketID = -1;
	{
		TicketID = gCurrentTicketId++;

		u64 Shift = TicketID % 64;
		u64 Mask = (1ULL << Shift);
		CHECK((gSharedTickets[TicketID / 64] & Mask) == 0, "Ticket already set?!");
		gSharedTickets[TicketID / 64] |= (Mask);

		//ScopedLock TicketAutoLock(gTicketsLock);
		//gDedicatedThreadData.emplace(TicketID);
	}
	{
		ScopedMovableLock ItemsAutoLock(DedicatedThread->ItemsLock);
		DedicatedThread->WorkItems.push({ MOVE(Work), Ticket {TicketID} });
		DedicatedThread->WakeUp->notify_one();
	}

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

