#include "state_machine.h"
#include "animation/animation.h"
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
	EdgeInstance(Edge& _edge) : edge(_edge) {}


	float getTime() const override { return time; }
	float getLength() const override { return edge.length; }


	ComponentInstance* update(RunningContext& rc) override
	{
		from = from->update(rc);
		to->update(rc);
		time += rc.time_delta;
		if (time > edge.length)
		{
			ComponentInstance* ret = to;
			LUMIX_DELETE(*rc.allocator, from);
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
	return LUMIX_NEW(allocator, EdgeInstance)(*this);
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
	, animation_hash(0)
{
}


void SimpleAnimationNode::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
	blob.write(animation_hash);
	blob.write(looped);
}


void SimpleAnimationNode::deserialize(InputBlob& blob, Container* parent)
{
	Component::deserialize(blob, parent);
	blob.read(animation_hash);
	blob.read(looped);
}



struct SimpleAnimationNodeInstance : public NodeInstance
{
	SimpleAnimationNodeInstance(SimpleAnimationNode& _node)
		: node(_node)
		, resource(nullptr)
	{
	}


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


	ComponentInstance* update(RunningContext& rc) override
	{
		time += rc.time_delta;
		if (node.looped) time = fmod(time, resource->getLength());
		return checkOutEdges(node, rc);
	}


	void enter(RunningContext& rc, ComponentInstance* from) override
	{ 
		time = 0;
		resource = (*rc.anim_set)[node.animation_hash];
	}


	Animation* resource;
	SimpleAnimationNode& node;
	float time;
};


ComponentInstance* SimpleAnimationNode::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, SimpleAnimationNodeInstance)(*this);
}


StateMachineInstance::StateMachineInstance(StateMachine& _source, IAllocator& _allocator)
	: NodeInstance()
	, source(_source)
	, allocator(_allocator)
	, current(nullptr)
{
}


ComponentInstance* StateMachineInstance::update(RunningContext& rc)
{
	current = current->update(rc);
	return checkOutEdges(source, rc);
}


void StateMachineInstance::fillPose(Engine& engine, Pose& pose, Model& model, float weight)
{
	current->fillPose(engine, pose, model, weight);
}


void StateMachineInstance::enter(RunningContext& rc, ComponentInstance* from)
{
	current = source.m_default_state->createInstance(*rc.allocator);
	current->enter(rc, nullptr);
}


ComponentInstance* StateMachine::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, StateMachineInstance)(*this, allocator);
}


void StateMachine::serialize(OutputBlob& blob)
{
	Container::serialize(blob);
	blob.write(m_default_state ? m_default_state->uid : -1);
}


void StateMachine::deserialize(InputBlob& blob, Container* parent)
{
	Container::deserialize(blob, parent);
	int uid;
	blob.read(uid);
	m_default_state = (Node*)getChildByUID(uid);
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
