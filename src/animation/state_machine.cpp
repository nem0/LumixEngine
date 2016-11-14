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


struct EdgeInstance : public ItemInstance
{
	EdgeInstance(Edge& _edge) : edge(_edge) {}


	float getTime() const override { return time; }
	float getLength() const override { return edge.length; }


	ItemInstance* update(RunningContext& rc) override
	{
		from = from->update(rc);
		to->update(rc);
		time += rc.time_delta;
		if (time > edge.length)
		{
			ItemInstance* ret = to;
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


	void enter(RunningContext& rc, ItemInstance* _from) override
	{ 
		from = _from;
		time = 0;
		to = edge.to->createInstance(*rc.allocator);
		to->enter(rc, this);
	}


	Edge& edge;
	float time;
	ItemInstance* from;
	ItemInstance* to;
};


void Node::serializeEdges(OutputBlob& blob)
{
	blob.write(out_edges.size());
	for (auto* edge : out_edges)
	{
		blob.write(edge->uid);
	}
}


void Node::deserializeEdges(InputBlob& blob, Composite& parent)
{
	int size;
	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		int uid;
		blob.read(uid);
		out_edges.push(static_cast<Edge*>(parent.getChildByUID(uid)));
	}
}


Edge::Edge(IAllocator& allocator)
	: Component(Component::EDGE)
	, condition(allocator)
{
}


ItemInstance* Edge::createInstance(IAllocator& allocator)
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
	blob.write(&condition.bytecode[0], condition.bytecode.size());
}


void Edge::deserialize(InputBlob& blob, Composite* parent)
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
	blob.read(&condition.bytecode[0], size);
}


SimpleAnimationNode::SimpleAnimationNode(IAllocator& allocator)
	: Node(Component::SIMPLE_ANIMATION, allocator)
	, animation(nullptr)
{
}


void SimpleAnimationNode::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
}


void SimpleAnimationNode::deserialize(InputBlob& blob, Composite* parent)
{
	Component::deserialize(blob, parent);
}



struct SimpleAnimationNodeInstance : public NodeInstance
{
	SimpleAnimationNodeInstance(SimpleAnimationNode& _node)
		: node(_node)
	{
	}


	float getTime() const override { return time; }
	float getLength() const override { return node.animation->getLength(); }


	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override
	{
		if (weight < 1)
		{
			node.animation->getRelativePose(time, pose, model, weight);
		}
		else
		{
			node.animation->getRelativePose(time, pose, model);
		}
	}


	ItemInstance* update(RunningContext& rc) override
	{
		time += rc.time_delta;
		if (node.looped) time = fmod(time, node.animation->getLength());
		return checkOutEdges(node, rc);
	}


	void enter(RunningContext& rc, ItemInstance* from) override { time = 0; }


	SimpleAnimationNode& node;
	float time;
};


ItemInstance* SimpleAnimationNode::createInstance(IAllocator& allocator)
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


ItemInstance* StateMachineInstance::update(RunningContext& rc)
{
	current = current->update(rc);
	return checkOutEdges(source, rc);
}


void StateMachineInstance::fillPose(Engine& engine, Pose& pose, Model& model, float weight)
{
	current->fillPose(engine, pose, model, weight);
}


void StateMachineInstance::enter(RunningContext& rc, ItemInstance* from)
{
	current = source.default_state->createInstance(*rc.allocator);
}


ItemInstance* StateMachine::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, StateMachineInstance)(*this, allocator);
}


void Composite::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
	blob.write(children.size());
	for (auto* child : children)
	{
		blob.write(child->type);
		child->serialize(blob);
	}

	serializeEdges(blob);
}


static Component* createComponent(Component::Type type, IAllocator& allocator)
{
	switch (type)
	{
		case Component::EDGE: return LUMIX_NEW(allocator, Edge)(allocator);
		case Component::STATE_MACHINE: return LUMIX_NEW(allocator, StateMachine)(allocator);
		case Component::SIMPLE_ANIMATION: return LUMIX_NEW(allocator, SimpleAnimationNode)(allocator);
		default: ASSERT(false); return nullptr;
	}
}


void Composite::deserialize(InputBlob& blob, Composite* parent)
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

	if (parent)
	{
		deserializeEdges(blob, *parent);
	}
	else
	{
		int size;
		blob.read(size);
		ASSERT(size == 0);
	}
}


} // namespace Anim


} // namespace Lumix
