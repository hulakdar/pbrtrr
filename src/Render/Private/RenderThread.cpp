#include "..\RenderThread.h"

#include "Containers/Function.h"
#include "Containers/Queue.h"
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "Render/CommandListPool.h"
#include "Render/RenderDX12.h"
#include <d3d12.h>

struct DelayedWork
{
	TicketGPU Ticket;
	TFunction<void(void)> Work;
};

static TQueue<DelayedWork> gDelayedWork[4];
static TracyLockable(Mutex, gDelayedWorkLock);

static DedicatedThreadData gRenderDedicatedThreadData;

void StartRenderThread()
{
	gRenderDedicatedThreadData.ThreadShouldStealWork = false;
	StartDedicatedThread(&gRenderDedicatedThreadData, String("RenderThread"), 0x2);
}

void StopRenderThread()
{
	StopDedicatedThread(&gRenderDedicatedThreadData);
}

void EnqueueDelayedWork(TFunction<void(void)>&& Work, TicketGPU Ticket)
{
	ScopedLock AutoLock(gDelayedWorkLock);

	gDelayedWork[Ticket.QueueType].push(
		DelayedWork {
			Ticket,
			MOVE(Work)
		}
	);
}

void RunDelayedWork()
{
	ZoneScoped;
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
				Item();
			}
		}
	}
}

