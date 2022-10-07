#pragma once

#include "Containers/Function.h"
#include "Threading/DedicatedThread.h"
#include "Common.h"

void InitializeMainThreadWork();
void EnqueueToMainThread(TFunction<void(void)>&& WorkItem);
void ExecuteMainThreadWork();

enum class TargetThread : u8
{
	MainThread,
	RenderThread,
	WorkerThread,
};

