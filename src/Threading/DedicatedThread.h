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

struct TicketCPU { u16 Value; };

struct WorkItem
{
	TFunction<void(void)> Work;
	TicketCPU                WorkDoneTicket;
	bool                  TicketValid;
};

struct WorkQueue
{
	int n;
};

void ExecuteItem(WorkItem& Item);

struct DedicatedThreadData
{
	bool ThreadShouldStop = false;
	bool ThreadShouldStealWork = true;
	MovableMutex ItemsLock;
	TUniquePtr<std::condition_variable_any> WakeUp;
	TQueue<WorkItem> WorkItems;
	String ThreadName;
	Thread ActualThread;
	u64 SleepMicrosecondsWhenIdle;
	Thread::id ThreadID;

	DedicatedThreadData()
		: WakeUp(new std::condition_variable_any())
	{
		SleepMicrosecondsWhenIdle = 80 + rand() % 40;
	}
};

Thread::id GetCurrentThreadID();

void StartDedicatedThread(DedicatedThreadData* DedicatedThread, const String& ThreadName, u64 AffinityMask);
void StopDedicatedThread(DedicatedThreadData* DedicatedThread);

void   EnqueueWork(DedicatedThreadData* DedicatedThread, TFunction<void(void)>&& Work);
TicketCPU EnqueueWorkWithTicket(DedicatedThreadData* DedicatedThread, TFunction<void(void)>&& Work);
bool   WorkIsDone(TicketCPU WorkDoneTicket);
void   WaitForCompletion(TicketCPU WorkDoneTicket);
void   ExecutePendingWork(DedicatedThreadData* DedicatedThread);
