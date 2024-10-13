#include "Threading/DedicatedThread.h"
#include "Containers/Map.h"
#include "Util/Util.h"
#include <unordered_set>
#include <Util/Debug.h>
#include <limits>

#include <Threading/Private/Worker.Declarations.h>

std::atomic<TicketType> gCurrentTicketId;
std::array<std::atomic<u64>, NumBitsForTickets / NumBitsInShared> gSharedTickets;

namespace {
	void PopAndExecute(DedicatedThreadData *DedicatedThread)
	{
		while (true)
		{
			UniqueMovableLock Lock(DedicatedThread->WakeUpLock);
			bool HasWork = DedicatedThread->WakeUp->wait_for(Lock, std::chrono::microseconds(DedicatedThread->SleepMicrosecondsWhenIdle),
				[DedicatedThread]() {
					return DedicatedThread->ThreadShouldStop || !DedicatedThread->WorkItems.Empty();
				}
			);

			if (DedicatedThread->ThreadShouldStop || HasWork)
			{
				break;
			}

			if (DedicatedThread->ThreadShouldStealWork)
			{
				StealWork();
			}
		}

		WorkItem Item;
		bool HasWork = DedicatedThread->WorkItems.Pop(Item);
		if (HasWork)
		{
			ExecuteItem(Item);
		}
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
	LockableName(DedicatedThread->WakeUpLock.Ptr->Lock, ThreadName.c_str(), ThreadName.size());

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

void   ExecutePendingWork(DedicatedThreadData* DedicatedThread)
{
	WorkItem Item;
	while(DedicatedThread->WorkItems.Pop(Item))
	{
		ExecuteItem(Item);
	}
}

TQueue<WorkItem>* ExchangeQueue::Aquire()
{
	TQueue<WorkItem>* Q = nullptr;
	while (Q == nullptr)
	{
		Q = Queue->exchange(nullptr, std::memory_order_acquire);
	}
	return Q;
}

void ExchangeQueue::Release(TQueue<WorkItem>* Q)
{
	Queue->store(Q, std::memory_order_release);
}

void ExchangeQueue::Emplace(WorkWrapper&& Item, TicketCPU Ticket, bool TicketValid)
{
	auto* Q = Aquire();
	Q->emplace(MOVE(Item), Ticket, TicketValid);
	Release(Q);
}

bool ExchangeQueue::Pop(WorkItem& Result)
{
	if (auto* Q = Queue->exchange(nullptr, std::memory_order_relaxed))
	{
		if (Q->empty())
		{
			Release(Q);
			return false;
		}
		Result = MOVE(Q->front());
		Q->pop();
		Release(Q);
		return true;
	}
	return false;
}

bool ExchangeQueue::Empty()
{
	if (TQueue<WorkItem>* Q = Queue->load(std::memory_order_relaxed))
	{
		return Q->empty();
	}

	TQueue<WorkItem>* Q = Aquire();
	bool Result = Q->empty();
	Release(Q);
	return Result;
}

