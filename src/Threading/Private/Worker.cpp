
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "..\Worker.h"

DedicatedThreadData gWorkersDedicatedThreadData;
std::thread			gWorkerThreads[4];

void EnqueueToWorker(eastl::function<void(void)> WorkItem)
{
	EnqueueWork(&gWorkersDedicatedThreadData, WorkItem);
}

void StartWorkerThreads()
{
	for (int i = 0; i < ArraySize(gWorkerThreads); ++i)
	{
		gWorkerThreads[i] = StartDedicatedThread(&gWorkersDedicatedThreadData);
	}
}

void StopWorkerThreads()
{
	StopDedicatedThread(&gWorkersDedicatedThreadData);

	for (int i = 0; i < ArraySize(gWorkerThreads); ++i)
		if (gWorkerThreads[i].joinable())
			gWorkerThreads[i].join();
}
