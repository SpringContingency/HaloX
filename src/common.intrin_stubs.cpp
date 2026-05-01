// Out-of-line implementations for x86 intrinsics that clang-cl in /Od (debug)
// fails to inline when used by static-library deps (notably minhook). Without
// these the link fails with "undefined symbol: __movsb" because clang emits
// an external reference to the intrinsic name when it can't expand inline.
//
// The MSVC/CRT runtime normally provides these as builtins, but in Debug
// builds clang-cl + lld-link sometimes lose that wiring. Defining them here
// resolves the references; release builds inline the intrinsics directly so
// these stubs go unused.

// Only clang-cl in /Od fails to inline __movsb and emits an external
// reference; pure MSVC (used in our Release config) refuses to let us
// redefine it as a function ("error C2169: intrinsic function, cannot be
// defined"). Compile this stub out for everything except clang-cl.
#if defined(__clang__)

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

extern "C" void __movsb(unsigned char* dst, const unsigned char* src, size_t n) {
	// Mirror the semantics of the MOVSB instruction with DF=0: forward copy
	// from src→dst, n bytes. Equivalent to memcpy for the bytewise case.
	while (n--) *dst++ = *src++;
}

#endif
