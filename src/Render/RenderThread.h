#pragma once

#include <Threading/DedicatedThread.h>

void StartRenderThread();
void StopRenderThread();
void EnqueueToRenderThread(TFunction<void(void)>&&);
Ticket EnqueueToRenderThreadWithTicket(TFunction<void(void)>&& RenderThreadWork);
