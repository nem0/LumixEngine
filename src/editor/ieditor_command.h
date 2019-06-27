#pragma once


namespace Lumix
{


struct IEditorCommand
{
	virtual ~IEditorCommand() {}

	virtual bool isReady() { return true; }
	virtual bool execute() = 0;
	virtual void undo() = 0;
	virtual const char* getType() = 0;
	virtual bool merge(IEditorCommand& command) = 0;
};


} // namespace Lumix