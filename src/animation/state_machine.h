#pragma once


#include "condition.h"
#include "engine/array.h"
#include "engine/lumix.h"


namespace Lumix
{


class Animation;
struct AnimationSystem;
class Engine;
class InputBlob;
class Model;
class OutputBlob;
struct Pose;
class Path;
struct Transform;


namespace Anim
{


struct Blend1DNode;
struct Component;
struct Container;
struct ComponentInstance;
struct Edge;
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
		STATE_MACHINE,
		BLEND1D
	};

	Component(Type _type) : type(_type), uid(-1) {}
	virtual ~Component() {}
	virtual ComponentInstance* createInstance(IAllocator& allocator) = 0;
	virtual void serialize(OutputBlob& blob);
	virtual void deserialize(InputBlob& blob, Container* parent, int version);

	int uid;
	const Type type;
};


struct Node : public Component
{
	Node(Component::Type type, IAllocator& _allocator);
	~Node();

	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	IAllocator& allocator;
	Array<Edge*> out_edges;
	Array<u8> events;
	int events_count = 0;
};


struct Container : public Node
{
	Container(Component::Type type, IAllocator& _allocator);
	~Container();

	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;
	Component* getChildByUID(int uid);

	IAllocator& allocator;
	Array<Component*> children;
};


struct Edge : public Component
{
	Edge(IAllocator& allocator);
	~Edge();
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	Condition condition;
	Node* from = nullptr;
	Node* to = nullptr;
	float length = 0.1f;
};


struct NodeInstance : public ComponentInstance
{
	NodeInstance(Node& node) : ComponentInstance(node) {}

	ComponentInstance* checkOutEdges(Node& node, RunningContext& rc);
	void queueEvents(RunningContext& rc, float old_time, float time, float length);
};


struct AnimationNode : public Node
{
	AnimationNode(IAllocator& allocator);
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	Array<u32> animations_hashes;
	bool looped = true;
	bool new_on_loop = true;
	int root_rotation_input_offset = -1;
	float max_root_rotation_speed = Math::degreesToRadians(90);
};


struct Blend1DNodeInstance : public NodeInstance
{
	Blend1DNodeInstance(Blend1DNode& _node);

	Transform getRootMotion() const override;
	float getTime() const override { return time; }
	float getLength() const override { return a0 ? a0->getLength() : 0; }
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override;
	ComponentInstance* update(RunningContext& rc, bool check_edges) override;
	void enter(RunningContext& rc, ComponentInstance* from) override;

	NodeInstance* a0 = nullptr;
	NodeInstance* a1 = nullptr;
	float current_weight = 1;
	NodeInstance* instances[16];
	Blend1DNode& node;
	float time;
};


struct Blend1DNode : public Container
{
	Blend1DNode(IAllocator& allocator);
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	struct Item
	{
		Node* node = nullptr;
		float value = 0;
	};

	Array<Item> items;
	int input_offset = 0;
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
	Transform getRootMotion() const override;

	StateMachine& source;
	ComponentInstance* current;
	IAllocator& allocator;
};


struct StateMachine : public Container
{
	StateMachine(IAllocator& _allocator);

	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

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
