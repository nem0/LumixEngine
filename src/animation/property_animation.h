#pragma once

#include "engine/resource.h"
#include "animation.h"

namespace Lumix {

namespace reflection { template <typename T> struct Property; }

struct PropertyAnimation final : Resource {
	enum class Version {
		TRANSFORM,
		TIME,
		
		LATEST
	};

	enum class CurveType : u32 {
		NOT_SET,
		PROPERTY,
		LOCAL_POS_X,
		LOCAL_POS_Y,
		LOCAL_POS_Z,
		POS_X,
		POS_Y,
		POS_Z,
		SCALE_X,
		SCALE_Y,
		SCALE_Z,
	};

	struct Curve {
		Curve(IAllocator& allocator) : frames(allocator), values(allocator) {}

		CurveType type = CurveType::NOT_SET;
		ComponentType cmp_type;
		const reflection::Property<float>* property = nullptr;
		
		Array<Time> frames;
		Array<float> values;
	};

	struct Header {
		static const u32 MAGIC = '_PRA';
		
		u32 magic = MAGIC;
		Version version = Version::LATEST;
	};

	PropertyAnimation(const Path& path, ResourceManager& resource_manager, IAllocator& allocator);

	ResourceType getType() const override { return TYPE; }
	Curve& addCurve();
	void deserialize(struct InputMemoryStream& blob);

	IAllocator& m_allocator;
	Array<Curve> curves;
	Time length;

	static const ResourceType TYPE;

private:
	void unload() override;
	bool load(Span<const u8> mem) override;
};


} // namespace Lumix
