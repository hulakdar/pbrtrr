#include "Threading/MainThread.h"
#include "Util/Util.h"

DedicatedThreadData gMainThreadData;

void   InitializeMainThreadWork()
{
    gMainThreadData.ThreadName = "MainThread";
}

Ticket EnqueueToMainThread(TFunction<void(void)>&& WorkItem)
{
    return EnqueueWork(&gMainThreadData, MOVE(WorkItem));
}

void   ExecuteMainThreadWork()
{
    ExecutePendingWork(&gMainThreadData);
}
