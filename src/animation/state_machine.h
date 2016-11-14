#pragma once


#include "condition.h"
#include "engine/array.h"
#include "engine/blob.h"
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


struct Composite;
struct ItemInstance;
struct StateMachine;


struct ItemInstance
{
	virtual ~ItemInstance() {}
	virtual ItemInstance* update(RunningContext& rc) = 0;
	virtual void fillPose(Engine& engine, Pose& pose, Model& model, float weight) = 0;
	virtual void enter(RunningContext& rc, ItemInstance* from) = 0;
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
	virtual ItemInstance* createInstance(IAllocator& allocator) = 0;
	virtual void serialize(OutputBlob& blob) { blob.write(uid); }
	virtual void deserialize(InputBlob& blob, Composite* parent) { blob.read(uid); }

	int uid;
	const Type type;
};


struct Edge;


struct Node : public Component
{
	Node(Component::Type type, IAllocator& allocator)
		: Component(type)
		, out_edges(allocator)
	{
	}


	void serializeEdges(OutputBlob& blob);
	void deserializeEdges(InputBlob& blob, Composite& parent);


	Array<Edge*> out_edges;
};


struct Composite : public Node
{
	Composite(Component::Type type, IAllocator& _allocator)
		: Node(type, _allocator)
		, children(_allocator)
		, allocator(_allocator)
	{
	}


	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Composite* parent) override;


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
	ItemInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Composite* parent) override;

	Condition condition;
	Node* from;
	Node* to;
	float length;
};


struct NodeInstance : public ItemInstance
{
	ItemInstance* checkOutEdges(Node& node, RunningContext& rc)
	{
		rc.current = this;
		for (auto* edge : node.out_edges)
		{
			if (edge->condition(rc))
			{
				ItemInstance* new_item = edge->createInstance(*rc.allocator);
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
	ItemInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Composite* parent) override;

	Animation* animation;
	bool looped = true;
};


struct StateMachineInstance : public NodeInstance
{
	StateMachineInstance(StateMachine& _source, IAllocator& _allocator);

	ItemInstance* update(RunningContext& rc) override;
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override;
	void enter(RunningContext& rc, ItemInstance* from) override;
	float getTime() const override { return 0; }
	float getLength() const override { return 0; }


	StateMachine& source;
	ItemInstance* current;
	IAllocator& allocator;
};


struct StateMachine : public Composite
{
	StateMachine(IAllocator& _allocator)
		: Composite(Component::STATE_MACHINE, _allocator)
	{
	}

	ItemInstance* createInstance(IAllocator& allocator) override;

	InputDecl input_decl;
	Node* default_state;
};


} // namespace Anim


} // namespace Lumix
