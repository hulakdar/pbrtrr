#pragma once

#include <Threading/DedicatedThread.h>

void StartRenderThread();
void StopRenderThread();
Ticket EnqueueRenderThreadWork(const TFunction<void(void)>&);
