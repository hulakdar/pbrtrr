#pragma once

#include <thread>
#include <future>

using Thread = std::thread;
using ThreadID = std::thread::id;

extern ThreadID gMainThreadID;

ThreadID CurrentThreadID();
