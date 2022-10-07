#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Containers/Map.h"
#include "Threading/Thread.h"
#include "Util/Util.h"
#include <windows.h>
#include <unordered_set>
#include <Util/Debug.h>
#include <limits>
#include <array>

using TicketType = decltype(TicketCPU().Value);
const u64 NumBitsForTickets = std::numeric_limits<TicketType>::max() + 1;
const u64 NumBitsInShared = 64;

std::atomic<TicketType> gCurrentTicketId;
std::array<std::atomic<u64>, NumBitsForTickets / NumBitsInShared> gSharedTickets;

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

			if (DedicatedThread->ThreadShouldStealWork)
			{
				Lock.unlock();
				StealWork();
				Lock.lock();
			}
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

	void DedicatedThreadProc(DedicatedThreadData* DedicatedThread)
	{
		tracy::SetThreadName(DedicatedThread->ThreadName.c_str());
		while (!DedicatedThread->ThreadShouldStop)
		{
			PopAndExecute(DedicatedThread);
		}
	}
}

Thread::id GetCurrentThreadID()
{
	return std::this_thread::get_id();
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
		u64 Shift = TicketID % NumBitsInShared;
		u64 Mask = (1ULL << Shift);

		u64 Test = (gSharedTickets[TicketID / NumBitsInShared].load(std::memory_order_acquire) & Mask);
		CHECK(Test == Mask, "Ticket already cleared?!");

		gSharedTickets[TicketID / NumBitsInShared].fetch_and(~(Mask), std::memory_order_release);
	}
}

void StartDedicatedThread(DedicatedThreadData* DedicatedThread, const String& ThreadName, u64 AffinityMask)
{
	DedicatedThread->ThreadName = ThreadName;
	LockableName(DedicatedThread->ItemsLock.Ptr->Lock, ThreadName.c_str(), ThreadName.size());

	DedicatedThread->ActualThread = Thread(DedicatedThreadProc, DedicatedThread);
	DedicatedThread->ThreadID = DedicatedThread->ActualThread.get_id();
	auto handle = DedicatedThread->ActualThread.native_handle();

	SetThreadAffinityMask(handle, AffinityMask);
}

void StopDedicatedThread(DedicatedThreadData* DedicatedThread)
{
	DedicatedThread->ThreadShouldStop = true;
	DedicatedThread->WakeUp->notify_one();
	if (DedicatedThread->ActualThread.joinable())
		DedicatedThread->ActualThread.join();
}

bool WorkIsDone(TicketCPU WorkDoneTicket)
{
	u64 Shift = WorkDoneTicket.Value % NumBitsInShared;
	u64 Mask = (1ULL << Shift);
	return (gSharedTickets[WorkDoneTicket.Value / NumBitsInShared].load(std::memory_order_relaxed) & Mask) == 0;
}

void WaitForCompletion(TicketCPU WorkDoneTicket)
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
	DedicatedThread->WorkItems.push({ MOVE(Work), TicketCPU(), false});
	DedicatedThread->WakeUp->notify_one();
}

TicketCPU EnqueueWorkWithTicket(DedicatedThreadData* DedicatedThread, TFunction<void(void)>&& Work)
{
	ZoneScoped;
	TicketCPU Result { gCurrentTicketId++ };

	u64 Shift = Result.Value % NumBitsInShared;
	u64 Mask = (1ULL << Shift);
	u64 Test = gSharedTickets[Result.Value / NumBitsInShared].load(std::memory_order_acquire) & Mask;
	CHECK(Test == 0, "Ticket bit already set");
	gSharedTickets[Result.Value / NumBitsInShared].fetch_or(Mask, std::memory_order_release);

	ScopedMovableLock ItemsAutoLock(DedicatedThread->ItemsLock);
	DedicatedThread->WorkItems.push({ MOVE(Work), Result, true });
	DedicatedThread->WakeUp->notify_one();

	return Result;
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

