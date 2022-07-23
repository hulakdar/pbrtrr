#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Util/Util.h"
#include "Util/Math.h"
#include "Util/Debug.h"
#include "Containers/Array.h"

//#define DEBUG_TICKETS

TArray<DedicatedThreadData> gWorkers;

Ticket EnqueueToWorker(TFunction<void(void)>&& Work)
{
	Ticket Result = EnqueueWork(&gWorkers[rand()%gWorkers.size()], MOVE(Work));
#ifdef DEBUG_TICKETS
	WaitForCompletion(Result);
#endif
	return Result;
}

u64 NumberOfWorkers()
{
	return gWorkers.size();
}

void ParallelFor(u64 Size, TFunction<void(u64, u64, u64)>&& Work)
{
	u64 WorkDivisor = Size / (gWorkers.size() + 1);
	u64 Begin = 0;
	u64 End = WorkDivisor;
	TArray<Ticket> Tickets;
	if (Begin != End)
	{
		Tickets.resize(gWorkers.size());
		for (u64 i = 0; i < gWorkers.size(); ++i)
		{
			Tickets[i] = EnqueueWork(&gWorkers[i],
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
		Work(gWorkers.size(), Begin, Size);
	}
	for (auto It : Tickets)
	{
		WaitForCompletion(It);
	}
}

bool StealWork()
{
	for (int i = 0; i < gWorkers.size() * 2; ++i)
	{
		DedicatedThreadData& randomWorker = gWorkers[rand() % gWorkers.size()];
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
		StartDedicatedThread(&gWorkers[i], Name);
	}
}

void StopWorkerThreads()
{
	for (int i = 0; i < gWorkers.size(); ++i)
		StopDedicatedThread(&gWorkers[i]);
}
