#include "state_machine.h"
#include "animation/animation.h"
#include "animation/animation_system.h"
#include "animation/controller.h"
#include "animation/events.h"
#include "engine/blob.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/log.h"
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


void Component::serialize(OutputBlob& blob)
{
	blob.write(uid);
}


void Component::deserialize(InputBlob& blob, Container* parent, int version)
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


	void onAnimationSetUpdated(AnimSet& anim_set) override
	{
		from->onAnimationSetUpdated(anim_set);
		to->onAnimationSetUpdated(anim_set);
	}


	RigidTransform getRootMotion() const override
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


void Edge::deserialize(InputBlob& blob, Container* parent, int version)
{
	Component::deserialize(blob, parent, version);
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
	, enter_events(allocator)
	, exit_events(allocator)
{
}


void Node::serialize(OutputBlob& blob)
{
	Component::serialize(blob);
	blob.write(events_count);
	if (events_count > 0)
	{
		blob.write(events.size());
		blob.write(&events[0], events.size());
	}
	blob.write(enter_events.count);
	if (enter_events.count > 0)
	{
		blob.write(enter_events.data.size());
		blob.write(&enter_events.data[0], enter_events.data.size());
	}
	blob.write(exit_events.count);
	if (exit_events.count > 0)
	{
		blob.write(exit_events.data.size());
		blob.write(&exit_events.data[0], exit_events.data.size());
	}
}


void Node::deserialize(InputBlob& blob, Container* parent, int version)
{
	Component::deserialize(blob, parent, version);
	blob.read(events_count);
	if (events_count > 0)
	{
		int size;
		blob.read(size);
		events.resize(size);
		blob.read(&events[0], size);
	}
	if (version > (int)ControllerResource::Version::ENTER_EXIT_EVENTS)
	{
		blob.read(enter_events.count);
		if (enter_events.count > 0)
		{
			int size;
			blob.read(size);
			enter_events.data.resize(size);
			blob.read(&enter_events.data[0], size);
		}
		blob.read(exit_events.count);
		if (exit_events.count > 0)
		{
			int size;
			blob.read(size);
			exit_events.data.resize(size);
			blob.read(&exit_events.data[0], size);
		}
	}
}


Blend1DNodeInstance::Blend1DNodeInstance(Blend1DNode& _node)
	: NodeInstance(_node)
	, node(_node)
{
}


void Blend1DNodeInstance::fillPose(Engine& engine, Pose& pose, Model& model, float weight)
{
	if (!a0 || !a1) return;
	a0->fillPose(engine, pose, model, weight);
	a1->fillPose(engine, pose, model, weight * current_weight);
}


RigidTransform Blend1DNodeInstance::getRootMotion() const
{
	if(!a0) return RigidTransform({0, 0, 0}, {0, 0, 0, 1});
	return a0->getRootMotion().interpolate(a1->getRootMotion(), current_weight);
}


ComponentInstance* Blend1DNodeInstance::update(RunningContext& rc, bool check_edges)
{
	if (!instances[0]) return check_edges ? checkOutEdges(node, rc) : this;

	float old_time = time;
	time += rc.time_delta;
	float length = instances[0]->getLength();
	time = fmod(time, length);

	float input_value = *(float*)&rc.input[node.input_offset];
	current_weight = 0;
	a0 = instances[node.items.size() - 1];
	a1 = a0;
	if (node.items[0].value > input_value)
	{
		a0 = instances[0];
		a1 = instances[0];
	}
	else
	{
		for (int i = 1; i < node.items.size(); ++i)
		{
			if (node.items[i].value > input_value)
			{
				a0 = instances[i - 1];
				a1 = instances[i];
				current_weight =
					(node.items[i - 1].value - input_value) / (node.items[i - 1].value - node.items[i].value);
				break;
			}
		}
	}
	for (int i = 0; i < lengthOf(instances) && i < node.items.size(); ++i)
	{
		if (!instances[i]) break;
		instances[i]->update(rc, false);
	}
	queueEvents(rc, old_time, time, length);

	return check_edges ? checkOutEdges(node, rc) : this;
}


void Blend1DNodeInstance::onAnimationSetUpdated(AnimSet& anim_set)
{
	if (a0) a0->onAnimationSetUpdated(anim_set);
	if (a1) a1->onAnimationSetUpdated(anim_set);
}


void Blend1DNodeInstance::enter(RunningContext& rc, ComponentInstance* from)
{
	queueEnterEvents(rc);
	time = 0;
	if (node.items.size() > lengthOf(instances))
	{
		g_log_error.log("Animation") << "Too many nodes in Blend1D, only " << lengthOf(instances) << " are used.";
	}
	for (int i = 0; i < node.items.size() && i < lengthOf(instances); ++i)
	{
		instances[i] = (NodeInstance*)node.items[i].node->createInstance(*rc.allocator);
		instances[i]->enter(rc, nullptr);
	}
}


Blend1DNode::Blend1DNode(IAllocator& allocator)
	: Container(Component::BLEND1D, allocator)
	, items(allocator)
{
}


ComponentInstance* Blend1DNode::createInstance(IAllocator& allocator)
{
	return LUMIX_NEW(allocator, Blend1DNodeInstance)(*this);
}


void Blend1DNode::serialize(OutputBlob& blob)
{
	Container::serialize(blob);
	blob.write(items.size());
	for (Item& item : items)
	{
		blob.write(item.node->uid);
		blob.write(item.value);
	}
	blob.write(input_offset);
}


void Blend1DNode::deserialize(InputBlob& blob, Container* parent, int version)
{
	Container::deserialize(blob, parent, version);
	int count;
	blob.read(count);
	items.resize(count);
	for (Item& item : items)
	{
		int uid;
		blob.read(uid);
		item.node = (Node*)getChildByUID(uid);
		blob.read(item.value);
	}
	blob.read(input_offset);
}


AnimationNode::AnimationNode(IAllocator& allocator)
	: Node(Component::SIMPLE_ANIMATION, allocator)
	, animations_hashes(allocator)
{
}


void AnimationNode::serialize(OutputBlob& blob)
{
	Node::serialize(blob);
	blob.write(animations_hashes.size());
	for (u32 hash : animations_hashes)
	{
		blob.write(hash);
	}
	blob.write(looped);
	blob.write(new_on_loop);
	blob.write(root_rotation_input_offset);
	blob.write(max_root_rotation_speed);
}


void AnimationNode::deserialize(InputBlob& blob, Container* parent, int version)
{
	Node::deserialize(blob, parent, version);

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
	if (version > (int)ControllerResource::Version::MAX_ROOT_ROTATION_SPEED)
	{
		blob.read(max_root_rotation_speed);
	}
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


void NodeInstance::queueEnterEvents(RunningContext& rc)
{
	Node& node = (Node&)source;
	queueEventArray(rc, node.enter_events);
}


void NodeInstance::queueExitEvents(RunningContext& rc)
{
	Node& node = (Node&)source;
	queueEventArray(rc, node.exit_events);
}


void NodeInstance::queueEventArray(RunningContext& rc, const EventArray& events)
{
	if (events.count <= 0) return;

	auto headers = (EnterExitEventHeader*)&events.data[0];
	for (int i = 0; i < events.count; ++i)
	{
		auto& header = headers[i];
		rc.event_stream->write(header.type);
		rc.event_stream->write(rc.controller);
		rc.event_stream->write(header.size);
		rc.event_stream->write(
			&events.data[0] + header.offset + sizeof(EnterExitEventHeader) * events.count, header.size);
	}
}


ComponentInstance* NodeInstance::checkOutEdges(Node& node, RunningContext& rc)
{
	rc.current = this;
	for (auto* edge : node.out_edges)
	{
		rc.edge = edge;
		if (edge->condition(rc))
		{
			ComponentInstance* new_item = edge->createInstance(*rc.allocator);
			queueExitEvents(rc);
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


	RigidTransform getRootMotion() const override { return root_motion; }


	void onAnimationSetUpdated(AnimSet& anim_set) override
	{
		time = 0;
		if (node.animations_hashes.empty()) return;
		int idx = Math::rand() % node.animations_hashes.size();
		auto iter = anim_set.find(node.animations_hashes[idx]);
		resource = iter.isValid() ? iter.value() : nullptr;
	}


	float getTime() const override { return time; }
	float getLength() const override { return resource ? resource->getLength() : 0; }


	void fillPose(Engine& engine, Pose& pose, Model& model, float weight) override
	{
		if (!resource) return;
		if (weight < 1)
		{
			resource->getRelativePose(time, pose, model, weight);
		}
		else if (weight > 0)
		{
			resource->getRelativePose(time, pose, model);
		}
	}


	ComponentInstance* update(RunningContext& rc, bool check_edges) override
	{
		if (!resource) return check_edges ? checkOutEdges(node, rc) : this;

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
			RigidTransform before = resource->getBoneTransform(old_time, bone_idx);
			if (time < old_time)
			{
				float anim_end_time = resource->getLength();
				RigidTransform end_anim = resource->getBoneTransform(anim_end_time, bone_idx);
				RigidTransform start_anim = resource->getBoneTransform(0, bone_idx);
				float time_to_end = anim_end_time - old_time;
				RigidTransform after = resource->getBoneTransform(time - time_to_end, bone_idx);
				root_motion.pos = end_anim.pos - before.pos + after.pos - start_anim.pos;
				root_motion.rot = end_anim.rot * before.rot.conjugated() * (after.rot * start_anim.rot.conjugated());
			}
			else
			{
				RigidTransform after = resource->getBoneTransform(time, bone_idx);
				root_motion.pos = after.pos - before.pos;
				root_motion.rot = before.rot.conjugated() * after.rot;
			}
		}
		else
		{
			root_motion = {{0, 0, 0}, {0, 0, 0, 1}};
		}
		
		auto& node = ((AnimationNode&)source);
		int root_rotation_input_offset = node.root_rotation_input_offset;
		if (root_rotation_input_offset >= 0)
		{
			float yaw = *(float*)&rc.input[root_rotation_input_offset];
			float max_yaw_diff = rc.time_delta * node.max_root_rotation_speed;
			yaw = Math::clamp(yaw, -max_yaw_diff, max_yaw_diff);
			root_motion.rot = Quat({ 0, 1, 0 }, yaw);
		}

		queueEvents(rc, old_time, time, length);

		return check_edges ? checkOutEdges(node, rc) : this;
	}


	void enter(RunningContext& rc, ComponentInstance* from) override
	{
		queueEnterEvents(rc);
		time = 0;
		if (node.animations_hashes.empty()) return;
		int idx = Math::rand() % node.animations_hashes.size();
		auto iter = rc.anim_set->find(node.animations_hashes[idx]);
		resource = iter.isValid() ? iter.value() : nullptr;
	}


	Animation* resource;
	AnimationNode& node;
	RigidTransform root_motion;
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


RigidTransform StateMachineInstance::getRootMotion() const
{
	return current ? current->getRootMotion() : RigidTransform({ 0, 0, 0 }, { 0, 0, 0, 1 });
}


void StateMachineInstance::onAnimationSetUpdated(AnimSet& anim_set)
{
	if (current) current->onAnimationSetUpdated(anim_set);
}


ComponentInstance* StateMachineInstance::update(RunningContext& rc, bool check_edges)
{
	float old_time = time;
	time += rc.time_delta;
	if (current) current = current->update(rc, true);
	queueEvents(rc, old_time, time, 0);
	return check_edges ? checkOutEdges(source, rc) : this;
}


void StateMachineInstance::fillPose(Engine& engine, Pose& pose, Model& model, float weight)
{
	if(current) current->fillPose(engine, pose, model, weight);
}


void StateMachineInstance::enter(RunningContext& rc, ComponentInstance* from)
{
	time = 0;
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


void StateMachine::serialize(OutputBlob& blob)
{
	Container::serialize(blob);
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


void StateMachine::deserialize(InputBlob& blob, Container* parent, int version)
{
	Container::deserialize(blob, parent, version);
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


Component* Container::getByUID(int _uid)
{
	if (uid == _uid) return this;
	for (auto* child : children)
	{
		Component* cmp = child->getByUID(_uid);
		if (cmp) return cmp;
	}
	return nullptr;
}


void Container::serialize(OutputBlob& blob)
{
	Node::serialize(blob);
	blob.write(children.size());
	for (auto* child : children)
	{
		blob.write(child->type);
		child->serialize(blob);
	}
}


void Container::deserialize(InputBlob& blob, Container* parent, int version)
{
	Node::deserialize(blob, parent, version);
	int size;
	blob.read(size);
	for (int i = 0; i < size; ++i)
	{
		Component::Type type;
		blob.read(type);
		Component* item = createComponent(type, allocator);
		item->deserialize(blob, this, version);
		children.push(item);
	}
}


Component* createComponent(Component::Type type, IAllocator& allocator)
{
	switch (type)
	{
		case Component::BLEND1D: return LUMIX_NEW(allocator, Blend1DNode)(allocator);
		case Component::EDGE: return LUMIX_NEW(allocator, Edge)(allocator);
		case Component::STATE_MACHINE: return LUMIX_NEW(allocator, StateMachine)(allocator);
		case Component::SIMPLE_ANIMATION: return LUMIX_NEW(allocator, AnimationNode)(allocator);
		default: ASSERT(false); return nullptr;
	}
}


void EventArray::remove(int index)
{
	auto headers = (Anim::EnterExitEventHeader*)&data[0];
	auto header = headers[index];
	u8* headers_end = (u8*)(headers + count);
	u8* end = &data.back() + 1;
	u8* event_start = headers_end + header.offset;
	u8* event_end = event_start + header.size;

	for (int i = index + 1; i < count; ++i)
	{
		auto& h = headers[i];
		h.offset -= header.size;
	}

	u8* header_start = (u8*)&headers[index];
	u8* header_end = header_start + sizeof(Anim::EnterExitEventHeader);
	moveMemory(header_start, header_end, event_start - header_end);
	moveMemory(event_start - sizeof(Anim::EnterExitEventHeader), event_end, end - event_end);

	data.resize(data.size() - sizeof(Anim::EnterExitEventHeader) - header.size);

	--count;
}


void EventArray::append(int size, u32 type)
{
	int old_payload_size = data.size() - sizeof(Anim::EnterExitEventHeader) * count;
	data.resize(data.size() + size + sizeof(Anim::EnterExitEventHeader));
	u8* headers_end = &data[count * sizeof(Anim::EnterExitEventHeader)];
	moveMemory(headers_end + sizeof(Anim::EnterExitEventHeader), headers_end, old_payload_size);
	auto& event_header = *(Anim::EnterExitEventHeader*)&data[sizeof(Anim::EnterExitEventHeader) * count];
	event_header.type = type;
	event_header.size = size;
	event_header.offset = old_payload_size;
	++count;
}


} // namespace Anim


} // namespace Lumix
