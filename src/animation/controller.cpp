#include "controller.h"
#include "animation/animation.h"
#include "engine/blob.h"
#include "engine/log.h"
#include "engine/resource_manager.h"
#include "engine/resource_manager_base.h"


namespace Lumix
{


static const ResourceType ANIMATION_TYPE("animation");


namespace Anim
{


struct Header
{
	static const u32 FILE_MAGIC = 0x5f4c4143; // == '_LAC'
	u32 magic = FILE_MAGIC;
	int version = (int)ControllerResource::Version::LAST;
	u32 reserved[4] = {0};
};


ControllerResource::ControllerResource(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_root(nullptr)
	, m_allocator(allocator)
	, m_animation_set(allocator)
	, m_sets_names(allocator)
{
}


ControllerResource::~ControllerResource()
{
	unload();
}


void ControllerResource::clearAnimationSets()
{
	for (AnimSetEntry& entry : m_animation_set)
	{
		if (!entry.animation) continue;

		removeDependency(*entry.animation);
		entry.animation->getResourceManager().unload(*entry.animation);
	}
	m_animation_set.clear();
}


void ControllerResource::unload()
{
	LUMIX_DELETE(m_allocator, m_root);
	m_root = nullptr;
	clearAnimationSets();
}


bool ControllerResource::load(FS::IFile& file)
{
	InputBlob blob(file.getBuffer(), (int)file.size());
	return deserialize(blob);
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
	if (header.version > (int)Version::LAST)
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

	clearAnimationSets();
	auto* manager = m_resource_manager.getOwner().get(ANIMATION_TYPE);

	int count = blob.read<int>();
	m_animation_set.reserve(count);
	for (int i = 0; i < count; ++i)
	{
		int set = 0;
		if (header.version > (int)Version::ANIMATION_SETS) set = blob.read<int>();
		u32 key = blob.read<u32>();
		char path[MAX_PATH_LENGTH];
		blob.readString(path, lengthOf(path));
		Animation* anim = path[0] ? (Animation*)manager->load(Path(path)) : nullptr;
		addAnimation(set, key, anim);
	}
	if (header.version > (int)Version::ANIMATION_SETS)
	{
		count = blob.read<int>();
		m_sets_names.resize(count);
		for (int i = 0; i < count; ++i)
		{
			blob.readString(m_sets_names[i].data, lengthOf(m_sets_names[i].data));
		}
	}
	else
	{
		m_sets_names.emplace("default");
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
	blob.write(m_animation_set.size());
	for (AnimSetEntry& entry : m_animation_set)
	{
		blob.write(entry.set);
		blob.write(entry.hash);
		blob.writeString(entry.animation ? entry.animation->getPath().c_str() : "");
	}
	blob.write(m_sets_names.size());
	for (const StaticString<32>& name : m_sets_names)
	{
		blob.writeString(name);
	}
}


void ControllerResource::addAnimation(int set, u32 hash, Animation* animation)
{
	m_animation_set.push({set, hash, animation});
	if(animation) addDependency(*animation);
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