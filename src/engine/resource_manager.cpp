#include "engine/black.h.h"

#include "core/array.h"
#include "core/log.h"
#include "core/stream.h"

#include "engine/resource.h"
#include "engine/resource_manager.h"


namespace black
{

void ResourceManager::create(ResourceType type, ResourceManagerHub& owner)
{
	owner.add(type, this);
	m_owner = &owner;
}

void ResourceManager::destroy()
{
	for (Resource* resource : m_resources) {
		if (!resource->isEmpty()) {
			logError("Leaking resource ", resource->getPath(), "\n");
		}
		destroyResource(*resource);
	}
	m_resources.clear();
}

Resource* ResourceManager::get(const Path& path)
{
	ResourceTable::Iterator it = m_resources.find(path.getHash());

	if(m_resources.end() != it)
	{
		return *it;
	}

	return nullptr;
}

Resource* ResourceManager::load(const Path& path)
{
	if (path.isEmpty()) return nullptr;
	Resource* resource = get(path);

	if(nullptr == resource)
	{
		resource = createResource(path);
		m_resources.insert(path.getHash(), resource);
	}

	if(resource->isEmpty() && resource->m_desired_state == Resource::State::EMPTY)
	{
		if (m_owner->onBeforeLoad(*resource) == ResourceManagerHub::LoadHook::Action::DEFERRED)
		{
			ASSERT(!resource->m_hooked);
			resource->m_hooked = true;
			resource->m_desired_state = Resource::State::READY;
			resource->incRefCount(); // for hook
			resource->incRefCount(); // for return value
			return resource;
		}
		resource->doLoad();
	}

	resource->incRefCount();
	return resource;
}

void ResourceManager::removeUnreferenced()
{
	if (!m_is_unload_enabled) return;

	Array<Resource*> to_remove(m_allocator);
	for (auto* i : m_resources)
	{
		if (i->getRefCount() == 0) to_remove.push(i);
	}

	for (auto* i : to_remove)
	{
		auto iter = m_resources.find(i->getPath().getHash());
		if (iter.value()->isReady()) iter.value()->doUnload();
	}
}

void ResourceManager::reload(const Path& path)
{
	Resource* resource = get(path);
	if (resource) reload(*resource);
}

void ResourceManager::reload(Resource& resource)
{
	if (resource.m_current_state != Resource::State::EMPTY) {
		resource.doUnload();
	}
	else if (resource.m_desired_state == Resource::State::READY) return;

	if (m_owner->onBeforeLoad(resource) == ResourceManagerHub::LoadHook::Action::DEFERRED)
	{
		ASSERT(!resource.m_hooked);
		resource.m_hooked = true;
		resource.m_desired_state = Resource::State::READY;
		resource.incRefCount(); // for hook
		resource.incRefCount(); // for return value
	}
	else {
		resource.doLoad();
	}
}

void ResourceManager::enableUnload(bool enable)
{
	m_is_unload_enabled = enable;
	if (!enable) return;

	for (auto* resource : m_resources)
	{
		if (resource->getRefCount() == 0)
		{
			resource->doUnload();
		}
	}
}

ResourceManager::ResourceManager(IAllocator& allocator)
	: m_resources(allocator)
	, m_allocator(allocator)
	, m_owner(nullptr)
	, m_is_unload_enabled(true)
{ }

ResourceManager::~ResourceManager()
{
	ASSERT(m_resources.empty());
}

ResourceManagerHub::ResourceManagerHub(Engine& engine, IAllocator& allocator) 
	: m_resource_managers(allocator)
	, m_engine(engine)
	, m_allocator(allocator)
	, m_load_hook(nullptr)
	, m_file_system(nullptr)
{
}

ResourceManagerHub::~ResourceManagerHub() = default;


void ResourceManagerHub::init(FileSystem& fs)
{
	m_file_system = &fs;
}

bool ResourceManagerHub::loadRaw(const Path& included_from, const Path& path, OutputMemoryStream& data) {
	if (m_load_hook) m_load_hook->loadRaw(included_from, path);
	return m_file_system->getContentSync(path, data);
}

Resource* ResourceManagerHub::load(ResourceType type, const Path& path)
{
	ResourceManager* manager = get(type);
	if(!manager) return nullptr;
	return load(*manager, path);
}
	
Resource* ResourceManagerHub::load(ResourceManager& manager, const Path& path)
{
	return manager.load(path);
}

ResourceManager* ResourceManagerHub::get(ResourceType type)
{
	auto iter = m_resource_managers.find(type); 
	if (!iter.isValid()) return nullptr;
	return iter.value();
}

void ResourceManagerHub::LoadHook::continueLoad(Resource& resource, bool success)
{
	ASSERT(resource.isEmpty());
	resource.decRefCount(); // release from hook
	resource.m_hooked = false;
	if (success) {
		resource.m_desired_state = Resource::State::EMPTY;
		resource.doLoad();
	}
	else {
		resource.m_current_state = Resource::State::FAILURE;
	}
}

void ResourceManagerHub::setLoadHook(LoadHook* hook)
{
	ASSERT(!m_load_hook || !hook);
	m_load_hook = hook;

	if (m_load_hook) {
		for (ResourceManager* rm : m_resource_managers) {
			for (Resource* res : rm->getResourceTable()) {
				if (res->isFailure()) {
					rm->reload(*res);
				}
			}
		}
	}
}

ResourceManagerHub::LoadHook::Action ResourceManagerHub::onBeforeLoad(Resource& resource) const
{
	return m_load_hook ? m_load_hook->onBeforeLoad(resource) : LoadHook::Action::IMMEDIATE;
}

void ResourceManagerHub::add(ResourceType type, ResourceManager* rm)
{ 
	m_resource_managers.insert(type, rm);
}

void ResourceManagerHub::remove(ResourceType type)
{ 
	m_resource_managers.erase(type);
}

void ResourceManagerHub::removeUnreferenced()
{
	for (auto* manager : m_resource_managers)
	{
		manager->removeUnreferenced();
	}
}

void ResourceManagerHub::enableUnload(bool enable)
{
	for (auto* manager : m_resource_managers)
	{
		manager->enableUnload(enable);
	}
}

void ResourceManagerHub::reloadAll() {
	while (m_file_system->hasWork()) m_file_system->processCallbacks();
	
	Array<Resource*> to_reload(m_allocator);
	for (auto* manager : m_resource_managers) {
		ResourceManager::ResourceTable& resources = manager->getResourceTable();
		for (Resource* res : resources) {
			if (res->isReady()) {
				res->doUnload();
				to_reload.push(res);
			}
		}
	}

	for (Resource* res : to_reload) {
		res->doLoad();
	}
}


void ResourceManagerHub::reload(const Path& path)
{
	for (auto* manager : m_resource_managers)
	{
		manager->reload(path);
	}
}

} // namespace black
