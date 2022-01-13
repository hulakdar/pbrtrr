
#include "Threading/DedicatedThread.h"
#include "Render/Context.h"
#include "..\RenderThread.h"

DedicatedThreadData gRenderDedicatedThreadData;
std::thread gRenderDedicatedThread;

RenderContext gRenderContext;

void EnqueueRenderThreadWork(eastl::function<void(RenderContext&)> RenderThreadWork)
{
	ZoneScoped;
	EnqueueWork(&gRenderDedicatedThreadData,
		[RenderThreadWork]() {
			RenderThreadWork(gRenderContext);
		}
	);
}

RenderContext& GetRenderContext()
{
	return gRenderContext;
}

void StartRenderThread(System::Window& Window)
{
	gRenderContext.Init(Window);
	gRenderDedicatedThread = StartDedicatedThread(&gRenderDedicatedThreadData);
}

void StopRenderThread()
{
	StopDedicatedThread(&gRenderDedicatedThreadData);

	if (gRenderDedicatedThread.joinable())
		gRenderDedicatedThread.join();
}

