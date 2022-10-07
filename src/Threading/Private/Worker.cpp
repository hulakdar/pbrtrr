#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Util/Util.h"
#include "Util/Math.h"
#include "Util/Debug.h"
#include "Containers/Array.h"

TArray<DedicatedThreadData> gWorkers;

void EnqueueToWorker(TFunction<void(void)>&& Work)
{
	EnqueueWork(&gWorkers[rand() % gWorkers.size()], MOVE(Work));
}

//#define DEBUG_TICKETS
TicketCPU EnqueueToWorkerWithTicket(TFunction<void(void)>&& Work)
{
	TicketCPU Result = EnqueueWorkWithTicket(&gWorkers[rand()%gWorkers.size()], MOVE(Work));
#ifdef DEBUG_TICKETS
	WaitForCompletion(Result);
#endif
	return Result;
}

u64 NumberOfWorkers()
{
	return gWorkers.size();
}

void ParallelFor(TFunction<void(u64, u64, u64)>&& Work, u64 Size, u64 MaxWorkers)
{
	MaxWorkers = MIN(NumberOfWorkers(), MaxWorkers);

	u64 WorkDivisor = Size / (MaxWorkers);
	u64 Begin = 0;
	u64 End = WorkDivisor;
	TArray<TicketCPU> Tickets;
	if (Begin != End)
	{
		Tickets.resize(MaxWorkers - 1);
		for (u64 i = 0; i < MaxWorkers - 1; ++i)
		{
			Tickets[i] = EnqueueWorkWithTicket(&gWorkers[i],
				[i, Begin, End, &Work]()
				{
					Work(i, Begin, End);
				}
			);
			Begin += WorkDivisor;
			End += WorkDivisor;
		}
		CHECK(Begin <= Size, "Debug ParallelFor");
	}
	if (Begin != Size)
	{
		ZoneScopedN("Inline parallel for");
		Work(MaxWorkers - 1, Begin, Size);
	}
	for (auto It : Tickets)
	{
		WaitForCompletion(It);
	}
}

void ParallelFor(TFunction<void(u64, u64)>&& Work, u64 Size, u64 MaxWorkers)
{
	ParallelFor([Work = MOVE(Work)](u64, u64 Begin, u64 End) {
		Work(Begin, End);
	}, Size, MaxWorkers);
}

bool StealWork()
{
	for (int i = 0; i < gWorkers.size() * 2; ++i)
	{
		int Index = rand() % gWorkers.size();
		DedicatedThreadData& randomWorker = gWorkers[Index];
		if (randomWorker.WorkItems.empty() || !randomWorker.ItemsLock.try_lock())
		{
			continue;
		}
		if (randomWorker.WorkItems.empty())
		{
			randomWorker.ItemsLock.unlock();
			continue;
		}
		WorkItem Item = MOVE(randomWorker.WorkItems.front());
		randomWorker.WorkItems.pop();
		randomWorker.ItemsLock.unlock();
		ExecuteItem(Item);
		return true;
	}
	return false;
}

void StartWorkerThreads()
{
	u32 CoreCount = std::thread::hardware_concurrency();
	gWorkers.resize(MAX(1, (i32)CoreCount - 2));

	for (int i = 0; i < gWorkers.size(); ++i)
	{
		String Name = StringFromFormat("Worker %d", i);
		StartDedicatedThread(&gWorkers[i], Name, 0x4ULL << i);
	}
}

void StopWorkerThreads()
{
	for (int i = 0; i < gWorkers.size(); ++i)
		StopDedicatedThread(&gWorkers[i]);
}
