#pragma once

#include "Containers/Queue.h"
#include "Threading/Mutex.h"
#include <thread>
#include <condition_variable>

struct DedicatedThreadData
{
	TracyLockable(Mutex, ItemsLock);
	std::condition_variable_any WakeUp;
	TQueue<eastl::function<void(void)>> WorkItems;
	bool ThreadShouldStop = false;
};

std::thread StartDedicatedThread(DedicatedThreadData* DedicatedThread);
void		StopDedicatedThread(DedicatedThreadData* DedicatedThread);

void EnqueueWork(DedicatedThreadData* DedicatedThread, eastl::function<void(void)> Work);
