#pragma once

#include <cassert>

// TODO: Move this to CMake
#ifdef _DEBUG
#define NEB_DEBUG
#endif


#if defined(NEB_DEBUG)
#define NEB_ASSERT(expr) assert(expr)
#define NEB_ASSERT_MSG(expr, msg) assert(expr && (msg))
#else
#define NEB_ASSERT(expr)
#define NEB_ASSERT_MSG(expr, msg)
#endif