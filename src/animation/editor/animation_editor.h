#pragma once


#include "animation/state_machine.h"
#include "editor/studio_app.h"
#include "engine/delegate.h"
#include "engine/lumix.h"
#include "imgui/imgui.h"


namespace Lumix
{


struct IEditorCommand;
namespace Anim
{
struct EventHeader;
}


namespace AnimEditor
{


class Component;
class Container;
class ControllerResource;
class Node;


struct IAnimationEditor : public StudioApp::IPlugin
{
public:
	struct EventType
	{
		u32 type;
		StaticString<32> label;
		int size;
		Delegate<void(u8*, Component&)> editor;
	};

public:
	static IAnimationEditor* create(IAllocator& allocator, StudioApp& app);

	virtual void setContainer(Container* container) = 0;
	virtual bool isEditorOpened() = 0;
	virtual void toggleEditorOpened() = 0;
	virtual bool isInputsOpened() = 0;
	virtual void toggleInputsOpened() = 0;
	virtual StudioApp& getApp() = 0;
	virtual int getEventTypesCount() const = 0;
	virtual EventType& createEventType(const char* type) = 0;
	virtual EventType& getEventTypeByIdx(int idx) = 0;
	virtual EventType& getEventType(u32 type) = 0;
	virtual void createEdge(ControllerResource& ctrl, Container* container, Node* from, Node* to) = 0;
	virtual void moveNode(ControllerResource& ctrl, Node* node, const ImVec2& pos) = 0;
	virtual void createNode(ControllerResource& ctrl,
		Container* container,
		Anim::Node::Type type,
		const ImVec2& pos) = 0;
};


} // namespace AnimEditor
} // namespace Lumix