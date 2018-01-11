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
{
}



bool PropertyAnimation::load(FS::IFile& file)
{
	auto& manager = (PropertyAnimationManager&)getResourceManager();
	JsonSerializer serializer(file, JsonSerializer::READ, getPath(), manager.getAllocator());
	if (serializer.isError()) return false;
	
	serializer.deserializeArrayBegin();
	while (!serializer.isArrayEnd())
	{
		serializer.nextArrayItem();
		Curve& curve = curves.emplace(manager.getAllocator());
		serializer.deserializeObjectBegin();
		u32 prop_hash = 0;
		while (!serializer.isObjectEnd())
		{
			char tmp[32];
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
			else if (equalIStrings(tmp, "keys"))
			{
				serializer.deserializeArrayBegin();
				while (!serializer.isArrayEnd())
				{
					serializer.nextArrayItem();
					serializer.deserializeObjectBegin();
					while (!serializer.isObjectEnd())
					{
						serializer.deserializeLabel(tmp, lengthOf(tmp));
						if (equalIStrings(tmp, "frame"))
						{
							int frame;
							serializer.deserialize(frame, 0);
							curve.frames.push(frame);
						}
						else if (equalIStrings(tmp, "value"))
						{
							float value;
							serializer.deserialize(value, 0);
							curve.values.push(value);
						}
						else
						{
							g_log_error.log("Animation") << "Unknown key " << tmp;
							curves.clear();
							return false;
						}
					}
					if (curve.values.size() != curve.frames.size())
					{
						g_log_error.log("Animation") << "Key without " << (curve.values.size() < curve.frames.size() ? "value" : "frame");
						curves.clear();
						return false;
					}

					serializer.deserializeObjectEnd();
				}
				serializer.deserializeArrayEnd();
			}
			else
			{
				g_log_error.log("Animation") << "Unknown key " << tmp;
				curves.clear();
				return false;
			}
		}
		serializer.deserializeObjectEnd();
		curve.property = Reflection::getProperty(curve.cmp_type, prop_hash);
	}
	serializer.deserializeArrayEnd();

	return true;
}


void PropertyAnimation::unload()
{
	curves.clear();
}


} // namespace Lumix
