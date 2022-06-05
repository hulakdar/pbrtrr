#pragma once

#include "Containers/Function.h"
#include "Containers/String.h"
#include "Containers/Queue.h"
#include "Threading/Mutex.h"
#include "Thread.h"
#include <thread>
#include <condition_variable>

struct Ticket { uint64_t Value; };

struct WorkItem
{
	TFunction<void(void)> Work;
	Ticket                WorkDoneTicket;
};

struct DedicatedThreadData
{
	bool ThreadShouldStop = false;
	TracyLockable(Mutex, ItemsLock);
	std::condition_variable_any WakeUp;
	TQueue<WorkItem> WorkItems;
	String ThreadName;
};

Thread StartDedicatedThread(DedicatedThreadData* DedicatedThread, WString ThreadName);
void StopDedicatedThread(DedicatedThreadData* DedicatedThread);

Ticket EnqueueWork(DedicatedThreadData* DedicatedThread, const TFunction<void(void)>& Work);
bool   WorkIsDone(Ticket WorkDoneTicket);
bool   TryPopAndExecute(DedicatedThreadData* DedicatedThread);
void   WaitForCompletion(Ticket WorkDoneTicket);
