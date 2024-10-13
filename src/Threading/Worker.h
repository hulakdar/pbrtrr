#pragma once

#include <Common.h>
#include <Containers/Function.h>
#include <Containers/Array.h>
#include <Threading/DedicatedThread.h>

extern TArray<DedicatedThreadData> gWorkers;

template <typename T>
void EnqueueToWorker(T&& Work)
{
#if NO_WORKERS
	Work();
	return;
#endif
	EnqueueWork(&gWorkers[rand() % gWorkers.size()], MOVE(Work));
}

template <typename T>
TicketCPU EnqueueToWorkerWithTicket(T&& Work)
{
#if NO_WORKERS
	Work();
	return TicketCPU{ 0 };
#endif

	return EnqueueWorkWithTicket(&gWorkers[rand()%gWorkers.size()], MOVE(Work));
}

template <typename T>
void ParallelFor(T&& Work, u64 Size, u64 MaxWorkers = NumberOfWorkers())
{
	MaxWorkers = std::min(NumberOfWorkers(), MaxWorkers);

	if (MaxWorkers == 0)
	{
		Work(0, 0, Size);
		return;
	}
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
					ZoneScopedN("Parallel for work item");
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

