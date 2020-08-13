#pragma once
#include <tbb/mutex.h>

using Mutex = tbb::mutex;
using ScopedLock = tbb::mutex::scoped_lock;
