#pragma once
#include <mutex>
#include <shared_mutex>
#include <Tracy.hpp>

using Mutex      = std::mutex;
using ScopedLock = std::lock_guard<LockableBase(Mutex)>;
using UniqueLock = std::unique_lock<LockableBase(Mutex)>;

using RWLock     = std::shared_mutex;
using WriteLock  = std::unique_lock<RWLock>;
using ReadLock   = std::shared_lock<RWLock>;
