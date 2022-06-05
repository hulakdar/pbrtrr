#pragma once

#include <Containers/Function.h>
#include <Threading/DedicatedThread.h>

DedicatedThreadData* GetWorkerDedicatedThreadData();

Ticket EnqueueToWorker(const TFunction<void(void)>& WorkItem);
void StartWorkerThreads();
void StopWorkerThreads();
