#pragma once

#include <Threading/DedicatedThread.h>

void StartRenderThread();
void StopRenderThread();
Ticket EnqueueRenderThreadWork(TFunction<void(void)>&&);
