#include "state_machine.h"
#include "animation/animation.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/resource.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"
#include "renderer/model.h"
#include "renderer/pose.h"
#include <cmath>


namespace Lumix
{


namespace Anim
{


struct EdgeInstance : public ComponentInstance
{
	EdgeInstance(Edge& _edge, IAllocator& _allocator)
		: ComponentInstance(_edge)
		, edge(_edge)
		, allocator(_allocator)
	{
	}
	
	
	~EdgeInstance()
	{
		LUMIX_DELETE(allocator, from);
		LUMIX_DELETE(allocator, to);
	}

	float getTime() const override { return time; }
	float getLength() const override { return edge.length; }


	Transform getRootMotion() const override
	{
		return from->getRootMotion().interpolate(to->getRootMotion(), time / edge.length);
	}


	ComponentInstance* update(RunningContext& rc, bool check_edges) override
	{
		from = from->update(rc, false);
		to = to->update(rc, check_edges);
		time += rc.time_delta;
		if (time > edge.length)
		{
			ComponentInstance* ret = to;
			to = nullptr;
			LUMIX_DELETE(*rc.allocator, this);
			return ret;
		}
		return this;
	}


	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override
	{
		from->fillPose(engine, pose, model, weight);
		to->fillPose(engine, pose, model, weight * time / edge.length);
	}


	void enter(RunningContext& rc, ComponentInstance* _from) override
	{ 
		from = _from;
		time = 0;
		to = edge.to->createInstance(*rc.allocator);
		to->enter(rc, this);
	}


	Edge& edge;
	float time;
	ComponentInstance* from;
	ComponentInstance* to;
	IAllocator& allocator;
};


Edge::Edge(IAllocator& allocator)
	: Component(Component::EDGE)
	, condition(allocator)
{
}


Edge::~Edge()
{
	if (from) from->out_edges.eraseItem(this);
}


ComponentInstance* Edge::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, EdgeInstance)(*this, allocator);
}


void Edge::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
	blob.write(from ? from->uid : -1);
	blob.write(to ? to->uid : -1);
	blob.write(length);
	blob.write(condition.bytecode.size());
	if(!condition.bytecode.empty()) blob.write(&condition.bytecode[0], condition.bytecode.size());
}


void Edge::deserialize(InputBlob& blob, Container* parent)
{
	Component::deserialize(blob, parent);
	int uid;
	blob.read(uid);
	from = static_cast<Node*>(parent->getChildByUID(uid));
	blob.read(uid);
	to = static_cast<Node*>(parent->getChildByUID(uid));
	blob.read(length);
	int size;
	blob.read(size);
	condition.bytecode.resize(size);
	if(size > 0) blob.read(&condition.bytecode[0], size);
	from->out_edges.push(this);
}


Node::~Node()
{
	while (!out_edges.empty())
	{
		LUMIX_DELETE(allocator, out_edges.back());
	}
}


SimpleAnimationNode::SimpleAnimationNode(IAllocator& allocator)
	: Node(Component::SIMPLE_ANIMATION, allocator)
	, animations_hashes(allocator)
{
}


void SimpleAnimationNode::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
	blob.write(animations_hashes.size());
	for (u32 hash : animations_hashes)
	{
		blob.write(hash);
	}
	blob.write(looped);
}


void SimpleAnimationNode::deserialize(InputBlob& blob, Container* parent)
{
	Component::deserialize(blob, parent);

	int count;
	blob.read(count);
	animations_hashes.resize(count);
	for (u32& hash : animations_hashes)
	{
		blob.read(hash);
	}
	blob.read(looped);
}



struct SimpleAnimationNodeInstance : public NodeInstance
{
	SimpleAnimationNodeInstance(SimpleAnimationNode& _node)
		: NodeInstance(_node)
		, node(_node)
		, resource(nullptr)
	{
		root_motion.pos = { 0, 0, 0};
		root_motion.rot = { 0, 0, 0, 1 };
	}


	Transform getRootMotion() const override { return root_motion; }


	float getTime() const override { return time; }
	float getLength() const override { return resource->getLength(); }


	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override
	{
		if (weight < 1)
		{
			resource->getRelativePose(time, pose, model, weight);
		}
		else
		{
			resource->getRelativePose(time, pose, model);
		}
	}


	ComponentInstance* update(RunningContext& rc, bool check_edges) override
	{
		float old_time = time;
		time += rc.time_delta;
		if (node.looped)
		{
			time = fmod(time, resource->getLength());
		}

		int bone_idx = resource->getRootMotionBoneIdx();
		if (bone_idx >= 0)
		{
			Transform before = resource->getBoneTransform(old_time, bone_idx);
			if (time < old_time)
			{
				Transform end_anim = resource->getBoneTransform(resource->getLength(), bone_idx);
				Transform start_anim = resource->getBoneTransform(0, bone_idx);
				Transform after = resource->getBoneTransform(time, bone_idx);
				root_motion = (end_anim * before.inverted()) * (after * start_anim.inverted());
			}
			else
			{
				Transform after = resource->getBoneTransform(time, bone_idx);
				root_motion = after * before.inverted();
			}
		}
		else
		{
			root_motion = { {0, 0, 0}, {0, 0, 0, 1} };
		}
		return check_edges ? checkOutEdges(node, rc) : this;
	}


	void enter(RunningContext& rc, ComponentInstance* from) override
	{ 
		time = 0;
		if (node.animations_hashes.empty()) return;
		int idx = Math::rand() % node.animations_hashes.size();
		resource = (*rc.anim_set)[node.animations_hashes[idx]];
	}


	Animation* resource;
	SimpleAnimationNode& node;
	Transform root_motion;
	float time;
};


ComponentInstance* SimpleAnimationNode::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, SimpleAnimationNodeInstance)(*this);
}


StateMachineInstance::StateMachineInstance(StateMachine& _source, IAllocator& _allocator)
	: NodeInstance(_source)
	, source(_source)
	, allocator(_allocator)
	, current(nullptr)
{
}


StateMachineInstance::~StateMachineInstance()
{
	LUMIX_DELETE(allocator, current);
}


ComponentInstance* StateMachineInstance::update(RunningContext& rc, bool check_edges)
{
	if (current) current = current->update(rc, true);
	return check_edges ? checkOutEdges(source, rc) : this;
}


void StateMachineInstance::fillPose(Engine& engine, Pose& pose, Model& model, float weight)
{
	if(current) current->fillPose(engine, pose, model, weight);
}


void StateMachineInstance::enter(RunningContext& rc, ComponentInstance* from)
{
	if (source.default_state)
	{
		current = source.default_state->createInstance(*rc.allocator);
		current->enter(rc, nullptr);
	}
}


ComponentInstance* StateMachine::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, StateMachineInstance)(*this, allocator);
}


void StateMachine::serialize(OutputBlob& blob)
{
	Container::serialize(blob);
	blob.write(default_state ? default_state->uid : -1);
}


void StateMachine::deserialize(InputBlob& blob, Container* parent)
{
	Container::deserialize(blob, parent);
	int uid;
	blob.read(uid);
	default_state = (Node*)getChildByUID(uid);
}


Container::~Container()
{
	while (!children.empty())
	{
		LUMIX_DELETE(allocator, children.back());
		children.pop();
	}
}


void Container::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
	blob.write(children.size());
	for (auto* child : children)
	{
		blob.write(child->type);
		child->serialize(blob);
	}
}


void Container::deserialize(InputBlob& blob, Container* parent)
{
	Component::deserialize(blob, parent);
	int size;
	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		Component::Type type;
		blob.read(type);
		Component* item = createComponent(type, allocator);
		item->deserialize(blob, this);
		children.push(item);
	}
}


Component* createComponent(Component::Type type, IAllocator& allocator)
{
	switch (type)
	{
		case Component::EDGE: return LUMIX_NEW(allocator, Edge)(allocator);
		case Component::STATE_MACHINE: return LUMIX_NEW(allocator, StateMachine)(allocator);
		case Component::SIMPLE_ANIMATION: return LUMIX_NEW(allocator, SimpleAnimationNode)(allocator);
		default: ASSERT(false); return nullptr;
	}
}


} // namespace Anim


} // namespace Lumix
