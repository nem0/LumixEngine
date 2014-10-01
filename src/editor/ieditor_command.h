#pragma once


namespace Lumix
{


	class IEditorCommand
	{
		public:
			virtual ~IEditorCommand() {}

			virtual void execute() = 0;
			virtual void undo() = 0;
			virtual uint32_t getType() = 0;
			virtual bool merge(IEditorCommand& command) = 0;
	};


} // namespace Lumix