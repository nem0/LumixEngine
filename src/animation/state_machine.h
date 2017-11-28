#pragma once


#include "condition.h"
#include "events.h"
#include "engine/array.h"
#include "engine/lumix.h"


namespace Lumix
{


class Animation;
struct AnimationSystem;
class Engine;
class InputBlob;
struct BoneMask;
class Model;
class OutputBlob;
struct Pose;
class Path;
struct RigidTransform;


namespace Anim
{


struct Blend1DNode;
struct Component;
struct ComponentInstance;
struct Container;
class ControllerResource;
struct Edge;
struct LayersNode;
struct StateMachine;


struct ComponentInstance
{
	explicit ComponentInstance(Component& _source) : source(_source) {}

	virtual ~ComponentInstance() {}
	virtual ComponentInstance* update(RunningContext& rc, bool check_edges) = 0;
	virtual RigidTransform getRootMotion() const = 0;
	virtual void fillPose(Engine& engine, Pose& pose, Model& model, float weight, BoneMask* mask) = 0;
	virtual void enter(RunningContext& rc, ComponentInstance* from) = 0;
	virtual float getTime() const = 0;
	virtual float getLength() const = 0;
	virtual void onAnimationSetUpdated(AnimSet& anim_set) = 0;

	Component& source;
};


struct Component
{
	enum Type : int
	{
		SIMPLE_ANIMATION,
		EDGE,
		STATE_MACHINE,
		BLEND1D,
		LAYERS
	};

	Component(ControllerResource& _controller, Type _type) 
		: controller(_controller)
		, type(_type)
		, uid(-1) {}
	virtual ~Component() {}
	virtual ComponentInstance* createInstance(IAllocator& allocator) = 0;
	virtual void serialize(OutputBlob& blob);
	virtual void deserialize(InputBlob& blob, Container* parent, int version);
	virtual Component* getByUID(int _uid) { return (uid == _uid) ? this : nullptr; }

	ControllerResource& controller;
	int uid;
	const Type type;
};


struct Node : public Component
{
	Node(ControllerResource& controller, Component::Type type, IAllocator& _allocator);
	~Node();

	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	IAllocator& allocator;
	Array<Edge*> out_edges;
	EventArray runtime_events;
	EventArray enter_events;
	EventArray exit_events;
};


struct Container : public Node
{
	Container(ControllerResource& controller, Component::Type type, IAllocator& _allocator);
	~Container();

	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;
	Component* getChildByUID(int uid);
	Component* getByUID(int _uid) override;

	IAllocator& allocator;
	Array<Component*> children;
};


struct Edge : public Component
{
	Edge(ControllerResource& controller, IAllocator& allocator);
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
	explicit NodeInstance(Node& node) : ComponentInstance(node) {}

	ComponentInstance* checkOutEdges(Node& node, RunningContext& rc);
	void queueEvents(RunningContext& rc, float old_time, float time, float length);
	void queueEnterEvents(RunningContext& rc);
	void queueExitEvents(RunningContext& rc);
protected:
	void queueEventArray(RunningContext& rc, const EventArray& events);
};


struct AnimationNode : public Node
{
	AnimationNode(ControllerResource& controller, IAllocator& allocator);
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	Array<u32> animations_hashes;
	float speed_multiplier = 1;
	bool looped = true;
	bool new_on_loop = true;
	int root_rotation_input_offset = -1;
	float max_root_rotation_speed = Math::degreesToRadians(90);
};


struct Blend1DNodeInstance : public NodeInstance
{
	explicit Blend1DNodeInstance(Blend1DNode& _node);

	RigidTransform getRootMotion() const override;
	float getTime() const override { return time; }
	float getLength() const override { return a0 ? a0->getLength() : 0; }
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight, BoneMask* mask) override;
	ComponentInstance* update(RunningContext& rc, bool check_edges) override;
	void enter(RunningContext& rc, ComponentInstance* from) override;
	void onAnimationSetUpdated(AnimSet& anim_set) override;


	NodeInstance* a0 = nullptr;
	NodeInstance* a1 = nullptr;
	float current_weight = 1;
	NodeInstance* instances[16];
	Blend1DNode& node;
	float time;
};


struct Blend1DNode : public Container
{
	Blend1DNode(ControllerResource& controller, IAllocator& allocator);
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


struct LayersNodeInstance : public NodeInstance
{
	explicit LayersNodeInstance(LayersNode& _node);

	RigidTransform getRootMotion() const override;
	float getTime() const override;
	float getLength() const override;
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight, BoneMask* mask) override;
	ComponentInstance* update(RunningContext& rc, bool check_edges) override;
	void enter(RunningContext& rc, ComponentInstance* from) override;
	void onAnimationSetUpdated(AnimSet& anim_set) override;

	NodeInstance* layers[16];
	struct BoneMask* masks[16];
	int layers_count = 0;
	LayersNode& node;
	float time;
};


struct LayersNode : public Container
{
	LayersNode(ControllerResource& controller, IAllocator& allocator);
	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	u32 masks[16];
};


struct StateMachineInstance : public NodeInstance
{
	StateMachineInstance(StateMachine& _source, IAllocator& _allocator);
	~StateMachineInstance();

	ComponentInstance* update(RunningContext& rc, bool check_edges) override;
	void fillPose(Engine& engine, Pose& pose, Model& model, float weight, BoneMask* mask) override;
	void enter(RunningContext& rc, ComponentInstance* from) override;
	float getTime() const override { return current ? current->getTime() : 0; }
	float getLength() const override { return current ? current->getLength() : 0; }
	RigidTransform getRootMotion() const override;
	void onAnimationSetUpdated(AnimSet& anim_set) override;

	StateMachine& source;
	ComponentInstance* current;
	IAllocator& allocator;
	float time;
};


struct StateMachine : public Container
{
	StateMachine(ControllerResource& controller, IAllocator& _allocator);

	ComponentInstance* createInstance(IAllocator& allocator) override;
	void serialize(OutputBlob& blob) override;
	void deserialize(InputBlob& blob, Container* parent, int version) override;

	struct Entry
	{
		explicit Entry(IAllocator& allocator) : condition(allocator) {}

		Condition condition;
		Node* node = nullptr;
	};
	Array<Entry> entries;
};


Component* createComponent(ControllerResource& controller, Component::Type type, IAllocator& allocator);


} // namespace Anim


} // namespace Lumix
