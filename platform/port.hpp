// portability shim for building the werkkzeug3/kkrieger codebase with
// clang++ on Apple Silicon / macOS. Force-included via -include port.hpp
#pragma once

#ifndef _MSC_VER

#define __forceinline inline
#define __cdecl
#define __stdcall
#define _stdcall
#define _cdecl
#define __int64 long long
#define __w64
#define _stricmp strcasecmp
#define stricmp strcasecmp
#define _strnicmp strnicmp
#define _snprintf snprintf
#define _vsnprintf vsnprintf
#define sNORETURN __attribute__((noreturn))

// __declspec is used for noreturn / dllexport / align etc.
#define __declspec(x) __attribute__((x))

#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <strings.h>
#include <stdarg.h>

// Collect a `first,...` argument list into arr[0..count-1]. The x86 code
// indexed the stack as (&first)[i], which arm64 does not lay out that way.
#define sGATHERARGS(arr,first,count) \
  decltype(first) arr[256]; \
  { va_list ap_; va_start(ap_,first); arr[0] = first; \
    for(int i_=1;i_<(count);i_++) arr[i_] = va_arg(ap_,decltype(first)); \
    va_end(ap_); }

// MSVC CRT bits the code expects
inline float powf_(float a, float b) { return ::powf(a, b); }
#define sPORT_MAC 1

#endif
