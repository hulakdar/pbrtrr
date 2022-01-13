#pragma once

#include <EASTL/functional.h>

void EnqueueToWorker(eastl::function<void(void)> WorkItem);

void StartWorkerThreads();
void StopWorkerThreads();
