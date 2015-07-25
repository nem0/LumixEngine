#pragma once


#include "core/lumix.h"


namespace Lumix
{
	
	namespace Debug
	{
		void LUMIX_ENGINE_API debugBreak();
		void LUMIX_ENGINE_API debugOutput(const char* message);
	}

	void LUMIX_ENGINE_API installUnhandledExceptionHandler(const char* base_path);


} // namespace Lumix