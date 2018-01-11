#include "animation/property_animation.h"
#include "engine/crc32.h"
#include "engine/iallocator.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/reflection.h"


namespace Lumix
{

	
Resource* PropertyAnimationManager::createResource(const Path& path)
{
	return LUMIX_NEW(m_allocator, PropertyAnimation)(path, *this, m_allocator);
}


void PropertyAnimationManager::destroyResource(Resource& resource)
{
	LUMIX_DELETE(m_allocator, static_cast<PropertyAnimation*>(&resource));
}


PropertyAnimation::PropertyAnimation(const Path& path, ResourceManagerBase& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, fps(30)
	, curves(allocator)
	, keys(allocator)
{
}



bool PropertyAnimation::load(FS::IFile& file)
{
	auto& manager = (PropertyAnimationManager&)getResourceManager();
	JsonSerializer serializer(file, JsonSerializer::READ, getPath(), manager.getAllocator());
	if (serializer.isError()) return false;
	
	serializer.deserializeObjectBegin();
	while (!serializer.isObjectEnd())
	{
		char tmp[32];
		serializer.deserializeLabel(tmp, lengthOf(tmp));
		if (equalIStrings(tmp, "curves"))
		{
			serializer.deserializeArrayBegin();
			while (!serializer.isArrayEnd())
			{
				serializer.nextArrayItem();
				Curve& curve = curves.emplace();
				serializer.deserializeObjectBegin();
				u32 prop_hash = 0;
				while (!serializer.isObjectEnd())
				{
					serializer.deserializeLabel(tmp, lengthOf(tmp));
					if (equalIStrings(tmp, "component"))
					{
						serializer.deserialize(tmp, lengthOf(tmp), "");
						curve.cmp_type = Reflection::getComponentType(tmp);
					}
					else if (equalIStrings(tmp, "property"))
					{
						serializer.deserialize(tmp, lengthOf(tmp), "");
						prop_hash = crc32(tmp);
					}
					else
					{
						g_log_error.log("Animation") << "Unknown key " << tmp;
						goto fail;
					}
				}
				serializer.deserializeObjectEnd();
				curve.property = Reflection::getProperty(curve.cmp_type, prop_hash);
			}
			serializer.deserializeArrayEnd();
		}
		else if (equalIStrings(tmp, "keys"))
		{
			serializer.deserializeArrayBegin();
			while (!serializer.isArrayEnd())
			{
				serializer.nextArrayItem();
				Key& key = keys.emplace();
				serializer.deserializeObjectBegin();
				while (!serializer.isObjectEnd())
				{
					serializer.deserializeLabel(tmp, lengthOf(tmp));
					if (equalIStrings(tmp, "frame"))
					{
						serializer.deserialize(key.frame, 0);
					}
					else if (equalIStrings(tmp, "curve"))
					{
						serializer.deserialize(key.curve, 0);
					}
					else if (equalIStrings(tmp, "value"))
					{
						serializer.deserialize(key.value, 0);
					}
					else
					{
						g_log_error.log("Animation") << "Unknown key " << tmp;
						goto fail;
					}
				}
				serializer.deserializeObjectEnd();
			}
			serializer.deserializeArrayEnd();
		}
		else
		{
			g_log_error.log("Animation") << "Unknown key " << tmp;
			goto fail;
		}
	}

	return true;
	
	fail:
		curves.clear();
		keys.clear();
		return false;
}


void PropertyAnimation::unload()
{
	curves.clear();
	keys.clear();
}


} // namespace Lumix
