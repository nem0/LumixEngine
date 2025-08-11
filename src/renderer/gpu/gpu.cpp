#include "gpu.h"
#include "core/page_allocator.h"
#include "core/array.h"
#include "core/hash.h"
#include "core/hash_map.h"
#include "core/log.h"
#include "core/math.h"
#include "core/stream.h"
#include "core/sync.h"
#include "core/os.h"
#include "core/profiler.h"
#include "core/string.h"

namespace Lumix {

namespace gpu {

u32 VertexDecl::getStride() const {
	u32 stride = 0;
	for (u32 i = 0; i < attributes_count; ++i) {
		stride += attributes[i].components_count * getSize(attributes[i].type);
	}
	return stride;
}

void VertexDecl::computeHash() {
	hash = RuntimeHash32(attributes, sizeof(Attribute) * attributes_count).getHashValue() ^ static_cast<u8>(primitive_type);
}

void VertexDecl::setPrimitiveType(gpu::PrimitiveType type) {
	primitive_type = type;
	hash = RuntimeHash32(attributes, sizeof(Attribute) * attributes_count).getHashValue() ^ static_cast<u8>(primitive_type);
}

VertexDecl::VertexDecl(PrimitiveType pt) {
	primitive_type = pt;
	hash = RuntimeHash32(attributes, sizeof(Attribute) * attributes_count).getHashValue() ^ static_cast<u8>(primitive_type);
}

void VertexDecl::addAttribute(u8 byte_offset, u8 components_num, AttributeType type, u8 flags)
{
	if(attributes_count >= lengthOf(attributes)) {
		ASSERT(false);
		return;
	}

	Attribute& attr = attributes[attributes_count];
	attr.components_count = components_num;
	attr.flags = flags;
	attr.type = type;
	attr.byte_offset = byte_offset;
	++attributes_count;
	hash = RuntimeHash32(attributes, sizeof(Attribute) * attributes_count).getHashValue() ^ static_cast<u8>(primitive_type);
}

int getSize(AttributeType type)
{
	switch(type) {
		case AttributeType::FLOAT: return 4;
		case AttributeType::I8: return 1;
		case AttributeType::U8: return 1;
		case AttributeType::I16: return 2;
		case AttributeType::U16: return 2;
		case AttributeType::U32: return 4;
	}
	ASSERT(false);
	return 0;
}

u32 getBytesPerPixel(gpu::TextureFormat format) {
	switch (format) {
		case gpu::TextureFormat::R8:
			return 1;

		case gpu::TextureFormat::R16F:
		case gpu::TextureFormat::R16:
			return 2;
		case gpu::TextureFormat::SRGB:
			return 3;
		case gpu::TextureFormat::R11G11B10F:
		case gpu::TextureFormat::R32F:
		case gpu::TextureFormat::SRGBA:
		case gpu::TextureFormat::RGBA8:
			return 4;
		case gpu::TextureFormat::RGBA16:
		case gpu::TextureFormat::RGBA16F:
			return 8;
		case gpu::TextureFormat::RGBA32F:
			return 16;
		default:
			ASSERT(false);
			return 0;
	}
}

} // namespace gpu

} // namespace Lumix
