#pragma once


#include "engine/hash_map.h"
#include "engine/resource.h"
#include "engine/resource_manager_base.h"
#include "state_machine.h"


namespace Lumix
{


namespace Anim
{


class ControllerManager LUMIX_FINAL : public ResourceManagerBase
{
public:
	explicit ControllerManager(IAllocator& allocator)
		: ResourceManagerBase(allocator)
		, m_allocator(allocator)
	{}
	~ControllerManager() {}
	IAllocator& getAllocator() { return m_allocator; }

protected:
	Resource* createResource(const Path& path) override;
	void destroyResource(Resource& resource) override;

private:
	IAllocator& m_allocator;
};


class ControllerResource : public Resource
{
public:
	enum class Version : int
	{
		ANIMATION_SETS,
		MAX_ROOT_ROTATION_SPEED,
		INPUT_REFACTOR,
		ENTER_EXIT_EVENTS,

		LAST
	};

public:
	ControllerResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	~ControllerResource();

	void create() { onCreated(State::READY); }
	void unload(void) override;
	bool load(FS::IFile& file) override;
	ComponentInstance* createInstance(IAllocator& allocator);
	void serialize(OutputBlob& blob);
	bool deserialize(InputBlob& blob);
	IAllocator& getAllocator() { return m_allocator; }
	void addAnimation(int set, u32 hash, Animation* animation);

	struct AnimSetEntry
	{
		int set;
		u32 hash;
		Animation* animation;
	};

	Array<AnimSetEntry> m_animation_set;
	Array<StaticString<32>> m_sets_names;
	InputDecl m_input_decl;
	Component* m_root;

private:
	void clearAnimationSets();

	IAllocator& m_allocator;
};


} // namespace Anim


} // namespace Lumix