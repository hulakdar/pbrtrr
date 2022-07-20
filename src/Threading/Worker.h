#pragma once

#include <Common.h>
#include <Containers/Function.h>
#include <Threading/DedicatedThread.h>

Ticket EnqueueToWorker(TFunction<void(void)>&& Work);
bool StealWork();
void StartWorkerThreads();
void StopWorkerThreads();

void ParallelFor(u64 Size, TFunction<void(u64, u64)>&& Work);
