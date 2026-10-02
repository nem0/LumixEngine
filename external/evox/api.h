#pragma once

// Define EVOX_SHARED when building or consuming a shared Evox library, and
// EVOX_BUILD only in the library that owns the implementation. Standalone and
// static builds need neither define. Hosts may override EVOX_API if needed.
#ifndef EVOX_API
	#if defined(_WIN32) && defined(EVOX_SHARED)
		#ifdef EVOX_BUILD
			#define EVOX_API __declspec(dllexport)
		#else
			#define EVOX_API __declspec(dllimport)
		#endif
	#elif defined(EVOX_SHARED) && (defined(__GNUC__) || defined(__clang__))
		#define EVOX_API __attribute__((visibility("default")))
	#else
		#define EVOX_API
	#endif
#endif
