#pragma once

#include "core/core.h"

#ifdef STATIC_PLUGINS
	#define LUMIX_GUI_NG_API
#elif defined BUILDING_GUI_NG
	#define LUMIX_GUI_NG_API LUMIX_LIBRARY_EXPORT
#else
	#define LUMIX_GUI_NG_API LUMIX_LIBRARY_IMPORT
#endif