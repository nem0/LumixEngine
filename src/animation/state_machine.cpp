#include "state_machine.h"
#include "animation/animation.h"
#include "animation/animation_system.h"
#include "animation/events.h"
#include "engine/blob.h"
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


void Component::serialize(OutputBlob& blob, SerializeContext& ctx)
{
	blob.write(uid);
}


void Component::deserialize(InputBlob& blob, Container* parent, SerializeContext& ctx)
{
	blob.read(uid);
}


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


void Edge::serialize(OutputBlob& blob, SerializeContext& ctx)
{
	Component::serialize(blob, ctx);
	blob.write(from ? from->uid : -1);
	blob.write(to ? to->uid : -1);
	blob.write(length);
	blob.write(condition.bytecode.size());
	if(!condition.bytecode.empty()) blob.write(&condition.bytecode[0], condition.bytecode.size());
}


void Edge::deserialize(InputBlob& blob, Container* parent, SerializeContext& ctx)
{
	Component::deserialize(blob, parent, ctx);
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


Node::Node(Component::Type type, IAllocator& _allocator)
	: Component(type)
	, out_edges(_allocator)
	, allocator(_allocator)
	, events(allocator)
{
}


void Node::serialize(OutputBlob& blob, SerializeContext& ctx)
{
	Component::serialize(blob, ctx);
	blob.write(events_count);
	if (events_count > 0)
	{
		blob.write(events.size());
		blob.write(&events[0], events.size());
	}
	for (int i = 0; i < events_count; ++i)
	{
		EventHeader* ev = (EventHeader*)&events[sizeof(EventHeader) * i];
		blob.write(ctx.system.getEventTypePersistent(ev->type));
	}
}


void Node::deserialize(InputBlob& blob, Container* parent, SerializeContext& ctx)
{
	Component::deserialize(blob, parent, ctx);
	blob.read(events_count);
	if (events_count > 0)
	{
		int size;
		blob.read(size);
		events.resize(size);
		blob.read(&events[0], size);
	}
	for (int i = 0; i < events_count; ++i)
	{
		EventHeader* ev = (EventHeader*)&events[sizeof(EventHeader) * i];
		u32 tmp;
		blob.read(tmp);
		ev->type = ctx.system.getEventTypeRuntime(tmp);
	}
}


AnimationNode::AnimationNode(IAllocator& allocator)
	: Node(Component::SIMPLE_ANIMATION, allocator)
	, animations_hashes(allocator)
{
}


void AnimationNode::serialize(OutputBlob& blob, SerializeContext& ctx)
{
	Node::serialize(blob, ctx);
	blob.write(animations_hashes.size());
	for (u32 hash : animations_hashes)
	{
		blob.write(hash);
	}
	blob.write(looped);
	blob.write(new_on_loop);
	blob.write(root_rotation_input_offset);
}


void AnimationNode::deserialize(InputBlob& blob, Container* parent, SerializeContext& ctx)
{
	Node::deserialize(blob, parent, ctx);

	int count;
	blob.read(count);
	animations_hashes.resize(count);
	for (u32& hash : animations_hashes)
	{
		blob.read(hash);
	}
	blob.read(looped);
	blob.read(new_on_loop);
	blob.read(root_rotation_input_offset);
}


void NodeInstance::queueEvents(RunningContext& rc, float old_time, float time, float length)
{
	Node& node = (Node&)source;
	if (node.events_count <= 0) return;

	if (time < old_time)
	{
		EventHeader* headers = (EventHeader*)&node.events[0];
		for (int i = 0; i < node.events_count; ++i)
		{
			EventHeader& header = headers[i];
			if ((header.time >= old_time && header.time < length) || header.time < time)
			{
				rc.event_stream->write(header.type);
				rc.event_stream->write(rc.controller);
				rc.event_stream->write(header.size);
				rc.event_stream->write(
					&node.events[0] + header.offset + sizeof(EventHeader) * node.events_count, header.size);
			}
		}
	}
	else
	{
		EventHeader* headers = (EventHeader*)&node.events[0];
		for (int i = 0; i < node.events_count; ++i)
		{
			EventHeader& header = headers[i];
			if (header.time >= old_time && header.time < time)
			{
				rc.event_stream->write(header.type);
				rc.event_stream->write(rc.controller);
				rc.event_stream->write(header.size);
				rc.event_stream->write(
					&node.events[0] + header.offset + sizeof(EventHeader) * node.events_count, header.size);
			}
		}
	}
}


ComponentInstance* NodeInstance::checkOutEdges(Node& node, RunningContext& rc)
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


struct AnimationNodeInstance : public NodeInstance
{
	AnimationNodeInstance(AnimationNode& _node)
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
		float length = resource->getLength();
		if (node.looped && time > length)
		{
			time = fmod(time, length);
			if (node.new_on_loop && !node.animations_hashes.empty())
			{
				int idx = Math::rand() % node.animations_hashes.size();
				resource = (*rc.anim_set)[node.animations_hashes[idx]];
			}
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
				root_motion.pos = end_anim.pos - before.pos + after.pos - start_anim.pos;
				root_motion.rot = end_anim.rot * before.rot.conjugated() * (after.rot * start_anim.rot.conjugated());
			}
			else
			{
				Transform after = resource->getBoneTransform(time, bone_idx);
				root_motion.pos = after.pos - before.pos;
				root_motion.rot = before.rot.conjugated() * after.rot;
			}
		}
		else
		{
			root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
		}
		int root_rotation_input_offset = ((AnimationNode&)source).root_rotation_input_offset;
		if (root_rotation_input_offset >= 0)
		{
			float yaw = *(float*)&rc.input[root_rotation_input_offset];
			root_motion.rot = Quat({ 0, 1, 0 }, yaw);
		}

		queueEvents(rc, old_time, time, length);

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
	AnimationNode& node;
	Transform root_motion;
	float time;
};


ComponentInstance* AnimationNode::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, AnimationNodeInstance)(*this);
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


Transform StateMachineInstance::getRootMotion() const
{
	return current ? current->getRootMotion() : Transform({ 0, 0, 0 }, { 0, 0, 0, 1 });
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
	auto& source_sm = (StateMachine&)source;
	for (auto& entry : source_sm.entries)
	{
		if (entry.condition(rc))
		{
			current = entry.node->createInstance(*rc.allocator);
			current->enter(rc, nullptr);
			return;
		}
	}
}


ComponentInstance* StateMachine::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, StateMachineInstance)(*this, allocator);
}


StateMachine::StateMachine(IAllocator& _allocator)
	: Container(Component::STATE_MACHINE, _allocator)
	, entries(_allocator)
{
}


void StateMachine::serialize(OutputBlob& blob, SerializeContext& ctx)
{
	Container::serialize(blob, ctx);
	blob.write(entries.size());
	for (Entry& entry : entries)
	{
		blob.write(entry.node ? entry.node->uid : -1);
		blob.write(entry.condition.bytecode.size());
		if (!entry.condition.bytecode.empty())
		{
			blob.write(&entry.condition.bytecode[0], entry.condition.bytecode.size());
		}
	}
}


void StateMachine::deserialize(InputBlob& blob, Container* parent, SerializeContext& ctx)
{
	Container::deserialize(blob, parent, ctx);
	int count;
	blob.read(count);
	entries.reserve(count);
	for (int i = 0; i < count; ++i)
	{
		int uid;
		blob.read(uid);
		Entry& entry = entries.emplace(allocator);
		entry.node = uid < 0 ? nullptr : (Node*)getChildByUID(uid);
		int size;
		blob.read(size);
		entry.condition.bytecode.resize(size);
		if (size > 0)
		{
			blob.read(&entry.condition.bytecode[0], size);
		}
	}
}


Container::Container(Component::Type type, IAllocator& _allocator)
	: Node(type, _allocator)
	, children(_allocator)
	, allocator(_allocator)
{
}


Container::~Container()
{
	while (!children.empty())
	{
		LUMIX_DELETE(allocator, children.back());
		children.pop();
	}
}


Component* Container::getChildByUID(int uid)
{
	for (auto* child : children)
	{
		if (child->uid == uid) return child;
	}
	return nullptr;
}


void Container::serialize(OutputBlob& blob, SerializeContext& ctx)
{
	Node::serialize(blob, ctx);
	blob.write(children.size());
	for (auto* child : children)
	{
		blob.write(child->type);
		child->serialize(blob, ctx);
	}
}


void Container::deserialize(InputBlob& blob, Container* parent, SerializeContext& ctx)
{
	Node::deserialize(blob, parent, ctx);
	int size;
	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		Component::Type type;
		blob.read(type);
		Component* item = createComponent(type, allocator);
		item->deserialize(blob, this, ctx);
		children.push(item);
	}
}


Component* createComponent(Component::Type type, IAllocator& allocator)
{
	switch (type)
	{
		case Component::EDGE: return LUMIX_NEW(allocator, Edge)(allocator);
		case Component::STATE_MACHINE: return LUMIX_NEW(allocator, StateMachine)(allocator);
		case Component::SIMPLE_ANIMATION: return LUMIX_NEW(allocator, AnimationNode)(allocator);
		default: ASSERT(false); return nullptr;
	}
}


} // namespace Anim


} // namespace Lumix
