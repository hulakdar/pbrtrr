#pragma once

#include "Common.h"
#include "Containers/Function.h"
#include "Containers/String.h"
#include "Containers/Queue.h"
#include "Containers/UniquePtr.h"
#include "Containers/RingBuffer.h"
#include "Threading/Mutex.h"
#include "Util/Util.h"

#include <thread>
#include <condition_variable>
#include <array>

#define NO_WORKERS 0

using Thread = std::thread;
using ThreadID = std::thread::id;

struct TicketCPU { u16 Value; };

struct WorkPreamble
{
	void (*Executer)(void*);
	void (*Deleter)(void*);
};

struct WorkWrapper
{
	WorkWrapper() : Data(nullptr) {}
	WorkWrapper(WorkPreamble* InData) : Data(InData) {}
	WorkWrapper(WorkWrapper& InData) = delete;
	WorkWrapper(WorkWrapper&& Other)
	{
		*this = MOVE(Other);
	}

	void operator=(WorkWrapper&& Other)
	{
		//CHECK(Data == nullptr);

		Data = Other.Data;
		Other.Data = nullptr;
	}

	~WorkWrapper()
	{
		if (Data)
		{
			void* Callable = (void*)(uintptr_t(Data) + sizeof(WorkPreamble));
			Data->Deleter(Callable);
		}
	}

	void operator()()
	{
		CHECK(Data);

		void* Callable = (void*)(uintptr_t(Data) + sizeof(WorkPreamble));
		Data->Executer(Callable);
	}

	WorkPreamble* Data;
};

struct WorkItem
{
	WorkItem() : WorkDoneTicket(TicketCPU()), TicketValid(false) {}
	WorkItem(WorkWrapper&& InWork, TicketCPU InWorkTicket = TicketCPU(), bool InTicketValid = false)
		: Work(MOVE(InWork))
		, WorkDoneTicket(InWorkTicket)
		, TicketValid(InTicketValid)
	{
	}

	WorkWrapper Work;
	TicketCPU   WorkDoneTicket;
	bool        TicketValid;
};

struct ExchangeQueue
{
	TUniquePtr<std::atomic<TQueue<WorkItem>*>> Queue;
	
	ExchangeQueue() : Queue(new std::atomic<TQueue<WorkItem>*>(new TQueue<WorkItem>())) {}
	ExchangeQueue(ExchangeQueue& Other) = delete;
	ExchangeQueue(ExchangeQueue&& Other)
	{
		*this = MOVE(Other);
	}
	void operator=(ExchangeQueue&& Other)
	{
		Queue = MOVE(Other.Queue);
	}
	~ExchangeQueue()
	{
		auto* Ptr = Queue->exchange(nullptr);
		delete Ptr;
	}

	TQueue<WorkItem>* Aquire();
	void Release(TQueue<WorkItem>* Ptr);

	void Emplace(WorkWrapper&& Item, TicketCPU Ticket, bool TicketValid);
	bool Pop(WorkItem& Result);
	bool Empty();
};

void ExecuteItem(WorkItem& Item);

struct DedicatedThreadData
{
	bool ThreadShouldStop = false;
	bool ThreadShouldStealWork = true;
	MovableMutex WakeUpLock;
	TUniquePtr<std::condition_variable_any> WakeUp;
	TUniquePtr<RingBufferGeneric>  CallableScratch;
	ExchangeQueue WorkItems;
	String ThreadName;
	Thread ActualThread;
	u64 SleepMicrosecondsWhenIdle;
	ThreadID ThreadID;

	DedicatedThreadData(DedicatedThreadData&& Other)
		: ThreadShouldStop(Other.ThreadShouldStop)
		, ThreadShouldStealWork(Other.ThreadShouldStealWork)
		, WakeUpLock(MOVE(Other.WakeUpLock))
		, WakeUp(MOVE(Other.WakeUp))
		, CallableScratch(MOVE(Other.CallableScratch))
		, WorkItems(MOVE(Other.WorkItems))
		, ThreadName(MOVE(Other.ThreadName))
		, ActualThread(MOVE(Other.ActualThread))
		, SleepMicrosecondsWhenIdle(Other.SleepMicrosecondsWhenIdle)
		, ThreadID(Other.ThreadID)
	{

	}

	DedicatedThreadData(DedicatedThreadData& Other) = delete;

	DedicatedThreadData()
		: WakeUp(new std::condition_variable_any())
		, CallableScratch(new RingBufferGeneric(128_kb))
	{
		SleepMicrosecondsWhenIdle = 80 + rand() % 40;
	}
};

template <typename T>
void EnqueueWork(DedicatedThreadData* DedicatedThread, T&& Work, TicketCPU Ticket = TicketCPU(), bool TicketValid = false)
{
	void* Scratch = DedicatedThread->CallableScratch->Aquire(sizeof(WorkPreamble) + sizeof(T));

	WorkPreamble* Preamble = (WorkPreamble*)Scratch;
	Preamble->Executer = [](void* Data) {
		T& Callable = *(T*)Data;
		Callable();
	};
	Preamble->Deleter = [](void* Data) {
		T* Callable = (T*)Data;
		Callable->~T();
	};

	T* Callable = (T*)(Preamble + 1);
	new (Callable) T(MOVE(Work));

	DedicatedThread->WorkItems.Emplace(WorkWrapper( Preamble ), Ticket, TicketValid);

	DedicatedThread->WakeUp->notify_one();
}

using TicketType = decltype(TicketCPU().Value);
const u64 NumBitsForTickets = std::numeric_limits<TicketType>::max() + 1;
const u64 NumBitsInShared = 64;

extern std::atomic<TicketType> gCurrentTicketId;
extern std::array<std::atomic<u64>, NumBitsForTickets / NumBitsInShared> gSharedTickets;

template <typename T>
TicketCPU EnqueueWorkWithTicket(DedicatedThreadData* DedicatedThread, T&& Work)
{
	ZoneScoped;
	TicketCPU Result { gCurrentTicketId.fetch_add(NumBitsInShared * 8 + 1, std::memory_order_relaxed)};

	u64 Shift = Result.Value % NumBitsInShared;
	u64 Mask = (1ULL << Shift);
	u64 Test = gSharedTickets[Result.Value / NumBitsInShared].load(std::memory_order_acquire) & Mask;
	CHECK(Test == 0, "Ticket bit already set");
	gSharedTickets[Result.Value / NumBitsInShared].fetch_or(Mask, std::memory_order_release);

	EnqueueWork(DedicatedThread, MOVE(Work), Result, true);

	return Result;
}

