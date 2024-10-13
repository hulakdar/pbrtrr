#include "Threading/DedicatedThread.h"
#include "Threading/Worker.h"
#include "Util/Util.h"
#include "Util/Math.h"
#include "Util/Debug.h"
#include "Containers/Array.h"

static TArray<DedicatedThreadData> gWorkers;

u64 NumberOfWorkers()
{
	return gWorkers.size();
}

bool StealWork()
{
	for (int i = 0; i < gWorkers.size() * 2; ++i)
	{
		int Index = rand() % gWorkers.size();
		DedicatedThreadData& ThreadData = gWorkers[Index];
		WorkItem Item;
		if (ThreadData.WorkItems.Empty() || !ThreadData.WorkItems.Pop(Item))
		{
			continue;
		}
		ExecuteItem(Item);
		return true;
	}
	return false;
}

void StartWorkerThreads()
{
#if NO_WORKERS
	return;
#endif
	u32 CoreCount = std::thread::hardware_concurrency();
	gWorkers.resize(std::max(1, (i32)CoreCount - 2));

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
