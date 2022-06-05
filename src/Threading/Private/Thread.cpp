#include "Threading/Thread.h"

ThreadID gMainThreadID;

ThreadID CurrentThreadID()
{
	return std::this_thread::get_id();
}
