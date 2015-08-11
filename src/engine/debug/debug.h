#pragma once


#include "lumix.h"


namespace Lumix
{
	
	namespace Debug
	{
		void LUMIX_ENGINE_API debugBreak();
		void LUMIX_ENGINE_API debugOutput(const char* message);
	}

	void LUMIX_ENGINE_API installUnhandledExceptionHandler();


} // namespace Lumix