#include "animation/property_animation.h"
#include "engine/crc32.h"
#include "engine/allocator.h"
#include "engine/log.h"
#include "engine/reflection.h"
#include "engine/serializer.h"


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


bool PropertyAnimation::save(TextSerializer& serializer)
{
	if (!isReady()) return false;

	serializer.write("count", curves.size());
	for (Curve& curve : curves)
	{
		serializer.write("component", Reflection::getComponentName(curve.cmp_type));
		serializer.write("property", curve.property.data);
		serializer.write("keys_count", curve.frames.size());
		for (int i = 0; i < curve.frames.size(); ++i)
		{
			serializer.write("frame", curve.frames[i]);
			serializer.write("value", curve.values[i]);
		}
	}

	return true;
}


bool PropertyAnimation::load(u64 size, const u8* mem)
{
	InputMemoryStream file(mem, size);
	TextDeserializer serializer(file);
	
	int count;
	serializer.read(Ref(count));
	for (int i = 0; i < count; ++i) {
		Curve& curve = curves.emplace(m_allocator);
		char tmp[32];
		serializer.read(Span(tmp));
		curve.cmp_type = Reflection::getComponentType(tmp);
		serializer.read(Span(tmp));
		
		int keys_count;
		serializer.read(Ref(keys_count));
		curve.frames.resize(keys_count);
		curve.values.resize(keys_count);
		for (int j = 0; j < keys_count; ++j) {
			serializer.read(Ref(curve.frames[j]));
			serializer.read(Ref(curve.values[j]));
		}
	}
	return true;
}


void PropertyAnimation::unload()
{
	curves.clear();
}


} // namespace Lumix
