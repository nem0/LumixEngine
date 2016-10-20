#pragma once


#include "engine/fs/file_system.h"
#include "engine/delegate_list.h"
#include "engine/path.h"


namespace Lumix
{


class ResourceManager;
class ResourceManagerBase;


struct LUMIX_ENGINE_API ResourceType
{
	ResourceType() : type(0) {}
	explicit ResourceType(const char* type_name);
	uint32 type;
	bool operator !=(const ResourceType& rhs) const { return rhs.type != type; }
	bool operator ==(const ResourceType& rhs) const { return rhs.type == type; }
};
inline bool isValid(ResourceType type) { return type.type != 0; }
const ResourceType INVALID_RESOURCE_TYPE("");


class LUMIX_ENGINE_API Resource
{
public:
	friend class ResourceManagerBase;

	enum class State : uint32
	{
		EMPTY = 0,
		READY,
		FAILURE,
	};

	typedef DelegateList<void(State, State, Resource&)> ObserverCallback;

public:
	State getState() const { return m_current_state; }

	bool isEmpty() const { return State::EMPTY == m_current_state; }
	bool isReady() const { return State::READY == m_current_state; }
	bool isFailure() const { return State::FAILURE == m_current_state; }
	uint32 getRefCount() const { return m_ref_count; }
	ObserverCallback& getObserverCb() { return m_cb; }
	size_t size() const { return m_size; }
	const Path& getPath() const { return m_path; }
	ResourceManagerBase& getResourceManager() { return m_resource_manager; }

	template <typename C, void (C::*Function)(State, State, Resource&)> void onLoaded(C* instance)
	{
		m_cb.bind<C, Function>(instance);
		if (isReady())
		{
			(instance->*Function)(State::READY, State::READY, *this);
		}
	}

protected:
	Resource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator);
	virtual ~Resource();

	virtual void onBeforeReady() {}
	virtual void unload(void) = 0;
	virtual bool load(FS::IFile& file) = 0;

	void onCreated(State state);
	void doUnload();

	void addDependency(Resource& dependent_resource);
	void removeDependency(Resource& dependent_resource);

protected:
	State m_desired_state;
	uint16 m_empty_dep_count;
	size_t m_size;
	ResourceManagerBase& m_resource_manager;

protected:
	void checkState();

private:
	void doLoad();
	void fileLoaded(FS::IFile& file, bool success);
	void onStateChanged(State old_state, State new_state, Resource&);
	uint32 addRef(void) { return ++m_ref_count; }
	uint32 remRef(void) { return --m_ref_count; }

	Resource(const Resource&);
	void operator=(const Resource&);

private:
	ObserverCallback m_cb;
	Path m_path;
	uint16 m_ref_count;
	uint16 m_failed_dep_count;
	State m_current_state;
	uint32 m_async_op;
}; // class Resource


} // namespace Lumix
