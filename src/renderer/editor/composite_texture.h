#pragma once

#include "engine/array.h"
#include "engine/path.h"

namespace Lumix {

struct CompositeTexture {
	struct ChannelSource {
		Path path;
		u32 src_channel = 0;
		bool invert = false;
	};
		
	struct Layer {
		ChannelSource red = {{}, 0};
		ChannelSource green = {{}, 1};
		ChannelSource blue = {{}, 2};
		ChannelSource alpha = {{}, 3};

		ChannelSource& getChannel(u32 i) {
			switch(i) {
				case 0: return red;
				case 1: return green;
				case 2: return blue;
				case 3: return alpha;
				default: ASSERT(false); return red;
			}
		}
	};

	enum class Output {
		BC1,
		BC3
	};

	CompositeTexture(IAllocator& allocator);
	bool init(Span<const u8> data, const char* src_path);
	bool loadSync(struct FileSystem& fs, const Path& path);
	bool save(struct FileSystem& fs, const Path& path);

	IAllocator& allocator;
	Array<Layer> layers;
	Output output = Output::BC1;
	bool cubemap = false;
};

} // namespace Lumix