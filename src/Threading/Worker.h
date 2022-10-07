#pragma once

#include <Common.h>
#include <Containers/Function.h>
#include <Threading/DedicatedThread.h>

void EnqueueToWorker(TFunction<void(void)>&& Work);
TicketCPU EnqueueToWorkerWithTicket(TFunction<void(void)>&& Work);
u64 NumberOfWorkers();
bool StealWork();
void StartWorkerThreads();
void StopWorkerThreads();

void ParallelFor(TFunction<void(u64, u64, u64)>&& Work, u64 Size, u64 MaxWorkers = NumberOfWorkers());
void ParallelFor(TFunction<void(u64, u64)>&& Work, u64 Size, u64 MaxWorkers = NumberOfWorkers());
