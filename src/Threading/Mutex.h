#pragma once
#include <mutex>
#include <Tracy.hpp>

using Mutex = std::mutex;
using ScopedLock = std::lock_guard<LockableBase(Mutex)>;
