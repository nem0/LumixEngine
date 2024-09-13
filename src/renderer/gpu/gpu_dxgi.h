#include "renderer/gpu/gpu.h"
#include <dxgi.h>
#include <dxgi1_6.h>

namespace Lumix::gpu {

static DXGI_FORMAT getDXGIFormat(const Attribute& attr) {
	const bool as_int = attr.flags & Attribute::AS_INT;
	switch (attr.type) {
		case AttributeType::FLOAT:
			switch (attr.components_count) {
				case 1: return DXGI_FORMAT_R32_FLOAT;
				case 2: return DXGI_FORMAT_R32G32_FLOAT;
				case 3: return DXGI_FORMAT_R32G32B32_FLOAT;
				case 4: return DXGI_FORMAT_R32G32B32A32_FLOAT;
			}
			break;

		case AttributeType::I8: 
			switch(attr.components_count) {
				case 1: return as_int ? DXGI_FORMAT_R8_SINT : DXGI_FORMAT_R8_SNORM;
				case 2: return as_int ? DXGI_FORMAT_R8G8_SINT : DXGI_FORMAT_R8G8_SNORM;
				case 4: return as_int ? DXGI_FORMAT_R8G8B8A8_SINT : DXGI_FORMAT_R8G8B8A8_SNORM;
			}
			break;
		case AttributeType::U8: 
			switch(attr.components_count) {
				case 1: return as_int ? DXGI_FORMAT_R8_UINT : DXGI_FORMAT_R8_UNORM;
				case 2: return as_int ? DXGI_FORMAT_R8G8_UINT : DXGI_FORMAT_R8G8_UNORM;
				case 4: return as_int ? DXGI_FORMAT_R8G8B8A8_UINT : DXGI_FORMAT_R8G8B8A8_UNORM;
			}
			break;
		case AttributeType::I16: 
			switch(attr.components_count) {
				case 4: return as_int ? DXGI_FORMAT_R16G16B16A16_SINT : DXGI_FORMAT_R16G16B16A16_SNORM;
			}
			break;
	}
	ASSERT(false);
	return DXGI_FORMAT_R32_FLOAT;
}

static u32 sizeDXTC(u32 w, u32 h, DXGI_FORMAT format) {
	const bool is_dxt1 = format == DXGI_FORMAT_BC1_UNORM || format == DXGI_FORMAT_BC1_UNORM_SRGB;
	const bool is_ati = format == DXGI_FORMAT_BC4_UNORM;
	return ((w + 3) / 4) * ((h + 3) / 4) * (is_dxt1 || is_ati ? 8 : 16);
}

struct FormatDesc {
	bool compressed;
	u32 block_bytes;
	DXGI_FORMAT internal;
	DXGI_FORMAT internal_srgb;

	u32 getRowPitch(u32 w) const {
		if (compressed) {
			return (w + 3) / 4 * block_bytes;
		}

		return w * block_bytes;
	}
	
	static FormatDesc get(DXGI_FORMAT format) {
		switch(format) {
			case DXGI_FORMAT_BC1_UNORM : return get(TextureFormat::BC1);
			case DXGI_FORMAT_BC2_UNORM : return get(TextureFormat::BC2);
			case DXGI_FORMAT_BC3_UNORM : return get(TextureFormat::BC3);
			case DXGI_FORMAT_BC4_UNORM : return get(TextureFormat::BC4);
			case DXGI_FORMAT_BC5_UNORM : return get(TextureFormat::BC5);
			case DXGI_FORMAT_R16_UNORM : return get(TextureFormat::R16);
			case DXGI_FORMAT_R8_UNORM : return get(TextureFormat::R8);
			case DXGI_FORMAT_R8G8_UNORM : return get(TextureFormat::RG8);
			case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : return get(TextureFormat::SRGBA);
			case DXGI_FORMAT_R8G8B8A8_UNORM : return get(TextureFormat::RGBA8);
			case DXGI_FORMAT_R16G16B16A16_UNORM : return get(TextureFormat::RGBA16);
			case DXGI_FORMAT_R16G16B16A16_FLOAT : return get(TextureFormat::RGBA16F);
			case DXGI_FORMAT_R32G32B32A32_FLOAT : return get(TextureFormat::RGBA32F);
			case DXGI_FORMAT_R11G11B10_FLOAT : return get(TextureFormat::R11G11B10F);
			case DXGI_FORMAT_R32G32_FLOAT : return get(TextureFormat::RG32F);
			case DXGI_FORMAT_R32G32B32_FLOAT : return get(TextureFormat::RGB32F);
			case DXGI_FORMAT_R16G16_FLOAT : return get(TextureFormat::RG16F);
			
			case DXGI_FORMAT_R32_TYPELESS : return get(TextureFormat::D32);
			case DXGI_FORMAT_R24G8_TYPELESS : return get(TextureFormat::D24S8);
			default: ASSERT(false); return {}; 
		}
	}

	static FormatDesc get(TextureFormat format) {
		switch(format) {
			case TextureFormat::BC1: return {			true,		8,	DXGI_FORMAT_BC1_UNORM,				DXGI_FORMAT_BC1_UNORM_SRGB};
			case TextureFormat::BC2: return {			true,		16,	DXGI_FORMAT_BC2_UNORM,				DXGI_FORMAT_BC2_UNORM_SRGB};
			case TextureFormat::BC3: return {			true,		16,	DXGI_FORMAT_BC3_UNORM,				DXGI_FORMAT_BC3_UNORM_SRGB};
			case TextureFormat::BC4: return {			true,		8,	DXGI_FORMAT_BC4_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::BC5: return {			true,		16,	DXGI_FORMAT_BC5_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R16: return {			false,		2,	DXGI_FORMAT_R16_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG16: return {			false,		4,	DXGI_FORMAT_R16G16_UNORM,			DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R8: return {			false,		1,	DXGI_FORMAT_R8_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG8: return {			false,		2,	DXGI_FORMAT_R8G8_UNORM,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::BGRA8: return {			false,		4,	DXGI_FORMAT_B8G8R8A8_UNORM,			DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
			case TextureFormat::SRGBA: return {			false,		4,	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,	DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
			case TextureFormat::RGBA8: return {			false,		4,	DXGI_FORMAT_R8G8B8A8_UNORM,			DXGI_FORMAT_R8G8B8A8_UNORM_SRGB};
			case TextureFormat::RGBA16: return {		false,		8,	DXGI_FORMAT_R16G16B16A16_UNORM,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R11G11B10F: return {	false,		4,	DXGI_FORMAT_R11G11B10_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RGBA16F: return {		false,		8,	DXGI_FORMAT_R16G16B16A16_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RGBA32F: return {		false,		16, DXGI_FORMAT_R32G32B32A32_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG32F: return {			false,		8,	DXGI_FORMAT_R32G32_FLOAT,			DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RGB32F: return {		false,		12,	DXGI_FORMAT_R32G32B32_FLOAT,		DXGI_FORMAT_UNKNOWN};
			case TextureFormat::R32F: return {			false,		4,	DXGI_FORMAT_R32_FLOAT,				DXGI_FORMAT_UNKNOWN};
			case TextureFormat::RG16F: return {			false,		4,	DXGI_FORMAT_R16G16_FLOAT,			DXGI_FORMAT_UNKNOWN};

			case TextureFormat::D32: return {			false,		4,	DXGI_FORMAT_R32_TYPELESS,			DXGI_FORMAT_UNKNOWN};
			case TextureFormat::D24S8: return {			false,		4,	DXGI_FORMAT_R24G8_TYPELESS,			DXGI_FORMAT_UNKNOWN};
			default: ASSERT(false); return {}; 
		}
	}
};

u32 getSize(TextureFormat format, u32 w, u32 h) {
	const FormatDesc& desc = FormatDesc::get(format);
	if (desc.compressed) return sizeDXTC(w, h, desc.internal);
	return desc.block_bytes * w * h;
}

static u32 getSize(DXGI_FORMAT format) {
	switch(format) {
		case DXGI_FORMAT_R8_UNORM: return 1;
		case DXGI_FORMAT_R8G8_UNORM: return 2;
		case DXGI_FORMAT_R32_TYPELESS: return 4;
		case DXGI_FORMAT_R24G8_TYPELESS: return 4;
		case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return 4;
		case DXGI_FORMAT_R8G8B8A8_UNORM: return 4;
		case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return 4;
		case DXGI_FORMAT_B8G8R8A8_UNORM: return 4;
		case DXGI_FORMAT_R16G16B16A16_UNORM: return 8;
		case DXGI_FORMAT_R16G16B16A16_FLOAT: return 8;
		case DXGI_FORMAT_R32G32_FLOAT: return 8;
		case DXGI_FORMAT_R32G32B32_FLOAT: return 12;
		case DXGI_FORMAT_R32G32B32A32_FLOAT: return 16;
		case DXGI_FORMAT_R16_UNORM: return 2;
		case DXGI_FORMAT_R16_FLOAT: return 2;
		case DXGI_FORMAT_R32_FLOAT: return 4;
		default: ASSERT(false); return 0;
	}
}

static DXGI_FORMAT getDXGIFormat(TextureFormat format, bool is_srgb) {
	const FormatDesc& fd = FormatDesc::get(format);
	return is_srgb && fd.internal_srgb != DXGI_FORMAT_UNKNOWN ? fd.internal_srgb : fd.internal;
}

} // namespace Lumix::gpu