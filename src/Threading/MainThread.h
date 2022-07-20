#pragma once

#include <Containers/Function.h>
#include <Threading/DedicatedThread.h>

void   InitializeMainThreadWork();
Ticket EnqueueToMainThread(TFunction<void(void)>&& WorkItem);
void   ExecuteMainThreadWork();
