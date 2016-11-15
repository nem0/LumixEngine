#include "controller.h"


namespace Lumix
{


namespace Anim
{


ControllerResource::ControllerResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_root(nullptr)
	, m_allocator(allocator)
{
}


void ControllerResource::unload(void)
{
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
}


bool ControllerResource::load(FS::IFile& file)
{
	InputBlob blob(file.getBuffer(), (int)file.size());
	deserialize(blob);
	return true;
}


void ControllerResource::setRoot(Component* component)
{
	ASSERT(m_root == nullptr);
	m_root = component;
}


void ControllerResource::deserialize(InputBlob& blob)
{
	Component::Type type;
	blob.read(type);
	m_root = createComponent(type, m_allocator);
	m_root->deserialize(blob, nullptr);
}


void ControllerResource::serialize(OutputBlob& blob)
{
	blob.write(m_root->type);
	m_root->serialize(blob);
}


ComponentInstance* ControllerResource::createInstance(IAllocator& allocator)
{
	return m_root ? m_root->createInstance(allocator) : nullptr;
}



Resource* ControllerManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, ControllerResource)(path, *this, m_allocator);
}


void ControllerManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, (ControllerResource*)&resource);
}



} // namespace Anim


} // namespace Lumix