#pragma once

#include <Threading/DedicatedThread.h>

extern DedicatedThreadData	gRenderDedicatedThreadData;

template <typename T>
void EnqueueToRenderThread(T&& Work)
{
	ZoneScoped;
#if NO_WORKERS
	Work();
	return;
#endif
	EnqueueWork(&gRenderDedicatedThreadData, MOVE(Work));
}

template <typename T>
TicketCPU EnqueueToRenderThreadWithTicket(T&& Work)
{
	ZoneScoped;
#if NO_WORKERS
	Work();
	return TicketCPU{ 0 };
#endif
	return EnqueueWorkWithTicket(&gRenderDedicatedThreadData, MOVE(Work));
}
