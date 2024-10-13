#include "Threading/Worker.h"
#include "Containers/Array.h"
#include "Util/Util.h"

static DedicatedThreadData gMainThreadData;

void EnqueueToMainThread(TFunction<void(void)>&& WorkItem)
{
    EnqueueWork(&gMainThreadData, MOVE(WorkItem));
}

void   ExecuteMainThreadWork()
{
    ExecutePendingWork(&gMainThreadData);
}

