#include "animation/property_animation.h"
#include "core/allocator.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine/reflection.h"


namespace Lumix {


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


void PropertyAnimation::deserialize(InputMemoryStream& blob) {
	bool res = load(Span((const u8*)blob.getData(), (u32)blob.size()));
	ASSERT(res);
}

bool PropertyAnimation::load(Span<const u8> mem) {
	InputMemoryStream stream(mem);
	Header header;
	stream.read(header);
	if (header.magic != Header::MAGIC) {
		logError(getPath(), ": invalid file");
		return false;
	}
	if (header.version != 0) {
		logError(getPath(), ": unsupported version");
		return false;
	}

	const u32 num_curves = stream.read<u32>();
	curves.reserve(num_curves);
	for (u32 i = 0; i < num_curves; ++i) {
		Curve& curve = curves.emplace(m_allocator);
		const char* cmp_typename = stream.readString();
		const char* property_name = stream.readString();
		const u32 num_frames = stream.read<u32>();
		curve.cmp_type = reflection::getComponentType(cmp_typename);
		curve.property = static_cast<const reflection::Property<float>*>(reflection::getProperty(curve.cmp_type, property_name));
		curve.frames.resize(num_frames);
		curve.values.resize(num_frames);
		stream.read(curve.frames.begin(), curve.frames.byte_size());
		stream.read(curve.values.begin(), curve.values.byte_size());
	}

	return !stream.hasOverflow();
}


void PropertyAnimation::unload()
{
	curves.clear();
}


} // namespace Lumix
