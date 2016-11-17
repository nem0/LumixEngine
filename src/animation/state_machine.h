#pragma once


#include "condition.h"
#include "engine/array.h"
#include "engine/blob.h"
#include "engine/hash_map.h"
#include "engine/lumix.h"
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


struct Container;
struct ComponentInstance;
struct StateMachine;


struct ComponentInstance
{
	virtual ~ComponentInstance() {}
	virtual ComponentInstance* update(RunningContext& rc) = 0;
	virtual void fillPose(Engine& engine, Pose& pose, Model& model, float weight) = 0;
	virtual void enter(RunningContext& rc, ComponentInstance* from) = 0;
	virtual float getTime() const = 0;
	virtual float getLength() const = 0;
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
	{
	}

	~Node();

	Array<Edge*> out_edges;
	IAllocator& allocator;
};


struct Container : public Node
{
	Container(Component::Type type, IAllocator& _allocator)
		: Node(type, _allocator)
		, children(_allocator)
		, allocator(_allocator)
	{
	}


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
};


struct SimpleAnimationNode : public Node
{
	SimpleAnimationNode(IAllocator& allocator);
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;

	uint32 animation_hash;
	bool looped = true;
};


struct StateMachineInstance : public NodeInstance
{
	StateMachineInstance(StateMachine& _source, IAllocator& _allocator);

	ComponentInstance* update(RunningContext& rc) override;
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override;
	void enter(RunningContext& rc, ComponentInstance* from) override;
	float getTime() const override { return 0; }
	float getLength() const override { return 0; }

	StateMachine& source;
	ComponentInstance* current;
	IAllocator& allocator;
};


struct StateMachine : public Container
{
	StateMachine(IAllocator& _allocator)
		: Container(Component::STATE_MACHINE, _allocator)
		, m_default_state(nullptr)
	{
	}

	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent) override;

	Node* m_default_state;
};


Component* createComponent(Component::Type type, IAllocator& allocator);


} // namespace Anim


} // namespace Lumix
