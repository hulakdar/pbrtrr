#include "Threading/MainThread.h"
#include "Threading/Worker.h"
#include "Containers/Array.h"
#include "Util/Util.h"

DedicatedThreadData gMainThreadData;

void   InitializeMainThreadWork()
{
    gMainThreadData.ThreadName = "MainThread";
	gMainThreadData.ThreadID = std::this_thread::get_id();
}

void EnqueueToMainThread(TFunction<void(void)>&& WorkItem)
{
    EnqueueWork(&gMainThreadData, MOVE(WorkItem));
}

void   ExecuteMainThreadWork()
{
    ExecutePendingWork(&gMainThreadData);
}

