#pragma once

#define DEBUG_BREAK() __debugbreak()

#if !defined(RELEASE) && !defined(PROFILE)
# define CHECK(x, ...) \
		do {           \
			if (!(x))  \
			{          \
				DEBUG_BREAK(); \
			}          \
		} while (false)

# define VALIDATE(x) \
		do {         \
			if (!ValidateImpl(x)) \
			{ \
				DEBUG_BREAK(); \
			} \
		} while (false)

#else
# define CHECK(x, ...) ((void)(x))
# define VALIDATE(x) (x)
#endif

# define DISABLE_OPTIMIZATION __pragma(optimize("", off))
# define ENABLE_OPTIMIZATION __pragma(optimize("", on))
