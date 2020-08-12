#pragma once
#include <mutex>

using Mutex = std::mutex;
using ScopedLock = std::lock_guard<Mutex>;
