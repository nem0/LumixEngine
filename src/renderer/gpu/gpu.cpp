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

u32 getSize(TextureFormat format, u32 w, u32 h) {
	switch (format) {
		case TextureFormat::BC1:
		case TextureFormat::BC4:
			return ((w + 3) / 4) * ((h + 3) / 4) * 8;
		case TextureFormat::BC2:
		case TextureFormat::BC3:
		case TextureFormat::BC5:
			return ((w + 3) / 4) * ((h + 3) / 4) * 16;
		case TextureFormat::R8: return w * h;
		case TextureFormat::RG8: return w * h * 2;
		case TextureFormat::R16:
		case TextureFormat::R16F: return w * h * 2;
		case TextureFormat::R32F: return w * h * 4;
		case TextureFormat::D32:
		case TextureFormat::D24S8:
		case TextureFormat::RGBA8:
		case TextureFormat::BGRA8:
		case TextureFormat::SRGB:
		case TextureFormat::SRGBA: return w * h * 4;
		case TextureFormat::RGBA16:
		case TextureFormat::RGBA16F:
		case TextureFormat::RG32F: return w * h * 8;
		case TextureFormat::RGBA32F: return w * h * 16;
		case TextureFormat::RGB32F: return w * h * 12;
		case TextureFormat::R11G11B10F: return w * h * 4;
		case TextureFormat::RG16:
		case TextureFormat::RG16F: return w * h * 4;
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
