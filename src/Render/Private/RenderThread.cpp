#include "..\RenderThread.h"

#include "Containers/Function.h"
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "Render/CommandListPool.h"
#include "Render/Context.h"
#include <d3d12.h>

DedicatedThreadData	gRenderDedicatedThreadData;

Thread::id GetRenderThreadID()
{
	return gRenderDedicatedThreadData.ThreadID;
}

void StartRenderThread()
{
	gRenderDedicatedThreadData.ThreadShouldStealWork = false;
	StartDedicatedThread(&gRenderDedicatedThreadData, String("RenderThread"), 0x2);
}

void StopRenderThread()
{
	StopDedicatedThread(&gRenderDedicatedThreadData);
}

void EnqueueToRenderThread(TFunction<void(void)>&& RenderThreadWork)
{
	ZoneScoped;
	EnqueueWork(&gRenderDedicatedThreadData, MOVE(RenderThreadWork));
}

TicketCPU EnqueueToRenderThreadWithTicket(TFunction<void(void)>&& RenderThreadWork)
{
	ZoneScoped;
	return EnqueueWorkWithTicket(&gRenderDedicatedThreadData, MOVE(RenderThreadWork));
}

struct DelayedWork
{
	TicketGPU Ticket;
	TFunction<void(void)> Work;
};

TQueue<DelayedWork> gDelayedWork[4];
TracyLockable(Mutex, gDelayedWorkLock);
//Mutex gDelayedWorkLock;

void EnqueueDelayedWork(TFunction<void(void)>&& Work, const TicketGPU& Ticket)
{
	ScopedLock AutoLock(gDelayedWorkLock);

	gDelayedWork[Ticket.Type].push(
		DelayedWork {
			Ticket,
			MOVE(Work)
		}
	);
}

void RunDelayedWork()
{
	for (int i = 0; i < ArrayCount(gDelayedWork); ++i)
	{
		if (!gDelayedWork[i].empty())
		{
			TArray<TFunction<void(void)>> Work;
			{
				ScopedLock AutoLock(gDelayedWorkLock);
				while (!gDelayedWork[i].empty() && WorkIsDone(gDelayedWork[i].front().Ticket))
				{
					Work.push_back(MOVE(gDelayedWork[i].front().Work));
					gDelayedWork[i].pop();
				}
			}
			for (auto& Item : Work)
			{
				EnqueueToRenderThread(MOVE(Item));
			}
		}
	}
}

