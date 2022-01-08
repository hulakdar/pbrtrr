#pragma once

#include <thread>

using Thread = std::thread;
using ThreadID = std::thread::id;

extern ThreadID gMainThreadID;

static ThreadID CurrentThreadID()
{
	return std::this_thread::get_id();
}
