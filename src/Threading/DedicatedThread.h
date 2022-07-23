#pragma once

#include "Common.h"
#include "Containers/Function.h"
#include "Containers/String.h"
#include "Containers/Queue.h"
#include "Containers/UniquePtr.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"
#include "Thread.h"
#include <thread>
#include <condition_variable>

struct Ticket { uint64_t Value; };

struct WorkItem
{
	TFunction<void(void)> Work;
	Ticket                WorkDoneTicket;
};

void ExecuteItem(WorkItem& Item);

struct DedicatedThreadData
{
	bool ThreadShouldStop = false;
	MovableMutex ItemsLock;
	TUniquePtr<std::condition_variable_any> WakeUp;
	TQueue<WorkItem> WorkItems;
	String ThreadName;
	Thread ActualThread;
	u64 SleepMicrosecondsWhenIdle;

	DedicatedThreadData()
		: WakeUp(new std::condition_variable_any())
	{
		SleepMicrosecondsWhenIdle = 80 + rand() % 40;
	}
};

void StartDedicatedThread(DedicatedThreadData* DedicatedThread, const String& ThreadName);
void StopDedicatedThread(DedicatedThreadData* DedicatedThread);

Ticket EnqueueWork(DedicatedThreadData* DedicatedThread, TFunction<void(void)>&& Work);
bool   WorkIsDone(Ticket WorkDoneTicket);
void   WaitForCompletion(Ticket WorkDoneTicket);
void   ExecutePendingWork(DedicatedThreadData* DedicatedThread);
