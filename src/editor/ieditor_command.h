#pragma once


#include "engine/lumix.h"


namespace Lumix
{


	class JsonSerializer;


	class IEditorCommand
	{
		public:
			virtual ~IEditorCommand() {}

			virtual bool execute() = 0;
			virtual void undo() = 0;
			virtual void serialize(JsonSerializer& serializer) = 0;
			virtual void deserialize(JsonSerializer& serializer) = 0;
			virtual const char* getType() = 0;
			virtual bool merge(IEditorCommand& command) = 0;
	};


} // namespace Lumix