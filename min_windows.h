#ifndef MIN_WINDOWS_H
#define MIN_WINDOWS_H

#ifdef _WIN32

#if !defined(NOMINMAX)
#define NOMINMAX
#endif

#if !defined(NOSERVICE)
#define NOSERVICE
#endif

#if !defined(NOMCX)
#define NOMCX
#endif

#if !defined(NOIME)
#define NOIME
#endif

#if !defined(WIN32_LEAN_AND_MEAN)
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#endif
#endif
