#pragma once


#include "editor/studio_app.h"
#include "engine/delegate.h"
#include "engine/lumix.h"
#include "imgui/imgui.h"


namespace Lumix
{
namespace Anim
{
struct EventHeader;
}
}


namespace AnimEditor
{


class Component;
class Container;
class ControllerResource;


class IAnimationEditor : public StudioApp::IPlugin
{
public:
	struct EventType
	{
		Lumix::u32 type;
		Lumix::StaticString<32> label;
		int size;
		Lumix::Delegate<void(Lumix::u8*, Component&)> editor;
	};

public:
	static IAnimationEditor* create(Lumix::IAllocator& allocator, StudioApp& app);

	virtual void setContainer(Container* container) = 0;
	virtual bool isEditorOpened() = 0;
	virtual void toggleEditorOpened() = 0;
	virtual bool isInputsOpened() = 0;
	virtual void toggleInputsOpened() = 0;
	virtual StudioApp& getApp() = 0;
	virtual int getEventTypesCount() const = 0;
	virtual EventType& createEventType(const char* type) = 0;
	virtual EventType& getEventTypeByIdx(int idx) = 0;
	virtual EventType& getEventType(Lumix::u32 type) = 0;
};


}