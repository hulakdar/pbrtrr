#pragma once
#include <mutex>
#include <shared_mutex>
#include <Tracy.hpp>
#include "Containers/UniquePtr.h"
#include "Util/Debug.h"

DISABLE_OPTIMIZATION

using Mutex      = std::mutex;

struct MovableMutex
{
	struct TracyLockableLock
	{
		TracyLockable(Mutex, Lock);
	};
	TUniquePtr<TracyLockableLock> Ptr;
	
	MovableMutex() : Ptr(new TracyLockableLock()) {}

	void lock() { Ptr->Lock.lock(); }
	void unlock() { Ptr->Lock.unlock(); }
	bool try_lock() { return Ptr->Lock.try_lock(); }
};

using ScopedLock = std::lock_guard<LockableBase(Mutex)>;
using UniqueLock = std::unique_lock<LockableBase(Mutex)>;

using ScopedMovableLock = std::lock_guard<MovableMutex>;
using UniqueMovableLock = std::unique_lock<MovableMutex>;

using RWLock     = std::shared_mutex;
using WriteLock  = std::unique_lock<RWLock>;
using ReadLock   = std::shared_lock<RWLock>;
