#include "controller.h"
#include "animation/animation.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


static const ResourceType ANIMATION_TYPE("animation");


namespace Anim
{


enum class Version : int
{
	LAST
};


struct Header
{
	static const uint32 FILE_MAGIC = 0x5f4c4143; // == '_LAC'
	uint32 magic = FILE_MAGIC;
	int version = (int)Version::LAST;
	uint32 reserved[4] = {0};
};


ControllerResource::ControllerResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_root(nullptr)
	, m_allocator(allocator)
	, m_anim_set(allocator)
{
}


ControllerResource::~ControllerResource()
{
	unload();
}


void ControllerResource::unload()
{
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
	for (Animation* anim : m_anim_set)
	{
		if (anim)
		{
			removeDependency(*anim);
			anim->getResourceManager().unload(*anim);
		}
	}
	m_anim_set.clear();
}


bool ControllerResource::load(FS::IFile& file)
{
	InputBlob blob(file.getBuffer(), (int)file.size());
	return deserialize(blob);
	return true;
}


void ControllerResource::setRoot(Component* component)
{
	m_root = component;
}


bool ControllerResource::deserialize(InputBlob& blob)
{
	Header header;
	blob.read(header);
	if (header.magic != Header::FILE_MAGIC)
	{
		g_log_error.log("Animation") << getPath().c_str() << " is not an animation controller file.";
		return false;
	}
	if (header.version != (int)Version::LAST)
	{
		g_log_error.log("Animation") << getPath().c_str() << " has unsupported version.";
		return false;
	}
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

	blob.read(m_input_decl.constants_count);
	for (int i = 0; i < m_input_decl.constants_count; ++i)
	{
		auto& constant = m_input_decl.constants[i];
		blob.readString(constant.name, lengthOf(constant.name));
		blob.read(constant.type);
		switch (constant.type)
		{
			case InputDecl::BOOL: blob.read(constant.b_value); break;
			case InputDecl::INT: blob.read(constant.i_value); break;
			case InputDecl::FLOAT: blob.read(constant.f_value); break;
			default: ASSERT(false); return false;
		}
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
	return true;
}


void ControllerResource::serialize(OutputBlob& blob)
{
	Header header;
	blob.write(header);
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
	blob.write(m_input_decl.constants_count);
	for (int i = 0; i < m_input_decl.constants_count; ++i)
	{
		auto& constant = m_input_decl.constants[i];
		blob.writeString(constant.name);
		blob.write(constant.type);
		switch (constant.type)
		{
			case InputDecl::BOOL: blob.write(constant.b_value); break;
			case InputDecl::INT: blob.write(constant.i_value); break;
			case InputDecl::FLOAT: blob.write(constant.f_value); break;
			default: ASSERT(false); return;
		}
		
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