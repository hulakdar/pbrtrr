
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "../Worker.h"

DedicatedThreadData gWorkersDedicatedThreadData;
std::thread			gWorkerThreads[3];

DedicatedThreadData* GetWorkerDedicatedThreadData()
{
	return &gWorkersDedicatedThreadData;
}

Ticket EnqueueToWorker(const TFunction<void(void)>& WorkItem)
{
	return EnqueueWork(&gWorkersDedicatedThreadData, MOVE(WorkItem));
}

void StartWorkerThreads()
{
	for (int i = 0; i < ArrayCount(gWorkerThreads); ++i)
	{
		gWorkerThreads[i] = StartDedicatedThread(&gWorkersDedicatedThreadData, L"Worker");
	}
}

void StopWorkerThreads()
{
	StopDedicatedThread(&gWorkersDedicatedThreadData);

	for (int i = 0; i < ArrayCount(gWorkerThreads); ++i)
		if (gWorkerThreads[i].joinable())
			gWorkerThreads[i].join();
}
