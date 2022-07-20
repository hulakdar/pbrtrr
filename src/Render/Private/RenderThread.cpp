#include "Containers/Function.h"
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "..\RenderThread.h"

DedicatedThreadData	gRenderDedicatedThreadData;

void StartRenderThread()
{
	StartDedicatedThread(&gRenderDedicatedThreadData, String("RenderThread"));
}

void StopRenderThread()
{
	StopDedicatedThread(&gRenderDedicatedThreadData);
}

Ticket EnqueueRenderThreadWork(TFunction<void(void)>&& RenderThreadWork)
{
	ZoneScoped;
	return EnqueueWork(&gRenderDedicatedThreadData, MOVE(RenderThreadWork));
}
