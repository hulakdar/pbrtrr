#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Containers/Map.h"
#include "Threading/Thread.h"
#include "Util/Util.h"
#include <windows.h>
#include <unordered_set>
#include <Util/Debug.h>

DISABLE_OPTIMIZATION

std::atomic<u8>  gCurrentTicketId;
std::atomic<u64> gSharedTickets[4];

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
			bool HasWork = DedicatedThread->WakeUp->wait_for(Lock, std::chrono::microseconds(DedicatedThread->SleepMicrosecondsWhenIdle),
				[DedicatedThread]() {
					return DedicatedThread->ThreadShouldStop || !DedicatedThread->WorkItems.empty();
				}
			);

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

	void DedicatedThreadProc(DedicatedThreadData* DedicatedThread, String ThreadName)
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

	if (Item.TicketValid)
	{
		u64 TicketID = Item.WorkDoneTicket.Value;
		u64 Shift = TicketID % 64;
		u64 Mask = (1ULL << Shift);

		u64 Test = (gSharedTickets[TicketID / 64].load() & Mask);
		CHECK(Test == Mask, "Ticket already cleared?!");

		gSharedTickets[TicketID / 64] &= ~(Mask);
	}
}

void StartDedicatedThread(DedicatedThreadData* DedicatedThread, const String& ThreadName, u64 AffinityMask)
{
	DedicatedThread->ActualThread = Thread(DedicatedThreadProc, DedicatedThread, ThreadName);
	auto handle = DedicatedThread->ActualThread.native_handle();
	SetThreadAffinityMask(handle, AffinityMask);
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

void EnqueueWork(DedicatedThreadData* DedicatedThread, TFunction<void(void)>&& Work)
{
	ScopedMovableLock ItemsAutoLock(DedicatedThread->ItemsLock);
	DedicatedThread->WorkItems.push({ MOVE(Work), Ticket {42}, false });
	DedicatedThread->WakeUp->notify_one();
}

Ticket EnqueueWorkWithTicket(DedicatedThreadData* DedicatedThread, TFunction<void(void)>&& Work)
{
	ZoneScoped;
	u8 TicketID = 0;

	while (true)
	{
		TicketID = gCurrentTicketId++;

		u64 Shift = TicketID % 64;
		u64 Mask = (1ULL << Shift);
		if ((gSharedTickets[TicketID / 64] & Mask) == 0)
		{
			gSharedTickets[TicketID / 64] |= (Mask);
			break;
		}
	}

	{
		ScopedMovableLock ItemsAutoLock(DedicatedThread->ItemsLock);
		DedicatedThread->WorkItems.push({ MOVE(Work), Ticket {TicketID}, true });
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

