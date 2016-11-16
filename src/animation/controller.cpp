#include "controller.h"
#include "animation/animation.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


static const ResourceType ANIMATION_TYPE("animation");


namespace Anim
{


ControllerResource::ControllerResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_root(nullptr)
	, m_allocator(allocator)
	, m_anim_set(allocator)
{
}


ControllerResource::~ControllerResource()
{
	ASSERT(!m_root);
	ASSERT(m_anim_set.empty());
}


void ControllerResource::unload(void)
{
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
	for (Animation* anim : m_anim_set)
	{
		anim->getResourceManager().unload(*anim);
	}
	m_anim_set.clear();
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
	blob.read(m_input_decl.inputs_count);
	for (int i = 0; i < m_input_decl.inputs_count; ++i)
	{
		auto& input = m_input_decl.inputs[i];
		blob.readString(input.name, lengthOf(input.name));
		blob.read(input.type);
		blob.read(input.offset);
	}
	m_anim_set.clear();
	int count;
	blob.read(count);
	auto* manager = m_resource_manager.getOwner().get(ANIMATION_TYPE);
	for (int i = 0; i < count; ++i)
	{
		uint32 key;
		char path[MAX_PATH_LENGTH];
		blob.read(key);
		blob.readString(path, lengthOf(path));
		Animation* anim = path[0] ? (Animation*)manager->load(Path(path)) : nullptr;
		if(anim) addDependency(*anim);
		m_anim_set.insert(key, anim);
	}
}


void ControllerResource::serialize(OutputBlob& blob)
{
	blob.write(m_root->type);
	m_root->serialize(blob);
	blob.write(m_input_decl.inputs_count);
	for (int i = 0; i < m_input_decl.inputs_count; ++i)
	{
		auto& input = m_input_decl.inputs[i];
		blob.writeString(input.name);
		blob.write(input.type);
		blob.write(input.offset);
	}
	blob.write(m_anim_set.size());
	for (auto iter = m_anim_set.begin(), end = m_anim_set.end(); iter != end; ++iter)
	{
		blob.write(iter.key());
		blob.writeString(iter.value() ? iter.value()->getPath().c_str() : "");
	}
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