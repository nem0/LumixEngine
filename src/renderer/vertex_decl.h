#pragma once

#include "core/core.h"
#include "engine/lumix.h"

namespace Lumix {

// Layout of vertices on the CPU side (models, procedural geometry). The GPU layer knows nothing about it: vertices are pulled by shaders,
// the layout is passed to them as defines, see Renderer::getSemanticDefines. It is serialized, keep the order and the size of the members.

enum class AttributeType : u8 {
	U8,
	FLOAT,
	I16,
	I8,
	U16,
	U32
};

inline int getSize(AttributeType type) {
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

#pragma pack(1)
struct VertexAttribute {
	enum Flags {
		NORMALIZED = 1 << 0
	};
	u8 components_count;
	u8 byte_offset;
	AttributeType type;
	u8 flags;
};
#pragma pack()

struct VertexDecl {
	enum { MAX_ATTRIBUTES = 16 };

	void addAttribute(u8 byte_offset, u8 components_num, AttributeType type, u8 flags) {
		if (attributes_count >= MAX_ATTRIBUTES) {
			ASSERT(false);
			return;
		}

		VertexAttribute& attr = attributes[attributes_count];
		attr.components_count = components_num;
		attr.flags = flags;
		attr.type = type;
		attr.byte_offset = byte_offset;
		++attributes_count;
	}

	// the decl can come from a file, check it before using it
	bool isValid() const { return attributes_count <= MAX_ATTRIBUTES; }

	u32 getStride() const {
		u32 stride = 0;
		for (u32 i = 0; i < attributes_count; ++i) {
			stride += attributes[i].components_count * getSize(attributes[i].type);
		}
		return stride;
	}

	u8 attributes_count = 0;
	VertexAttribute attributes[MAX_ATTRIBUTES];
};

} // namespace Lumix
