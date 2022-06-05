#pragma once

#include <thread>

using Thread = std::thread;
using ThreadID = std::thread::id;

extern ThreadID gMainThreadID;

ThreadID CurrentThreadID();
