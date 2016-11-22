#pragma once


#include "condition.h"
#include "engine/array.h"
#include "engine/blob.h"
#include "engine/hash_map.h"
#include "engine/lumix.h"
#include "engine/matrix.h"
#include "renderer/pose.h"


namespace Lumix
{


class Animation;
class Engine;
class Model;
struct Pose;
struct Vec3;
class Path;
struct Quat;


namespace Anim
{


struct Component;
struct Container;
struct ComponentInstance;
struct StateMachine;


struct ComponentInstance
{
	ComponentInstance(Component& _source) : source(_source) {}

	virtual ~ComponentInstance() {}
	virtual ComponentInstance* update(RunningContext& rc, bool check_edges) = 0;
	virtual Transform getRootMotion() const = 0;
	virtual void fillPose(Engine& engine, Pose& pose, Model& model, float weight) = 0;
	virtual void enter(RunningContext& rc, ComponentInstance* from) = 0;
	virtual float getTime() const = 0;
	virtual float getLength() const = 0;

	Component& source;
};


struct Component
{
	enum Type : int
	{
		SIMPLE_ANIMATION,
		EDGE,
		STATE_MACHINE
	};

	Component(Type _type) : type(_type), uid(-1) {}
	virtual ~Component() {}
	virtual ComponentInstance* createInstance(IAllocator& allocator) = 0;
	virtual void serialize(OutputBlob& blob) { blob.write(uid); }
	virtual void deserialize(InputBlob& blob, Container* parent) { blob.read(uid); }

	int uid;
	const Type type;
};


struct Edge;


struct Node : public Component
{
	Node(Component::Type type, IAllocator& _allocator)
		: Component(type)
		, out_edges(_allocator)
		, allocator(_allocator)
		, events(allocator)
	{
	}

	~Node();
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;

	IAllocator& allocator;
	Array<Edge*> out_edges;
	Array<u8> events;
	int events_count = 0;
};


struct Container : public Node
{
	Container(Component::Type type, IAllocator& _allocator)
		: Node(type, _allocator)
		, children(_allocator)
		, allocator(_allocator)
	{
	}


	~Container();


	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;


	Component* getChildByUID(int uid)
	{
		for (auto* child : children)
		{
			if (child->uid == uid) return child;
		}
		return nullptr;
	}


	IAllocator& allocator;
	Array<Component*> children;
};


struct Edge : public Component
{
	Edge(IAllocator& allocator);
	~Edge();
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;

	Condition condition;
	Node* from = nullptr;
	Node* to = nullptr;
	float length = 0.1f;
};


struct NodeInstance : public ComponentInstance
{
	NodeInstance(Node& node) : ComponentInstance(node) {}

	ComponentInstance* checkOutEdges(Node& node, RunningContext& rc)
	{
		rc.current = this;
		for (auto* edge : node.out_edges)
		{
			if (edge->condition(rc))
			{
				ComponentInstance* new_item = edge->createInstance(*rc.allocator);
				new_item->enter(rc, this);
				return new_item;
			}
		}
		return this;
	}

	void queueEvents(RunningContext& rc, float old_time, float time, float length);
};


struct EventHeader
{
	enum BuiltinType
	{
		SET_INPUT
	};

	float time;
	u8 type;
	u8 size;
	u16 offset;
};


struct SetInputEvent
{
	int input_idx;
	union
	{
		int i_value;
		float f_value;
		bool b_value;
	};
};


struct SimpleAnimationNode : public Node
{
	SimpleAnimationNode(IAllocator& allocator);
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;

	Array<u32> animations_hashes;
	bool looped = true;
	int root_rotation_input_offset = -1;
};


struct StateMachineInstance : public NodeInstance
{
	StateMachineInstance(StateMachine& _source, IAllocator& _allocator);
	~StateMachineInstance();

	ComponentInstance* update(RunningContext& rc, bool check_edges) override;
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override;
	void enter(RunningContext& rc, ComponentInstance* from) override;
	float getTime() const override { return current ? current->getTime() : 0; }
	float getLength() const override { return current ? current->getLength() : 0; }
	Transform getRootMotion() const override 
	{
		return current ? current->getRootMotion() : Transform({0, 0, 0}, {0, 0, 0, 1});
	}

	StateMachine& source;
	ComponentInstance* current;
	IAllocator& allocator;
};


struct StateMachine : public Container
{
	StateMachine(IAllocator& _allocator)
		: Container(Component::STATE_MACHINE, _allocator)
		, entries(_allocator)
	{
	}

	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;

	struct Entry
	{
		Entry(IAllocator& allocator) : condition(allocator) {}

		Condition condition;
		Node* node = nullptr;
	};
	Lumix::Array<Entry> entries;
};


Component* createComponent(Component::Type type, IAllocator& allocator);


} // namespace Anim


} // namespace Lumix
