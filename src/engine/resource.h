#pragma once


#include "engine/delegate_list.h"
#include "engine/file_system.h"
#include "engine/path.h"


namespace Lumix
{


struct LUMIX_ENGINE_API ResourceType
{
	ResourceType() : type(0) {}
	explicit ResourceType(const char* type_name);
	u32 type;
	bool operator !=(const ResourceType& rhs) const { return rhs.type != type; }
	bool operator ==(const ResourceType& rhs) const { return rhs.type == type; }
	bool operator <(const ResourceType& rhs) const { return rhs.type < type; }
};
inline bool isValid(ResourceType type) { return type.type != 0; }
const ResourceType INVALID_RESOURCE_TYPE("");


struct LUMIX_ENGINE_API Resource
{
public:
	friend struct ResourceManager;
	friend struct ResourceManagerHub;

	enum class State : u32
	{
		EMPTY = 0,
		READY,
		FAILURE,
	};

	using ObserverCallback = DelegateList<void(State, State, Resource&)>;

public:
	virtual ~Resource();
	virtual ResourceType getType() const = 0;
	State getState() const { return m_current_state; }

	bool isEmpty() const { return State::EMPTY == m_current_state; }
	bool isReady() const { return State::READY == m_current_state; }
	bool isFailure() const { return State::FAILURE == m_current_state; }
	u32 getRefCount() const { return m_ref_count; }
	ObserverCallback& getObserverCb() { return m_cb; }
	size_t size() const { return m_size; }
	const Path& getPath() const { return m_path; }
	struct ResourceManager& getResourceManager() { return m_resource_manager; }

	template <auto Function, typename C> void onLoaded(C* instance)
	{
		m_cb.bind<Function>(instance);
		if (isReady())
		{
			(instance->*Function)(State::READY, State::READY, *this);
		}
	}

protected:
	Resource(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

	virtual void onBeforeReady() {}
	virtual void onBeforeEmpty() {}
	virtual void unload() = 0;
	virtual bool load(u64 size, const u8* mem) = 0;

	void onCreated(State state);
	void doUnload();

	void addDependency(Resource& dependent_resource);
	void removeDependency(Resource& dependent_resource);

protected:
	State m_desired_state;
	u16 m_empty_dep_count;
	size_t m_size;
	ResourceManager& m_resource_manager;

protected:
	void checkState();
	void refresh();

private:
	void doLoad();
	void fileLoaded(u64 size, const u8* mem, bool success);
	void onStateChanged(State old_state, State new_state, Resource&);
	u32 addRef() { return ++m_ref_count; }
	u32 remRef() { return --m_ref_count; }

	Resource(const Resource&);
	void operator=(const Resource&);

private:
	ObserverCallback m_cb;
	Path m_path;
	u16 m_ref_count;
	u16 m_failed_dep_count;
	State m_current_state;
	FileSystem::AsyncHandle m_async_op;
	#ifdef LUMIX_DEBUG
		bool m_invoking = false;
	#endif
}; // struct Resource


} // namespace Lumix
