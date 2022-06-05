#include "Containers/Function.h"
#include "Threading/DedicatedThread.h"
#include "Util/Util.h"
#include "..\RenderThread.h"

DedicatedThreadData	gRenderDedicatedThreadData;
Thread				gRenderDedicatedThread;

void StartRenderThread()
{
	gRenderDedicatedThread = StartDedicatedThread(&gRenderDedicatedThreadData, L"RenderThread");
}

void StopRenderThread()
{
	StopDedicatedThread(&gRenderDedicatedThreadData);

	if (gRenderDedicatedThread.joinable())
		gRenderDedicatedThread.join();
}

Ticket EnqueueRenderThreadWork(const TFunction<void(void)>& RenderThreadWork)
{
	ZoneScoped;
	return EnqueueWork(&gRenderDedicatedThreadData, MOVE(RenderThreadWork));
}
