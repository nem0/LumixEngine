#include "animation/property_animation.h"
#include "core/allocator.h"
#include "core/log.h"
#include "core/stream.h"
#include "engine/reflection.h"


namespace black {

const ResourceType PropertyAnimation::TYPE("property_animation");

PropertyAnimation::PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
	: Resource(path, resource_manager, allocator)
	, m_allocator(allocator)
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

	if (header.version > Version::LATEST) {
		logError(getPath(), ": unsupported version");
		return false;
	}
 
	if (header.version > Version::TIME) {
		stream.read(length);
	}

	const u32 num_curves = stream.read<u32>();
	curves.reserve(num_curves);
	for (u32 i = 0; i < num_curves; ++i) {
		Curve& curve = curves.emplace(m_allocator);
		if (header.version > Version::TRANSFORM) {
			curve.type = stream.read<CurveType>();
		}
		else {
			curve.type = CurveType::PROPERTY;
		}
		switch (curve.type) {
			case CurveType::PROPERTY: {
				const char* cmp_typename = stream.readString();
				const char* property_name = stream.readString();
				const u32 num_frames = stream.read<u32>();
				curve.cmp_type = reflection::getComponentType(cmp_typename);
				curve.property = static_cast<const reflection::Property<float>*>(reflection::getProperty(curve.cmp_type, property_name));
				curve.frames.resize(num_frames);
				curve.values.resize(num_frames);
				stream.read(curve.frames.begin(), curve.frames.byte_size());
				stream.read(curve.values.begin(), curve.values.byte_size());
				break;
			}
			default: {
				const u32 num_frames = stream.read<u32>();
				curve.frames.resize(num_frames);
				curve.values.resize(num_frames);
				stream.read(curve.frames.begin(), curve.frames.byte_size());
				stream.read(curve.values.begin(), curve.values.byte_size());
				break;
			}
		}
	}

	if (header.version <= Version::TIME) {
		length = Time(0);
		for (const Curve& curve : curves) {
			if (curve.frames.empty()) continue;

			// Time::raw() is in frames, convert to Time
			for (Time& t : curve.frames) {
				t = Time::fromSeconds(t.raw() / 30.f);
			}

			// length is not saved in file, calculate it from curves
			length = maximum(length, curve.frames.back());
		}
	}


	return !stream.hasOverflow();
}


void PropertyAnimation::unload()
{
	curves.clear();
}


} // namespace black
