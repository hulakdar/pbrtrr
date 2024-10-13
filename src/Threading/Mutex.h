#pragma once

#include "Containers/UniquePtr.h"
#include "Util/Debug.h"

#include <mutex>
#include <shared_mutex>
#include <tracy/Tracy.hpp>

using Mutex      = std::mutex;

struct MovableMutex
{
	struct TracyLockableMutex
	{
		TracyLockable(Mutex, Lock);
		//Mutex Lock;
	};
	TUniquePtr<TracyLockableMutex> Ptr;
	
	MovableMutex() : Ptr(new TracyLockableMutex()) {}

	void lock() { Ptr->Lock.lock(); }
	void unlock() { Ptr->Lock.unlock(); }
	bool try_lock() { return Ptr->Lock.try_lock(); }
};

using ScopedLock = std::lock_guard<LockableBase(Mutex)>;
using UniqueLock = std::unique_lock<LockableBase(Mutex)>;
//using ScopedLock = std::lock_guard<Mutex>;
//using UniqueLock = std::unique_lock<Mutex>;

using ScopedMovableLock = std::lock_guard<MovableMutex>;
using UniqueMovableLock = std::unique_lock<MovableMutex>;

using RWLock     = std::shared_mutex;
using WriteLock  = std::unique_lock<SharedLockableBase(RWLock)>;
using ReadLock   = std::shared_lock<SharedLockableBase(RWLock)>;
