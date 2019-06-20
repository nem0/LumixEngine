#include "animation/property_animation.h"
#include "engine/crc32.h"
#include "engine/allocator.h"
#include "engine/json_serializer.h"
#include "engine/log.h"
#include "engine/reflection.h"


namespace Lumix
{


const ResourceType PropertyAnimation::TYPE("property_animation");


PropertyAnimation::PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
	, fps(30)
	, curves(allocator)
{
}


PropertyAnimation::Curve& PropertyAnimation::addCurve()
{
	return curves.emplace(m_allocator);
}


bool PropertyAnimation::save(JsonSerializer& serializer)
{
	if (!isReady()) return false;

	serializer.beginArray();
	for (Curve& curve : curves)
	{
		serializer.beginObject();
		serializer.serialize("component", Reflection::getComponent(curve.cmp_type)->name);
		serializer.serialize("property", curve.property->name);
		serializer.beginArray("keys");
		for (int i = 0; i < curve.frames.size(); ++i)
		{
			serializer.beginObject();
			serializer.serialize("frame", curve.frames[i]);
			serializer.serialize("value", curve.values[i]);
			serializer.endObject();
		}
		serializer.endArray();
		serializer.endObject();
	}
	serializer.endArray();

	return true;
}


bool PropertyAnimation::load(u64 size, const u8* mem)
{
	InputMemoryStream file(mem, size);
	JsonDeserializer serializer(file, getPath(), m_allocator);
	if (serializer.isError()) return false;
	
	serializer.deserializeArrayBegin();
	while (!serializer.isArrayEnd())
	{
		serializer.nextArrayItem();
		Curve& curve = curves.emplace(m_allocator);
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
