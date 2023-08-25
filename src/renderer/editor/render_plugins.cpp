#define LUMIX_NO_CUSTOM_CRT
#ifdef LUMIX_BASIS_UNIVERSAL
	#include <encoder/basisu_comp.h>
#endif

#include "animation/animation.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "renderer/editor/particle_editor.h"
#include "editor/editor_asset.h"
#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/allocators.h"
#include "engine/associative_array.h"
#include "engine/atomic.h"
#include "engine/command_line_parser.h"
#include "engine/core.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/hash.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/os.h"
#include "engine/path.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/world.h"
#include "fbx_importer.h"
#include "game_view.h"
#include "model_meta.h"
#include "renderer/culling_system.h"
#include "renderer/editor/composite_texture.h"
#include "renderer/draw_stream.h"
#include "renderer/font.h"
#include "renderer/gpu/gpu.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "renderer/terrain.h"
#include "scene_view.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include "terrain_editor.h"
#include "world_viewer.h"

#define RGBCX_IMPLEMENTATION
#include <rgbcx/rgbcx.h>
#include <stb/stb_image_resize.h>


using namespace Lumix;

static Animation::Flags operator | (Animation::Flags a, Animation::Flags b) {
	return Animation::Flags(u32(a) | u32(b));
}

namespace {

static const ComponentType PARTICLE_EMITTER_TYPE = reflection::getComponentType("particle_emitter");
static const ComponentType TERRAIN_TYPE = reflection::getComponentType("terrain");
static const ComponentType CAMERA_TYPE = reflection::getComponentType("camera");
static const ComponentType DECAL_TYPE = reflection::getComponentType("decal");
static const ComponentType CURVE_DECAL_TYPE = reflection::getComponentType("curve_decal");
static const ComponentType POINT_LIGHT_TYPE = reflection::getComponentType("point_light");
static const ComponentType ENVIRONMENT_TYPE = reflection::getComponentType("environment");
static const ComponentType INSTANCED_MODEL_TYPE = reflection::getComponentType("instanced_model");
static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
static const ComponentType ENVIRONMENT_PROBE_TYPE = reflection::getComponentType("environment_probe");
static const ComponentType REFLECTION_PROBE_TYPE = reflection::getComponentType("reflection_probe");
static const ComponentType FUR_TYPE = reflection::getComponentType("fur");
static const ComponentType PROCEDURAL_GEOM_TYPE = reflection::getComponentType("procedural_geom");

namespace TextureCompressor {

struct Options {
	bool compress = true;
	bool generate_mipmaps = false;
	bool stochastic_mipmap = false;
	float scale_coverage_ref = -0.5f;
};

struct Input {
	struct Image {
		Image(IAllocator& allocator) : pixels(allocator) {}

		OutputMemoryStream pixels;
		u32 mip;
		u32 face;
		u32 slice;
	};

	Input(u32 w, u32 h, u32 slices, u32 mips, IAllocator& allocator) 
		: allocator(allocator)
		, images(allocator)
		, w(w)
		, h(h)
		, slices(slices)
		, mips(mips)
	{}

	bool has(u32 face, u32 slice, u32 mip) const {
		for (const Image& i : images) {
			if (i.face == face && i.mip == mip && i.slice == slice) return true;
		}
		return false;
	}

	const Image& get(u32 face, u32 slice, u32 mip) const {
		for (const Image& i : images) {
			if (i.face == face && i.mip == mip && i.slice == slice) return i;
		}
		ASSERT(false);
		return images[0];
	}

	Image& add(u32 face, u32 slice, u32 mip) {
		Image img(allocator);
		img.face = face;
		img.mip = mip;
		img.slice = slice;
		img.pixels.resize(maximum(1, (w >> mip)) * maximum(1, (h >> mip)) * 4);
		images.push(static_cast<Image&&>(img));
		return images.back();
	}

	void add(Span<const u8> data, u32 face, u32 slice, u32 mip) {
		Image img(allocator);
		img.face = face;
		img.mip = mip;
		img.slice = slice;
		ASSERT(data.length() == maximum(1, (w >> mip)) * maximum(1, (h >> mip)) * 4);
		img.pixels.reserve(data.length());
		img.pixels.write(data.begin(), data.length());
		images.push(static_cast<Image&&>(img));
	}

	IAllocator& allocator;
	Array<Image> images;
	u32 w;
	u32 h;
	u32 slices;
	u32 mips;
	bool is_srgb = false;
	bool is_normalmap = false;
	bool has_alpha = false;
	bool is_cubemap = false;
};
	
static u32 getCompressedMipSize(u32 w, u32 h, u32 bytes_per_block) {
	return ((w + 3) >> 2) * ((h + 3) >> 2) * bytes_per_block;
}

static u32 getCompressedSize(u32 w, u32 h, u32 mips, u32 faces, u32 bytes_per_block) {
	u32 total = getCompressedMipSize(w, h, bytes_per_block) * faces;
	for (u32 i = 1; i < mips; ++i) {
		u32 mip_w = maximum(1, w >> i);
		u32 mip_h = maximum(1, h >> i);
		total += getCompressedMipSize(mip_w, mip_h, bytes_per_block) * faces;
	}
	return total;
}

static void downsampleNormal(Span<const u8> src, Span<u8> dst, u32 w, u32 h, u32 dst_w, u32 dst_h) {
	ASSERT(w / dst_w <= 3);
	ASSERT(h / dst_h <= 3);

	const float rw = w / float(dst_w);
	const float rh = h / float(dst_h);

	auto fract = [](float f) { return f - u32(f); };

	const u32* sptr = (const u32*)src.begin();
	u32* dptr = (u32*)dst.begin();

	jobs::forEach(dst_h, 1, [&](i32 j, i32) {
		RandomGenerator rg(521288629, 362436069 + 1337 * j);
		for (u32 i = 0; i < dst_w; ++i) {
			float r = rg.randFloat(0, rh);
			float s = j * rh;
			float r0 = 1 - fract(s);
			const u32 row = (r > r0) + (r > (r0 + 1));

			r = rg.randFloat(0, rw);
			s = i * rw;
			r0 = 1 - fract(s);
			const u32 col = (r > r0) + (r > (r0 + 1));

			const u32 isrc = u32(i * rw) + col;
			const u32 jsrc = u32(j * rh) + row;

			ASSERT(isrc < w);
			ASSERT(jsrc < h);

			dptr[i + j * dst_w] = sptr[isrc + jsrc * w];
		}
	});
}

static void computeMip(Span<const u8> src, Span<u8> dst, u32 w, u32 h, u32 dst_w, u32 dst_h, bool is_srgb, bool stochastic, IAllocator& allocator) {
	PROFILE_FUNCTION();
	if (stochastic) {
		downsampleNormal(src, dst, w, h, dst_w, dst_h);
	}
	else if (is_srgb) {
		i32 res = stbir_resize_uint8_srgb(src.begin(), w, h, 0, dst.begin(), dst_w, dst_h, 0, 4, 3, STBIR_ALPHA_CHANNEL_NONE);
		ASSERT(res == 1);
	}
	else {
		i32 res = stbir_resize_uint8(src.begin(), w, h, 0, dst.begin(), dst_w, dst_h, 0, 4);
		ASSERT(res == 1);
	}
}

static void compressBC1(Span<const u8> src, OutputMemoryStream& dst, u32 w, u32 h) {
	PROFILE_FUNCTION();
	
	const u32 dst_block_size = 8;
	const u32 size = getCompressedMipSize(w, h, dst_block_size);
	const u64 offset = dst.size();
	dst.resize(offset + size);
	u8* out = dst.getMutableData() + offset;

	jobs::forEach(h, 4, [&](i32 j, i32){
		PROFILE_FUNCTION();
		u32 tmp[32];
		const u8* src_row_begin = &src[j * w * 4];

		const u32 src_block_h = minimum(h - j, 4);
		for (u32 i = 0; i < w; i += 4) {
			const u8* src_block_begin = src_row_begin + i * 4;
			
			const u32 src_block_w = minimum(w - i, 4);
			for (u32 jj = 0; jj < src_block_h; ++jj) {
				memcpy(&tmp[jj * 4], &src_block_begin[jj * w * 4], 4 * src_block_w);
			}

			const u32 bi = i >> 2;
			const u32 bj = j >> 2;
			rgbcx::encode_bc1(10, &out[(bi + bj * ((w + 3) >> 2)) * dst_block_size], (const u8*)tmp, true, false);
		}
	});
}

static void compressRGBA(Span<const u8> src, OutputMemoryStream& dst, u32 w, u32 h) {
	PROFILE_FUNCTION();
	dst.write(src.begin(), src.length());
}

static void compressBC5(Span<const u8> src, OutputMemoryStream& dst, u32 w, u32 h) {
	PROFILE_FUNCTION();
	
	const u32 dst_block_size = 16;
	const u32 size = getCompressedMipSize(w, h, dst_block_size);
	const u64 offset = dst.size();
	dst.resize(offset + size);
	u8* out = dst.getMutableData() + offset;

	jobs::forEach(h, 4, [&](i32 j, i32){
		PROFILE_FUNCTION();
		u32 tmp[32];
		const u8* src_row_begin = &src[j * w * 4];

		const u32 src_block_h = minimum(h - j, 4);
		for (u32 i = 0; i < w; i += 4) {
			const u8* src_block_begin = src_row_begin + i * 4;
			
			const u32 src_block_w = minimum(w - i, 4);
			for (u32 jj = 0; jj < src_block_h; ++jj) {
				memcpy(&tmp[jj * 4], &src_block_begin[jj * w * 4], 4 * src_block_w);
			}

			const u32 bi = i >> 2;
			const u32 bj = j >> 2;
			rgbcx::encode_bc5(&out[(bi + bj * ((w + 3) >> 2)) * dst_block_size], (const u8*)tmp);
		}
	});
}

static void compressBC3(Span<const u8> src, OutputMemoryStream& dst, u32 w, u32 h) {
	PROFILE_FUNCTION();
	
	const u32 dst_block_size = 16;
	const u32 size = getCompressedMipSize(w, h, dst_block_size);
	const u64 offset = dst.size();
	dst.resize(offset + size);
	u8* out = dst.getMutableData() + offset;

	jobs::forEach(h, 4, [&](i32 j, i32){
		PROFILE_FUNCTION();
		u32 tmp[32] = {};
		const u8* src_row_begin = &src[j * w * 4];

		const u32 src_block_h = minimum(h - j, 4);
		for (u32 i = 0; i < w; i += 4) {
			const u8* src_block_begin = src_row_begin + i * 4;
			
			const u32 src_block_w = minimum(w - i, 4);
			for (u32 jj = 0; jj < src_block_h; ++jj) {
				memcpy(&tmp[jj * 4], &src_block_begin[jj * w * 4], 4 * src_block_w);
			}

			const u32 bi = i >> 2;
			const u32 bj = j >> 2;
			rgbcx::encode_bc3(10, &out[(bi + bj * ((w + 3) >> 2)) * dst_block_size], (const u8*)tmp);
		}
	});
}

static void writeLBCHeader(OutputMemoryStream& out, u32 w, u32 h, u32 slices, u32 mips, gpu::TextureFormat format, bool is_3d, bool is_cubemap) {
	LBCHeader header;
	header.w = w;
	header.h = h;
	header.slices = slices;
	header.mips = mips;
	header.format = format;
	if (is_3d) header.flags |= LBCHeader::IS_3D;
	if (is_cubemap) header.flags |= LBCHeader::CUBEMAP;
	out.write(header);
}

static float computeCoverage(Span<const u8> data, u32 w, u32 h, float ref_norm) {
	const u8 ref = u8(clamp(255 * ref_norm, 0.f, 255.f));

	u32 count = 0;
	const Color* pixels = (const Color*)data.begin();
	for (u32 i = 0; i < w * h; ++i) {
		if (pixels[i].a > ref) ++count;
	}

	return float(double(count) / double(w * h));
}

static void scaleCoverage(Span<u8> data, u32 w, u32 h, float ref_norm, float wanted_coverage) {
	u32 histogram[256] = {};
	Color* pixels = (Color*)data.begin();
	for (u32 i = 0; i < w * h; ++i) {
		++histogram[pixels[i].a];
	}

	u32 count = u32(w * h  * (1 - wanted_coverage));
	float new_ref;
	for (new_ref = 0; new_ref < 255; ++new_ref) {
		if (count < histogram[u32(new_ref)]) {
			new_ref += (float)count / histogram[u32(new_ref)];
			break;
		}
		count -= histogram[u32(new_ref)];
	}
	const float scale = ref_norm / (new_ref / 255.f);

	for (u32 i = 0; i < w * h; ++i) {
		pixels[i].a = u8(clamp(pixels[i].a * scale, 0.f, 255.f));
	}
}

static void compress(void (*compressor)(Span<const u8>, OutputMemoryStream&, u32, u32), const Input& src_data, const Options& options, OutputMemoryStream& dst, IAllocator& allocator) {
	const u32 mips = options.generate_mipmaps ? 1 + log2(maximum(src_data.w, src_data.h)) : src_data.mips;
	const u32 faces = src_data.is_cubemap ? 6 : 1;
	const u32 block_size = src_data.has_alpha || src_data.is_normalmap ? 16 : 8;
	const u32 total_compressed_size = getCompressedSize(src_data.w, src_data.h, mips, faces, block_size);
	dst.reserve(dst.size() + total_compressed_size);
	Array<u8> mip_data(allocator);
	Array<u8> prev_mip(allocator);

	const float coverage = options.scale_coverage_ref >= 0.f
		? computeCoverage(src_data.get(0, 0, 0).pixels, src_data.w, src_data.h, options.scale_coverage_ref) 
		: -1;

	for (u32 slice = 0; slice < src_data.slices; ++slice) {
		for (u32 face = 0; face < faces; ++face) {
			for (u32 mip = 0; mip < mips; ++mip) {
				u32 mip_w = maximum(src_data.w >> mip, 1);
				u32 mip_h = maximum(src_data.h >> mip, 1);
				if (options.generate_mipmaps) {
					if (mip == 0) {
						const Input::Image& src_mip = src_data.get(face, slice, mip);
						compressor(src_mip.pixels, dst, mip_w, mip_h);
					}
					else {
						mip_data.resize(mip_w * mip_h * 4);
						u32 prev_w = maximum(src_data.w >> (mip - 1), 1);
						u32 prev_h = maximum(src_data.h >> (mip - 1), 1);
						computeMip(mip == 1 ? (Span<const u8>)src_data.get(face, slice, 0).pixels : prev_mip, mip_data, prev_w, prev_h, mip_w, mip_h, src_data.is_srgb, options.stochastic_mipmap, allocator);
						if (options.scale_coverage_ref >= 0.f) {
							scaleCoverage(mip_data, mip_w, mip_h, options.scale_coverage_ref, coverage);
						}
						compressor(mip_data, dst, mip_w, mip_h);
						prev_mip.swap(mip_data);
					}
				}
				else {
					const Input::Image& src_mip = src_data.get(face, slice, mip);
					compressor(src_mip.pixels, dst, mip_w, mip_h);
				}
			}
		}
	}
}

static bool isValid(const Input& src_data, const Options& options) {
	if (options.generate_mipmaps && src_data.mips != 1) return false;
	for (u32 mip = 0; mip < src_data.mips; ++mip) {
		for (u32 slice = 0; slice < src_data.slices; ++slice) {
			for (u32 face = 0; face < (src_data.is_cubemap ? 6u : 1u); ++face) {
				if (!src_data.has(face, slice, mip)) return false;
			}
		}
	}
	return true;
}

[[nodiscard]] static bool compress(const Input& src_data, const Options& options, OutputMemoryStream& dst, IAllocator& allocator) {
	PROFILE_FUNCTION();

	if (!isValid(src_data, options)) return false;

	const u32 mips = options.generate_mipmaps ? 1 + log2(maximum(src_data.w, src_data.h)) : src_data.mips;
	gpu::TextureFormat format;

	const bool can_compress = options.compress && (src_data.w % 4) == 0 && (src_data.h % 4) == 0;
	if (!can_compress) format = gpu::TextureFormat::RGBA8;
	else if (src_data.is_normalmap) format = gpu::TextureFormat::BC5;
	else if (src_data.has_alpha) format = gpu::TextureFormat::BC3;
	else format = gpu::TextureFormat::BC1;
		
	writeLBCHeader(dst, src_data.w, src_data.h, src_data.slices, mips, format, false, src_data.is_cubemap);

	if (!can_compress) {
		compress(compressRGBA, src_data, options, dst, allocator);
	}
	if (src_data.is_normalmap) {
		compress(compressBC5, src_data, options, dst, allocator);
	}
	else if (src_data.has_alpha) {
		compress(compressBC3, src_data, options, dst, allocator);
	}
	else {
		compress(compressBC1, src_data, options, dst, allocator);
	}
	return true;
}

} // namespace TextureCompressor

// https://www.khronos.org/opengl/wiki/Cubemap_Texture
static const Vec3 cube_fwd[6] = {
	{1, 0, 0},
	{-1, 0, 0},
	{0, 1, 0},
	{0, -1, 0},
	{0, 0, 1},
	{0, 0, -1}
};

static const Vec3 cube_right[6] = {
	{0, 0, -1},
	{0, 0, 1},
	{1, 0, 0},
	{1, 0, 0},
	{1, 0, 0},
	{-1, 0, 0}
};

static const Vec3 cube_up[6] = {
	{0, -1, 0},
	{0, -1, 0},
	{0, 0, 1},
	{0, 0, -1},
	{0, -1, 0},
	{0, -1, 0}
};

struct SphericalHarmonics {
	Vec3 coefs[9];

	SphericalHarmonics() {
		for (u32 i = 0; i < 9; ++i) {
			coefs[i] = Vec3(0);
		}
	}

	SphericalHarmonics operator *(const Vec3& v) {
		SphericalHarmonics res;
		for (u32 i = 0; i < 9; ++i) {
			res.coefs[i] = coefs[i] * v;
		}
		return res;
	}

	SphericalHarmonics operator *(float v) {
		SphericalHarmonics res;
		for (u32 i = 0; i < 9; ++i) {
			res.coefs[i] = coefs[i] * v;
		}
		return res;
	}

	void operator +=(const SphericalHarmonics& rhs) {
		for (u32 i = 0; i < 9; ++i) {
			coefs[i] += rhs.coefs[i];
		}
	}

	static SphericalHarmonics project(const Vec3& dir) {
		SphericalHarmonics sh;
    
		sh.coefs[0] = Vec3(0.282095f);
		sh.coefs[1] = Vec3(0.488603f * dir.y);
		sh.coefs[2] = Vec3(0.488603f * dir.z);
		sh.coefs[3] = Vec3(0.488603f * dir.x);
		sh.coefs[4] = Vec3(1.092548f * dir.x * dir.y);
		sh.coefs[5] = Vec3(1.092548f * dir.y * dir.z);
		sh.coefs[6] = Vec3(0.315392f * (3.0f * dir.z * dir.z - 1.0f));
		sh.coefs[7] = Vec3(1.092548f * dir.x * dir.z);
		sh.coefs[8] = Vec3(0.546274f * (dir.x * dir.x - dir.y * dir.y));
    
		return sh;
	}
	

	static Vec3 cube2dir(u32 x, u32 y, u32 s, u32 width, u32 height) {
		float u = ((x + 0.5f) / float(width)) * 2.f - 1.f;
		float v = ((y + 0.5f) / float(height)) * 2.f - 1.f;
		v *= -1.f;

		Vec3 dir(0.f);

		switch(s) {
			case 0: return normalize(Vec3(1.f, v, -u));
			case 1: return normalize(Vec3(-1.f, v, u));
			case 2: return normalize(Vec3(u, 1.f, -v));
			case 3: return normalize(Vec3(u, -1.f, v));
			case 4: return normalize(Vec3(u, v, 1.f));
			case 5: return normalize(Vec3(-u, v, -1.f));
		}

		return dir;
	}

	// https://github.com/TheRealMJP/LowResRendering/blob/master/SampleFramework11/v1.01/Graphics/SH.cpp
	// https://www.gamedev.net/forums/topic/699721-spherical-harmonics-irradiance-from-hdr/
	void compute(const Array<Vec4>& pixels) {
		PROFILE_FUNCTION();
		for (u32 i = 0; i < 9; ++i) {
			coefs[i] = Vec3(0);
		}
		const u32 w = (u32)sqrtf(pixels.size() / 6.f);
		const u32 h = w;
		ASSERT(6 * w * h == pixels.size());

		float weightSum = 0.0f;
		for (u32 face = 0; face < 6; ++face) {
			for (u32 y = 0; y < h; y++) {
				for (u32 x = 0; x < w; ++x) {
					const float u = (x + 0.5f) / w;
					const float v = (y + 0.5f) / h;
					const float temp = 1.0f + u * u + v * v;
					const float weight = 4.0f / (sqrtf(temp) * temp);
					const Vec3 dir = cube2dir(x, y, face, w, h);
					const Vec3 color = pixels[(x + y * w + face * w * h)].rgb();
					*this += project(dir) * (color * weight);
					weightSum += weight;
				}
			}
		}
		*this = *this * ((4.0f * PI) / weightSum);

		const float mults[] = {
			0.282095f,
			0.488603f * 2 / 3.f,
			0.488603f * 2 / 3.f,
			0.488603f * 2 / 3.f,
			1.092548f / 4.f,
			1.092548f / 4.f,
			0.315392f / 4.f,
			1.092548f / 4.f,
			0.546274f / 4.f
		};

		for (u32 i = 0; i < 9; ++i) {
			coefs[i] = coefs[i] * mults[i];
		}
	}
};

static void flipY(Vec4* data, int texture_size)
{
	for (int y = 0; y < texture_size / 2; ++y)
	{
		for (int x = 0; x < texture_size; ++x)
		{
			const Vec4 t = data[x + y * texture_size];
			data[x + y * texture_size] = data[x + (texture_size - y - 1) * texture_size];
			data[x + (texture_size - y - 1) * texture_size] = t;
		}
	}
}


static void flipX(Vec4* data, int texture_size)
{
	for (int y = 0; y < texture_size; ++y)
	{
		Vec4* tmp = &data[y * texture_size];
		for (int x = 0; x < texture_size / 2; ++x)
		{
			const Vec4 t = tmp[x];
			tmp[x] = tmp[texture_size - x - 1];
			tmp[texture_size - x - 1] = t;
		}
	}
}
	
static bool saveAsLBC(const char* path, const u8* data, int w, int h, bool generate_mipmaps, bool is_origin_bottom_left, IAllocator& allocator) {
	ASSERT(data);
	
	OutputMemoryStream blob(allocator);
	TextureCompressor::Options options;
	options.generate_mipmaps = generate_mipmaps;
	TextureCompressor::Input input(w, h, 1, 1, allocator);
	input.add(Span(data, w * h * 4), 0, 0, 0);
	if (!TextureCompressor::compress(input, options, blob, allocator)) {
		return false;
	}
	os::OutputFile file;
	if (!file.open(path)) return false;
	(void)file.write("lbc", 3);
	(void)file.write(u32(0));
	(void)file.write(blob.data(), blob.size());
	file.close();
	return !file.isError();
}


struct FontPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	FontPlugin(StudioApp& app) 
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("ttf", FontResource::TYPE); 
	}

	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Font"; }

	StudioApp& m_app;
};


struct PipelinePlugin final : AssetCompiler::IPlugin, AssetBrowser::IPlugin {
	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app, IAllocator& allocator)
			: AssetEditorWindow(app)
			, m_buffer(allocator)
			, m_app(app)
		{
			m_resource = app.getEngine().getResourceManager().load<PipelineResource>(path);
		}

		~EditorWindow() {
			m_resource->decRefCount();
		}

		void save() {
			Span<const u8> data((const u8*)m_buffer.c_str(), m_buffer.length());
			m_app.getAssetBrowser().saveResource(*m_resource, data);
			m_dirty = false;
		}
	
		bool onAction(const Action& action) override { 
			if (&action == &m_app.getCommonActions().save) save();
			else return false;
			return true;
		}

		void windowGUI() override {
			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(*m_resource);
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (m_buffer.length() == 0) m_buffer = m_resource->content;

			ImGui::PushFont(m_app.getMonospaceFont());
			if (inputStringMultiline("##code", &m_buffer, ImGui::GetContentRegionAvail())) {
				m_dirty = true;
			}
			ImGui::PopFont();
		}
	
		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "pipeline editor"; }

		StudioApp& m_app;
		PipelineResource* m_resource;
		String m_buffer;
	};
	
	explicit PipelinePlugin(StudioApp& app)
		: m_app(app)
	{}

	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Pipeline"; }

	void openEditor(const struct Path& path) {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, m_app, m_app.getAllocator());
		m_app.getAssetBrowser().addWindow(win.move());
	}

	StudioApp& m_app;
};


struct ParticleSystemPropertyPlugin final : PropertyGrid::IPlugin
{
	ParticleSystemPropertyPlugin(StudioApp& app) : m_app(app) {}

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != PARTICLE_EMITTER_TYPE) return;
		if (entities.length() != 1) return;
		
		RenderModule* module = (RenderModule*)editor.getWorld()->getModule(cmp_type);
		ParticleSystem& system = module->getParticleSystem(entities[0]);

		if (m_playing && ImGui::Button(ICON_FA_STOP " Stop")) m_playing = false;
		else if (!m_playing && ImGui::Button(ICON_FA_PLAY " Play")) m_playing = true;

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_UNDO_ALT " Reset")) system.reset();

		ImGuiEx::Label("Time scale");
		ImGui::SliderFloat("##ts", &m_time_scale, 0, 1);
		if (m_playing) {
			float dt = m_app.getEngine().getLastTimeDelta() * m_time_scale;
			module->updateParticleSystem(entities[0], dt);
		}
			
		ImGui::TextUnformatted("Particle count");
		for (ParticleSystem::Emitter& emitter : system.getEmitters()) {
			ImGui::Text("%d", emitter.particles_count);
		}
			
		ImGuiEx::Label("Time");
		ImGui::Text("%.2f", system.m_total_time);
	}

	StudioApp& m_app;
	bool m_playing = false;
	float m_time_scale = 1.f;
};

struct MaterialPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow, SimpleUndoRedo {
		EditorWindow(const Path& path, StudioApp& app, IAllocator& allocator)
			: AssetEditorWindow(app)
			, SimpleUndoRedo(allocator)
			, m_app(app)
			, m_allocator(allocator)
		{
			m_resource = app.getEngine().getResourceManager().load<Material>(path);
		}

		~EditorWindow() {
			m_resource->decRefCount();
		}

		void deserialize(InputMemoryStream& blob) override { m_resource->deserialize(blob); }
		void serialize(OutputMemoryStream& blob) override { m_resource->serialize(blob); }

		void save() {
			ASSERT(m_resource->getShader());
			OutputMemoryStream blob(m_allocator);
			m_resource->serialize(blob);
			m_app.getAssetBrowser().saveResource(*m_resource, blob);
			m_dirty = false;
		}
		
		bool onAction(const Action& action) override { 
			const CommonActions& actions = m_app.getCommonActions();
			if (&action == &actions.save) save();
			else if (m_resource->isReady()) {
				if (&action == &actions.undo) undo();
				else if (&action == &actions.redo) redo();
				else return false;
			}
			else return false;
			return true;
		}

		void saveUndo(bool changed) {
			if (changed) {
				m_dirty = true;
				pushUndo(ImGui::GetItemID());
			}
		}

		void windowGUI() override {
			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(*m_resource);
				if (ImGuiEx::IconButton(ICON_FA_UNDO, "Undo", canUndo())) undo();
				if (ImGuiEx::IconButton(ICON_FA_REDO, "Redo", canRedo())) redo();
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!SimpleUndoRedo::isReady()) pushUndo(NO_MERGE_UNDO);

			Shader* shader = m_resource->getShader();
			Path shader_path = shader ? shader->getPath() : Path();
			
			if (m_app.getAssetBrowser().resourceInput("shader", shader_path, Shader::TYPE)) {
				m_resource->setShader(shader_path);
				shader = m_resource->getShader();
				saveUndo(true);
			}

			ImGuiEx::Label("Backface culling");
			bool is_backface_culling = m_resource->isBackfaceCulling();
			if (ImGui::Checkbox("##bfcul", &is_backface_culling)) {
				m_resource->enableBackfaceCulling(is_backface_culling);
				saveUndo(true);
			}

			Renderer& renderer = m_resource->getRenderer();

			const char* current_layer_name = renderer.getLayerName(m_resource->getLayer());
			ImGuiEx::Label("Layer");
			if (ImGui::BeginCombo("##layer", current_layer_name)) {
				for (u8 i = 0, c = renderer.getLayersCount(); i < c; ++i) {
					const char* name = renderer.getLayerName(i);
					if (ImGui::Selectable(name)) {
						m_resource->setLayer(i);
						saveUndo(true);
					}
				}
				ImGui::EndCombo();
			}

			if (!shader || !shader->isReady()) return;

			for (u32 i = 0; i < shader->m_texture_slot_count; ++i) {
				auto& slot = shader->m_texture_slots[i];
				Texture* texture = m_resource->getTexture(i);
				Path path = texture ? texture->getPath() : Path();
				ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
				ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
				bool is_node_open = ImGui::TreeNodeEx((const void*)(intptr_t)(i + 1),
					ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed,
					"%s",
					"");
				ImGui::PopStyleColor(4);
				ImGui::SameLine();

				ImGuiEx::Label(slot.name);
				ImGui::PushID(&slot);
				if (m_app.getAssetBrowser().resourceInput("##res", path, Texture::TYPE)) { 
					m_resource->setTexturePath(i, path);
					saveUndo(true);
				}
				ImGui::PopID();
				if (!texture && is_node_open) {
					ImGui::TreePop();
					continue;
				}

				if (is_node_open) {
					ImGui::Image(texture->handle, ImVec2(96, 96));
					ImGui::TreePop();
				}
			}

			if (m_resource->isReady()) {
				for (int i = 0; i < shader->m_uniforms.size(); ++i) {
					const Shader::Uniform& shader_uniform = shader->m_uniforms[i];
					Material::Uniform* uniform = m_resource->findUniform(shader_uniform.name_hash);
					if (!uniform) {
						uniform = &m_resource->getUniforms().emplace();
						uniform->name_hash = shader_uniform.name_hash;
						memcpy(uniform->vec4, shader_uniform.default_value.vec4, sizeof(shader_uniform.default_value)); 
					}

					ImGui::PushID(&shader_uniform);
					ImGuiEx::Label(shader_uniform.name);
					switch (shader_uniform.type) {
						case Shader::Uniform::FLOAT:
							saveUndo(ImGui::DragFloat("##f", &uniform->float_value));
							break;
						case Shader::Uniform::NORMALIZED_FLOAT:
							saveUndo(ImGui::DragFloat("##nf", &uniform->float_value, 0.01f, 0.f, 1.f));
							break;
						case Shader::Uniform::INT:
							saveUndo(ImGui::DragInt("##i", &uniform->int_value));
							break;
						case Shader::Uniform::VEC3:
							saveUndo(ImGui::DragFloat3("##v3", uniform->vec3));
							break;
						case Shader::Uniform::VEC4:
							saveUndo(ImGui::DragFloat4("##v4", uniform->vec4));
							break;
						case Shader::Uniform::VEC2:
							saveUndo(ImGui::DragFloat2("##v2", uniform->vec2));
							break;
						case Shader::Uniform::COLOR:
							saveUndo(ImGui::ColorEdit4("##c", uniform->vec4));
							break;
						default: ASSERT(false); break;
					}
					ImGui::PopID();
				}
			}

			if (Material::getCustomFlagCount() > 0 && ImGui::CollapsingHeader("Flags")) {
				for (int i = 0; i < Material::getCustomFlagCount(); ++i) {
					bool b = m_resource->isCustomFlag(1 << i);
					if (ImGui::Checkbox(Material::getCustomFlagName(i), &b)) {
						if (b) m_resource->setCustomFlag(1 << i);
						else m_resource->unsetCustomFlag(1 << i);
						saveUndo(true);
					}
				}
			}
		
			if (ImGui::CollapsingHeader("Defines")) {
				for (int i = 0; i < renderer.getShaderDefinesCount(); ++i) {
					const char* define = renderer.getShaderDefine(i);
					if (!shader->hasDefine(i)) continue;

					auto isBuiltinDefine = [](const char* define) {
						const char* BUILTIN_DEFINES[] = {"HAS_SHADOWMAP", "SKINNED"};
						for (const char* builtin_define : BUILTIN_DEFINES) {
							if (equalStrings(builtin_define, define)) return true;
						}
						return false;
					};

					bool value = m_resource->isDefined(i);
					bool is_texture_define = m_resource->isTextureDefine(i);
					if (is_texture_define || isBuiltinDefine(define)) continue;
				
					if (ImGui::Checkbox(define, &value)) {
						m_resource->setDefine(i, value);
						saveUndo(true);
					}
				}
			}
		}
	
		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "material editor"; }

		StudioApp& m_app;
		IAllocator& m_allocator;
		Material* m_resource;
	};

	explicit MaterialPlugin(StudioApp& app)
		: m_app(app)
		, m_allocator(app.getAllocator(), "material editor")
	{
		m_wireframe_action.init("     Wireframe", "Wireframe", "wireframe", "", (os::Keycode)'W', Action::Modifiers::CTRL, true);
		m_wireframe_action.func.bind<&MaterialPlugin::toggleWireframe>(this);

		app.getAssetCompiler().registerExtension("mat", Material::TYPE);
		app.addToolAction(&m_wireframe_action);
	}

	~MaterialPlugin() {
		m_app.removeAction(&m_wireframe_action);
	}

	void openEditor(const Path& path) override {
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(m_allocator, path, m_app, m_allocator);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	void toggleWireframe() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.empty()) return;

		World& world = *editor.getWorld();
		RenderModule& module = *(RenderModule*)world.getModule(MODEL_INSTANCE_TYPE);

		Array<Material*> materials(m_allocator);
		for (EntityRef e : selected) {
			if (world.hasComponent(e, MODEL_INSTANCE_TYPE)) {
				Model* model = module.getModelInstanceModel(e);
				if (!model->isReady()) continue;
				
				for (u32 i = 0; i < (u32)model->getMeshCount(); ++i) {
					Mesh& mesh = model->getMesh(i);
					materials.push(mesh.material);
				}
			}
			if (world.hasComponent(e, TERRAIN_TYPE)) {
				materials.push(module.getTerrainMaterial(e));
			}
			if (world.hasComponent(e, PROCEDURAL_GEOM_TYPE)) {
				materials.push(module.getProceduralGeometry(e).material);
			}
		}
		materials.removeDuplicates();
		for (Material* m : materials) {
			m->setWireframe(!m->wireframe());
		}
	}

	bool canCreateResource() const override { return true; }
	const char* getDefaultExtension() const override { return "mat"; }
	void createResource(OutputMemoryStream& blob) override { blob << "shader \"/pipelines/standard.shd\""; }
	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Material"; }

	StudioApp& m_app;
	TagAllocator m_allocator;
	Action m_wireframe_action;
};

struct TextureMeta {
	enum WrapMode : u32 {
		REPEAT,
		CLAMP
	};

	enum Filter : u32 {
		LINEAR,
		POINT,
		ANISOTROPIC
	};

	static const char* toString(Filter filter) {
		switch (filter) {
			case Filter::POINT: return "point";
			case Filter::LINEAR: return "linear";
			case Filter::ANISOTROPIC: return "anisotropic";
		}
		ASSERT(false);
		return "linear";
	}

	static const char* toString(WrapMode wrap) {
		switch (wrap) {
			case WrapMode::CLAMP: return "clamp";
			case WrapMode::REPEAT: return "repeat";
		}
		ASSERT(false);
		return "repeat";
	}

	void deserialize(lua_State* L) {
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "srgb", &srgb);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "compress", &compress);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "mip_scale_coverage", &scale_coverage);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "stochastic_mip", &stochastic_mipmap);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "normalmap", &is_normalmap);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "invert_green", &invert_normal_y);
		LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "mips", &mips);
		char tmp[32];
		if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "filter", Span(tmp))) {
			if (equalIStrings(tmp, "point")) {
				filter = TextureMeta::Filter::POINT;
			}
			else if (equalIStrings(tmp, "anisotropic")) {
				filter = TextureMeta::Filter::ANISOTROPIC;
			}
			else {
				filter = TextureMeta::Filter::LINEAR;
			}
		}
		if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_u", Span(tmp))) {
			wrap_mode_u = equalIStrings(tmp, "repeat") ? TextureMeta::WrapMode::REPEAT : TextureMeta::WrapMode::CLAMP;
		}
		if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_v", Span(tmp))) {
			wrap_mode_v = equalIStrings(tmp, "repeat") ? TextureMeta::WrapMode::REPEAT : TextureMeta::WrapMode::CLAMP;
		}
		if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_w", Span(tmp))) {
			wrap_mode_w = equalIStrings(tmp, "repeat") ? TextureMeta::WrapMode::REPEAT : TextureMeta::WrapMode::CLAMP;
		}
	}

	bool deserialize(InputMemoryStream& blob, const char* path) {
		ASSERT(blob.getPosition() == 0);
		lua_State* L = luaL_newstate();
		if (!LuaWrapper::execute(L, StringView((const char*)blob.getData(), (u32)blob.size()), path, 0)) {
			return false;
		}
		
		deserialize(L);

		lua_close(L);
		return true;	
	}

	void serialize(OutputMemoryStream& blob) {
		blob << "srgb = " << (srgb ? "true" : "false")
			<< "\ncompress = " << (compress ? "true" : "false")
			<< "\nstochastic_mip = " << (stochastic_mipmap ? "true" : "false")
			<< "\nmip_scale_coverage = " << scale_coverage
			<< "\nmips = " << (mips ? "true" : "false")
			<< "\nnormalmap = " << (is_normalmap ? "true" : "false")
			<< "\ninvert_green = " << (invert_normal_y ? "true" : "false")
			<< "\nwrap_mode_u = \"" << toString(wrap_mode_u) << "\""
			<< "\nwrap_mode_v = \"" << toString(wrap_mode_v) << "\""
			<< "\nwrap_mode_w = \"" << toString(wrap_mode_w) << "\""
			<< "\nfilter = \"" << toString(filter) << "\"";
	}

	void load(const Path& path, StudioApp& app) {
		if (Path::hasExtension(path, "raw")) {
			compress = false;
			mips = false;
		}

		if (lua_State* L = app.getAssetCompiler().getMeta(path)) {
			deserialize(L);
			lua_close(L);
		}
	}

	bool srgb = false;
	bool is_normalmap = false;
	bool invert_normal_y = false;
	bool mips = true;
	float scale_coverage = -0.5f;
	bool stochastic_mipmap = false;
	bool compress = true;
	WrapMode wrap_mode_u = WrapMode::REPEAT;
	WrapMode wrap_mode_v = WrapMode::REPEAT;
	WrapMode wrap_mode_w = WrapMode::REPEAT;
	Filter filter = Filter::LINEAR;
};

struct TextureAssetEditorWindow : AssetEditorWindow, SimpleUndoRedo {
	TextureAssetEditorWindow(const Path& path, StudioApp& app, IAllocator& allocator)
		: AssetEditorWindow(app)
		, SimpleUndoRedo(allocator)
		, m_app(app)
		, m_allocator(allocator)
	{
		m_texture = app.getEngine().getResourceManager().load<Texture>(path);
		m_meta.load(m_texture->getPath(), m_app);
		pushUndo(NO_MERGE_UNDO);
		if (Path::hasExtension(path, "ltc")) {
			m_composite_editor = CompositeTextureEditor::open(path, app, m_allocator);
		}
		app.getAssetCompiler().resourceCompiled().bind<&TextureAssetEditorWindow::onResourceCompiled>(this);
	}

	~TextureAssetEditorWindow() {
		m_app.getAssetCompiler().resourceCompiled().unbind<&TextureAssetEditorWindow::onResourceCompiled>(this);
		m_texture->decRefCount();
		clearTextureView();
	}

	void onResourceCompiled(Resource& res) { if (m_texture == &res) clearTextureView(); }

	void saveUndo(bool changed) {
		if (!changed) return;

		pushUndo(ImGui::GetItemID());
		m_dirty = true;
	}

	void deserialize(InputMemoryStream& blob) override {
		if (!m_meta.deserialize(blob, "undo/redo")) {
			logError("Failed to deserialize texture meta data for undo/redo");
		}
	}
	void serialize(OutputMemoryStream& blob) override { m_meta.serialize(blob); }

	void save() {
		AssetCompiler& compiler = m_app.getAssetCompiler();
		char buf[1024];
		OutputMemoryStream blob(buf, sizeof(buf));
		m_meta.serialize(blob);
		compiler.updateMeta(m_texture->getPath(), blob);
		if (m_composite_editor) m_composite_editor->save();
		m_dirty = false;
	}

	void clearTextureView() {
		if (!m_texture_view) return;
		SystemManager& system_manager = m_app.getEngine().getSystemManager();
		auto* renderer = (Renderer*)system_manager.getSystem("renderer");
		renderer->getEndFrameDrawStream().destroy(m_texture_view);
		m_texture_view = gpu::INVALID_TEXTURE;
	}


	static const char* getCubemapLabel(u32 idx) {
		switch (idx) {
			case 0: return "X+";
			case 1: return "X-";
			case 2: return "Y+ (top)";
			case 3: return "Y- (bottom)";
			case 4: return "Z+";
			case 5: return "Z-";
			default: return "Too many faces in cubemap";
		}
	}

	static const char* toString(gpu::TextureFormat format) {
		switch (format) {
			case gpu::TextureFormat::R8: return "R8";
			case gpu::TextureFormat::RGB32F: return "RGB32F";
			case gpu::TextureFormat::RG32F: return "RG32F";
			case gpu::TextureFormat::RG8: return "RG8";
			case gpu::TextureFormat::D24S8: return "D24S8";
			case gpu::TextureFormat::D32: return "D32";
			case gpu::TextureFormat::BGRA8: return "BGRA8";
			case gpu::TextureFormat::RGBA8: return "RGBA8";
			case gpu::TextureFormat::RGBA16: return "RGBA16";
			case gpu::TextureFormat::R11G11B10F: return "R11G11B10F";
			case gpu::TextureFormat::RGBA16F: return "RGBA16F";
			case gpu::TextureFormat::RGBA32F: return "RGBA32F";
			case gpu::TextureFormat::R16F: return "R16F";
			case gpu::TextureFormat::R16: return "R16";
			case gpu::TextureFormat::R32F: return "R32F";
			case gpu::TextureFormat::SRGB: return "SRGB";
			case gpu::TextureFormat::SRGBA: return "SRGBA";
			case gpu::TextureFormat::BC1: return "BC1";
			case gpu::TextureFormat::BC2: return "BC2";
			case gpu::TextureFormat::BC3: return "BC3";
			case gpu::TextureFormat::BC4: return "BC4";
			case gpu::TextureFormat::BC5: return "BC5";
		}
		ASSERT(false);
		return "Unknown";
	}

	bool onAction(const Action& action) override {
		const CommonActions& actions = m_app.getCommonActions();
		if (&actions.save == &action) save();
		else if (&actions.undo == &action) m_composite_editor ? m_composite_editor->doUndo() : undo();
		else if (&actions.redo == &action) m_composite_editor ? m_composite_editor->doRedo() : redo();
		else return false;
		return true;
	}

	void windowGUI() override {
		if (ImGui::BeginMenuBar()) {
			if (m_composite_editor) m_composite_editor->menu();
			if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
			if (!m_composite_editor) {
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_texture);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(*m_texture);
				if (ImGuiEx::IconButton(ICON_FA_FOLDER_OPEN, "Open folder")) {
					StaticString<MAX_PATH> dir(m_app.getEngine().getFileSystem().getBasePath(), Path::getDir(m_texture->getPath()));
					os::openExplorer(dir);
				}
				if (ImGuiEx::IconButton(ICON_FA_UNDO, "Undo", canUndo())) undo();
				if (ImGuiEx::IconButton(ICON_FA_REDO, "Redo", canRedo())) redo();
			}
			ImGui::EndMenuBar();
		}

		if (!ImGui::BeginTable("tab", m_composite_editor ? 3 : 2, ImGuiTableFlags_Resizable)) return;

		ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 250);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		
		if (m_composite_editor) {
			if (ImGuiEx::IconButton(ICON_FA_UNDO, "Undo", canUndo())) undo();
			ImGui::SameLine();
			if (ImGuiEx::IconButton(ICON_FA_REDO, "Redo", canRedo())) redo();
		}

		ImGuiEx::Label("Path");
		ImGuiEx::TextUnformatted(m_texture->getPath());
		ImGuiEx::Label("Size");
		ImGui::Text("%dx%d", m_texture->width, m_texture->height);
		ImGuiEx::Label("Mips");
		ImGui::Text("%d", m_texture->mips);
		if (m_texture->depth > 1) {
			ImGuiEx::Label("Depth");
			ImGui::Text("%d", m_texture->depth);
		}
		ImGuiEx::Label("Format");
		ImGui::TextUnformatted(toString(m_texture->format));

		ImGuiEx::Label("SRGB");
		saveUndo(ImGui::Checkbox("##srgb", &m_meta.srgb));
		ImGuiEx::Label("Mipmaps");
		saveUndo(ImGui::Checkbox("##mip", &m_meta.mips));
		if (m_meta.mips) {
			ImGuiEx::Label("Stochastic mipmap");
			saveUndo(ImGui::Checkbox("##stomip", &m_meta.stochastic_mipmap));
		}

		ImGuiEx::Label("Compress");
		saveUndo(ImGui::Checkbox("##cmprs", &m_meta.compress));
			
		if (m_meta.compress && (m_texture->width % 4 != 0 || m_texture->height % 4 != 0)) {
			ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " Block compression will not be used because texture size is not multiple of 4");
		}

		bool scale_coverage = m_meta.scale_coverage >= 0;
		ImGuiEx::Label("Mipmap scale coverage");
		if (ImGui::Checkbox("##mmapsccov", &scale_coverage)) {
			m_meta.scale_coverage *= -1;
			saveUndo(true);
		}
		if (m_meta.scale_coverage >= 0) {
			ImGuiEx::Label("Coverage alpha ref");
			saveUndo(ImGui::SliderFloat("##covaref", &m_meta.scale_coverage, 0, 1));
		}
		ImGuiEx::Label("Is normalmap");
		saveUndo(ImGui::Checkbox("##nrmmap", &m_meta.is_normalmap));

		if (m_meta.is_normalmap) {
			ImGuiEx::Label("Invert normalmap Y");
			saveUndo(ImGui::Checkbox("##nrmmapinvy", &m_meta.invert_normal_y));
		}

		ImGuiEx::Label("U Wrap mode");
		saveUndo(ImGui::Combo("##uwrp", (int*)&m_meta.wrap_mode_u, "Repeat\0Clamp\0"));
		ImGuiEx::Label("V Wrap mode");
		saveUndo(ImGui::Combo("##vwrp", (int*)&m_meta.wrap_mode_v, "Repeat\0Clamp\0"));
		ImGuiEx::Label("W Wrap mode");
		saveUndo(ImGui::Combo("##wwrp", (int*)&m_meta.wrap_mode_w, "Repeat\0Clamp\0"));
		ImGuiEx::Label("Filter");
		saveUndo(ImGui::Combo("##Filter", (int*)&m_meta.filter, "Linear\0Point\0Anisotropic\0"));

		ImGui::TableNextColumn();
		ImGui::CheckboxFlags("Red", &m_channel_view_mask, 1);
		ImGui::SameLine();
		ImGui::CheckboxFlags("Green", &m_channel_view_mask, 2);
		ImGui::SameLine();
		ImGui::CheckboxFlags("Blue", &m_channel_view_mask, 4);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100);
		ImGui::DragFloat("Zoom", &m_zoom, 0.01f, 0.01f, 100.f);

		if (m_texture->depth > 1) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100);
			if (ImGui::InputInt("View layer", (i32*)&m_view_layer)) {
				m_view_layer = m_view_layer % m_texture->depth;
				clearTextureView();
			}
		}
		if (m_texture->is_cubemap) {
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100);
			if (ImGui::Combo("Side", (i32*)&m_view_layer, "X+\0X-\0Y+\0Y-\0Z+\0Z-\0")) {
				clearTextureView();
			}
		}
		if (!m_texture_view && m_texture->isReady()) {
			m_texture_view = gpu::allocTextureHandle();

			SystemManager& system_manager = m_app.getEngine().getSystemManager();
			auto* renderer = (Renderer*)system_manager.getSystem("renderer");
			DrawStream& stream = renderer->getDrawStream();

			stream.createTextureView(m_texture_view
				, m_texture->handle
				, m_texture->is_cubemap ? m_view_layer : m_view_layer % m_texture->depth);
		}
		if (m_texture_view) {
			ImVec2 texture_size((float)m_texture->width, (float)m_texture->height);
			texture_size = texture_size * m_zoom;
		
			ImGui::BeginChild("imgpreview", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
			const ImVec4 tint(float(m_channel_view_mask & 1)
				, float((m_channel_view_mask >> 1) & 1)
				, float((m_channel_view_mask >> 2) & 1)
				, 1.f);
			if (texture_size.x < ImGui::GetContentRegionAvail().x) {
				ImVec2 cp = ImGui::GetCursorPos();
				cp.x += (ImGui::GetContentRegionAvail().x - texture_size.x) * 0.5f;
				ImGui::SetCursorPos(cp);
			}

			ImGui::Image(m_texture_view, texture_size, ImVec2(0, 0), ImVec2(1, 1), tint);
			const float wheel = ImGui::GetIO().MouseWheel;
			ImGui::EndChild();
			if (ImGui::IsItemHovered() && wheel && ImGui::GetIO().KeyAlt)	{
				m_zoom += wheel / 5.f;
				m_zoom = maximum(0.01f, m_zoom);
			}
		}

		if (m_composite_editor) {
			ImGui::TableNextColumn();
			m_composite_editor->gui();
			m_dirty = m_dirty || m_composite_editor->isDirty();
		}

		ImGui::EndTable();
	}

	const char* getName() const override { return "texture editor"; }

	const Path& getPath() { return m_texture->getPath(); }

	IAllocator& m_allocator;
	StudioApp& m_app;
	UniquePtr<CompositeTextureEditor> m_composite_editor;
	Texture* m_texture;
	gpu::TextureHandle m_texture_view = gpu::INVALID_TEXTURE;
	u32 m_view_layer = 0;
	float m_zoom = 1.f;
	u32 m_channel_view_mask = 0b1111;
	TextureMeta m_meta;
};

struct TexturePlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	explicit TexturePlugin(StudioApp& app)
		: m_app(app)
		, m_allocator(app.getAllocator(), "texture editor")
	{
		PROFILE_FUNCTION();
		rgbcx::init();

		app.getAssetCompiler().registerExtension("png", Texture::TYPE);
		app.getAssetCompiler().registerExtension("jpeg", Texture::TYPE);
		app.getAssetCompiler().registerExtension("jpg", Texture::TYPE);
		app.getAssetCompiler().registerExtension("tga", Texture::TYPE);
		app.getAssetCompiler().registerExtension("raw", Texture::TYPE);
		app.getAssetCompiler().registerExtension("ltc", Texture::TYPE);
	}

	void openEditor(const Path& path) override {
		UniquePtr<TextureAssetEditorWindow> win = UniquePtr<TextureAssetEditorWindow>::create(m_allocator, path, m_app, m_allocator);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	const char* getDefaultExtension() const override { return "ltc"; }
	bool canCreateResource() const override { return true; }
	void createResource(OutputMemoryStream& blob) override { 
		CompositeTexture ltc(m_app, m_allocator);
		ltc.initDefault();
		ltc.serialize(blob);
	}

	struct TextureTileJob
	{
		TextureTileJob(StudioApp& app, FileSystem& filesystem, IAllocator& allocator) 
			: m_allocator(allocator) 
			, m_filesystem(filesystem)
			, m_app(app)
		{}

		void execute() {
			const FilePathHash hash(m_in_path.c_str());
			const Path out_path(".lumix/asset_tiles/", hash, ".lbc");
			OutputMemoryStream resized_data(m_allocator);
			resized_data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
			FileSystem& fs = m_app.getEngine().getFileSystem();
			OutputMemoryStream tmp(m_allocator);
			if (!fs.getContentSync(m_in_path, tmp)) {
				logError("Failed to load ", m_in_path);
				return;
			}

			int image_comp;
			int w, h;
			if (Path::hasExtension(m_in_path, "ltc")) {
				CompositeTexture ct(m_app, m_allocator);
				InputMemoryStream blob(tmp);
				if (!ct.deserialize(blob)) {
					logError("Failed to deserialize ", m_in_path);
					return;
				}
				CompositeTexture::Result res(m_allocator);
				if (!ct.generate(&res)) return;

				const CompositeTexture::Image& layer0 = res.layers[0];
				if (layer0.channels != 4) return;

				stbir_resize_uint8(layer0.asU8().data(),
					layer0.w,
					layer0.h,
					0,
					resized_data.getMutableData(),
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);

				if (!saveAsLBC(m_out_path.c_str(), resized_data.data(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, false, true, m_allocator)) {
					logError("Failed to save ", m_out_path);
				}
			}
			else {

				auto data = stbi_load_from_memory(tmp.data(), (int)tmp.size(), &w, &h, &image_comp, 4);
				if (!data)
				{
					logError("Failed to load ", m_in_path);
					return;
				}

				stbir_resize_uint8(data,
					w,
					h,
					0,
					resized_data.getMutableData(),
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
				stbi_image_free(data);

				if (!saveAsLBC(m_out_path.c_str(), resized_data.data(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, false, true, m_allocator)) {
					logError("Failed to save ", m_out_path);
				}
			}
		}

		static void execute(void* data) {
			PROFILE_FUNCTION();
			TextureTileJob* that = (TextureTileJob*)data;
			that->execute();
			LUMIX_DELETE(that->m_allocator, that);
		}

		StudioApp& m_app;
		IAllocator& m_allocator;
		FileSystem& m_filesystem;
		Path m_in_path; 
		Path m_out_path;
		TextureTileJob* m_next = nullptr;
	};

	void update() override {
		if (!m_jobs_tail) return;

		TextureTileJob* job = m_jobs_tail;
		m_jobs_tail = job->m_next;
		if (!m_jobs_tail) m_jobs_head = nullptr;

		// to keep editor responsive, we don't want to create too many tiles per frame 
		jobs::runEx(job, &TextureTileJob::execute, nullptr, jobs::getWorkersCount() - 1);
	}

	bool createTile(const char* in_path, const char* out_path, ResourceType type) override {
		if (type != Texture::TYPE) return false;
		if (!Path::hasExtension(in_path, "raw")) {
			FileSystem& fs = m_app.getEngine().getFileSystem();
			TextureTileJob* job = LUMIX_NEW(m_allocator, TextureTileJob)(m_app, fs, m_allocator);
			job->m_in_path = in_path;
			job->m_out_path = out_path;
			if (m_jobs_head) m_jobs_head->m_next = job;
			else {
				m_jobs_tail = job;
			}
			m_jobs_head = job;
			return true;
		}
		return false;
	}

	bool compileComposite(const OutputMemoryStream& src_data, OutputMemoryStream& dst, const TextureMeta& meta, StringView src_path) {
		CompositeTexture tc(m_app, m_allocator);
		InputMemoryStream blob(src_data);
		if (!tc.deserialize(blob)) {
			logError("Failed to load ", src_path);
			return false;
		}

		CompositeTexture::Result img(m_allocator);
		if (!tc.generate(&img)) return false;
		if (img.layers.empty()) {
			logError(src_path, " : empty output");
			return false;
		}
		const u32 w = img.layers[0].w;
		const u32 h = img.layers[0].h;

		TextureCompressor::Input input(w, h, img.is_cubemap ? 1 : img.layers.size(), 1, m_allocator);
		input.is_normalmap = meta.is_normalmap;
		input.is_srgb = meta.srgb;
		input.is_cubemap = img.is_cubemap;
		input.has_alpha = img.layers[0].channels == 4;

		for (CompositeTexture::Image& layer : img.layers) {
			const u32 idx = u32(&layer - img.layers.begin());
			if (layer.channels != 4) {
				TextureCompressor::Input::Image& tmp = input.add(img.is_cubemap ? idx : 0, input.is_cubemap ? 0 : idx, 0);
				tmp.pixels.resize(layer.w * layer.h * 4);
				OutputMemoryStream pixels = layer.asU8();
				const u8* src = pixels.data();
				u8* dst = tmp.pixels.getMutableData();
	
				for(u32 i = 0; i < layer.w * layer.h; ++i) {
					memcpy(dst + i * 4, src + i * layer.channels, layer.channels);
					for (u32 j = layer.channels; j < 4; ++j) {
						dst[i * 4 + j] = 1;
					}
				}
			}
			else {
				input.add(layer.asU8(), img.is_cubemap ? idx : 0, input.is_cubemap ? 0 : idx, 0);
			}
		}

		dst.write("lbc", 3);
		u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
		flags |= meta.wrap_mode_u == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
		flags |= meta.wrap_mode_v == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
		flags |= meta.wrap_mode_w == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
		flags |= meta.filter == TextureMeta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
		flags |= meta.filter == TextureMeta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
		dst.write(&flags, sizeof(flags));
		TextureCompressor::Options options;
		options.generate_mipmaps = meta.mips;
		options.stochastic_mipmap = meta.stochastic_mipmap;
		options.scale_coverage_ref = meta.scale_coverage;
		return TextureCompressor::compress(input, options, dst, m_allocator);
	}

	bool compileImage(const Path& path, const OutputMemoryStream& src_data, OutputMemoryStream& dst, const TextureMeta& meta)
	{
		PROFILE_FUNCTION();
		int w, h, comps;
		const bool is_16_bit = stbi_is_16_bit_from_memory(src_data.data(), (i32)src_data.size());
		if (is_16_bit) logWarning(path, ": 16bit images not yet supported. Converting to 8bit.");

		stbi_uc* stb_data = stbi_load_from_memory(src_data.data(), (i32)src_data.size(), &w, &h, &comps, 4);
		if (!stb_data) return false;

		const u8* data;
		Array<u8> inverted_y_data(m_allocator);
		if (meta.is_normalmap && meta.invert_normal_y) {
			inverted_y_data.resize(w * h * 4);
			for (i32 y = 0; y < h; ++y) {
				for (i32 x = 0; x < w; ++x) {
					const u32 idx = (x + y * w) * 4;
					inverted_y_data[idx] = stb_data[idx];
					inverted_y_data[idx + 1] = 0xff - stb_data[idx + 1];
					inverted_y_data[idx + 2] = stb_data[idx + 2];
					inverted_y_data[idx + 3] = stb_data[idx + 3];
				}
			}
			data = inverted_y_data.begin();
		} else {
			data = stb_data;
		}

		#ifdef LUMIX_BASIS_UNIVERSAL
			dst.write("bsu", 3);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			flags |= meta.filter == Meta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
			dst.write(&flags, sizeof(flags));
			gpu::TextureFormat gpu_format = meta.is_normalmap ? gpu::TextureFormat::BC5 : (comps == 4 ? gpu::TextureFormat::BC3 : gpu::TextureFormat::BC1);
			dst.write(gpu_format);

			static bool once = [](){ basisu::basisu_encoder_init(); return true; }();
			basisu::job_pool job_pool(jobs::getWorkersCount());
			basisu::basis_compressor c;
			basisu::basis_compressor_params params;
			params.m_pJob_pool = &job_pool;
			params.m_source_images.push_back({});
			params.m_source_images[0].init(data, w, h, 4);
			params.m_quality_level = 255;
			params.m_perceptual = !meta.is_normalmap && meta.srgb;
			params.m_mip_gen = meta.mips;
			if (meta.is_normalmap) {
				params.m_mip_srgb = false;
				params.m_no_selector_rdo = true;
				params.m_no_endpoint_rdo = true;
				params.m_swizzle[0] = 0;
				params.m_swizzle[1] = 0;
				params.m_swizzle[2] = 0;
				params.m_swizzle[3] = 1;
			}
			if (!c.init(params)) {
				stbi_image_free(stb_data);
				return false;
			}
			basisu::basis_compressor::error_code err = c.process();
			stbi_image_free(stb_data);
			if (err != basisu::basis_compressor::cECSuccess) return false;

			const basisu::uint8_vec& out = c.get_output_basis_file();
			return dst.write(out.get_ptr(), out.size_in_bytes());
		#else
			dst.write("lbc", 3);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == TextureMeta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			flags |= meta.filter == TextureMeta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
			dst.write(&flags, sizeof(flags));

			TextureCompressor::Input input(w, h, 1, 1, m_allocator);
			input.add(Span(data, w * h * 4), 0, 0, 0);
			input.is_srgb = meta.srgb;
			input.is_normalmap = meta.is_normalmap;
			input.has_alpha = comps == 4;
			TextureCompressor::Options options;
			options.generate_mipmaps = meta.mips;
			options.stochastic_mipmap = meta.stochastic_mipmap; 
			options.scale_coverage_ref = meta.scale_coverage;
			options.compress = meta.compress;
			const bool res = TextureCompressor::compress(input, options, dst, m_allocator);
			stbi_image_free(stb_data);
			return res;
		#endif
	}

	bool compile(const Path& src) override {
		char ext[5] = {};
		copyString(Span(ext), Path::getExtension(src));
		makeLowercase(Span(ext), ext);

		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_allocator);
		if (!fs.getContentSync(src, src_data)) return false;
		
		OutputMemoryStream out(m_allocator);
		TextureMeta meta;
		meta.load(src, m_app);
		if (equalStrings(ext, "raw")) {
			if (meta.scale_coverage >= 0) logError(src, ": RAW can not scale coverage");
			if (meta.compress) logError(src, ": RAW can not be copressed");
			if (meta.mips) logError(src, ": RAW can not have mipmaps");
			
			out.write(ext, 3);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == TextureMeta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == TextureMeta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			flags |= meta.filter == TextureMeta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
			out.write(flags);
			out.write(src_data.data(), src_data.size());
		}
		else if(equalStrings(ext, "jpg") || equalStrings(ext, "jpeg") || equalStrings(ext, "png") || equalStrings(ext, "tga")) {
			if (!compileImage(src, src_data, out, meta)) return false;
		}
		else if (equalStrings(ext, "ltc")) {
			if (!compileComposite(src_data, out, meta, src)) return false;
		}
		else {
			ASSERT(false);
		}

		return m_app.getAssetCompiler().writeCompiledResource(src, Span(out.data(), (i32)out.size()));
	}

	const char* getLabel() const override { return "Texture"; }

	TagAllocator m_allocator;
	StudioApp& m_app;
	TextureTileJob* m_jobs_head = nullptr;
	TextureTileJob* m_jobs_tail = nullptr;
	TextureMeta m_meta;
	FilePathHash m_meta_res;
};

struct ModelPropertiesPlugin final : PropertyGrid::IPlugin {
	ModelPropertiesPlugin(StudioApp& app) : m_app(app) {}
	
	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != MODEL_INSTANCE_TYPE) return;
		if (entities.length() != 1) return;

		RenderModule* module = (RenderModule*)editor.getWorld()->getModule(cmp_type);
		EntityRef entity = entities[0];
		Model* model = module->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;

		const i32 count = model->getMeshCount();
		if (count == 1) {
			ImGuiEx::Label("Material");
			
			Path path = module->getModelInstanceMaterialOverride(entity);
			if (path.isEmpty()) {
				path = model->getMesh(0).material->getPath();
			}
			if (m_app.getAssetBrowser().resourceInput("##mat", path, Material::TYPE)) {
				editor.setProperty(MODEL_INSTANCE_TYPE, "", -1, "Material", Span(&entity, 1), path);
			}
			return;
		}
		

		bool open = true;
		if (count > 1) {
			open = ImGui::TreeNodeEx("Materials", ImGuiTreeNodeFlags_DefaultOpen);
		}
		if (open) {
			const float go_to_w = ImGui::CalcTextSize(ICON_FA_BULLSEYE).x;
			for (i32 i = 0; i < count; ++i) {
				Material* material = model->getMesh(i).material;
				bool duplicate = false;
				for (i32 j = 0; j < i; ++j) {
					if (model->getMesh(j).material == material) {
						duplicate = true;
					}
				}
				if (duplicate) continue;
				ImGui::PushID(i);
				
				const float w = ImGui::GetContentRegionAvail().x - go_to_w;
				ImGuiEx::TextClipped(material->getPath().c_str(), w);
				ImGui::SameLine();
				if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to"))
				{
					m_app.getAssetBrowser().openEditor(material->getPath());
				}
				ImGui::PopID();
			}
			if(count > 1) ImGui::TreePop();
		}
	}

	StudioApp& m_app;
};

static void getTextureImage(DrawStream& stream, gpu::TextureHandle texture, u32 w, u32 h, gpu::TextureFormat out_format, Span<u8> data) {
	gpu::TextureHandle staging = gpu::allocTextureHandle();
	const gpu::TextureFlags flags = gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::READBACK;
	stream.createTexture(staging, w, h, 1, out_format, flags, "staging_buffer");
	stream.copy(staging, texture, 0, 0);
	stream.readTexture(staging, 0, data);
	stream.destroy(staging);
}

struct ModelPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow, SimpleUndoRedo {
		EditorWindow(const Path& path, ModelPlugin& plugin, StudioApp& app, IAllocator& allocator)
			: AssetEditorWindow(app)
			, SimpleUndoRedo(allocator)
			, m_app(app)
			, m_plugin(plugin)
			, m_meta(allocator)
			, m_viewer(app)
		{
			Engine& engine = app.getEngine();
			m_resource = engine.getResourceManager().load<Model>(path);
			m_meta.load(path, m_app);
			pushUndo(NO_MERGE_UNDO);

			m_renderer = static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));

			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(MODEL_INSTANCE_TYPE));
			render_module->setModelInstancePath(*m_viewer.m_mesh, m_resource->getPath());

			m_fbx_async_handle = engine.getFileSystem().getContent(path, makeDelegate<&EditorWindow::onFBXLoaded>(this));
		}

		~EditorWindow() {
			if (m_fbx_async_handle.isValid()) {
				m_app.getEngine().getFileSystem().cancel(m_fbx_async_handle);
			}
			m_resource->decRefCount();
		}
		
		void onFBXLoaded(Span<const u8> data, bool success) {
			m_fbx_async_handle = FileSystem::AsyncHandle::invalid();
			ofbx::IScene* fbx_scene = ofbx::load(data.begin(), data.length(), (u16)ofbx::LoadFlags::IGNORE_GEOMETRY);
			if (!fbx_scene) return;

			m_has_meshes = fbx_scene->getMeshCount() > 0;

			fbx_scene->destroy();
		}

		void deserialize(InputMemoryStream& blob) override { m_meta.deserialize(blob, Path("undo/redo")); }
		void serialize(OutputMemoryStream& blob) override { m_meta.serialize(blob); }

		void saveUndo(bool changed) {
			if (!changed) return;

			pushUndo(ImGui::GetItemID());
			m_dirty = true;
		}

		void save() {
			OutputMemoryStream blob(m_app.getAllocator());
			m_meta.serialize(blob);
			m_app.getAssetCompiler().updateMeta(m_resource->getPath(), blob);
			m_dirty = false;
		}
		
		bool onAction(const Action& action) override { 
			const CommonActions& actions = m_app.getCommonActions();
			if (&action == &actions.save) save();
			else if (&action == &actions.undo) undo();
			else if (&action == &actions.redo) redo();
			else return false;
			return true;
		}

		void importGUI() {
			if (m_has_meshes) {
				ImGuiEx::Label("Bake vertex AO");
				saveUndo(ImGui::Checkbox("##vrtxao", &m_meta.bake_vertex_ao));
				ImGuiEx::Label("Mikktspace tangents");
				saveUndo(ImGui::Checkbox("##mikktspace", &m_meta.use_mikktspace));
				ImGuiEx::Label("Force skinned");
				saveUndo(ImGui::Checkbox("##frcskn", &m_meta.force_skin));
				ImGuiEx::Label("Split");
				saveUndo(ImGui::Checkbox("##split", &m_meta.split));
				ImGuiEx::Label("Create impostor mesh");
				saveUndo(ImGui::Checkbox("##creimp", &m_meta.create_impostor));
				if (m_meta.create_impostor) {
					ImGuiEx::Label("Bake impostor normals");
					saveUndo(ImGui::Checkbox("##impnrm", &m_meta.bake_impostor_normals));
					ImGui::TextDisabled("(?)");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("%s", "To use impostors, check `Create impostor mesh` and press this button. "
						"When the mesh changes, you need to regenerate the impostor texture by pressing this button again.");
					}
					ImGui::SameLine();
					if (ImGui::Button("Create impostor texture")) {
						FBXImporter importer(m_app);
						importer.init();
						IAllocator& allocator = m_app.getAllocator();
						Array<u32> gb0(allocator); 
						Array<u32> gb1(allocator);
						Array<u16> gbdepth(allocator);
						Array<u32> shadow(allocator); 
						IVec2 tile_size;
						importer.createImpostorTextures(m_resource, gb0, gb1, gbdepth, shadow, tile_size, m_meta.bake_impostor_normals);
						postprocessImpostor(gb0, gb1, shadow, tile_size, allocator);
						const PathInfo fi(m_resource->getPath());
						Path img_path(fi.dir, fi.basename, "_impostor0.tga");
						ASSERT(gb0.size() == tile_size.x * 9 * tile_size.y * 9);
				
						os::OutputFile file;
						FileSystem& fs = m_app.getEngine().getFileSystem();
						if (fs.open(img_path, file)) {
							Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)gb0.begin(), gpu::isOriginBottomLeft(), Path(img_path), allocator);
							file.close();
						}
						else {
							logError("Failed to open ", img_path);
						}

						img_path = Path(fi.dir, fi.basename, "_impostor1.tga");
						if (fs.open(img_path, file)) {
							Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)gb1.begin(), gpu::isOriginBottomLeft(), Path(img_path), allocator);
							file.close();
						}
						else {
							logError("Failed to open ", img_path);
						}

						img_path = Path(fi.dir, fi.basename, "_impostor_depth.raw");
						if (fs.open(img_path, file)) {
							RawTextureHeader header;
							header.width = tile_size.x * 9;
							header.height = tile_size.y * 9;
							header.depth = 1;
							header.channel_type = RawTextureHeader::ChannelType::U16;
							header.channels_count = 1;
							bool res = file.write(header);
							if (gpu::isOriginBottomLeft()) {
								res = file.write(gbdepth.begin(), gbdepth.byte_size()) && res;
							} else {
								Array<u16> flipped_depth(m_app.getAllocator());
								flipped_depth.resize(gbdepth.size());
								for (u32 j = 0; j < header.height; ++j) {
									for (u32 i = 0; i < header.width; ++i) {
										flipped_depth[i + j * header.width] = gbdepth[i + (header.height - j - 1) * header.width];
									}
								}
								res = file.write(flipped_depth.begin(), flipped_depth.byte_size()) && res;
							}
							if (!res) logError("Failed to write ", img_path);
							file.close();
						}
						else {
							logError("Failed to open ", img_path);
						}

						img_path = Path(fi.dir, fi.basename, "_impostor2.tga");
						if (fs.open(img_path, file)) {
							Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)shadow.begin(), gpu::isOriginBottomLeft(), Path(img_path), allocator);
							file.close();
						}
						else {
							logError("Failed to open ", img_path);
						}
					}
				}
				ImGuiEx::Label("Scale");
				saveUndo(ImGui::InputFloat("##scale", &m_meta.scale));
				ImGuiEx::Label("Culling scale (?)");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", "Use this for animated meshes if they are culled when still visible.");
				}
				ImGui::SameLine();
				saveUndo(ImGui::InputFloat("##cull_scale", &m_meta.culling_scale));
				ImGuiEx::Label("Vertex colors");
				i32 vertex_colors_mode = m_meta.import_vertex_colors ? (m_meta.vertex_color_is_ao ? 2 : 1) : 0;
				if (ImGui::Combo("##vercol", &vertex_colors_mode, "Do not import\0Import\0Import as AO")) {
					switch(vertex_colors_mode) {
						case 0:
							m_meta.import_vertex_colors = false;
							m_meta.vertex_color_is_ao = false;
							break;
						case 1:
							m_meta.import_vertex_colors = true;
							m_meta.vertex_color_is_ao = false;
							break;
						case 2:
							m_meta.import_vertex_colors = true;
							m_meta.vertex_color_is_ao = true;
							break;
					}
					saveUndo(true);
				}
				ImGuiEx::Label("Physics");
				if (ImGui::BeginCombo("##phys", ModelMeta::toString(m_meta.physics))) {
					if (ImGui::Selectable("None")) {
						m_meta.physics = FBXImporter::ImportConfig::Physics::NONE;
						saveUndo(true);
					}
					if (ImGui::Selectable("Convex")) {
						m_meta.physics = FBXImporter::ImportConfig::Physics::CONVEX;
						saveUndo(true);
					}
					if (ImGui::Selectable("Triangle mesh")) {
						m_meta.physics = FBXImporter::ImportConfig::Physics::TRIMESH;
						saveUndo(true);
					}
					ImGui::EndCombo();
				}

				ImGuiEx::Label("Skeleton");
				saveUndo(m_app.getAssetBrowser().resourceInput("##ske", m_meta.skeleton, Model::TYPE));
				if (m_meta.skeleton.isEmpty()) {
					ImGuiEx::Label("Root motion bone");
					saveUndo(inputString("##rmb", &m_meta.root_motion_bone));
				}

				ImGui::SeparatorText("LODs");
				ImGuiEx::Label("LOD count");
				if (ImGui::SliderInt("##lodcount", (i32*)&m_meta.lod_count, 1, 4)) {
					m_meta.lods_distances[1] = maximum(m_meta.lods_distances[0] + 0.01f, m_meta.lods_distances[1]);
					m_meta.lods_distances[2] = maximum(m_meta.lods_distances[1] + 0.01f, m_meta.lods_distances[2]);
					m_meta.lods_distances[3] = maximum(m_meta.lods_distances[2] + 0.01f, m_meta.lods_distances[3]);
					saveUndo(true);
				}

				if (ImGui::BeginTable("lods", 4, ImGuiTableFlags_BordersOuter)) {
					ImGui::TableSetupColumn("LOD", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
					ImGui::TableSetupColumn("Distance");
					ImGui::TableSetupColumn("Auto LOD", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
					ImGui::TableSetupColumn("% triangles", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
					ImGui::TableHeadersRow();
				
					for(u32 i = 0; i < m_meta.lod_count; ++i) {
						ImGui::PushID(i);
					
						ImGui::TableNextColumn();
						if (m_meta.create_impostor && i == m_meta.lod_count - 1) {
							ImGui::TextUnformatted("Impostor");
						}
						else {
							ImGui::Text("%d", i);
						}

						ImGui::TableNextColumn();
						ImGui::SetNextItemWidth(-1);
						if (ImGui::DragFloat("##lod", &m_meta.lods_distances[i], 1, 0, FLT_MAX, "%.1f")) {
							m_meta.lods_distances[0] = maximum(0.f, m_meta.lods_distances[0]);
							m_meta.lods_distances[1] = maximum(m_meta.lods_distances[0] + 0.01f, m_meta.lods_distances[1]);
							m_meta.lods_distances[2] = maximum(m_meta.lods_distances[1] + 0.01f, m_meta.lods_distances[2]);
							m_meta.lods_distances[3] = maximum(m_meta.lods_distances[2] + 0.01f, m_meta.lods_distances[3]);
							saveUndo(true);
						}

						ImGui::TableNextColumn();
						bool autolod = m_meta.autolod_mask & (1 << i);
						if (!m_meta.create_impostor || i < m_meta.lod_count - 1) {
							ImGui::SetNextItemWidth(-1);
							if (ImGui::Checkbox("##auto_lod", &autolod)) {
								m_meta.autolod_mask &= ~(1 << i);
								if (autolod) m_meta.autolod_mask |= 1 << i;
								saveUndo(true);
							}
						}

						ImGui::TableNextColumn();
						if ((!m_meta.create_impostor || i < m_meta.lod_count - 1) && autolod) {
							ImGui::SetNextItemWidth(-1);
							float f = m_meta.autolod_coefs[i] * 100;
							if (ImGui::DragFloat("##lodcoef", &f, 1, 0, 100, "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
								m_meta.autolod_coefs[i] = f * 0.01f;
								saveUndo(true);
							}
						}
					
						ImGui::PopID();
					}

					ImGui::EndTable();
				}
			}
			else {
				ImGui::TextUnformatted("No mesh data");
				ImGuiEx::Label("Skeleton");
				saveUndo(m_app.getAssetBrowser().resourceInput("##ske", m_meta.skeleton, Model::TYPE));
			}

			if (m_meta.clips.empty()) {
				if (ImGui::Button(ICON_FA_PLUS " Add subclip")) {
					m_meta.clips.emplace();
					saveUndo(true);
				}
			}
			else {
				if (ImGui::BeginTable("clips", 4, ImGuiTableFlags_BordersOuter)) {
					ImGui::TableSetupColumn("Name");
					ImGui::TableSetupColumn("Start frame");
					ImGui::TableSetupColumn("End frame");
					ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
					ImGui::TableHeadersRow();

					for (FBXImporter::ImportConfig::Clip& clip : m_meta.clips) {
						ImGui::TableNextColumn();
						ImGui::PushID(&clip);
						ImGui::SetNextItemWidth(-1);
						saveUndo(ImGui::InputText("##name", clip.name.data, sizeof(clip.name.data)));
						ImGui::TableNextColumn();
						ImGui::SetNextItemWidth(-1);
						saveUndo(ImGui::InputInt("##from", (i32*)&clip.from_frame));
						ImGui::TableNextColumn();
						ImGui::SetNextItemWidth(-1);
						saveUndo(ImGui::InputInt("##to", (i32*)&clip.to_frame));
						ImGui::TableNextColumn();
						if (ImGuiEx::IconButton(ICON_FA_TRASH, "Delete")) {
							m_meta.clips.erase(u32(&clip - m_meta.clips.begin()));
							saveUndo(true);
							ImGui::PopID();
							break;
						}
						ImGui::PopID();
					}

					ImGui::TableNextColumn();
					if (ImGuiEx::IconButton(ICON_FA_PLUS_CIRCLE, " Add subclip")) {
						m_meta.clips.emplace();
						saveUndo(true);
					}

					ImGui::EndTable();
				}
			}
		}

		void infoGUI() {
			if (!m_resource->isReady()) {
				ImGui::TextUnformatted("Failed to load.");
				return;
			}

			ImGuiEx::Label("Bounding radius (from origin)");
			ImGui::Text("%f", m_resource->getOriginBoundingRadius());
			ImGuiEx::Label("Bounding radius (from center)");
			ImGui::Text("%f", m_resource->getCenterBoundingRadius());

			if (m_resource->getMeshCount() > 0) {
				ImGui::SeparatorText("LODs");
				const LODMeshIndices* lods = m_resource->getLODIndices();
				float* distances = m_resource->getLODDistances();
				if (lods[0].to >= 0 && !m_resource->isFailure() && ImGui::BeginTable("lodtbl", 4, ImGuiTableFlags_Resizable)) {
					ImGui::TableSetupColumn("LOD");
					ImGui::TableSetupColumn("Distance");
					ImGui::TableSetupColumn("# of meshes");
					ImGui::TableSetupColumn("# of triangles");
					ImGui::TableHeadersRow();

					for (i32 i = 0; i < Model::MAX_LOD_COUNT && lods[i].to >= 0; ++i) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						ImGui::Text("%d", i);
						ImGui::TableNextColumn();
						float dist = sqrtf(distances[i]);
						if (ImGui::DragFloat("", &dist)) {
							distances[i] = dist * dist;
						}
						ImGui::TableNextColumn();
						ImGui::Text("%d", lods[i].to - lods[i].from + 1);
						ImGui::TableNextColumn();
						int tri_count = 0;
						for (i32 j = lods[i].from; j <= lods[i].to; ++j) {
							i32 indices_count = (i32)m_resource->getMesh(j).indices.size() >> 1;
							if (!m_resource->getMesh(j).flags.isSet(Mesh::Flags::INDICES_16_BIT)) {
								indices_count >>= 1;
							}
							tri_count += indices_count / 3;
						}
						ImGui::Text("%d", tri_count);
					}

					ImGui::EndTable();
				}

				ImGui::SeparatorText("Meshes");
				if (ImGui::BeginTable("mshtbl", 3, ImGuiTableFlags_Resizable)) {
					ImGui::TableSetupColumn("Name");
					ImGui::TableSetupColumn("Triangles");
					ImGui::TableSetupColumn("Material");
					ImGui::TableHeadersRow();
				
					const float go_to_w = ImGui::CalcTextSize(ICON_FA_BULLSEYE).x;
					for (i32 i = 0; i < m_resource->getMeshCount(); ++i) {
						ImGui::PushID(i);
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const Mesh& mesh = m_resource->getMesh(i);
						ImGuiEx::TextUnformatted(mesh.name);
						ImGui::TableNextColumn();
						ImGui::Text("%d", ((i32)mesh.indices.size() >> (mesh.areIndices16() ? 1 : 2)) / 3);
						ImGui::TableNextColumn();
						const float w = ImGui::GetContentRegionAvail().x - go_to_w;
						ImGuiEx::TextClipped(mesh.material->getPath().c_str(), w);
						ImGui::SameLine();
						if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to")) {
							m_app.getAssetBrowser().openEditor(mesh.material->getPath());
						}
						ImGui::PopID();
					}
					ImGui::EndTable();
				}
			}

			if (m_resource->isReady() && m_resource->getBoneCount() > 0) {
				ImGui::SeparatorText("Bones");
				ImGuiEx::Label("Count");
				ImGui::Text("%d", m_resource->getBoneCount());
				if (ImGui::BeginTable("bnstbl", 4, ImGuiTableFlags_Resizable)) {
					ImGui::TableSetupColumn("Name");
					ImGui::TableSetupColumn("Position");
					ImGui::TableSetupColumn("Rotation");
					ImGui::TableSetupColumn("Parent");
					ImGui::TableHeadersRow();
					for (i32 i = 0; i < m_resource->getBoneCount(); ++i) {
						ImGui::TableNextRow();
						ImGui::TableNextColumn();
						const Model::Bone& bone = m_resource->getBone(i);
						ImGuiEx::TextUnformatted(bone.name);
						ImGui::TableNextColumn();
						Vec3 pos = bone.transform.pos;
						ImGui::Text("%f; %f; %f", pos.x, pos.y, pos.z);
						ImGui::TableNextColumn();
						Quat rot = bone.transform.rot;
						ImGui::Text("%f; %f; %f; %f", rot.x, rot.y, rot.z, rot.w);
						ImGui::TableNextColumn();
						if (bone.parent_idx >= 0) {
							ImGuiEx::TextUnformatted(m_resource->getBone(bone.parent_idx).name);
						}
					}
					ImGui::EndTable();
				}
			}
		}

		void windowGUI() override {
			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_resource);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(*m_resource);
				if (ImGuiEx::IconButton(ICON_FA_UNDO, "Undo", canUndo())) undo();
				if (ImGuiEx::IconButton(ICON_FA_REDO, "Redo", canRedo())) redo();
				ImGui::EndMenuBar();
			}

			if (m_resource->isEmpty()) {
				ImGui::TextUnformatted("Loading...");
				return;
			}

			if (!ImGui::BeginTable("tab", 2, ImGuiTableFlags_Resizable)) return;

			ImGui::TableSetupColumn(nullptr, ImGuiTableColumnFlags_WidthFixed, 250);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();

			importGUI();
			if (ImGui::CollapsingHeader("Info")) infoGUI();

			ImGui::TableNextColumn();
			previewGUI();

			ImGui::EndTable();
		}

		static u32 getMaxLOD(Model& model) {
			for (u32 i = 1; i < Model::MAX_LOD_COUNT; ++i) {
				if (model.getLODIndices()[i].to < 0) return i - 1;
			}
			return 0;
		}
		 
		static void enableWireframe(Model& model, bool enable) {
			for (u32 i = 0; i < (u32)model.getMeshCount(); ++i) {
				Mesh& mesh = model.getMesh(i);
				mesh.material->setWireframe(enable);
			}
		}

		void previewGUI() {
			if (!m_resource->isReady()) return;

			if (ImGui::Checkbox("Wireframe", &m_wireframe)) enableWireframe(*m_resource, m_wireframe);
			ImGui::SameLine();
			ImGui::Checkbox("Show skeleton", &m_show_skeleton);
			ImGui::SameLine();
			if (ImGui::Button("Save preview")) {
				m_resource->incRefCount();
				m_plugin.renderTile(m_resource, &m_viewer.m_viewport);
			}
			ImGui::SameLine();
			if (ImGui::Button("Reset camera")) m_viewer.resetCamera(*m_resource);
			// TODO this does not work, two lods are rendered
			/*
			auto* render_module = static_cast<RenderModule*>(m_viewer.m_world->getModule(MODEL_INSTANCE_TYPE));
			ASSERT(render_module);
			ImGui::SameLine();
			ImGui::InputInt("LOD", &m_preview_lod);
			m_preview_lod = clamp(m_preview_lod, 0, getMaxLOD(*m_resource));
			render_module->setModelInstanceLOD((EntityRef)m_mesh, m_preview_lod);
			*/

			if (!m_init) {
				m_init = true;
				m_viewer.resetCamera(*m_resource);
			}

			m_viewer.gui();
			if (m_show_skeleton) m_viewer.drawSkeleton(BoneNameHash());
		}
	
		const Path& getPath() override { return m_resource->getPath(); }
		const char* getName() const override { return "model editor"; }

		StudioApp& m_app;
		ModelPlugin& m_plugin;
		Model* m_resource;
		WorldViewer m_viewer;
		Renderer* m_renderer;
		ModelMeta m_meta;
		bool m_wireframe = false;
		bool m_init = false;
		i32 m_preview_lod = 0;
		bool m_has_meshes = true;
		bool m_show_skeleton = true;
		FileSystem::AsyncHandle m_fbx_async_handle = FileSystem::AsyncHandle::invalid();
	};

	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
		, m_tile(app.getAllocator())
		, m_fbx_importer(app)
	{
		app.getAssetCompiler().registerExtension("fbx", Model::TYPE);
	}

	~ModelPlugin() {
		if (m_downscale_program) m_renderer->getEndFrameDrawStream().destroy(m_downscale_program);
		jobs::wait(&m_subres_signal);
		
		Engine& engine = m_app.getEngine();
		if (m_tile.world) engine.destroyWorld(*m_tile.world);
		m_tile.pipeline.reset();
	}

	void openEditor(const Path& path) override {
		IAllocator& allocator = m_app.getAllocator();
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(allocator, path, *this, m_app, m_app.getAllocator());
		m_app.getAssetBrowser().addWindow(win.move());
	}

	void init() {
		Engine& engine = m_app.getEngine();
		m_renderer = static_cast<Renderer*>(engine.getSystemManager().getSystem("renderer"));
		m_fbx_importer.init();
	}

	void addSubresources(AssetCompiler& compiler, const Path& _path) override {
		compiler.addResource(Model::TYPE, _path);

		ModelMeta meta(m_app.getAllocator());
		meta.load(_path, m_app);
		jobs::runLambda([this, _path, meta = static_cast<ModelMeta&&>(meta)]() {
			FBXImporter importer(m_app);
			AssetCompiler& compiler = m_app.getAssetCompiler();

			const char* path = _path.c_str();
			if (path[0] == '/') ++path;
			importer.setSource(Path(path), true, false);

			if(meta.split) {
				const Array<FBXImporter::ImportMesh>& meshes = importer.getMeshes();
				for (int i = 0; i < meshes.size(); ++i) {
					char mesh_name[256];
					importer.getImportMeshName(meshes[i], mesh_name);
					Path tmp(mesh_name, ".fbx:", path);
					compiler.addResource(Model::TYPE, tmp);
				}
			}

			if (meta.physics != FBXImporter::ImportConfig::Physics::NONE) {
				Path tmp(".phy:", path);
				ResourceType physics_geom("physics_geometry");
				compiler.addResource(physics_geom, tmp);
			}

			if (meta.clips.empty()) {
				const Array<FBXImporter::ImportAnimation>& animations = importer.getAnimations();
				for (const FBXImporter::ImportAnimation& anim : animations) {
					Path tmp(anim.name, ".ani:", path);
					compiler.addResource(ResourceType("animation"), tmp);
				}
			}
			else {
				for (const FBXImporter::ImportConfig::Clip& clip : meta.clips) {
					Path tmp(clip.name, ".ani:", Path(path));
					compiler.addResource(ResourceType("animation"), tmp);
				}
			}

		}, &m_subres_signal, 2);			
	}

	bool compile(const Path& src) override {
		ASSERT(Path::hasExtension(src, "fbx"));
		Path filepath = Path(Path::getResource(src));
		FBXImporter::ImportConfig cfg;
		ModelMeta meta(m_app.getAllocator()); 
		meta.load(Path(filepath), m_app);
		cfg.autolod_mask = meta.autolod_mask;
		memcpy(cfg.autolod_coefs, meta.autolod_coefs, sizeof(meta.autolod_coefs));
		cfg.mikktspace_tangents = meta.use_mikktspace;
		cfg.mesh_scale = meta.scale;
		cfg.bounding_scale = meta.culling_scale;
		cfg.physics = meta.physics;
		cfg.bake_vertex_ao = meta.bake_vertex_ao;
		cfg.import_vertex_colors = meta.import_vertex_colors;
		cfg.vertex_color_is_ao = meta.vertex_color_is_ao;
		cfg.lod_count = meta.lod_count;
		memcpy(cfg.lods_distances, meta.lods_distances, sizeof(meta.lods_distances));
		cfg.create_impostor = meta.create_impostor;
		cfg.clips = meta.clips;
		cfg.animation_flags = meta.root_motion_flags;
		cfg.anim_rotation_error = meta.anim_rotation_error;
		cfg.anim_translation_error = meta.anim_translation_error;
		m_fbx_importer.setSource(filepath, false, meta.force_skin);
		if (m_fbx_importer.getMeshes().empty() && m_fbx_importer.getAnimations().empty()) {
			if (m_fbx_importer.getOFBXScene()) {
				if (m_fbx_importer.getOFBXScene()->getMeshCount() > 0) {
					logError("No meshes with materials found in ", src);
				}
				else {
					logError("No meshes or animations found in ", src);
				}
			}
		}

		if (meta.split) {
			cfg.origin = FBXImporter::ImportConfig::Origin::CENTER;
			m_fbx_importer.writeSubmodels(filepath, cfg);
			m_fbx_importer.writePrefab(filepath, cfg);
		}
		cfg.origin = FBXImporter::ImportConfig::Origin::SOURCE;
		m_fbx_importer.writeModel(src, cfg);
		m_fbx_importer.writeMaterials(filepath, cfg);
		m_fbx_importer.writeAnimations(filepath, cfg);
		m_fbx_importer.writePhysics(filepath, cfg);
		return true;
	}


	void createTileWorld()
	{
		Engine& engine = m_app.getEngine();
		m_tile.world = &engine.createWorld(false);
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_tile.pipeline = Pipeline::create(*m_renderer, pres, "PREVIEW");

		RenderModule* render_module = (RenderModule*)m_tile.world->getModule(MODEL_INSTANCE_TYPE);
		const EntityRef env_probe = m_tile.world->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_tile.world->createComponent(ENVIRONMENT_PROBE_TYPE, env_probe);
		render_module->getEnvironmentProbe(env_probe).outer_range = Vec3(1e3);
		render_module->getEnvironmentProbe(env_probe).inner_range = Vec3(1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_tile.world->createEntity({10, 10, 10}, mtx.getRotation());
		m_tile.world->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_module->getEnvironment(light_entity).direct_intensity = 5;
		render_module->getEnvironment(light_entity).indirect_intensity = 1;
		
		m_tile.pipeline->setWorld(m_tile.world);
	}


	static void postprocessImpostor(Array<u32>& gb0, Array<u32>& gb1, Array<u32>& shadow, const IVec2& tile_size, IAllocator& allocator) {
		struct Cell {
			i16 x, y;
		};
		const IVec2 size = tile_size * 9;
		Array<Cell> cells(allocator);
		cells.resize(gb0.size());
		const u32* data = gb0.begin();
		for (i32 j = 0; j < size.y; ++j) {
			for (i32 i = 0; i < size.x; ++i) {
				const u32 idx = i + j * size.x;
				if (data[idx] & 0xff000000) {
					cells[i].x = i;
					cells[i].y = j;
				}
				else {
					cells[i].x = -3 * size.x;
					cells[i].y = -3 * size.y;
				}
			}
		}

		auto pow2 = [](i32 v){
			return v * v;
		};

		for (i32 j = 0; j < size.y; ++j) {
			for (i32 i = 0; i < size.x; ++i) {
				const u32 idx = i + j * size.x;
				if (data[idx] & 0xff000000) {
					cells[idx].x = i;
					cells[idx].y = j;
				}
				else {
					if(i > 0) {
						const u32 dist_0 = pow2(cells[idx].x - i) + pow2(cells[idx].y - j);
						const u32 dist_x = pow2(cells[idx - 1].x - i) + pow2(cells[idx - 1].y - j);
						if(dist_x < dist_0) {
							cells[idx] = cells[idx - 1];
						}
					}					
					if(j > 0) {
						const u32 dist_0 = pow2(cells[idx].x - i) + pow2(cells[idx].y - j);
						const u32 dist_y = pow2(cells[idx - size.x].x - i) + pow2(cells[idx - size.x].y - j);
						if(dist_y < dist_0) {
							cells[idx] = cells[idx - size.x];
						}
					}					
				}
			}
		}

		for (i32 j = size.y - 1; j >= 0; --j) {
			for (i32 i = size.x - 1; i>= 0; --i) {
				const u32 idx = i + j * size.x;
				if (data[idx] & 0xff000000) {
					cells[idx].x = i;
					cells[idx].y = j;
				}
				else {
					if(i < size.x - 1) {
						const u32 dist_0 = pow2(cells[idx].x - i) + pow2(cells[idx].y - j);
						const u32 dist_x = pow2(cells[idx + 1].x - i) + pow2(cells[idx + 1].y - j);
						if(dist_x < dist_0) {
							cells[idx] = cells[idx + 1];
						}
					}					
					if(j < size.y - 1) {
						const u32 dist_0 = pow2(cells[idx].x - i) + pow2(cells[idx].y - j);
						const u32 dist_y = pow2(cells[idx + size.x].x - i) + pow2(cells[idx + size.x].y - j);
						if(dist_y < dist_0) {
							cells[idx] = cells[idx + size.x];
						}
					}					
				}
			}
		}

		Array<u32> tmp(allocator);
		tmp.resize(gb0.size());
		if (cells[0].x >= 0) {
			for (i32 j = 0; j < size.y; ++j) {
				for (i32 i = 0; i < size.x; ++i) {
					const u32 idx = i + j * size.x;
					const u8 alpha = data[idx] >> 24;
					tmp[idx] = data[cells[idx].x + cells[idx].y * size.x];
					tmp[idx] = (alpha << 24) | (tmp[idx] & 0xffFFff);
				}
			}
			memcpy(gb0.begin(), tmp.begin(), tmp.byte_size());

			const u32* gb1_data = gb1.begin();
			for (i32 j = 0; j < size.y; ++j) {
				for (i32 i = 0; i < size.x; ++i) {
					const u32 idx = i + j * size.x;
					tmp[idx] = gb1_data[cells[idx].x + cells[idx].y * size.x];
				}
			}
			memcpy(gb1.begin(), tmp.begin(), tmp.byte_size());

			const u32* shadow_data = shadow.begin();
			for (i32 j = 0; j < size.y; ++j) {
				for (i32 i = 0; i < size.x; ++i) {
					const u32 idx = i + j * size.x;
					tmp[idx] = shadow_data[cells[idx].x + cells[idx].y * size.x];
				}
			}
			memcpy(shadow.begin(), tmp.begin(), tmp.byte_size());
		}
		else {
			// nothing was rendered
			memset(gb0.begin(), 0xff, gb0.byte_size());
			memset(gb1.begin(), 0xff, gb1.byte_size());
		}
	}

	const char* getLabel() const override { return "Model"; }

	void pushTileQueue(const Path& path)
	{
		ASSERT(!m_tile.queue.full());
		Engine& engine = m_app.getEngine();
		ResourceManagerHub& resource_manager = engine.getResourceManager();

		Resource* resource;
		if (Path::hasExtension(path, "fab")) {
			resource = resource_manager.load<PrefabResource>(path);
		}
		else if (Path::hasExtension(path, "mat")) {
			resource = resource_manager.load<Material>(path);
		}
		else {
			resource = resource_manager.load<Model>(path);
		}
		m_tile.queue.push(resource);
	}


	void popTileQueue()
	{
		m_tile.queue.pop();
		if (m_tile.paths.empty()) return;

		Path path = m_tile.paths.back();
		m_tile.paths.pop();
		pushTileQueue(path);
	}
	
	static void destroyEntityRecursive(World& world, EntityPtr entity)
	{
		if (!entity.isValid()) return;
			
		EntityRef e = (EntityRef)entity;
		destroyEntityRecursive(world, world.getFirstChild(e));
		destroyEntityRecursive(world, world.getNextSibling(e));

		world.destroyEntity(e);
	}

	void update() override
	{
		if (m_tile.waiting) {
			if (!m_app.getEngine().getFileSystem().hasWork()) {
				renderPrefabSecondStage();
				m_tile.waiting = false;
			}
		}
		if (m_tile.frame_countdown >= 0) {
			--m_tile.frame_countdown;
			if (m_tile.frame_countdown == -1) {
				destroyEntityRecursive(*m_tile.world, (EntityRef)m_tile.entity);
				Engine& engine = m_app.getEngine();
				FileSystem& fs = engine.getFileSystem();
				const Path path(fs.getBasePath(), ".lumix/asset_tiles/", m_tile.path_hash, ".lbc");

				if (!gpu::isOriginBottomLeft()) {
					u32* p = (u32*)m_tile.data.getMutableData();
					for (u32 y = 0; y < AssetBrowser::TILE_SIZE >> 1; ++y) {
						for (u32 x = 0; x < AssetBrowser::TILE_SIZE; ++x) {
							swap(p[x + y * AssetBrowser::TILE_SIZE], p[x + (AssetBrowser::TILE_SIZE - y - 1) * AssetBrowser::TILE_SIZE]);
						}
					}
				}

				saveAsLBC(path.c_str(), m_tile.data.data(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, false, gpu::isOriginBottomLeft(), m_app.getAllocator());
				memset(m_tile.data.getMutableData(), 0, m_tile.data.size());
				m_renderer->getEndFrameDrawStream().destroy(m_tile.texture);
				m_tile.entity = INVALID_ENTITY;
				m_app.getAssetBrowser().reloadTile(m_tile.path_hash);
			}
			return;
		}

		if (m_tile.entity.isValid()) return;
		if (m_tile.queue.empty()) return;

		Resource* resource = m_tile.queue.front();
		if (resource->isFailure()) {
			if (resource->getType() == Model::TYPE) {
				const Path out_path(".lumix/asset_tiles/", resource->getPath().getHash(), ".lbc");
				m_app.getAssetBrowser().reloadTile(m_tile.path_hash);
			}
			popTileQueue();
			resource->decRefCount();
			return;
		}
		if (!resource->isReady()) return;

		popTileQueue();

		if (resource->getType() == Model::TYPE) {
			renderTile((Model*)resource, nullptr);
		}
		else if (resource->getType() == Material::TYPE) {
			renderTile((Material*)resource);
		}
		else if (resource->getType() == PrefabResource::TYPE) {
			renderTile((PrefabResource*)resource);
		}
		else {
			ASSERT(false);
		}
	}


	void renderTile(PrefabResource* prefab) {
		if (!m_tile.world) createTileWorld();
		Engine& engine = m_app.getEngine();

		EntityMap entity_map(m_app.getAllocator());
		if (!engine.instantiatePrefab(*m_tile.world, *prefab, DVec3(0), Quat::IDENTITY, Vec3(1), entity_map)) return;
		if (entity_map.m_map.empty() || !entity_map.m_map[0].isValid()) return;

		m_tile.path_hash = prefab->getPath().getHash();
		prefab->decRefCount();
		m_tile.entity = entity_map.m_map[0];
		m_tile.waiting = true;
	}


	void renderPrefabSecondStage() {
		if (!m_tile.world) createTileWorld();
		AABB aabb({0, 0, 0}, {0, 0, 0});
		float radius = 1;
		World& world = *m_tile.world;
		for (EntityPtr e = world.getFirstEntity(); e.isValid(); e = world.getNextEntity((EntityRef)e)) {
			EntityRef ent = (EntityRef)e;
			const DVec3 pos = world.getPosition(ent);
			aabb.addPoint(Vec3(pos));
			if (world.hasComponent(ent, MODEL_INSTANCE_TYPE)) {
				RenderModule* module = (RenderModule*)world.getModule(MODEL_INSTANCE_TYPE);
				Model* model = module->getModelInstanceModel(ent);
				module->setModelInstanceLOD(ent, 0);
				if (model->isReady()) {
					const Transform tr = world.getTransform(ent);
					DVec3 points[8];
					model->getAABB().getCorners(tr, points);
					for (const DVec3& p : points) {
						aabb.addPoint(Vec3(p));
					}
					radius = maximum(radius, model->getCenterBoundingRadius());
				}
			}
		}

		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(1, 1, 1) * length(aabb.max - aabb.min) / SQRT2;
		Matrix mtx;
		mtx.lookAt(eye, center, normalize(Vec3(1, -1, 1)));
		Viewport viewport;
		viewport.is_ortho = true;
		viewport.ortho_size = radius * 1.1f;
		viewport.far = 8 * radius;
		viewport.near = -8 * radius;
		viewport.h = AssetBrowser::TILE_SIZE * 4;
		viewport.w = AssetBrowser::TILE_SIZE * 4;
		viewport.pos = DVec3(center);
		viewport.rot = mtx.getRotation().conjugated();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);

		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);

		DrawStream& stream = m_renderer->getDrawStream();
		
		m_tile.texture = gpu::allocTextureHandle();
		stream.createTexture(m_tile.texture, AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, "tile_final");
		gpu::TextureHandle tile_tmp = gpu::allocTextureHandle();
		stream.createTexture(tile_tmp, AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, "tile_tmp");
		stream.copy(tile_tmp, m_tile.pipeline->getOutput(), 0, 0);
		downscale(stream, tile_tmp, AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, m_tile.texture, AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);

		getTextureImage(stream
			, m_tile.texture
			, AssetBrowser::TILE_SIZE
			, AssetBrowser::TILE_SIZE
			, gpu::TextureFormat::RGBA8
			, Span(m_tile.data.getMutableData(), (u32)m_tile.data.size()));
		stream.destroy(tile_tmp);

		m_tile.frame_countdown = 3;
	}

	void downscale(DrawStream& stream, gpu::TextureHandle src, u32 src_w, u32 src_h, gpu::TextureHandle dst, u32 dst_w, u32 dst_h) {
		if (!m_downscale_program) {
			static const char* downscale_src = R"#(
				layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;
				layout (rgba8, binding = 0) uniform readonly image2D u_src;
				layout (rgba8, binding = 1) uniform writeonly image2D u_dst;
				layout(std140, binding = 4) uniform Data {
					ivec2 u_scale;
				};
				void main() {
					vec4 accum = vec4(0);
					for (int j = 0; j < u_scale.y; ++j) {
						for (int i = 0; i < u_scale.x; ++i) {
							vec4 v = imageLoad(u_src, ivec2(gl_GlobalInvocationID.xy) * u_scale + ivec2(i, j));
							accum += v;
						}
					}
					accum *= 1.0 / (u_scale.x * u_scale.y);
					imageStore(u_dst, ivec2(gl_GlobalInvocationID.xy), accum);
				}
			)#";

			m_downscale_program = gpu::allocProgramHandle();
			const gpu::ShaderType type = gpu::ShaderType::COMPUTE;
			const char* srcs[] = { downscale_src };
			stream.createProgram(m_downscale_program, gpu::StateFlags::NONE, gpu::VertexDecl(gpu::PrimitiveType::NONE), srcs, &type, 1, nullptr, 0, "downscale");
		}

		ASSERT(src_w % dst_w == 0);
		ASSERT(src_h % dst_h == 0);
		
		IVec2 src_size((i32)src_w, (i32)src_h);
		IVec2 dst_size = {(i32)dst_w, (i32)dst_h};
		const IVec2 scale = src_size / dst_size;
		const Renderer::TransientSlice ub_slice = m_renderer->allocUniform(&scale, sizeof(scale));
		stream.bindUniformBuffer(4, ub_slice.buffer, ub_slice.offset, ub_slice.size);
		stream.bindImageTexture(src, 0);
		stream.bindImageTexture(dst, 1);
		stream.useProgram(m_downscale_program);
		stream.dispatch((dst_size.x + 15) / 16, (dst_size.y + 15) / 16, 1);
	}


	void renderTile(Material* material) {
		const Path& in_path = material->getTexture(0)->getPath();
		const Path out_path(".lumix/asset_tiles/", material->getPath().getHash(), ".lbc");
		if (material->getTextureCount() == 0) {
			return;
		}
		m_texture_plugin->createTile(in_path.c_str(), out_path.c_str(), Texture::TYPE);
		material->decRefCount();
	}

	void renderTile(Model* model, const Viewport* in_viewport)
	{
		if (!m_tile.world) createTileWorld();
		RenderModule* render_module = (RenderModule*)m_tile.world->getModule(MODEL_INSTANCE_TYPE);
		if (!render_module || model->getMeshCount() == 0) {
			model->decRefCount();
			return;
		}

		EntityRef mesh_entity = m_tile.world->createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		m_tile.world->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		render_module->setModelInstancePath(mesh_entity, model->getPath());
		render_module->setModelInstanceLOD(mesh_entity, 0);
		const AABB aabb = model->getAABB();
		const float radius = model->getCenterBoundingRadius();

		Matrix mtx;
		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(radius * 2);
		Vec3 dir = normalize(center - eye);
		mtx.lookAt(eye, center, normalize(Vec3(1, -1, 1)));
		mtx = mtx.inverted();

		Viewport viewport;
		if (in_viewport) {
			viewport = *in_viewport;
		}
		else {
			viewport.near = 0.01f;
			viewport.far = 8 * radius;
			viewport.is_ortho = true;
			viewport.ortho_size = radius * 1.1f;
			viewport.pos = DVec3(center - dir * 4 * radius);
			viewport.rot = mtx.getRotation();
		}
		viewport.h = AssetBrowser::TILE_SIZE * 4;
		viewport.w = AssetBrowser::TILE_SIZE * 4;
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);
		if (!m_tile.pipeline->getOutput()) {
			logError("Could not create ", model->getPath(), " thumbnail");
			model->decRefCount();
			m_tile.frame_countdown = -1;
			return;
		}

		DrawStream& stream = m_renderer->getDrawStream();
		m_tile.texture = gpu::allocTextureHandle();
		stream.createTexture(m_tile.texture, AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, "tile_final");
		gpu::TextureHandle tile_tmp = gpu::allocTextureHandle();
		stream.createTexture(tile_tmp, AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, "tile_tmp");
		stream.copy(tile_tmp, m_tile.pipeline->getOutput(), 0, 0);
		downscale(stream, tile_tmp, AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, m_tile.texture, AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);

		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		getTextureImage(stream
			, m_tile.texture 
			, AssetBrowser::TILE_SIZE
			, AssetBrowser::TILE_SIZE
			, gpu::TextureFormat::RGBA8
			, Span(m_tile.data.getMutableData(), (u32)m_tile.data.size()));
		
		stream.destroy(tile_tmp);
		m_tile.entity = mesh_entity;
		m_tile.frame_countdown = 2;
		m_tile.path_hash = model->getPath().getHash();
		model->decRefCount();
	}


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override {
		if (type != Model::TYPE && type != Material::TYPE && type != PrefabResource::TYPE) return false;

		Path path(in_path);
		if (!m_tile.queue.full()) {
			pushTileQueue(path);
			return true;
		}

		m_tile.paths.push(path);
		return true;
	}

	struct TileData {
		TileData(IAllocator& allocator)
			: data(allocator)
			, paths(allocator)
			, queue()
		{}

		World* world = nullptr;
		UniquePtr<Pipeline> pipeline;
		EntityPtr entity = INVALID_ENTITY;
		int frame_countdown = -1;
		FilePathHash path_hash;
		OutputMemoryStream data;
		gpu::TextureHandle texture = gpu::INVALID_TEXTURE;
		Queue<Resource*, 8> queue;
		Array<Path> paths;
		bool waiting = false;
	} m_tile;
	

	StudioApp& m_app;
	Renderer* m_renderer = nullptr;
	TexturePlugin* m_texture_plugin;
	FBXImporter m_fbx_importer;
	jobs::Signal m_subres_signal;
	gpu::ProgramHandle m_downscale_program = gpu::INVALID_PROGRAM;
};


struct ShaderPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin {
	struct EditorWindow : AssetEditorWindow {
		EditorWindow(const Path& path, StudioApp& app)
			: AssetEditorWindow(app)
			, m_buffer(app.getAllocator())
			, m_app(app)
			, m_path(path)
		{
			OutputMemoryStream blob(app.getAllocator());
			if (app.getEngine().getFileSystem().getContentSync(path, blob)) {
				m_buffer = StringView((const char*)blob.data(), (u32)blob.size());
			}
		}

		void save() {
			Span<const u8> data((const u8*)m_buffer.c_str(), m_buffer.length());
			m_app.getAssetBrowser().saveResource(m_path, data);
			m_dirty = false;
		}
	
		bool onAction(const Action& action) override { 
			if (&action == &m_app.getCommonActions().save) save();
			else return false;
			return true;
		}

		void windowGUI() override {
			if (ImGui::BeginMenuBar()) {
				if (ImGuiEx::IconButton(ICON_FA_SAVE, "Save")) save();
				if (ImGuiEx::IconButton(ICON_FA_EXTERNAL_LINK_ALT, "Open externally")) m_app.getAssetBrowser().openInExternalEditor(m_path);
				if (ImGuiEx::IconButton(ICON_FA_SEARCH, "View in browser")) m_app.getAssetBrowser().locate(m_path);
				ImGui::EndMenuBar();
			}

			ImGui::PushFont(m_app.getMonospaceFont());
			if (inputStringMultiline("##code", &m_buffer, ImGui::GetContentRegionAvail())) {
				m_dirty = true;
			}
			ImGui::PopFont();
		}
	
		const Path& getPath() override { return m_path; }
		const char* getName() const override { return "shader editor"; }

		StudioApp& m_app;
		String m_buffer;
		Path m_path;
	};

	explicit ShaderPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("shd", Shader::TYPE);
	}

	void findIncludes(const Path& path) {
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);

		os::InputFile file;
		if (!file.open(path.c_str()[0] == '/' ? path.c_str() + 1 : path.c_str())) return;
		
		IAllocator& allocator = m_app.getAllocator();
		OutputMemoryStream content(allocator);
		content.resize((int)file.size());
		if (!file.read(content.getMutableData(), content.size())) {
			logError("Could not read ", path);
			content.clear();
		}
		file.close();

		struct Context {
			const Path& path;
			ShaderPlugin* plugin;
			u8* content;
			u32 content_len;
			int idx;
		} ctx = { path, this, content.getMutableData(), (u32)content.size(), 0 };

		lua_pushlightuserdata(L, &ctx);
		lua_setfield(L, LUA_GLOBALSINDEX, "this");

		auto reg_dep = [](lua_State* L) -> int {
			lua_getfield(L, LUA_GLOBALSINDEX, "this");
			Context* that = LuaWrapper::toType<Context*>(L, -1);
			lua_pop(L, 1);
			const char* path = LuaWrapper::checkArg<const char*>(L, 1);
			that->plugin->m_app.getAssetCompiler().registerDependency(that->path, Path(path));
			return 0;
		};

		lua_pushcclosure(L, reg_dep, 0);
		lua_setfield(L, LUA_GLOBALSINDEX, "include");
		lua_pushcclosure(L, reg_dep, 0);
		lua_setfield(L, LUA_GLOBALSINDEX, "import");

		static const char* preface = 
			"local new_g = setmetatable({include = include, import = import}, {__index = function() return function() end end })\n"
			"setfenv(1, new_g)\n";

		auto reader = [](lua_State* L, void* data, size_t* size) -> const char* {
			Context* ctx = (Context*)data;
			++ctx->idx;
			switch(ctx->idx) {
				case 1: 
					*size = stringLength(preface);
					return preface;
				case 2: 
					*size = ctx->content_len;
					return (const char*)ctx->content;
				default:
					*size = 0;
					return nullptr;
			}
		};

		if (lua_load(L, reader, &ctx, path.c_str()) != 0) {
			logError(path, ": ", lua_tostring(L, -1));
			lua_pop(L, 2);
			lua_close(L);
			return;
		}

		if (lua_pcall(L, 0, 0, -2) != 0) {
			logError(lua_tostring(L, -1));
			lua_pop(L, 2);
			lua_close(L);
			return;
		}
		lua_pop(L, 1);
		lua_close(L);
	}

	void addSubresources(AssetCompiler& compiler, const Path& path) override {
		compiler.addResource(Shader::TYPE, path);
		findIncludes(path);
	}
	
	void openEditor(const Path& path) override {
		UniquePtr<EditorWindow> win = UniquePtr<EditorWindow>::create(m_app.getAllocator(), path, m_app);
		m_app.getAssetBrowser().addWindow(win.move());
	}

	bool compile(const Path& src) override { return m_app.getAssetCompiler().copyCompile(src); }
	const char* getLabel() const override { return "Shader"; }

	StudioApp& m_app;
};

template <typename F>
void captureCubemap(StudioApp& app
	, World& world
	, Pipeline& pipeline
	, const u32 texture_size
	, const DVec3& position
	, Array<Vec4>& data
	, F&& f) {
	memoryBarrier();

	Engine& engine = app.getEngine();
	SystemManager& system_manager = engine.getSystemManager();

	Viewport viewport;
	viewport.is_ortho = false;
	viewport.fov = degreesToRadians(90.f);
	viewport.near = 0.1f;
	viewport.far = 10'000;
	viewport.w = texture_size;
	viewport.h = texture_size;

	pipeline.setWorld(&world);
	pipeline.setViewport(viewport);

	Renderer* renderer = static_cast<Renderer*>(system_manager.getSystem("renderer"));
	Vec3 dirs[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
	Vec3 ups[] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
	Vec3 ups_opengl[] = { { 0, -1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 },{ 0, -1, 0 },{ 0, -1, 0 } };

	data.resize(6 * texture_size * texture_size);

	const bool ndc_bottom_left = gpu::isOriginBottomLeft();
	for (int i = 0; i < 6; ++i) {
		Vec3 side = cross(ndc_bottom_left ? ups_opengl[i] : ups[i], dirs[i]);
		Matrix mtx = Matrix::IDENTITY;
		mtx.setZVector(dirs[i]);
		mtx.setYVector(ndc_bottom_left ? ups_opengl[i] : ups[i]);
		mtx.setXVector(side);
		viewport.pos = position;
		viewport.rot = mtx.getRotation();
		pipeline.setViewport(viewport);
		pipeline.render(false);

		const gpu::TextureHandle res = pipeline.getOutput();
		ASSERT(res);
		DrawStream& stream = renderer->getDrawStream();
		getTextureImage(stream
			, res
			, texture_size
			, texture_size
			, gpu::TextureFormat::RGBA32F
			, Span((u8*)(data.begin() + (i * texture_size * texture_size)), u32(texture_size * texture_size * sizeof(*data.begin())))
		);
	}

	DrawStream& stream = renderer->getDrawStream();
	stream.pushLambda(f);
}

struct EnvironmentProbePlugin final : PropertyGrid::IPlugin
{
	explicit EnvironmentProbePlugin(StudioApp& app)
		: m_app(app)
		, m_probes(app.getAllocator())
	{
	}


	~EnvironmentProbePlugin()
	{
		m_ibl_filter_shader->decRefCount();
	}

	void init() {
		Engine& engine = m_app.getEngine();
		SystemManager& system_manager = engine.getSystemManager();
		Renderer* renderer = static_cast<Renderer*>(system_manager.getSystem("renderer"));
		ResourceManagerHub& rm = engine.getResourceManager();
		PipelineResource* pres = rm.load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PROBE");
		m_ibl_filter_shader = rm.load<Shader>(Path("pipelines/ibl_filter.shd"));
	}

	bool saveCubemap(u64 probe_guid, const Vec4* data, u32 texture_size, u32 mips_count) {
		ASSERT(data);
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		Path path(base_path, "universes");
		if (!os::makePath(path.c_str()) && !os::dirExists(path)) {
			logError("Failed to create ", path);
		}
		path.append("/probes_tmp/");
		if (!os::makePath(path.c_str()) && !os::dirExists(path)) {
			logError("Failed to create ", path);
		}
		path.append(probe_guid, ".lbc");

		OutputMemoryStream blob(m_app.getAllocator());

		const Vec4* mip_pixels = data;
		TextureCompressor::Input input(texture_size, texture_size, 1, mips_count, m_app.getAllocator());
		for (u32 mip = 0; mip < mips_count; ++mip) {
			const u32 mip_size = texture_size >> mip;
			for (int face = 0; face < 6; ++face) {
				TextureCompressor::Input::Image& img = input.add(face, 0, mip);
				Color* rgbm = (Color*)img.pixels.getMutableData();
				for (u32 j = 0, c = mip_size * mip_size; j < c; ++j) {
					const float m = clamp(maximum(mip_pixels[j].x, mip_pixels[j].y, mip_pixels[j].z), 1 / 64.f, 4.f);
					rgbm[j].r = u8(clamp(mip_pixels[j].x / m * 255 + 0.5f, 0.f, 255.f));
					rgbm[j].g = u8(clamp(mip_pixels[j].y / m * 255 + 0.5f, 0.f, 255.f));
					rgbm[j].b = u8(clamp(mip_pixels[j].z / m * 255 + 0.5f, 0.f, 255.f));
					rgbm[j].a = u8(clamp(255.f * m / 4 + 0.5f, 1.f, 255.f));
				}
				mip_pixels += mip_size * mip_size;
			}
		}
		input.has_alpha = true;
		input.is_cubemap = true;
		if (!TextureCompressor::compress(input, TextureCompressor::Options(), blob, m_app.getAllocator())) return false;

		os::OutputFile file;
		if (!file.open(path.c_str())) {
			logError("Failed to create ", path);
			return false;
		}
		bool res = file.write(blob.data(), blob.size());
		file.close();
		return res;
	}


	void generateCubemaps(bool bounce, World& world) {
		ASSERT(m_probes.empty());

		m_pipeline->setIndirectLightMultiplier(bounce ? 1.f : 0.f);

		RenderModule* module = (RenderModule*)world.getModule(ENVIRONMENT_PROBE_TYPE);
		const Span<EntityRef> env_probes = module->getEnvironmentProbesEntities();
		const Span<EntityRef> reflection_probes = module->getReflectionProbesEntities();
		m_probes.reserve(env_probes.length() + reflection_probes.length());
		IAllocator& allocator = m_app.getAllocator();
		for (EntityRef p : env_probes) {
			ProbeJob* job = LUMIX_NEW(m_app.getAllocator(), ProbeJob)(*this, world, p, allocator);
			
			job->env_probe = module->getEnvironmentProbe(p);
			job->is_reflection = false;
			job->position = world.getPosition(p);

			m_probes.push(job);
		}

		for (EntityRef p : reflection_probes) {
			ProbeJob* job = LUMIX_NEW(m_app.getAllocator(), ProbeJob)(*this, world, p, allocator);
			
			job->reflection_probe = module->getReflectionProbe(p);
			job->is_reflection = true;
			job->position = world.getPosition(p);

			m_probes.push(job);
		}

		m_probe_counter += m_probes.size();
	}

	struct ProbeJob {
		ProbeJob(EnvironmentProbePlugin& plugin, World& world, EntityRef& entity, IAllocator& allocator) 
			: entity(entity)
			, data(allocator)
			, plugin(plugin)
			, world(world)
		{}
		
		EntityRef entity;
		union {
			EnvironmentProbe env_probe;
			ReflectionProbe reflection_probe;
		};
		bool is_reflection = false;
		EnvironmentProbePlugin& plugin;
		DVec3 position;

		World& world;
		Array<Vec4> data;
		SphericalHarmonics sh;
		bool render_dispatched = false;
		bool done = false;
		bool done_counted = false;
	};

	void render(ProbeJob& job) {
		const u32 texture_size = job.is_reflection ? job.reflection_probe.size : 128;

		captureCubemap(m_app, job.world, *m_pipeline, texture_size, job.position, job.data, [&job](){
			jobs::runLambda([&job]() {
				job.plugin.processData(job);
			}, nullptr);

		});
	}

	void update() override
	{
		if (m_ibl_filter_shader->isReady() && !m_ibl_filter_program) {
			m_ibl_filter_program = m_ibl_filter_shader->getProgram(gpu::StateFlags::NONE, gpu::VertexDecl(gpu::PrimitiveType::TRIANGLE_STRIP), 0);
		}

		if (m_done_counter != m_probe_counter) {
			const float ui_width = maximum(300.f, ImGui::GetIO().DisplaySize.x * 0.33f);

			const ImVec2 pos = ImGui::GetMainViewport()->Pos;
			ImGui::SetNextWindowPos(ImVec2((ImGui::GetIO().DisplaySize.x - ui_width) * 0.5f + pos.x, 30 + pos.y));
			ImGui::SetNextWindowSize(ImVec2(ui_width, -1));
			ImGui::SetNextWindowSizeConstraints(ImVec2(-FLT_MAX, 0), ImVec2(FLT_MAX, 200));
			ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar 
				| ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoMove
				| ImGuiWindowFlags_NoSavedSettings;
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1);
			if (ImGui::Begin("Env probe generation", nullptr, flags)) {
				ImGui::TextUnformatted("Generating probes...");
				ImGui::TextUnformatted("Manipulating with entities at this time can produce incorrect probes.");
				ImGui::ProgressBar(((float)m_done_counter) / m_probe_counter, ImVec2(-1, 0), StaticString<64>(m_done_counter, " / ", m_probe_counter));
			}
			ImGui::End();
			ImGui::PopStyleVar();
		}
		else {
			m_probe_counter = 0;
			m_done_counter = 0;
		}

		for (ProbeJob* j : m_probes) {
			if (!j->render_dispatched) {
				j->render_dispatched = true;
				render(*j);
				break;
			}
		}

		memoryBarrier();
		for (ProbeJob* j : m_probes) {
			if (j->done && !j->done_counted) {
				j->done_counted = true;
				++m_done_counter;
			}
		}

		if (m_done_counter == m_probe_counter && !m_probes.empty()) {
			const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
			Path dir_path(base_path, "universes/");
			if (!os::dirExists(dir_path) && !os::makePath(dir_path.c_str())) {
				logError("Failed to create ", dir_path);
			}
			dir_path.append("/probes/");
			if (!os::dirExists(dir_path) && !os::makePath(dir_path.c_str())) {
				logError("Failed to create ", dir_path);
			}
			RenderModule* module = nullptr;
			while (!m_probes.empty()) {
				ProbeJob& job = *m_probes.back();
				m_probes.pop();
				ASSERT(job.done);
				ASSERT(job.done_counted);

				if (job.is_reflection) {

					const u64 guid = job.reflection_probe.guid;

					const Path tmp_path(base_path, "/universes/probes_tmp/", guid, ".lbc");
					const Path path(base_path, "/universes/probes/", guid, ".lbc");
					if (!os::fileExists(tmp_path)) {
						if (module) module->reloadReflectionProbes();
						return;
					}
					if (!os::moveFile(tmp_path, path)) {
						logError("Failed to move file ", tmp_path, " to ", path);
					}
				}

				if (job.world.hasComponent(job.entity, ENVIRONMENT_PROBE_TYPE)) {
					module = (RenderModule*)job.world.getModule(ENVIRONMENT_PROBE_TYPE);
					EnvironmentProbe& p = module->getEnvironmentProbe(job.entity);
					static_assert(sizeof(p.sh_coefs) == sizeof(job.sh.coefs));
					memcpy(p.sh_coefs, job.sh.coefs, sizeof(p.sh_coefs));
				}

				IAllocator& allocator = m_app.getAllocator();
				LUMIX_DELETE(allocator, &job);
			}
			if (module) module->reloadReflectionProbes();
		}
	}

	void radianceFilter(const Vec4* data, u32 size, u64 guid) {
		PROFILE_FUNCTION();
		if (!m_ibl_filter_shader->isReady()) {
			logError(m_ibl_filter_shader->getPath(), "is not ready");
			return;
		}
		SystemManager& system_manager = m_app.getEngine().getSystemManager();
		Renderer* renderer = (Renderer*)system_manager.getSystem("renderer");
		enum { roughness_levels = 5 };
		
		jobs::Signal signal;
		jobs::setRed(&signal);
		Array<u8> tmp(m_app.getAllocator());
		renderer->pushJob([&](DrawStream& stream){
			gpu::TextureHandle src = gpu::allocTextureHandle();
			gpu::TextureHandle dst = gpu::allocTextureHandle();
			stream.createTexture(src, size, size, 1, gpu::TextureFormat::RGBA32F, gpu::TextureFlags::IS_CUBE, "env");
			for (u32 face = 0; face < 6; ++face) {
				stream.update(src, 0, 0, 0, face, size, size, gpu::TextureFormat::RGBA32F, (void*)(data + size * size * face), size * size * sizeof(*data));
			}
			stream.generateMipmaps(src);
			stream.createTexture(dst, size, size, 1, gpu::TextureFormat::RGBA32F, gpu::TextureFlags::IS_CUBE, "env_filtered");

			stream.useProgram(m_ibl_filter_program);
			stream.bindTextures(&src, 0, 1);
			for (u32 mip = 0; mip < roughness_levels; ++mip) {
				const float roughness = float(mip) / (roughness_levels - 1);
				for (u32 face = 0; face < 6; ++face) {
					stream.setFramebufferCube(dst, face, mip);
					struct {
						float roughness;
						u32 face;
						u32 mip;
					} drawcall = {roughness, face, mip};
					const Renderer::TransientSlice ub = renderer->allocUniform(&drawcall, sizeof(drawcall));
					stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
					stream.viewport(0, 0, size >> mip, size >> mip);
					stream.drawArrays(0, 4);
				}
			}

			stream.setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);

			gpu::TextureHandle staging = gpu::allocTextureHandle();
			const gpu::TextureFlags flags = gpu::TextureFlags::IS_CUBE | gpu::TextureFlags::READBACK;
			stream.createTexture(staging, size, size, 1, gpu::TextureFormat::RGBA32F, flags, "staging_buffer");
			
			u32 data_size = 0;
			{
				u32 mip_size = size;
				for (u32 mip = 0; mip < roughness_levels; ++mip) {
					data_size += mip_size * mip_size * sizeof(Vec4) * 6;
					mip_size >>= 1;
				}
			}

			tmp.resize(data_size);

			stream.copy(staging, dst, 0, 0);
			u8* tmp_ptr = tmp.begin();
			for (u32 mip = 0; mip < roughness_levels; ++mip) {
				const u32 mip_size = size >> mip;
				stream.readTexture(staging, mip, Span(tmp_ptr, mip_size * mip_size * sizeof(Vec4) * 6));
				tmp_ptr += mip_size * mip_size * sizeof(Vec4) * 6;
			}

			stream.destroy(staging);
			stream.destroy(src);
			stream.destroy(dst);	

			struct Payload {
				EnvironmentProbePlugin* plugin;
				u64 guid;
				jobs::Signal* signal;
				Array<u8>* data;
				u32 texture_size;
			};

			stream.pushLambda([&](){
				saveCubemap(guid, (Vec4*)tmp.begin(), size, roughness_levels);
				jobs::setGreen(&signal);		
			});
		});
		jobs::wait(&signal); // wait to keep `data` alive until renderer is done with it
	}

	void processData(ProbeJob& job) {
		Array<Vec4>& data = job.data;
		const u32 texture_size = (u32)sqrtf(data.size() / 6.f);
				
		const bool ndc_bottom_left = gpu::isOriginBottomLeft();
		if (!ndc_bottom_left) {
			for (int i = 0; i < 6; ++i) {
				Vec4* tmp = &data[i * texture_size * texture_size];
				if (i == 2 || i == 3) {
					flipY(tmp, texture_size);
				}
				else {
					flipX(tmp, texture_size);
				}
			}
		}

		if (job.is_reflection) {
			radianceFilter(data.begin(), texture_size, job.reflection_probe.guid);
		}
		else {
			job.sh.compute(data);
		}

		memoryBarrier();
		job.done = true;
	}


	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (entities.length() != 1) return;

		World& world = *editor.getWorld();
		const EntityRef e = entities[0];
		auto* module = static_cast<RenderModule*>(world.getModule(cmp_type));
		if (cmp_type == ENVIRONMENT_PROBE_TYPE) {
			if (m_probe_counter) ImGui::Text("Generating...");
			else {
				if (ImGui::CollapsingHeader("Generator")) {
					if (ImGui::Button("Generate")) generateCubemaps(false, world);
					ImGui::SameLine();
					if (ImGui::Button("Add bounce")) generateCubemaps(true, world);
				}
			}
		}

		if (cmp_type == REFLECTION_PROBE_TYPE) {
			if (m_probe_counter) ImGui::Text("Generating...");
			else {
				const ReflectionProbe& probe = module->getReflectionProbe(e);
				if (probe.flags.isSet(ReflectionProbe::ENABLED)) {
					const Path path("universes/probes/", probe.guid, ".lbc");
					ImGuiEx::Label("Path");
					ImGuiEx::TextUnformatted(path);
					if (ImGui::Button("View radiance")) m_app.getAssetBrowser().openEditor(path);
				}
				if (ImGui::CollapsingHeader("Generator")) {
					if (ImGui::Button("Generate")) generateCubemaps(false, world);
					ImGui::SameLine();
					if (ImGui::Button("Add bounce")) generateCubemaps(true, world);
				}
			}
		}
	}


	StudioApp& m_app;
	UniquePtr<Pipeline> m_pipeline;
	Shader* m_ibl_filter_shader = nullptr;
	gpu::ProgramHandle m_ibl_filter_program = gpu::INVALID_PROGRAM;
	
	// TODO to be used with http://casual-effects.blogspot.com/2011/08/plausible-environment-lighting-in-two.html
	Array<ProbeJob*> m_probes;
	u32 m_done_counter = 0;
	u32 m_probe_counter = 0;
};

struct InstancedModelPlugin final : PropertyGrid::IPlugin, StudioApp::MousePlugin {
	struct SetTransformCommand : IEditorCommand {
		SetTransformCommand(EntityRef entity, const InstancedModel::InstanceData& old_value, const InstancedModel::InstanceData& new_value, WorldEditor& editor)
			: editor(editor)
			, entity(entity)
			, new_value(new_value)
			, old_value(old_value)
		{
			merge_value = old_value;
		}

		bool execute() override {
			RenderModule* module = (RenderModule*)editor.getWorld()->getModule(INSTANCED_MODEL_TYPE);
			InstancedModel& im = module->beginInstancedModelEditing(entity);

			for (auto& i : im.instances) {
				if (memcmp(&i, &old_value, sizeof(old_value)) == 0) {
					i = new_value;
					break;
				}
			}

			module->endInstancedModelEditing(entity);
			old_value = merge_value;
			return true;
		}

		void undo() override {
			RenderModule* module = (RenderModule*)editor.getWorld()->getModule(INSTANCED_MODEL_TYPE);
			InstancedModel& im = module->beginInstancedModelEditing(entity);

			for (auto& i : im.instances) {
				if (memcmp(&i, &new_value, sizeof(new_value)) == 0) {
					i = old_value;
					break;
				}
			}

			module->endInstancedModelEditing(entity);
		}

		const char* getType() override { return "set_intanced_model_transform"; }
		bool merge(IEditorCommand& command) override {
			SetTransformCommand& rhs = ((SetTransformCommand&)command);
			if (memcmp(&rhs.new_value, &old_value, sizeof(old_value)) != 0) return false;
			rhs.new_value = new_value;
			rhs.merge_value = rhs.old_value;
			rhs.old_value = old_value;
			return true;
		}

		EntityRef entity;
		WorldEditor& editor;
		InstancedModel::InstanceData new_value;
		InstancedModel::InstanceData old_value;
		InstancedModel::InstanceData merge_value;
	};

	struct RemoveCommand : IEditorCommand {
		RemoveCommand(EntityRef entity, Vec2 center_xz, float radius_squared, WorldEditor& editor)
			: editor(editor)
			, entity(entity)
			, instances(editor.getAllocator())
			, center_xz(center_xz)
			, radius_squared(radius_squared)
		{}
		
		bool execute() override {
			instances.clear();
			
			RenderModule* module = (RenderModule*)editor.getWorld()->getModule(INSTANCED_MODEL_TYPE);
			InstancedModel& im = module->beginInstancedModelEditing(entity);
			for (i32 i = im.instances.size() - 1; i >= 0; --i) {
				const InstancedModel::InstanceData& id = im.instances[i];
				if (squaredLength(id.pos.xz() - center_xz) < radius_squared) {
					instances.push(im.instances[i]);
					im.instances.swapAndPop(i);
				}
			}
			
			module->endInstancedModelEditing(entity);
			return true;
		}
		
		void undo() override {
			RenderModule* module = (RenderModule*)editor.getWorld()->getModule(INSTANCED_MODEL_TYPE);
			InstancedModel& im = module->beginInstancedModelEditing(entity);
			
			for (const InstancedModel::InstanceData& id : instances) {
				im.instances.push(id);
			}
			module->endInstancedModelEditing(entity);
		}
		
		bool merge(IEditorCommand& command) override { return false; }

		const char* getType() override { return "remove_instanced_model_instances"; }

		WorldEditor& editor;
		EntityRef entity;
		Vec2 center_xz;
		float radius_squared;
		Array<InstancedModel::InstanceData> instances;
	};

	struct AddCommand : IEditorCommand {
		AddCommand(EntityRef entity, WorldEditor& editor)
			: editor(editor)
			, instances(editor.getAllocator())
			, entity(entity)
		{}

		bool execute() override {
			RenderModule* module = (RenderModule*)editor.getWorld()->getModule(INSTANCED_MODEL_TYPE);
			InstancedModel& im = module->beginInstancedModelEditing(entity);
			for (const InstancedModel::InstanceData& i : instances) {
				im.instances.push(i);
			}
			module->endInstancedModelEditing(entity);
			return true;
		}
		
		void undo() override {
			RenderModule* module = (RenderModule*)editor.getWorld()->getModule(INSTANCED_MODEL_TYPE);
			InstancedModel& im = module->beginInstancedModelEditing(entity);
			for (u32 j = 0, cj = (u32)instances.size(); j < cj; ++j) {
				for (u32 i = 0, ci = (u32)im.instances.size(); i < ci; ++i) {
					if (memcmp(&instances[j], &im.instances[i], sizeof(im.instances[i])) == 0) {
						im.instances.swapAndPop(i);
						break;
					}
				}
			}
			module->endInstancedModelEditing(entity);
		}
		
		bool merge(IEditorCommand& command) override { return false; }

		const char* getType() override { return "add_instanced_model_instances"; }
		Array<InstancedModel::InstanceData> instances;
		EntityRef entity;
		WorldEditor& editor;
	};

	explicit InstancedModelPlugin(StudioApp& app)
		: m_app(app)
	{
		m_app.addPlugin(*this);
		m_rotate_x_spread = m_rotate_y_spread = m_rotate_z_spread = Vec2(0, PI * 2);
		m_selected.pos = Vec3(FLT_MAX);
	}

	~InstancedModelPlugin() {
		m_app.removePlugin(*this);
	}

	struct Component {
		const InstancedModel* im;
		EntityRef entity;
		RenderModule* module;
	};

	Component getComponent() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected_entities = editor.getSelectedEntities();
		if (selected_entities.size() != 1) return { nullptr };

		World& world = *editor.getWorld();
		RenderModule* module = (RenderModule*)world.getModule(INSTANCED_MODEL_TYPE);
		auto iter = module->getInstancedModels().find(selected_entities[0]);
		if (!iter.isValid()) return { nullptr };
		return { &iter.value(), selected_entities[0], module };
	}

	static Quat getInstanceQuat(Vec3 q) {
		Quat res;
		res.x = q.x;
		res.y = q.y;
		res.z = q.z;
		res.w = sqrtf(1 - (q.x * q.x + q.y * q.y + q.z * q.z));
		return res;
	}

	static bool isOBBCollision(Span<const InstancedModel::InstanceData> meshes, const InstancedModel::InstanceData& obj, Model* model, float bounding_offset)
	{
		ASSERT(bounding_offset <= 0);
		AABB aabb = model->getAABB();
		aabb.shrink(-bounding_offset);
		float radius_a_squared = model->getOriginBoundingRadius() * obj.scale;
		radius_a_squared = radius_a_squared * radius_a_squared;
		const LocalTransform tr_a(obj.pos, getInstanceQuat(obj.rot_quat), obj.scale);
		for (const InstancedModel::InstanceData& inst : meshes) {
			const float radius_b = model->getOriginBoundingRadius() * inst.scale + bounding_offset;
			const float radius_squared = radius_a_squared + radius_b * radius_b;
			if (squaredLength(inst.pos - obj.pos) < radius_squared) {
				const LocalTransform tr_b(inst.pos, getInstanceQuat(inst.rot_quat), inst.scale);
				const LocalTransform rel_tr = tr_a.inverted() * tr_b;
				Matrix mtx = rel_tr.rot.toMatrix();
				mtx.multiply3x3(rel_tr.scale);
				mtx.setTranslation(Vec3(rel_tr.pos));

				if (testOBBCollision(aabb, mtx, aabb)) {
					return true;
				}
			}
		}
		return false;
	}

	bool paint(i32 x, i32 y) {
		PROFILE_FUNCTION();
		auto cmp = getComponent();
		if (!cmp.im) return false;
		if (!cmp.im->model || !cmp.im->model->isReady()) return false;

		WorldEditor& editor = m_app.getWorldEditor();
		DVec3 ray_origin;
		Vec3 ray_dir;
		editor.getView().getViewport().getRay(Vec2((float)x, (float)y), ray_origin, ray_dir);
		const RayCastModelHit hit = m_brush != Brush::TERRAIN ? cmp.module->castRay(ray_origin, ray_dir, INVALID_ENTITY) : cmp.module->castRayTerrain(ray_origin, ray_dir);
		if (!hit.is_hit) return false;

		const DVec3 hit_pos = hit.origin + hit.t * hit.dir;
		const DVec3 origin = editor.getWorld()->getPosition(cmp.entity);
		switch (m_brush) {
			case Brush::SINGLE: {
				UniquePtr<AddCommand> add_cmd = UniquePtr<AddCommand>::create(editor.getAllocator(), cmp.entity, editor);
				InstancedModel::InstanceData& id = add_cmd->instances.emplace();
				id.scale = 1;
				id.rot_quat = Vec3::ZERO;
				id.lod = 3;
				id.pos = Vec3(hit_pos - origin);
				m_selected = id;
				editor.executeCommand(add_cmd.move());
				break;
			}
			case Brush::TERRAIN: {
				const EntityRef terrain = *hit.entity;
				const Transform terrain_tr = editor.getWorld()->getTransform(terrain);
				const Transform inv_terrain_tr = terrain_tr.inverted();

				const bool remove = ImGui::GetIO().KeyCtrl; // TODO

				Array<InstancedModel::InstanceData> existing(m_app.getAllocator());
				Vec2 center_xz = Vec3(hit_pos - origin).xz();
				const float model_radius = cmp.im->model->getOriginBoundingRadius();
				const float radius_squared = (m_brush_radius + 2 * model_radius) * (m_brush_radius + 2 * model_radius);
				
				if (!remove) {
					for (u32 i = 0; i < (u32)cmp.im->instances.size(); ++i) {
						const InstancedModel::InstanceData& id = cmp.im->instances[i];
						if (squaredLength(id.pos.xz() - center_xz) < radius_squared) {
							existing.push(id);
						}
					}
					UniquePtr<AddCommand> add_cmd = UniquePtr<AddCommand>::create(editor.getAllocator(), cmp.entity, editor);
					for (int i = 0; i <= m_brush_radius * m_brush_radius / 100.0f * m_brush_strength; ++i) {
						const float angle = randFloat(0, PI * 2);
						const float dist = randFloat(0, 1.0f) * m_brush_radius;
						DVec3 pos(hit_pos.x + cosf(angle) * dist, 0, hit_pos.z + sinf(angle) * dist);
						const Vec3 terrain_pos = Vec3(inv_terrain_tr.transform(pos));
						pos.y = cmp.module->getTerrainHeightAt(terrain, terrain_pos.x, terrain_pos.z) + terrain_tr.pos.y;
						pos.y += randFloat(m_y_spread.x, m_y_spread.y);
						
						InstancedModel::InstanceData id;
						id.scale = randFloat(m_size_spread.x, m_size_spread.y);
						id.rot_quat = Vec3::ZERO;
						id.lod = 3;

						Quat rot = Quat::IDENTITY;
						if (m_is_rotate_x) {
							float xangle = randFloat(m_rotate_x_spread.x, m_rotate_x_spread.y);
							Quat q(Vec3(1, 0, 0), xangle);
							rot = q * rot;
						}

						if (m_is_rotate_y) {
							float yangle = randFloat(m_rotate_y_spread.x, m_rotate_y_spread.y);
							Quat q(Vec3(0, 1, 0), yangle);
							rot = q * rot;
						}

						if (m_is_rotate_z) {
							float zangle = randFloat(m_rotate_z_spread.x, m_rotate_z_spread.y);
							Quat q(rot.rotate(Vec3(0, 0, 1)), zangle);
							rot = q * rot;
						}

						id.rot_quat = Vec3(rot.x, rot.y, rot.z);
						if (rot.w < 0) id.rot_quat = -id.rot_quat;

						id.pos = Vec3(pos - origin);
						if (!isOBBCollision(existing, id, cmp.im->model, m_bounding_offset)) {
							 add_cmd->instances.push(id);
							 existing.push(id);
						}
					}
					if (!add_cmd->instances.empty()) {
						editor.beginCommandGroup("add_instanced_model_instances_group");
						editor.executeCommand(add_cmd.move());
						editor.endCommandGroup();
						m_can_lock_group = true;
					}
				}
				else {
					UniquePtr<RemoveCommand> remove_cmd = UniquePtr<RemoveCommand>::create(editor.getAllocator(), cmp.entity, center_xz, radius_squared, editor);
					editor.beginCommandGroup("remove_instanced_model_instances_group");
					editor.executeCommand(remove_cmd.move());
					editor.endCommandGroup();
					m_can_lock_group = true;
				}
				break;
			}
		}
		return true;
	}

	void onMouseMove(WorldView& view, int x, int y, int, int) override {
		if (ImGui::GetIO().KeyShift && m_brush == Brush::TERRAIN) {
			paint(x, y);
		}
	}

	void onMouseUp(WorldView& view, int x, int y, os::MouseButton button) override {
		if (m_can_lock_group) {
			m_app.getWorldEditor().lockGroupCommand();
			m_can_lock_group = false;
		}
	}

	bool onMouseDown(WorldView& view, int x, int y) override {
		if (ImGui::GetIO().KeyShift) return paint(x, y);

		auto cmp = getComponent();
		if (!cmp.im) return false;
		if (!cmp.im->model || !cmp.im->model->isReady()) return false;

		DVec3 ray_origin;
		Vec3 ray_dir;
		view.getViewport().getRay(Vec2((float)x, (float)y), ray_origin, ray_dir);
		RayCastModelHit hit = cmp.module->castRayInstancedModels(ray_origin, ray_dir, [](const RayCastModelHit&){ return true; });
		if (hit.is_hit && hit.entity == cmp.entity) {
			m_selected = cmp.module->getInstancedModels()[cmp.entity].instances[hit.subindex];
			return true;
		}
		return false;
	}

	static void drawCircle(RenderModule& module, const DVec3& center, float radius, u32 color) {
		constexpr i32 SLICE_COUNT = 30;
		constexpr float angle_step = PI * 2 / SLICE_COUNT;
		for (i32 i = 0; i < SLICE_COUNT + 1; ++i) {
			const float angle = i * angle_step;
			const float next_angle = i * angle_step + angle_step;
			const DVec3 from = center + DVec3(cosf(angle), 0, sinf(angle)) * radius;
			const DVec3 to = center + DVec3(cosf(next_angle), 0, sinf(next_angle)) * radius;
			module.addDebugLine(from, to, color);
		}		
	}

	i32 getInstanceIndex(const InstancedModel& im, const InstancedModel::InstanceData& inst) {
		for (i32 i = 0, c = im.instances.size(); i < c; ++i) {
			if (memcmp(&im.instances[i], &inst, sizeof(inst)) == 0) {
				return i;
			}
		}
		return -1;
	}

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != INSTANCED_MODEL_TYPE) return;
		if (entities.length() != 1) return;

		RenderModule* render_module = (RenderModule*)editor.getWorld()->getModule(cmp_type);
		const InstancedModel& im = render_module->getInstancedModels()[entities[0]];
		
		ImGuiEx::Label("Instances");
		ImGui::Text("%d", im.instances.size());

		ImGuiEx::Label("Selected instance");
		i32 selected = getInstanceIndex(im, m_selected);
		if (ImGui::InputInt("##sel", &selected)) {
			selected = clamp(selected, -1, im.instances.size() - 1);
			if (selected < 0) m_selected.pos = Vec3(FLT_MAX);
			else m_selected = im.instances[selected];
		}

		if (selected >= 0 && selected < im.instances.size()) {
			DVec3 origin = editor.getWorld()->getPosition(entities[0]);
			Transform tr;
			tr.rot = getInstanceQuat(m_selected.rot_quat);
			tr.scale = Vec3(m_selected.scale);
			tr.pos = origin + DVec3(m_selected.pos);
			const Gizmo::Config& cfg = m_app.getGizmoConfig();
			bool changed = Gizmo::manipulate(u64(4) << 32 | entities[0].index, editor.getView(), tr, cfg);

			Vec3 p = m_selected.pos;
			ImGuiEx::Label("Position");
			if (ImGui::DragFloat3("##pos", &p.x, 0.01f)) {
				changed = true;
				tr.pos = origin + DVec3(p);
			}

			ImGuiEx::Label("Rotation");
			Vec3 euler = tr.rot.toEuler();
			if (ImGuiEx::InputRotation("##rot", &euler.x)) {
				tr.rot.fromEuler(euler);
				changed = true;
			}

			ImGuiEx::Label("Scale");
			if (ImGui::DragFloat("##scale", &tr.scale.x, 0.01f)) {
				tr.scale.y = tr.scale.z = tr.scale.x;
				changed = true;
			}

			if (changed) {
				tr.pos = tr.pos - origin;

				InstancedModel::InstanceData new_value;
				new_value.pos = Vec3(tr.pos);
				new_value.rot_quat = Vec3(tr.rot.x, tr.rot.y, tr.rot.z);
				if (tr.rot.w < 0) new_value.rot_quat *= -1;
				new_value.scale = tr.scale.x;
				new_value.lod = 3;
				
				UniquePtr<SetTransformCommand> cmd = UniquePtr<SetTransformCommand>::create(editor.getAllocator(), entities[0], m_selected, new_value, editor);
				editor.executeCommand(cmd.move());

				m_selected = new_value;
			}
		}

		ImGui::Separator();
		ImGuiEx::Label("Brush");
		ImGui::Combo("##brush", (i32*)&m_brush, "Single\0Terrain\0");

		switch (m_brush) {
			case Brush::SINGLE:
				break;
			case Brush::TERRAIN: {
				ImGuiEx::Label("Brush radius");
				ImGui::DragFloat("##brush_radius", &m_brush_radius, 0.1f, 0.f, FLT_MAX);
				ImGuiEx::Label("Brush strength");
				ImGui::SliderFloat("##brush_str", &m_brush_strength, 0.f, 1.f, "%.2f");
				ImGuiEx::Label("Bounding offset");
				ImGui::DragFloat("##bounding_offset", &m_bounding_offset, 0.1f, -FLT_MAX, 0);
				ImGuiEx::Label("Size spread");
				ImGui::DragFloatRange2("##size_spread", &m_size_spread.x, &m_size_spread.y, 0.01f);
				m_size_spread.x = minimum(m_size_spread.x, m_size_spread.y);
				ImGuiEx::Label("Y spread");
				ImGui::DragFloatRange2("##y_spread", &m_y_spread.x, &m_y_spread.y, 0.01f);
				m_y_spread.x = minimum(m_y_spread.x, m_y_spread.y);
				
				if (ImGui::Checkbox("Rotate around X", &m_is_rotate_x)) {
					//if (m_is_rotate_x) m_is_align_with_normal = false;
				}
				if (m_is_rotate_x) {
					Vec2 tmp = m_rotate_x_spread;
					tmp.x = radiansToDegrees(tmp.x);
					tmp.y = radiansToDegrees(tmp.y);
					if (ImGui::DragFloatRange2("Rotate X spread", &tmp.x, &tmp.y)) {
						m_rotate_x_spread.x = degreesToRadians(tmp.x);
						m_rotate_x_spread.y = degreesToRadians(tmp.y);
					}
				}

				if (ImGui::Checkbox("Rotate around Y", &m_is_rotate_y)) {
					//if (m_is_rotate_y) m_is_align_with_normal = false;
				}
				if (m_is_rotate_y) {
					Vec2 tmp = m_rotate_y_spread;
					tmp.x = radiansToDegrees(tmp.x);
					tmp.y = radiansToDegrees(tmp.y);
					if (ImGui::DragFloatRange2("Rotate Y spread", &tmp.x, &tmp.y)) {
						m_rotate_y_spread.x = degreesToRadians(tmp.x);
						m_rotate_y_spread.y = degreesToRadians(tmp.y);
					}
				}

				if (ImGui::Checkbox("Rotate around Z", &m_is_rotate_z)) {
					//if (m_is_rotate_z) m_is_align_with_normal = false;
				}
				if (m_is_rotate_z) {
					Vec2 tmp = m_rotate_z_spread;
					tmp.x = radiansToDegrees(tmp.x);
					tmp.y = radiansToDegrees(tmp.y);
					if (ImGui::DragFloatRange2("Rotate Z spread", &tmp.x, &tmp.y)) {
						m_rotate_z_spread.x = degreesToRadians(tmp.x);
						m_rotate_z_spread.y = degreesToRadians(tmp.y);
					}
				}

				if (ImGui::GetIO().KeyShift) {
					const Vec2 mp = editor.getView().getMousePos();
					DVec3 ray_origin;
					Vec3 ray_dir;
					editor.getView().getViewport().getRay(mp, ray_origin, ray_dir);
					const RayCastModelHit hit = render_module->castRayTerrain(ray_origin, ray_dir);
					if (hit.is_hit) {
						drawCircle(*render_module, hit.origin + hit.t * hit.dir, m_brush_radius, 0xff880000);
					}
				}
				break;
			}
		}
	}

	const char* getName() const override { return "instanced_model"; }

	enum class Brush : i32 {
		SINGLE,
		TERRAIN
	};

	StudioApp& m_app;
	Brush m_brush = Brush::SINGLE;
	float m_brush_radius = 10.f;
	float m_brush_strength = 1.f;
	float m_bounding_offset = 0;
	InstancedModel::InstanceData m_selected;
	Vec2 m_size_spread = Vec2(1);
	Vec2 m_y_spread = Vec2(0);

	bool m_is_rotate_x = false;
	bool m_is_rotate_y = false;
	bool m_is_rotate_z = false;
	Vec2 m_rotate_x_spread;
	Vec2 m_rotate_y_spread;
	Vec2 m_rotate_z_spread;
	bool m_can_lock_group = false;
};

struct ProceduralGeomPlugin final : PropertyGrid::IPlugin, StudioApp::MousePlugin {
	ProceduralGeomPlugin(StudioApp& app) : m_app(app) {}

	const char* getName() const override { return "procedural_geom"; }

	void paint(const DVec3& pos
		, const World& world
		, EntityRef entity
		, ProceduralGeometry& pg
		, Renderer& renderer) const
	{
		if (!m_is_open) return;
		if (pg.vertex_data.size() == 0) return;
	
		// TODO undo/redo

		const Transform tr = world.getTransform(entity);
		const Vec3 center(tr.inverted().transform(pos));

		const float R2 = m_brush_size * m_brush_size;

		const u8* end = pg.vertex_data.data() + pg.vertex_data.size();
		const u32 stride = pg.vertex_decl.getStride();
		ASSERT(stride != 0);
		const u32 offset = pg.vertex_decl.attributes[4].byte_offset + (m_paint_as_color ? 0 : m_brush_channel);
		ImGuiIO& io = ImGui::GetIO();
		const u8 color[4] = { u8(m_brush_color.x * 0xff), u8(m_brush_color.y * 0xff), u8(m_brush_color.z * 0xff), u8(m_brush_color.w * 0xff) };
		for (u8* iter = pg.vertex_data.getMutableData(); iter < end; iter += stride) {
			Vec3 p;
			memcpy(&p, iter, sizeof(p));

			if (squaredLength(p - center) < R2) {
				if (m_paint_as_color) {
					memcpy(iter + offset, color, pg.vertex_decl.attributes[4].components_count);
				}
				else {
					*(iter + offset) = io.KeyAlt ? 255 - m_brush_value : m_brush_value;
				}
			}
		}

		if (pg.vertex_buffer) renderer.getEndFrameDrawStream().destroy(pg.vertex_buffer);
		const Renderer::MemRef mem = renderer.copy(pg.vertex_data.data(), (u32)pg.vertex_data.size());
		pg.vertex_buffer = renderer.createBuffer(mem, gpu::BufferFlags::IMMUTABLE);	
	}

	bool paint(WorldView& view, i32 x, i32 y) {
		if (!m_is_open) return false;

		WorldEditor& editor = view.getEditor();
		const Array<EntityRef>& selected = editor.getSelectedEntities();
		if (selected.size() != 1) return false;

		const EntityRef entity = selected[0];
		const World& world = *editor.getWorld();
		RenderModule* module = (RenderModule*)world.getModule("renderer");
		if (!world.hasComponent(entity, PROCEDURAL_GEOM_TYPE)) return false;

		DVec3 origin;
		Vec3 dir;
		view.getViewport().getRay({(float)x, (float)y}, origin, dir);
		const RayCastModelHit hit = module->castRay(origin, dir, [entity](const RayCastModelHit& hit) {
			return hit.entity == entity;
		});
		if (!hit.is_hit) return false;
		if (hit.entity != entity) return false;

		Renderer* renderer = (Renderer*)editor.getEngine().getSystemManager().getSystem("renderer");
		ASSERT(renderer);

		ProceduralGeometry& pg = module->getProceduralGeometry(entity);
		paint(hit.origin + hit.t * hit.dir, world, entity, pg, *renderer);

		return true;
	}

	void drawCursor(WorldEditor& editor, EntityRef entity) const {
		if (!m_is_open) return;
		const WorldView& view = editor.getView();
		const Vec2 mp = view.getMousePos();
		World& world = *editor.getWorld();
	
		RenderModule* module = static_cast<RenderModule*>(world.getModule("renderer"));
		DVec3 origin;
		Vec3 dir;
		view.getViewport().getRay(mp, origin, dir);
		const RayCastModelHit hit = module->castRay(origin, dir, [entity](const RayCastModelHit& hit){
			return hit.entity == entity;
		});

		if (hit.is_hit) {
			const DVec3 center = hit.origin + hit.dir * hit.t;
			drawCursor(editor, *module, entity, center);
			return;
		}
	}

	void drawCursor(WorldEditor& editor, RenderModule& module, EntityRef entity, const DVec3& center) const {
		if (!m_is_open) return;
		WorldView& view = editor.getView();
		addCircle(view, center, m_brush_size, Vec3(0, 1, 0), Color::GREEN);
		const ProceduralGeometry& pg = module.getProceduralGeometry(entity);

		if (pg.vertex_data.size() == 0) return;

		const u8* data = pg.vertex_data.data();
		const u32 stride = pg.vertex_decl.getStride();

		const float R2 = m_brush_size * m_brush_size;

		const Transform tr = module.getWorld().getTransform(entity);
		const Vec3 center_local = Vec3(tr.inverted().transform(center));

		for (u32 i = 0, c = pg.getVertexCount(); i < c; ++i) {
			Vec3 p;
			memcpy(&p, data + stride * i, sizeof(p));
			if (squaredLength(center_local - p) < R2) {
				addCircle(view, tr.transform(p), 0.1f, Vec3(0, 1, 0), Color::BLUE);
			}
		}
	}

	void onMouseWheel(float value) override { m_brush_size = maximum(0.f, m_brush_size + value * 0.2f); }
	bool onMouseDown(WorldView& view, int x, int y) override { return paint(view, x, y); }
	void onMouseUp(WorldView& view, int x, int y, os::MouseButton button) override {}
	void onMouseMove(WorldView& view, int x, int y, int rel_x, int rel_y) override { paint(view, x, y); }
	
	void exportToOBJ(const ProceduralGeometry& pg) {
		char filename[MAX_PATH];
		if (!os::getSaveFilename(Span(filename), "Wavefront obj\0*.obj\0", "obj")) return;
		
		os::OutputFile file;
		if (!file.open(filename)) {
			logError("Failed to open ", filename);
			return;
		}

		StringView basename = Path::getBasename(filename);

		OutputMemoryStream blob(m_app.getAllocator());
		blob.reserve(8 * 1024 * 1024);
		blob << "mtllib " << basename << ".mtl\n";
		blob << "o Terrain\n";
	
		const u32 stride = pg.vertex_decl.getStride();
		const u8* vertex_data = pg.vertex_data.data();
		const u32 uv_offset = 12;

		for (u32 i = 0, c = u32(pg.vertex_data.size() / stride); i < c; ++i) {
			Vec3 p;
			Vec2 uv;
			memcpy(&p, vertex_data + i * stride, sizeof(p));
			memcpy(&uv, vertex_data + i * stride + uv_offset, sizeof(uv));
			blob << "v " << p.x << " " << p.y << " " << p.z << "\n";
			blob << "vt " << uv.x << " " << uv.y << "\n";
		}

		blob << "usemtl Material\n";

		auto write_face_vertex = [&](u32 idx){
			blob << idx + 1 << "/" << idx + 1;
		};

		u32 index_size = 4;
		switch (pg.index_type) {
			case gpu::DataType::U16: index_size = 2; break;
			case gpu::DataType::U32: index_size = 4; break;
		}

		const u16* idx16 = (const u16*)pg.index_data.data();
		const u32* idx32 = (const u32*)pg.index_data.data();
		for (u32 i = 0, c = u32(pg.index_data.size() / index_size); i < c; i += 3) {
			u32 idx[3];
			switch (pg.index_type) {
				case gpu::DataType::U16:
					idx[0] = idx16[i];
					idx[1] = idx16[i + 1];
					idx[2] = idx16[i + 2];
					break;
				case gpu::DataType::U32:
					idx[0] = idx32[i];
					idx[1] = idx32[i + 1];
					idx[2] = idx32[i + 2];
					break;
			}

			blob << "f ";
			write_face_vertex(idx[0]);
			blob << " ";
			write_face_vertex(idx[1]);
			blob << " ";
			write_face_vertex(idx[2]);
			blob << "\n";
		}

		if (!file.write(blob.data(), blob.size())) {
			logError("Failed to write ", filename);
		}

		file.close();

		StringView dir = Path::getDir(filename);
		StaticString<MAX_PATH> mtl_filename(dir, basename, ".mtl");

		if (!file.open(mtl_filename)) {
			logError("Failed to open ", mtl_filename);
			return;
		}

		blob.clear();
		blob << "newmtl Material";

		if (!file.write(blob.data(), blob.size())) {
			logError("Failed to write ", mtl_filename);
		}

		file.close();
	}

	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != PROCEDURAL_GEOM_TYPE) return;
		if (entities.length() != 1) return;

		RenderModule* module = (RenderModule*)editor.getWorld()->getModule(PROCEDURAL_GEOM_TYPE);
		ProceduralGeometry& pg = module->getProceduralGeometry(entities[0]);
		ImGuiEx::Label("Vertex count");
		const u32 stride = pg.vertex_decl.getStride();
		const u32 vertex_count = stride ? u32(pg.vertex_data.size() / stride) : 0;
		ImGui::Text("%d", vertex_count);
		ImGuiEx::Label("Index count");
		
		u32 index_count = 0;
		if (!pg.index_data.empty()) {
			switch (pg.index_type) {
				case gpu::DataType::U16: index_count = u32(pg.index_data.size() / sizeof(u16)); break;
				case gpu::DataType::U32: index_count = u32(pg.index_data.size() / sizeof(u32)); break;
			}
		}
		ImGui::Text("%d", index_count);

		if (ImGui::Button(ICON_FA_FILE_EXPORT)) exportToOBJ(pg);

		m_is_open = false;
		if (ImGui::CollapsingHeader("Edit")) {
			m_is_open = true;
			drawCursor(editor, entities[0]);
			ImGuiEx::Label("Brush size");
			ImGui::DragFloat("##bs", &m_brush_size, 0.1f, 0, FLT_MAX);

			if (pg.vertex_decl.attributes_count > 4) {
				if (pg.vertex_decl.attributes[4].components_count > 2) {
					ImGui::Checkbox("As color", &m_paint_as_color);
				}
				else {
					m_paint_as_color = false;
				}
				if (pg.vertex_decl.attributes[4].components_count == 4 && m_paint_as_color) {
					ImGui::ColorEdit4("Color", &m_brush_color.x);
				}
				if (pg.vertex_decl.attributes[4].components_count == 3 && m_paint_as_color) {
					ImGui::ColorEdit3("Color", &m_brush_color.x);
				}
				if (pg.vertex_decl.attributes[4].components_count > 1 && !m_paint_as_color) {
					ImGuiEx::Label("Paint channel");
					ImGui::SliderInt("##pc", (int*)&m_brush_channel, 0, pg.vertex_decl.attributes[4].components_count - 1);
				}

				if (!m_paint_as_color) {
					ImGuiEx::Label("Paint value");
					ImGui::SliderInt("##pv", (int*)&m_brush_value, 0, 255);
				}
			}
		}
	}

	StudioApp& m_app;
	float m_brush_size = 1.f;
	u32 m_brush_channel = 0;
	u8 m_brush_value = 0xff;
	Vec4 m_brush_color = Vec4(1);
	bool m_is_open = false;
	bool m_paint_as_color = false;
};

struct TerrainPlugin final : PropertyGrid::IPlugin
{
	explicit TerrainPlugin(StudioApp& app)
		: m_terrain_editor(app)
	{}


	void onGUI(PropertyGrid& grid, Span<const EntityRef> entities, ComponentType cmp_type, WorldEditor& editor) override {
		if (cmp_type != TERRAIN_TYPE) return;
		if (entities.length() != 1) return;

		ComponentUID cmp;
		cmp.entity = entities[0];
		cmp.module = editor.getWorld()->getModule(cmp_type);
		cmp.type = cmp_type;
		m_terrain_editor.onGUI(cmp, editor);
	}

	TerrainEditor m_terrain_editor;
};

struct EditorUIRenderPlugin;

struct RenderInterfaceImpl final : RenderInterface
{
	RenderInterfaceImpl(StudioApp& app, Renderer& renderer, EditorUIRenderPlugin& plugin)
		: m_app(app)
		, m_textures(app.getAllocator())
		, m_renderer(renderer)
		, m_plugin(plugin)
	{}

	~RenderInterfaceImpl() {
		for (auto iter = m_textures.begin(), end = m_textures.end(); iter != end; ++iter) {
			if (iter.value().loaded) {
				iter.value().texture->decRefCount();
			}
			else {
				iter.value().texture->destroy();
				IAllocator& allocator = m_app.getAllocator();
				LUMIX_DELETE(allocator, iter.value().texture);
			}
		} 
	}

	bool saveTexture(Engine& engine, const char* path_cstr, const void* pixels, int w, int h, bool upper_left_origin) override
	{
		Path path(path_cstr);
		FileSystem& fs = engine.getFileSystem();
		os::OutputFile file;
		if (!fs.open(path, file)) return false;

		if (!Texture::saveTGA(&file, w, h, gpu::TextureFormat::RGBA8, (const u8*)pixels, upper_left_origin, path, engine.getAllocator())) {
			file.close();
			return false;
		}

		file.close();
		return true;
	}

	ImTextureID createTexture(const char* name, const void* pixels, int w, int h) override
	{
		Engine& engine = m_app.getEngine();
		auto& rm = engine.getResourceManager();
		auto& allocator = m_app.getAllocator();

		Texture* texture = LUMIX_NEW(allocator, Texture)(Path(name), *rm.get(Texture::TYPE), m_renderer, allocator);
		texture->create(w, h, gpu::TextureFormat::RGBA8, pixels, w * h * 4);
		m_textures.insert(&texture->handle, {texture, false});
		return texture->handle;
	}


	void destroyTexture(ImTextureID handle) override
	{
		auto& allocator = m_app.getAllocator();
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		ASSERT(!iter.value().loaded);
		auto* texture = iter.value().texture;
		m_textures.erase(iter);
		texture->destroy();
		LUMIX_DELETE(allocator, texture);
	}


	bool isValid(ImTextureID texture) override
	{
		return texture && *((gpu::TextureHandle*)texture);
	}


	ImTextureID loadTexture(const Path& path) override
	{
		auto& rm = m_app.getEngine().getResourceManager();
		auto* texture = rm.load<Texture>(path);
		m_textures.insert(&texture->handle, {texture, true});
		return &texture->handle;
	}


	void unloadTexture(ImTextureID handle) override
	{
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		ASSERT(iter.value().loaded);
		auto* texture = iter.value().texture;
		texture->decRefCount();
		m_textures.erase(iter);
	}


	WorldView::RayHit castRay(World& world, const DVec3& origin, const Vec3& dir, EntityPtr ignored) override
	{
		RenderModule* module = (RenderModule*)world.getModule(ENVIRONMENT_PROBE_TYPE);
		const RayCastModelHit hit = module->castRay(origin, dir, ignored);

		return {hit.is_hit, hit.t, hit.entity, hit.origin + hit.dir * hit.t};
	}


	AABB getEntityAABB(World& world, EntityRef entity, const DVec3& base) override
	{
		AABB aabb;

		if (world.hasComponent(entity, MODEL_INSTANCE_TYPE)) {
			RenderModule* module = (RenderModule*)world.getModule(ENVIRONMENT_PROBE_TYPE);
			Model* model = module->getModelInstanceModel(entity);
			if (!model) return aabb;

			aabb = model->getAABB();
			aabb.transform(world.getRelativeMatrix(entity, base));

			return aabb;
		}

		Vec3 pos = Vec3(world.getPosition(entity) - base);
		aabb = AABB(pos, pos);

		return aabb;
	}


	Path getModelInstancePath(World& world, EntityRef entity) override {
		RenderModule* module = (RenderModule*)world.getModule(ENVIRONMENT_PROBE_TYPE);
		return module->getModelInstancePath(entity); 
	}

	StudioApp& m_app;
	Renderer& m_renderer;
	EditorUIRenderPlugin& m_plugin;
	struct TextureItem {
		Texture* texture;
		bool loaded;
	};
	HashMap<void*, TextureItem> m_textures;
};


struct EditorUIRenderPlugin final : StudioApp::GUIPlugin
{
	EditorUIRenderPlugin(StudioApp& app)
		: m_app(app)
		, m_engine(app.getEngine())
		, m_programs(app.getAllocator())
	{
		PROFILE_FUNCTION();
		SystemManager& system_manager = m_engine.getSystemManager();
		Renderer* renderer = (Renderer*)system_manager.getSystem("renderer");

		unsigned char* pixels;
		int width, height;
		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		atlas->GetTexDataAsRGBA32(&pixels, &width, &height);

		const Renderer::MemRef mem = renderer->copy(pixels, width * height * 4);
		atlas->ClearTexData();
		m_texture = renderer->createTexture(width, height, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NO_MIPS, mem, "editor_font_atlas");
		ImGui::GetIO().Fonts->TexID = m_texture;

		m_render_interface.create(app, *renderer, *this);
		app.setRenderInterface(m_render_interface.get());
	}


	~EditorUIRenderPlugin()
	{
		m_app.setRenderInterface(nullptr);
		shutdownImGui();
		SystemManager& system_manager = m_engine.getSystemManager();
		Renderer* renderer = (Renderer*)system_manager.getSystem("renderer");
		for (gpu::ProgramHandle program : m_programs) {
			renderer->getEndFrameDrawStream().destroy(program);
		}
		if (m_texture) renderer->getEndFrameDrawStream().destroy(m_texture);
	}

	void onGUI() override {}

	const char* getName() const override { return "editor_ui_render"; }

	void shutdownImGui()
	{
		ImGui::DestroyContext();
	}

	gpu::ProgramHandle getProgram(void* window_handle, bool& new_program) {
		auto iter = m_programs.find(window_handle);
		if (!iter.isValid()) {
			m_programs.insert(window_handle, gpu::allocProgramHandle());
			iter = m_programs.find(window_handle);
			new_program = true;
		}

		return iter.value();
	}

	void encode(const ImDrawList* cmd_list, const ImGuiViewport* vp, Renderer* renderer, DrawStream& stream, gpu::ProgramHandle program) {
		const Renderer::TransientSlice ib = renderer->allocTransient(cmd_list->IdxBuffer.size_in_bytes());
		memcpy(ib.ptr, &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size_in_bytes());

		const Renderer::TransientSlice vb  = renderer->allocTransient(cmd_list->VtxBuffer.size_in_bytes());
		memcpy(vb.ptr, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size_in_bytes());

		stream.useProgram(program);
		stream.bindIndexBuffer(ib.buffer);
		stream.bindVertexBuffer(0, vb.buffer, vb.offset, sizeof(ImDrawVert));
		stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

		for (int i = 0, c = cmd_list->CmdBuffer.size(); i < c; ++i) {
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[i];
			ASSERT(!pcmd->UserCallback);
			if (0 == pcmd->ElemCount) continue;

			gpu::TextureHandle tex = (gpu::TextureHandle)(intptr_t)pcmd->TextureId;
			if (!tex) tex = m_texture;
			stream.bindTextures(&tex, 0, 1);

			const u32 h = u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y), 0.f, 65535.f));

			const ImVec2 pos = vp->DrawData->DisplayPos;
			const u32 vp_height = u32(vp->Size.y);
			if (gpu::isOriginBottomLeft()) {
				stream.scissor(u32(maximum((pcmd->ClipRect.x - pos.x), 0.0f)),
					vp_height - u32(maximum((pcmd->ClipRect.y - pos.y), 0.0f)) - h,
					u32(clamp((pcmd->ClipRect.z - pcmd->ClipRect.x), 0.f, 65535.f)),
					u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y), 0.f, 65535.f)));
			} else {
				stream.scissor(u32(maximum((pcmd->ClipRect.x - pos.x), 0.0f)),
					u32(maximum((pcmd->ClipRect.y - pos.y), 0.0f)),
					u32(clamp((pcmd->ClipRect.z - pcmd->ClipRect.x), 0.f, 65535.f)),
					u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y), 0.f, 65535.f)));
			}

			stream.drawIndexed(pcmd->IdxOffset * sizeof(u32) + ib.offset, pcmd->ElemCount, gpu::DataType::U32);
		}
	}

	void guiEndFrame() override
	{
		Renderer* renderer = static_cast<Renderer*>(m_engine.getSystemManager().getSystem("renderer"));

		DrawStream& stream = renderer->getDrawStream();
		stream.beginProfileBlock("imgui", 0);

		ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
		for (const ImGuiViewport* vp : platform_io.Viewports) {
			ImDrawData* draw_data = vp->DrawData;
			bool new_program = false;
			gpu::ProgramHandle program = getProgram(vp->PlatformHandle, new_program);
			const Renderer::TransientSlice ub = renderer->allocUniform(sizeof(Vec4) * 2);

			const u32 w = u32(vp->Size.x);
			const u32 h = u32(vp->Size.y);
			const Vec4 canvas_mtx[] = {
				Vec4(2.f / w, 0, -1 + (float)-draw_data->DisplayPos.x * 2.f / w, 0),
				Vec4(0, -2.f / h, 1 + (float)draw_data->DisplayPos.y * 2.f / h, 0)
			};
			memcpy(ub.ptr, &canvas_mtx, sizeof(canvas_mtx));

			if (new_program) {
				const char* vs =
					R"#(
					layout(location = 0) in vec2 a_pos;
					layout(location = 1) in vec2 a_uv;
					layout(location = 2) in vec4 a_color;
					layout(location = 0) out vec4 v_color;
					layout(location = 1) out vec2 v_uv;
					layout (std140, binding = 4) uniform IMGUIState {
						mat2x4 u_canvas_mtx;
					};
					void main() {
						v_color = a_color;
						v_uv = a_uv;
						vec2 p = vec3(a_pos, 1) * mat2x3(u_canvas_mtx);
						gl_Position = vec4(p.xy, 0, 1);
					})#";
				const char* fs = 
					R"#(
					layout(location = 0) in vec4 v_color;
					layout(location = 1) in vec2 v_uv;
					layout(location = 0) out vec4 o_color;
					layout(binding = 0) uniform sampler2D u_texture;
					void main() {
						vec4 tc = textureLod(u_texture, v_uv, 0);
						o_color.rgb = pow(tc.rgb, vec3(1/2.2)) * v_color.rgb;
						o_color.a = v_color.a * tc.a;
					})#";
				const char* srcs[] = {vs, fs};
				gpu::ShaderType types[] = {gpu::ShaderType::VERTEX, gpu::ShaderType::FRAGMENT};
				gpu::VertexDecl decl(gpu::PrimitiveType::TRIANGLES);
				decl.addAttribute(0, 0, 2, gpu::AttributeType::FLOAT, 0);
				decl.addAttribute(1, 8, 2, gpu::AttributeType::FLOAT, 0);
				decl.addAttribute(2, 16, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
				const gpu::StateFlags blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
				const gpu::StateFlags state = gpu::StateFlags::SCISSOR_TEST | blend_state;
				stream.createProgram(program, state, decl, srcs, types, 2, nullptr, 0, "imgui shader");
			}

			stream.setCurrentWindow(vp->PlatformHandle);
			stream.setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
			stream.viewport(0, 0, w, h);
			const Vec4 clear_color = Vec4(0.2f, 0.2f, 0.2f, 1.f);
			stream.clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH, &clear_color.x, 1.0);
			stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
				
			for (int i = 0; i < draw_data->CmdListsCount; ++i) {
				encode(draw_data->CmdLists[i], vp, renderer, stream, program);
			}
		}
		stream.setCurrentWindow(nullptr);
		stream.endProfileBlock();
		renderer->frame();
	}


	StudioApp& m_app;
	Engine& m_engine;
	HashMap<void*, gpu::ProgramHandle> m_programs;
	gpu::TextureHandle m_texture;
	Local<RenderInterfaceImpl> m_render_interface;
};

struct AddTerrainComponentPlugin final : StudioApp::IAddComponentPlugin {
	explicit AddTerrainComponentPlugin(StudioApp& app)
		: m_app(app)
		, m_file_selector("mat", app)
	{}

	bool createHeightmap(const Path& material_path, int size)
	{
		PathInfo info(material_path);
		const Path hm_path(info.dir, info.basename, ".raw");
		const Path albedo_path(info.dir, "albedo_detail.ltc");
		const Path normal_path(info.dir, "normal_detail.ltc");
		const Path splatmap_path(info.dir, "splatmap.tga");
		const Path splatmap_meta_path(info.dir, "splatmap.tga.meta");
		os::OutputFile file;
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (!fs.open(hm_path, file))
		{
			logError("Failed to create heightmap ", hm_path);
			return false;
		}
		RawTextureHeader header;
		header.width = size;
		header.height = size;
		header.depth = 1;
		header.is_array = false;
		header.channel_type = RawTextureHeader::ChannelType::U16;
		header.channels_count = 1;
		bool written = file.write(&header, sizeof(header));
		u16 tmp = 0;
		for (int i = 0; i < size * size; ++i) {
			written = file.write(&tmp, sizeof(tmp)) && written;
		}
		file.close();
		
		if (!written) {
			logError("Could not write ", hm_path);
			os::deleteFile(hm_path);
			return false;
		}

		if (!fs.open(splatmap_meta_path, file)) {
			logError("Failed to create meta ", splatmap_meta_path);
			os::deleteFile(hm_path);
			return false;
		}

		file << "compress = false\n";
		file << "mips = false\n";
		file << "filter = \"point\"";
		file.close();

		if (!fs.open(splatmap_path, file)) {
			logError("Failed to create texture ", splatmap_path);
			os::deleteFile(splatmap_meta_path);
			os::deleteFile(hm_path);
			return false;
		}

		OutputMemoryStream splatmap(m_app.getAllocator());
		splatmap.resize(size * size * 4);
		memset(splatmap.getMutableData(), 0, size * size * 4);
		if (!Texture::saveTGA(&file, size, size, gpu::TextureFormat::RGBA8, splatmap.data(), true, splatmap_path, m_app.getAllocator())) {
			logError("Failed to create texture ", splatmap_path);
			os::deleteFile(hm_path);
			return false;
		}
		file.close();

		CompositeTexture albedo(m_app, m_app.getAllocator());
		albedo.initTerrainAlbedo();
		if (!albedo.save(fs, albedo_path)) {
			logError("Failed to create texture ", albedo_path);
			os::deleteFile(hm_path);
			os::deleteFile(splatmap_path);
			os::deleteFile(splatmap_meta_path);
			return false;
		}

		CompositeTexture normal(m_app, m_app.getAllocator());
		normal.initTerrainNormal();
		if (!normal.save(fs, normal_path)) {
			logError("Failed to create texture ", normal_path);
			os::deleteFile(albedo_path);
			os::deleteFile(hm_path);
			os::deleteFile(splatmap_path);
			os::deleteFile(splatmap_meta_path);
			return false;
		}

		if (!fs.open(material_path, file)) {
			logError("Failed to create material ", material_path);
			os::deleteFile(normal_path);
			os::deleteFile(albedo_path);
			os::deleteFile(hm_path);
			os::deleteFile(splatmap_path);
			os::deleteFile(splatmap_meta_path);
			return false;
		}

		file << R"#(
			shader "pipelines/terrain.shd"
			texture ")#";
		file << info.basename;
		file << R"#(.raw"
			texture "albedo_detail.ltc"
			texture "normal_detail.ltc"
			texture "splatmap.tga"
			uniform("Detail distance", 50.000000)
			uniform("Detail scale", 1.000000)
			uniform("Noise UV scale", 0.200000)
			uniform("Detail diffusion", 0.500000)
			uniform("Detail power", 16.000000)
		)#";

		file.close();
		return true;
	}


	void onGUI(bool create_entity, bool from_filter, EntityPtr parent, WorldEditor& editor) override
	{
		if (!ImGui::BeginMenu("Terrain")) return;
		Path path;
		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::BeginMenu("New"))
		{
			static int size = 1024;
			ImGuiEx::Label("Size");
			ImGui::InputInt("##size", &size);
			m_file_selector.gui(false, "mat");
			if (m_file_selector.getPath()[0] &&  ImGui::Button("Create"))
			{
				new_created = createHeightmap(Path(m_file_selector.getPath()), size);
				path = m_file_selector.getPath();
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);
		static FilePathHash selected_res_hash;
		if (asset_browser.resourceList(path, selected_res_hash, Material::TYPE, false) || create_empty || new_created)
		{
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(Span(&entity, 1), false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getWorld()->hasComponent(entity, TERRAIN_TYPE))
			{
				editor.addComponent(Span(&entity, 1), TERRAIN_TYPE);
			}

			if (!create_empty)
			{
				editor.setProperty(TERRAIN_TYPE, "", -1, "Material", Span(&entity, 1), path);
			}
			if (parent.isValid()) editor.makeParent(parent, entity);
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}

	const char* getLabel() const override { return "Render / Terrain"; }

	StudioApp& m_app;
	FileSelector m_file_selector;
	bool m_show_save_as = false;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_pipeline_plugin(app)
		, m_font_plugin(app)
		, m_material_plugin(app)
		, m_particle_emitter_property_plugin(app)
		, m_shader_plugin(app)
		, m_model_properties_plugin(app)
		, m_texture_plugin(app)
		, m_game_view(app)
		, m_scene_view(app)
		, m_editor_ui_render_plugin(app)
		, m_env_probe_plugin(app)
		, m_terrain_plugin(app)
		, m_instanced_model_plugin(app)
		, m_model_plugin(app)
		, m_procedural_geom_plugin(app)
	{}

	const char* getName() const override { return "renderer"; }

	static bool renderDocOption() {
		char cmd_line[2048];
		os::getCommandLine(Span(cmd_line));

		CommandLineParser parser(cmd_line);
		while (parser.next())
		{
			if (parser.currentEquals("-renderdoc")) return true;
		}
		return false;
	}

	void init() override
	{
		PROFILE_FUNCTION();
		m_renderdoc_capture_action.init("     Capture RenderDoc", "Capture with RenderDoc", "capture_renderdoc", "", false);
		m_renderdoc_capture_action.func.bind<&StudioAppPlugin::captureRenderDoc>(this);

		if (renderDocOption()) {
			m_app.addToolAction(&m_renderdoc_capture_action);
		}

		IAllocator& allocator = m_app.getAllocator();

		AddTerrainComponentPlugin* add_terrain_plugin = LUMIX_NEW(allocator, AddTerrainComponentPlugin)(m_app);
		m_app.registerComponent(ICON_FA_MAP, "terrain", *add_terrain_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();

		const char* shader_exts[] = {"shd"};
		asset_compiler.addPlugin(m_shader_plugin, Span(shader_exts));

		const char* texture_exts[] = {"png", "jpg", "jpeg", "tga", "raw", "ltc"};
		asset_compiler.addPlugin(m_texture_plugin, Span(texture_exts));

		const char* pipeline_exts[] = {"pln"};
		asset_compiler.addPlugin(m_pipeline_plugin, Span(pipeline_exts));

		const char* material_exts[] = {"mat"};
		asset_compiler.addPlugin(m_material_plugin, Span(material_exts));

		m_model_plugin.m_texture_plugin = &m_texture_plugin;
		const char* model_exts[] = {"fbx"};
		asset_compiler.addPlugin(m_model_plugin, Span(model_exts));

		const char* fonts_exts[] = {"ttf"};
		asset_compiler.addPlugin(m_font_plugin, Span(fonts_exts));
		
		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(m_model_plugin, Span(model_exts));
		asset_browser.addPlugin(m_material_plugin, Span(material_exts));
		asset_browser.addPlugin(m_font_plugin, Span(fonts_exts));
		asset_browser.addPlugin(m_shader_plugin, Span(shader_exts));
		asset_browser.addPlugin(m_texture_plugin, Span(texture_exts));
		asset_browser.addPlugin(m_pipeline_plugin, Span(pipeline_exts));

		m_app.addPlugin(m_scene_view);
		m_app.addPlugin(m_game_view);
		m_app.addPlugin(m_editor_ui_render_plugin);
		m_app.addPlugin(m_procedural_geom_plugin);

		PropertyGrid& property_grid = m_app.getPropertyGrid();
		property_grid.addPlugin(m_model_properties_plugin);
		property_grid.addPlugin(m_env_probe_plugin);
		property_grid.addPlugin(m_terrain_plugin);
		property_grid.addPlugin(m_procedural_geom_plugin);
		property_grid.addPlugin(m_instanced_model_plugin);
		property_grid.addPlugin(m_particle_emitter_property_plugin);

		m_scene_view.init();
		m_game_view.init();
		m_env_probe_plugin.init();
		m_model_plugin.init();

		m_particle_editor = ParticleEditor::create(m_app);
	}

	void captureRenderDoc() { gpu::captureRenderDocFrame(); }

	void showEnvironmentProbeGizmo(WorldView& view, ComponentUID cmp) {
		RenderModule* module = static_cast<RenderModule*>(cmp.module);
		const World& world = module->getWorld();
		EntityRef e = (EntityRef)cmp.entity;
		EnvironmentProbe& p = module->getEnvironmentProbe(e);
		Transform tr = world.getTransform(e);

		const Gizmo::Config& cfg = m_app.getGizmoConfig();
		WorldEditor& editor = view.getEditor();
		if (Gizmo::box(u64(cmp.entity.index) | (u64(1) << 33), view, tr, p.inner_range, cfg, true)) {
			editor.beginCommandGroup("env_probe_inner_range");
			editor.setProperty(ENVIRONMENT_PROBE_TYPE, "", -1, "Inner range", Span(&e, 1), p.inner_range);
			editor.setEntitiesPositions(&e, &tr.pos, 1);
			editor.endCommandGroup();
		}
		if (Gizmo::box(u64(cmp.entity.index) | (u64(1) << 32), view, tr, p.outer_range, cfg, false)) {
			editor.beginCommandGroup("env_probe_outer_range");
			editor.setProperty(ENVIRONMENT_PROBE_TYPE, "", -1, "Outer range", Span(&e, 1), p.outer_range);
			editor.setEntitiesPositions(&e, &tr.pos, 1);
			editor.endCommandGroup();
		}
	}

	void showReflectionProbeGizmo(WorldView& view, ComponentUID cmp) {
		RenderModule* module = static_cast<RenderModule*>(cmp.module);
		const World& world = module->getWorld();
		EntityRef e = (EntityRef)cmp.entity;
		ReflectionProbe& p = module->getReflectionProbe(e);
		Transform tr = world.getTransform(e);

		const Gizmo::Config& cfg = m_app.getGizmoConfig();
		WorldEditor& editor = view.getEditor();
		if (Gizmo::box(u64(cmp.entity.index) | (u64(1) << 32), view, tr, p.half_extents, cfg, false)) {
			editor.beginCommandGroup("refl_probe_half_ext");
			editor.setProperty(ENVIRONMENT_PROBE_TYPE, "", -1, "Half extents", Span(&e, 1), p.half_extents);
			editor.setEntitiesPositions(&e, &tr.pos, 1);
			editor.endCommandGroup();
		}
	}

	void showPointLightGizmo(WorldView& view, ComponentUID light)
	{
		RenderModule* module = static_cast<RenderModule*>(light.module);
		World& world = module->getWorld();

		const float range = module->getLightRange((EntityRef)light.entity);
		const float fov = module->getPointLight((EntityRef)light.entity).fov;

		const DVec3 pos = world.getPosition((EntityRef)light.entity);
		if (fov > PI) {
			addSphere(view, pos, range, Color::BLUE);
		}
		else {
			const Quat rot = world.getRotation((EntityRef)light.entity);
			const float t = tanf(fov * 0.5f);
			addCone(view, pos, rot.rotate(Vec3(0, 0, -range)), rot.rotate(Vec3(0, range * t, 0)), rot.rotate(Vec3(range * t, 0, 0)), Color::BLUE);
		}
	}

	static Vec3 minCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(minimum(a.x, b.x), minimum(a.y, b.y), minimum(a.z, b.z));
	}

	static Vec3 maxCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(maximum(a.x, b.x), maximum(a.y, b.y), maximum(a.z, b.z));
	}

	void showGlobalLightGizmo(WorldView& view, ComponentUID light)
	{
		const World& world = light.module->getWorld();
		const EntityRef entity = (EntityRef)light.entity;
		const DVec3 pos = world.getPosition(entity);

		const Vec3 dir = world.getRotation(entity).rotate(Vec3(0, 0, 1));
		const Vec3 right = world.getRotation(entity).rotate(Vec3(1, 0, 0));
		const Vec3 up = world.getRotation(entity).rotate(Vec3(0, 1, 0));

		addLine(view, pos, pos + dir, Color::BLUE);
		addLine(view, pos + right, pos + dir + right, Color::BLUE);
		addLine(view, pos - right, pos + dir - right, Color::BLUE);
		addLine(view, pos + up, pos + dir + up, Color::BLUE);
		addLine(view, pos - up, pos + dir - up, Color::BLUE);

		addLine(view, pos + right + up, pos + dir + right + up, Color::BLUE);
		addLine(view, pos + right - up, pos + dir + right - up, Color::BLUE);
		addLine(view, pos - right - up, pos + dir - right - up, Color::BLUE);
		addLine(view, pos - right + up, pos + dir - right + up, Color::BLUE);

		addSphere(view, pos - dir, 0.1f, Color::BLUE);
	}

	void showDecalGizmo(WorldView& view, ComponentUID cmp)
	{
		RenderModule* module = static_cast<RenderModule*>(cmp.module);
		const EntityRef e = (EntityRef)cmp.entity;
		World& world = module->getWorld();
		Decal& decal = module->getDecal(e);
		const Transform tr = world.getTransform(e);
		const Vec3 x = tr.rot * Vec3(1, 0, 0) * decal.half_extents.x;
		const Vec3 y = tr.rot * Vec3(0, 1, 0) * decal.half_extents.y;
		const Vec3 z = tr.rot * Vec3(0, 0, 1) * decal.half_extents.z;
		addCube(view, tr.pos, x, y, z, Color::BLUE);
	}

	void showCurveDecalGizmo(WorldView& view, ComponentUID cmp)
	{
		RenderModule* module = static_cast<RenderModule*>(cmp.module);
		const EntityRef e = (EntityRef)cmp.entity;
		World& world = module->getWorld();
		CurveDecal& decal = module->getCurveDecal(e);
		const Transform tr = world.getTransform(e);
		const Vec3 x = tr.rot * Vec3(1, 0, 0) * decal.half_extents.x;
		const Vec3 y = tr.rot * Vec3(0, 1, 0) * decal.half_extents.y;
		const Vec3 z = tr.rot * Vec3(0, 0, 1) * decal.half_extents.z;
		addCube(view, tr.pos, x, y, z, Color::BLUE);

		Gizmo::Config cfg;
		const DVec3 pos0 = tr.transform(DVec3(decal.bezier_p0.x, 0, decal.bezier_p0.y));
		Transform p0_tr = { pos0, Quat::IDENTITY, Vec3(1) };
		WorldEditor& editor = view.getEditor();
		if (Gizmo::manipulate((u64(1) << 32) | cmp.entity.index, view, p0_tr, cfg)) {
			const Vec2 p0 = Vec2(tr.inverted().transform(p0_tr.pos).xz());
			editor.setProperty(CURVE_DECAL_TYPE, "", 0, "Bezier P0", Span(&e, 1), p0);
		}

		const DVec3 pos2 = tr.transform(DVec3(decal.bezier_p2.x, 0, decal.bezier_p2.y));
		Transform p2_tr = { pos2, Quat::IDENTITY, Vec3(1) };
		if (Gizmo::manipulate((u64(2) << 32) | cmp.entity.index, view, p2_tr, cfg)) {
			const Vec2 p2 = Vec2(tr.inverted().transform(p2_tr.pos).xz());
			editor.setProperty(CURVE_DECAL_TYPE, "", 0, "Bezier P2", Span(&e, 1), p2);
		}

		addLine(view, tr.pos, p0_tr.pos, Color::BLUE);
		addLine(view, tr.pos, p2_tr.pos, Color::GREEN);
	}

	void showCameraGizmo(WorldView& view, ComponentUID cmp)
	{
		RenderModule* module = static_cast<RenderModule*>(cmp.module);

		addFrustum(view, module->getCameraFrustum((EntityRef)cmp.entity), Color::BLUE);
	}

	bool showGizmo(WorldView& view, ComponentUID cmp) override
	{
		if (cmp.type == CAMERA_TYPE)
		{
			showCameraGizmo(view, cmp);
			return true;
		}
		if (cmp.type == DECAL_TYPE)
		{
			showDecalGizmo(view, cmp);
			return true;
		}
		if (cmp.type == CURVE_DECAL_TYPE)
		{
			showCurveDecalGizmo(view, cmp);
			return true;
		}
		if (cmp.type == POINT_LIGHT_TYPE)
		{
			showPointLightGizmo(view, cmp);
			return true;
		}
		if (cmp.type == ENVIRONMENT_TYPE)
		{
			showGlobalLightGizmo(view, cmp);
			return true;
		}
		if (cmp.type == ENVIRONMENT_PROBE_TYPE) {
			showEnvironmentProbeGizmo(view, cmp);
			return true;
		}
		if (cmp.type == REFLECTION_PROBE_TYPE) {
			showReflectionProbeGizmo(view, cmp);
			return true;
		}
		return false;
	}

	~StudioAppPlugin()
	{
		m_app.removeAction(&m_renderdoc_capture_action);

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(m_model_plugin);
		asset_browser.removePlugin(m_material_plugin);
		asset_browser.removePlugin(m_font_plugin);
		asset_browser.removePlugin(m_texture_plugin);
		asset_browser.removePlugin(m_shader_plugin);
		asset_browser.removePlugin(m_pipeline_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();
		asset_compiler.removePlugin(m_font_plugin);
		asset_compiler.removePlugin(m_shader_plugin);
		asset_compiler.removePlugin(m_texture_plugin);
		asset_compiler.removePlugin(m_model_plugin);
		asset_compiler.removePlugin(m_material_plugin);
		asset_compiler.removePlugin(m_pipeline_plugin);

		m_app.removePlugin(m_scene_view);
		m_app.removePlugin(m_game_view);
		m_app.removePlugin(m_editor_ui_render_plugin);
		m_app.removePlugin(m_procedural_geom_plugin);

		PropertyGrid& property_grid = m_app.getPropertyGrid();

		property_grid.removePlugin(m_model_properties_plugin);
		property_grid.removePlugin(m_env_probe_plugin);
		property_grid.removePlugin(m_procedural_geom_plugin);
		property_grid.removePlugin(m_terrain_plugin);
		property_grid.removePlugin(m_instanced_model_plugin);
		property_grid.removePlugin(m_particle_emitter_property_plugin);
	}

	StudioApp& m_app;
	Action m_renderdoc_capture_action;
	UniquePtr<ParticleEditor> m_particle_editor;
	EditorUIRenderPlugin m_editor_ui_render_plugin;
	MaterialPlugin m_material_plugin;
	ParticleSystemPropertyPlugin m_particle_emitter_property_plugin;
	PipelinePlugin m_pipeline_plugin;
	FontPlugin m_font_plugin;
	ShaderPlugin m_shader_plugin;
	ModelPropertiesPlugin m_model_properties_plugin;
	TexturePlugin m_texture_plugin;
	GameView m_game_view;
	SceneView m_scene_view;
	EnvironmentProbePlugin m_env_probe_plugin;
	TerrainPlugin m_terrain_plugin;
	ProceduralGeomPlugin m_procedural_geom_plugin;
	InstancedModelPlugin m_instanced_model_plugin;
	ModelPlugin m_model_plugin;
};

}

LUMIX_STUDIO_ENTRY(renderer) {
	PROFILE_FUNCTION();
	IAllocator& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
