#include "Containers/Function.h"
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "..\RenderThread.h"

DedicatedThreadData	*gRenderDedicatedThreadData = new DedicatedThreadData();

void StartRenderThread()
{
	StartDedicatedThread(gRenderDedicatedThreadData, String("RenderThread"), 0x2);
}

void StopRenderThread()
{
	StopDedicatedThread(gRenderDedicatedThreadData);
}

void EnqueueToRenderThread(TFunction<void(void)>&& RenderThreadWork)
{
	ZoneScoped;
	EnqueueWork(gRenderDedicatedThreadData, MOVE(RenderThreadWork));
}

Ticket EnqueueToRenderThreadWithTicket(TFunction<void(void)>&& RenderThreadWork)
{
	ZoneScoped;
	return EnqueueWorkWithTicket(gRenderDedicatedThreadData, MOVE(RenderThreadWork));
}
