#define LUMIX_NO_CUSTOM_CRT
#ifdef LUMIX_BASIS_UNIVERSAL
	#include <encoder/basisu_comp.h>
#endif

#include <imgui/imgui_freetype.h>

#include "animation/animation.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/gizmo.h"
#include "renderer/editor/particle_editor.h"
#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/allocators.h"
#include "engine/associative_array.h"
#include "engine/atomic.h"
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
#include "engine/universe.h"
#include "fbx_importer.h"
#include "game_view.h"
#include "render_plugins.h"
#include "renderer/culling_system.h"
#include "renderer/editor/composite_texture.h"
#include "renderer/editor/spline_geometry_plugin.h"
#include "renderer/font.h"
#include "renderer/gpu/gpu.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "renderer/terrain.h"
#include "scene_view.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include "terrain_editor.h"

#define RGBCX_IMPLEMENTATION
#include <rgbcx/rgbcx.h>
#include <stb/stb_image_resize.h>


using namespace Lumix;

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
		ASSERT(!is_srgb);
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

	const u8 ref = u8(clamp(255 * ref_norm, 0.f, 255.f));
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
	static bool once = []() { rgbcx::init(); return false; }();

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
	
	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}

	void onGUI(Span<Resource*> resources) override {}
	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Font"; }

	ResourceType getResourceType() const override { return FontResource::TYPE; }

	StudioApp& m_app;
};


struct PipelinePlugin final : AssetCompiler::IPlugin
{
	explicit PipelinePlugin(StudioApp& app)
		: m_app(app)
	{}

	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}

	StudioApp& m_app;
};


struct ParticleEmitterPropertyPlugin final : PropertyGrid::IPlugin
{
	ParticleEmitterPropertyPlugin(StudioApp& app) : m_app(app) {}

	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override {
		
		if (cmp.type != PARTICLE_EMITTER_TYPE) return;
		
		RenderScene* scene = (RenderScene*)cmp.scene;
		ParticleEmitter& emitter = scene->getParticleEmitter(*cmp.entity);

		if (m_playing && ImGui::Button(ICON_FA_STOP " Stop")) m_playing = false;
		else if (!m_playing && ImGui::Button(ICON_FA_PLAY " Play")) m_playing = true;

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_UNDO_ALT " Reset")) emitter.reset();

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_EDIT " Edit")) m_particle_editor->open(emitter.getResource()->getPath().c_str());

		ImGuiEx::Label("Time scale");
		ImGui::SliderFloat("##ts", &m_time_scale, 0, 1);
		if (m_playing) {
			float dt = m_app.getEngine().getLastTimeDelta() * m_time_scale;
			scene->updateParticleEmitter((EntityRef)cmp.entity, dt);
		}
			
		ImGuiEx::Label("Particle count");
		ImGui::Text("%d", emitter.m_particles_count);
	}

	StudioApp& m_app;
	bool m_playing = false;
	float m_time_scale = 1.f;
	ParticleEditor* m_particle_editor;
};

struct ParticleEmitterPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit ParticleEmitterPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("par", ParticleEmitterResource::TYPE);
	}

	bool compile(const Path& src) override {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;

		InputMemoryStream input(src_data);
		OutputMemoryStream output(m_app.getAllocator());
		if (!m_particle_editor->compile(input, output, src.c_str())) return false;

		return m_app.getAssetCompiler().writeCompiledResource(src.c_str(), Span(output.data(), (i32)output.size()));
	}
	
	void onGUI(Span<Resource*> resources) override {
		if (resources.length() != 1) return;

		if (ImGui::Button(ICON_FA_EDIT " Edit")) {
			m_particle_editor->open(resources[0]->getPath().c_str());
		}
	}

	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Particle Emitter"; }
	ResourceType getResourceType() const override { return ParticleEmitterResource::TYPE; }

	StudioApp& m_app;
	ParticleEditor* m_particle_editor;
};


struct MaterialPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit MaterialPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("mat", Material::TYPE);
	}

	bool canCreateResource() const override { return true; }
	const char* getFileDialogFilter() const override { return "Material\0*.mat\0"; }
	const char* getFileDialogExtensions() const override { return "mat"; }
	const char* getDefaultExtension() const override { return "mat"; }

	bool createResource(const char* path) override {
		os::OutputFile file;
		if (!file.open(path)) {
			logError("Failed to create ", path);
			return false;
		}

		file << "shader \"pipelines/standard.shd\"";
		file.close();
		return true;
	}

	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}


	void saveMaterial(Material* material)
	{
		if (OutputMemoryStream* file = m_app.getAssetBrowser().beginSaveResource(*material)) {
			bool success = true;
			if (!material->save(*file))
			{
				success = false;
				logError("Could not save file ", material->getPath());
			}
			m_app.getAssetBrowser().endSaveResource(*material, *file, success);
		}
	}

	void onGUI(Span<Resource*> ress) {
		Span<Material*> resources;
		resources.m_begin = (Material**)ress.m_begin;
		resources.m_end = (Material**)ress.m_end;
		if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "Open externally")) {
			for (Resource* res : resources) {
				m_app.getAssetBrowser().openInExternalEditor(res);
			}
		}

		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_SAVE "Save")) {
			for (Material* res : resources) {
				saveMaterial(res);
			}
		}

		char buf[LUMIX_MAX_PATH];
		Material* first = static_cast<Material*>(resources[0]);
		Shader* shader = first->getShader();

		bool same_shader = true;
		for (Material* res : resources) {
			if (res->getShader() != shader) {
				same_shader = false;
			}
		}

		if (same_shader) {
			copyString(buf, shader ? shader->getPath().c_str() : "");
		}
		else {
			copyString(buf, "<different values>");
		}

		ImGuiEx::Label("Shader");
		if (m_app.getAssetBrowser().resourceInput("shader", Span(buf), Shader::TYPE)) {
			for (Material* res : resources) {
				res->setShader(Path(buf));
			}
		}

		multiLabel<&Material::isBackfaceCulling>("Backface culling", resources);
		bool b = first->isBackfaceCulling();
		if (ImGui::Checkbox("##bfcul", &b)) {
			set<&Material::enableBackfaceCulling>(resources, b);
		}

		if (!same_shader) return;
		if (!shader) return;
		if (!shader->isReady()) return;
		
		Renderer& renderer = first->getRenderer();

		const u8 alpha_cutout_idx = renderer.getShaderDefineIdx("ALPHA_CUTOUT");
		if (shader->hasDefine(alpha_cutout_idx)) {
			multiLabel<&Material::isAlphaCutout>("Alpha cutout", resources);
			b = first->isAlphaCutout();
			if (ImGui::Checkbox("##acutout", &b)) {
				set<&Material::setAlphaCutout>(resources, b);
			}
		}

		if (!shader->isIgnored(Shader::COLOR)) {
			Vec4 color = first->getColor();
			multiLabel<&Material::getColor>("Color", resources);
			if (ImGui::ColorEdit4("##col", &color.x)) {
				set<&Material::setColor>(resources, color);
			}
		}

		if (!shader->isIgnored(Shader::ROUGHNESS)) {
			float roughness = first->getRoughness();
			multiLabel<&Material::getRoughness>("Roughness", resources);
			if (ImGui::DragFloat("##rgh", &roughness, 0.1f, 0.0f, 1.0f)) {
				set<&Material::setRoughness>(resources, roughness);
			}
		}

		if (!shader->isIgnored(Shader::METALLIC)) {
			float metallic = first->getMetallic();
			multiLabel<&Material::getMetallic>("Metallic", resources);
			if (ImGui::DragFloat("##met", &metallic, 0.1f, 0.0f, 1.0f)) {
				set<&Material::setMetallic>(resources, metallic);
			}
		}
			
		if (!shader->isIgnored(Shader::EMISSION)) {
			float emission = first->getEmission();
			multiLabel<&Material::getEmission>("Emission", resources);
			if (ImGui::DragFloat("##emis", &emission, 0.1f, 0.0f)) {
				set<&Material::setEmission>(resources, emission);
			}
		}

		if (!shader->isIgnored(Shader::TRANSLUCENCY)) {
			float translucency = first->getTranslucency();
			multiLabel<&Material::getTranslucency>("Translucency", resources);
			if (ImGui::DragFloat("##trns", &translucency, 0.1f, 0.f, 1.f)) {
				set<&Material::setTranslucency>(resources, translucency);
			}
		}

		const char* current_layer_name = renderer.getLayerName(first->getLayer());
		multiLabel<&Material::getLayer>("Layer", resources);
		if (ImGui::BeginCombo("##layer", current_layer_name)) {
			for (u8 i = 0, c = renderer.getLayersCount(); i < c; ++i) {
				const char* name = renderer.getLayerName(i);
				if (ImGui::Selectable(name)) {
					set<&Material::setLayer>(resources, i);
				}
			}
			ImGui::EndCombo();
		}

		for (u32 i = 0; i < shader->m_texture_slot_count; ++i) {
			auto& slot = shader->m_texture_slots[i];
			Texture* texture = first->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
			ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0, 0, 0, 0));
			bool is_node_open = ImGui::TreeNodeEx((const void*)(intptr_t)(i + 1), //-V1028
				ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_AllowItemOverlap | ImGuiTreeNodeFlags_Framed,
				"%s",
				"");
			ImGui::PopStyleColor(4);
			ImGui::SameLine();

			bool is_same = true;
			for (Material* res : resources) {
				if (res->getTexture(i) != texture) {
					is_same = false;
					break;
				}
			}
			if (!is_same) {
				ImGui::TextUnformatted("(?)");
				ImGui::SameLine();
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Objects have different values");
				}
			}
			ImGuiEx::Label(slot.name);
			if (m_app.getAssetBrowser().resourceInput(StaticString<30>("", (u64)&slot), Span(buf), Texture::TYPE)) { 
				for (Material* res : resources) {
					res->setTexturePath(i, Path(buf));
				}
			}
			if (!texture && is_node_open) {
				ImGui::TreePop();
				continue;
			}

			if (is_node_open) {
				ImGui::Image(texture->handle, ImVec2(96, 96));
				ImGui::TreePop();
			}
		}

		if (first->isReady()) {
			for (int i = 0; i < shader->m_uniforms.size(); ++i) {
				const Shader::Uniform& shader_uniform = shader->m_uniforms[i];
				Material::Uniform* uniform = first->findUniform(shader_uniform.name_hash);
				if (!uniform) {
					uniform = &first->getUniforms().emplace();
					uniform->name_hash = shader_uniform.name_hash;
					memcpy(uniform->matrix, shader_uniform.default_value.matrix, sizeof(uniform->matrix)); 
				}

				ImGuiEx::Label(shader_uniform.name);
				bool changed = false;
				switch (shader_uniform.type) {
					case Shader::Uniform::FLOAT:
						changed = ImGui::DragFloat(StaticString<256>("##", shader_uniform.name), &uniform->float_value);
						break;
					case Shader::Uniform::INT:
						changed = ImGui::DragInt(StaticString<256>("##", shader_uniform.name), &uniform->int_value);
						break;
					case Shader::Uniform::VEC3:
						changed = ImGui::DragFloat3(StaticString<256>("##", shader_uniform.name), uniform->vec3);
						break;
					case Shader::Uniform::VEC4:
						changed = ImGui::DragFloat4(StaticString<256>("##", shader_uniform.name), uniform->vec4);
						break;
					case Shader::Uniform::VEC2:
						changed = ImGui::DragFloat2(StaticString<256>("##", shader_uniform.name), uniform->vec2);
						break;
					case Shader::Uniform::COLOR:
						changed = ImGui::ColorEdit3(StaticString<256>("##", shader_uniform.name), uniform->vec3);
						break;
					default: ASSERT(false); break;
				}
				if (changed) {
					for(Material* mat : resources) {
						if (mat != first) {
							Material::Uniform* u = mat->findUniform(shader_uniform.name_hash);
							if (!u) u = &mat->getUniforms().emplace();
							memcpy(u, uniform, sizeof(*u));
						}
						mat->updateRenderData(false);
					}
				}
			}
		}

		if (Material::getCustomFlagCount() > 0 && ImGui::CollapsingHeader("Flags")) {

			for (int i = 0; i < Material::getCustomFlagCount(); ++i) {
				bool b = first->isCustomFlag(1 << i);
				bool is_same = true;
				for (Material* mat : resources) {
					if (mat->isCustomFlag(1 << i) != b) {
						is_same = false;
						break;
					}
				}
				if (ImGui::Checkbox(Material::getCustomFlagName(i), &b)) {
					for (Material* mat : resources) {
						if (b)
							mat->setCustomFlag(1 << i);
						else
							mat->unsetCustomFlag(1 << i);
					}
				}
				if (!is_same) {
					ImGui::SameLine();
					ImGui::TextUnformatted("(?)");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Objects have different values");
					}
				}
			}
		}
		
		if (ImGui::CollapsingHeader("Defines")) {
			for (int i = 0; i < renderer.getShaderDefinesCount(); ++i) {
				const char* define = renderer.getShaderDefine(i);
				if (!shader->hasDefine(i)) continue;

				auto isBuiltinDefine = [](const char* define) {
					const char* BUILTIN_DEFINES[] = {"HAS_SHADOWMAP", "ALPHA_CUTOUT", "SKINNED"};
					for (const char* builtin_define : BUILTIN_DEFINES) {
						if (equalStrings(builtin_define, define)) return true;
					}
					return false;
				};

				bool is_same = true;
				for (Material* res : resources) {
					if (res->isDefined(i) != first->isDefined(i)) {
						is_same = false;
						break;
					}
				}

				bool value = first->isDefined(i);
				bool is_texture_define = first->isTextureDefine(i);
				if (is_texture_define || isBuiltinDefine(define)) continue;
				
				if (ImGui::Checkbox(define, &value)) {
					for (Material* res : resources) {
						res->setDefine(i, value);
					}
				}
				if (!is_same) {
					ImGui::SameLine();
					ImGui::TextUnformatted("(?)");
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Objects have different values");
					}
				}
			}
		}
	}

	template <auto F, typename T>
	static void set(Span<Material*> resources, T value) {
		for (Material* r : resources) {
			(r->*F)(value);
		}
	}

	template <auto F>
	static void multiLabel(const char* label, Span<Material*> resources) {
		ASSERT(resources.length() > 0);
		auto v = (resources[0]->*F)();
		for (Material* r : resources) {
			auto v2 = (r->*F)();
			if (v2 != v) {
				ImGui::TextUnformatted("(?)");
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("Objects have different values");
				}
				ImGui::SameLine();
				break;
			}
		}
		ImGuiEx::Label(label);
	}

	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Material"; }
	ResourceType getResourceType() const override { return Material::TYPE; }


	StudioApp& m_app;
};


struct TexturePlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	struct Meta
	{
		enum WrapMode : u32 {
			REPEAT,
			CLAMP
		};
		enum Filter : u32 {
			LINEAR,
			POINT,
			ANISOTROPIC
		};
		bool srgb = false;
		bool is_normalmap = false;
		bool mips = true;
		float scale_coverage = -0.5f;
		bool stochastic_mipmap = false;
		bool compress = true;
		WrapMode wrap_mode_u = WrapMode::REPEAT;
		WrapMode wrap_mode_v = WrapMode::REPEAT;
		WrapMode wrap_mode_w = WrapMode::REPEAT;
		Filter filter = Filter::LINEAR;
	};

	explicit TexturePlugin(StudioApp& app)
		: m_app(app)
		, m_composite(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("png", Texture::TYPE);
		app.getAssetCompiler().registerExtension("jpeg", Texture::TYPE);
		app.getAssetCompiler().registerExtension("jpg", Texture::TYPE);
		app.getAssetCompiler().registerExtension("tga", Texture::TYPE);
		app.getAssetCompiler().registerExtension("raw", Texture::TYPE);
		app.getAssetCompiler().registerExtension("ltc", Texture::TYPE);
	}


	~TexturePlugin() {
		PluginManager& plugin_manager = m_app.getEngine().getPluginManager();
		auto* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
		if(m_texture_view) {
			renderer->destroy(m_texture_view);
		}
	}

	const char* getDefaultExtension() const override { return "ltc"; }
	const char* getFileDialogFilter() const override { return "Composite texture\0*.ltc\0"; }
	const char* getFileDialogExtensions() const override { return "ltc"; }
	bool canCreateResource() const override { return true; }
	bool createResource(const char* path) override { 
		os::OutputFile file;
		if (!file.open(path)) {
			logError("Failed to create ", path);
			return false;
		}

		file.close();
		return true;
	}

	struct TextureTileJob
	{
		TextureTileJob(StudioApp& app, FileSystem& filesystem, IAllocator& allocator) 
			: m_allocator(allocator) 
			, m_filesystem(filesystem)
			, m_app(app)
		{}

		void execute() {
			IAllocator& allocator = m_allocator;

			const FilePathHash hash(m_in_path);
			StaticString<LUMIX_MAX_PATH> out_path(".lumix/asset_tiles/", hash, ".lbc");
			OutputMemoryStream resized_data(allocator);
			resized_data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
			int image_comp;
			int w, h;
			os::InputFile file;
			if (!file.open(m_in_path)) {
				logError("Failed to load ", m_in_path);
				m_app.getAssetBrowser().copyTile("editor/textures/tile_texture.tga", out_path);
				return;
			}
			Array<u8> tmp(m_allocator);
			tmp.resize((u32)file.size());
			if (!file.read(tmp.begin(), tmp.byte_size())) {
				logError("Failed to load ", m_in_path);
				m_app.getAssetBrowser().copyTile("editor/textures/tile_texture.tga", out_path);
				return;
			}
			file.close();

			auto data = stbi_load_from_memory(tmp.begin(), tmp.byte_size(), &w, &h, &image_comp, 4);
			if (!data)
			{
				logError("Failed to load ", m_in_path);
				m_app.getAssetBrowser().copyTile("editor/textures/tile_texture.tga", out_path);
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

			if (!saveAsLBC(m_out_path, resized_data.data(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, false, true, m_allocator)) {
				logError("Failed to save ", m_out_path);
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
		StaticString<LUMIX_MAX_PATH> m_in_path; 
		StaticString<LUMIX_MAX_PATH> m_out_path;
		TextureTileJob* m_next = nullptr;
	};

	void update() {
		if (!m_tiles_to_create.tail) return;

		TextureTileJob* job = m_tiles_to_create.tail;
		m_tiles_to_create.tail = job->m_next;
		if (!m_tiles_to_create.tail) m_tiles_to_create.head = nullptr;

		// to keep editor responsive, we don't want to create too many tiles per frame 
		jobs::runEx(job, &TextureTileJob::execute, nullptr, jobs::getWorkersCount() - 1);
	}

	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Texture::TYPE && !Path::hasExtension(in_path, "ltc") && !Path::hasExtension(in_path, "raw")) {
			FileSystem& fs = m_app.getEngine().getFileSystem();
			TextureTileJob* job = LUMIX_NEW(m_app.getAllocator(), TextureTileJob)(m_app, fs, m_app.getAllocator());
			job->m_in_path = fs.getBasePath();
			job->m_in_path << in_path;
			job->m_out_path = fs.getBasePath();
			job->m_out_path << out_path;
			if (m_tiles_to_create.head) m_tiles_to_create.head->m_next = job;
			else {
				m_tiles_to_create.tail = job;
			}
			m_tiles_to_create.head = job;
			return true;
		}
		return false;
	}

	bool createComposite(const OutputMemoryStream& src_data, OutputMemoryStream& dst, const Meta& meta, const char* src_path) {
		IAllocator& allocator = m_app.getAllocator();
		CompositeTexture tc(allocator);
		if (!tc.init(Span(src_data.data(), (u32)src_data.size()), src_path)) return false;
		struct Src {
			stbi_uc* data;
			int w, h;
			Path path;
		};

		HashMap<FilePathHash, Src> sources(allocator);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream tmp_src(allocator);
		int w = -1, h = -1;
		bool has_alpha = false;

		auto prepare_source =[&](const CompositeTexture::ChannelSource& ch){
			if (ch.path.isEmpty()) return false;
			if (sources.find(ch.path.getHash()).isValid()) return true;

			tmp_src.clear();
			if (!fs.getContentSync(ch.path, tmp_src)) {
				return false;
			}

			int cmp;
			stbi_uc* data = stbi_load_from_memory(tmp_src.data(), (i32)tmp_src.size(), &w, &h, &cmp, 4);
			if (!data) {
				return false;
			}

			sources.insert(ch.path.getHash(), {data, w, h, ch.path});
			return true;
		};

		bool success = true;
		for (CompositeTexture::Layer& layer : tc.layers) {
			success = success && prepare_source(layer.red);
			success = success && prepare_source(layer.green);
			success = success && prepare_source(layer.blue);
			success = success && prepare_source(layer.alpha);
		}

		if (!success) return false;

		if (tc.layers.empty()) {
			return true;
		}

		if (sources.size() == 0) return false;

		const Src first_src = sources.begin().value(); 
		for (auto iter = sources.begin(); iter.isValid(); ++iter) {
			if (iter.value().w != first_src.w || iter.value().h != first_src.h) {
				logError(src_path, ": ", first_src.path, "(", first_src.w, "x", first_src.h, ") does not match "
					, iter.value().path, "(", iter.value().w, "x", iter.value().h, ")");
				return false;
			}
		}

		TextureCompressor::Input input(w, h, tc.cubemap ? 1 : tc.layers.size(), 1, allocator);
		input.is_normalmap = meta.is_normalmap;
		input.is_srgb = meta.srgb;
		input.is_cubemap = tc.cubemap;

		OutputMemoryStream out_data(allocator);
		out_data.resize(w * h * 4);
		u8* out_data_ptr = out_data.getMutableData();
		for (CompositeTexture::Layer& layer : tc.layers) {
			const u32 idx = u32(&layer - tc.layers.begin());
			for (u32 ch = 0; ch < 4; ++ch) {
				const Path& p = layer.getChannel(ch).path;
				if (p.isEmpty()) continue;
				stbi_uc* from = sources[p.getHash()].data;
				if (!from) continue;
				u32 from_ch = layer.getChannel(ch).src_channel;
				const bool invert = layer.getChannel(ch).invert;

				if (invert) {
					for (u32 j = 0; j < (u32)h; ++j) {
						for (u32 i = 0; i < (u32)w; ++i) {
							out_data_ptr[(j * w + i) * 4 + ch] = 0xff - from[(i + j * w) * 4 + from_ch];
						}
					}
				}
				else {
					for (u32 j = 0; j < (u32)h; ++j) {
						for (u32 i = 0; i < (u32)w; ++i) {
							out_data_ptr[(j * w + i) * 4 + ch] = from[(i + j * w) * 4 + from_ch];
						}
					}
				}

				input.has_alpha = input.has_alpha || ch == 3;
			}

			input.add(out_data, input.is_cubemap ? idx : 0, input.is_cubemap ? 0 : idx, 0);
		}

		for (const Src& i : sources) {
			free(i.data);
		}

		dst.write("lbc", 3);
		u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
		flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
		flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
		flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
		flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
		flags |= meta.filter == Meta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
		dst.write(&flags, sizeof(flags));
		TextureCompressor::Options options;
		options.generate_mipmaps = meta.mips;
		options.stochastic_mipmap = meta.stochastic_mipmap;
		options.scale_coverage_ref = meta.scale_coverage;
		return TextureCompressor::compress(input, options, dst, allocator);
	}

	bool compileImage(const Path& path, const OutputMemoryStream& src_data, OutputMemoryStream& dst, const Meta& meta)
	{
		PROFILE_FUNCTION();
		int w, h, comps;
		const bool is_16_bit = stbi_is_16_bit_from_memory(src_data.data(), (i32)src_data.size());
		if (is_16_bit) logWarning(path, ": 16bit images not yet supported. Converting to 8bit.");

		stbi_uc* data = stbi_load_from_memory(src_data.data(), (i32)src_data.size(), &w, &h, &comps, 4);
		if (!data) return false;

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
			if (!c.init(params)) return false;
			basisu::basis_compressor::error_code err = c.process();
			if (err != basisu::basis_compressor::cECSuccess) return false;

			const basisu::uint8_vec& out = c.get_output_basis_file();
			return dst.write(out.get_ptr(), out.size_in_bytes());
		#else
			dst.write("lbc", 3);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			flags |= meta.filter == Meta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
			dst.write(&flags, sizeof(flags));

			TextureCompressor::Input input(w, h, 1, 1, m_app.getAllocator());
			input.add(Span(data, w * h * 4), 0, 0, 0);
			input.is_srgb = meta.srgb;
			input.is_normalmap = meta.is_normalmap;
			input.has_alpha = comps == 4;
			TextureCompressor::Options options;
			options.generate_mipmaps = meta.mips;
			options.stochastic_mipmap = meta.stochastic_mipmap; 
			options.scale_coverage_ref = meta.scale_coverage;
			options.compress = meta.compress;
			const bool res = TextureCompressor::compress(input, options, dst, m_app.getAllocator());
			stbi_image_free(data);
			return res;
		#endif
	}

	Meta getMeta(const Path& path) const
	{
		Meta meta;
		if (Path::hasExtension(path.c_str(), "raw")) {
			meta.compress = false;
			meta.mips = false;
		}

		m_app.getAssetCompiler().getMeta(path, [&path, &meta](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "srgb", &meta.srgb);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "compress", &meta.compress);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "mip_scale_coverage", &meta.scale_coverage);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "stochastic_mip", &meta.stochastic_mipmap);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "normalmap", &meta.is_normalmap);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "mips", &meta.mips);
			char tmp[32];
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "filter", Span(tmp))) {
				if (equalIStrings(tmp, "point")) {
					meta.filter = Meta::Filter::POINT;
				}
				else if (equalIStrings(tmp, "anisotropic")) {
					meta.filter = Meta::Filter::ANISOTROPIC;
				}
				else {
					meta.filter = Meta::Filter::LINEAR;
				}
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_u", Span(tmp))) {
				meta.wrap_mode_u = equalIStrings(tmp, "repeat") ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_v", Span(tmp))) {
				meta.wrap_mode_v = equalIStrings(tmp, "repeat") ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_w", Span(tmp))) {
				meta.wrap_mode_w = equalIStrings(tmp, "repeat") ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
		});
		return meta;
	}

	bool compile(const Path& src) override
	{
		char ext[5] = {};
		copyString(Span(ext), Path::getExtension(Span(src.c_str(), src.length())));
		makeLowercase(Span(ext), ext);

		FileSystem& fs = m_app.getEngine().getFileSystem();
		OutputMemoryStream src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, src_data)) return false;
		
		OutputMemoryStream out(m_app.getAllocator());
		Meta meta = getMeta(src);
		if (equalStrings(ext, "raw")) {
			if (meta.scale_coverage >= 0) logError(src, ": RAW can not scale coverage");
			if (meta.compress) logError(src, ": RAW can not be copressed");
			if (meta.mips) logError(src, ": RAW can not have mipmaps");
			
			out.write(ext, 3);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			flags |= meta.filter == Meta::Filter::ANISOTROPIC ? (u32)Texture::Flags::ANISOTROPIC : 0;
			out.write(flags);
			out.write(src_data.data(), src_data.size());
		}
		else if(equalStrings(ext, "jpg") || equalStrings(ext, "jpeg") || equalStrings(ext, "png") || equalStrings(ext, "tga")) {
			if (!compileImage(src, src_data, out, meta)) return false;
		}
		else if (equalStrings(ext, "ltc")) {
			if (!createComposite(src_data, out, meta, src.c_str())) return false;
		}
		else {
			ASSERT(false);
		}

		return m_app.getAssetCompiler().writeCompiledResource(src.c_str(), Span(out.data(), (i32)out.size()));
	}

	const char* toString(Meta::Filter filter) {
		switch (filter) {
			case Meta::Filter::POINT: return "point";
			case Meta::Filter::LINEAR: return "linear";
			case Meta::Filter::ANISOTROPIC: return "anisotropic";
			default: ASSERT(false); return "linear";
		}
	}

	const char* toString(Meta::WrapMode wrap) {
		switch (wrap) {
			case Meta::WrapMode::CLAMP: return "clamp";
			case Meta::WrapMode::REPEAT: return "repeat";
			default: ASSERT(false); return "repeat";
		}
	}

	static const char* getCubemapLabel(u32 idx) {
		switch (idx) {
			case 0: return "X+";
			case 1: return "X-";
			case 2: return "Y+ (top)";
			case 3: return "Y- (bottom)";
			case 4: return "Z+";
			case 5: return "Z+";
			default: return "Too many faces in cubemap";
		}
	}

	void compositeGUI(Texture& texture) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (m_composite_tag != &texture) {
			m_composite_tag = &texture;
			IAllocator& allocator = m_app.getAllocator();
			OutputMemoryStream content(allocator);
			if (fs.getContentSync(texture.getPath(), content)) {
				m_composite.init(Span(content.data(), (u32)content.size()), texture.getPath().c_str());
			}
			else {
				logError("Could not load", texture.getPath());
			}
		}

		if (ImGui::CollapsingHeader("Edit")) {
			static bool show_channels = false;
			bool same_channels = true;
			for (CompositeTexture::Layer& layer : m_composite.layers) {
				if (layer.red.path != layer.green.path
					|| layer.red.path != layer.blue.path
					|| layer.red.path != layer.alpha.path) 
				{
					same_channels = false;
					break;
				}
				if (layer.red.src_channel != 0
					|| layer.green.src_channel != 1
					|| layer.blue.src_channel != 2
					|| layer.alpha.src_channel != 3)
				{
					same_channels = false;
					break;
				}
			}

			if (same_channels) {
				ImGui::Checkbox("Show single channels", &show_channels);
			}
			else {
				show_channels = true;
			}
			ImGui::Checkbox("Cubemap", &m_composite.cubemap);

			for (CompositeTexture::Layer& layer : m_composite.layers) {
				const u32 idx = u32(&layer - m_composite.layers.begin());
				bool open;
				if (m_composite.cubemap) open = ImGui::TreeNodeEx(&layer, 0, "%s", getCubemapLabel(idx));
				else open = ImGui::TreeNodeEx(&layer, 0, "%d", idx);
				if (open) {
					if (ImGui::Button("Remove")) {
						m_composite.layers.erase(idx);
						ImGui::TreePop();
						break;
					}

					char tmp[LUMIX_MAX_PATH];
					
					if (show_channels) {
						copyString(Span(tmp), layer.red.path.c_str());
						ImGuiEx::Label("Red");
						if (m_app.getAssetBrowser().resourceInput("r", Span(tmp), Texture::TYPE)) {
							layer.red.path = tmp;
						}
						ImGuiEx::Label("Red source channel");
						ImGui::Combo("##rsrcch", (int*)&layer.red.src_channel, "Red\0Green\0Blue\0Alpha\0");
						ImGuiEx::Label("Invert");
						ImGui::Checkbox("##invr", &layer.red.invert);

						copyString(Span(tmp), layer.green.path.c_str());
						ImGuiEx::Label("Green");
						if (m_app.getAssetBrowser().resourceInput("g", Span(tmp), Texture::TYPE)) {
							layer.green.path = tmp;
						}
						ImGuiEx::Label("Green source channel");
						ImGui::Combo("##gsrcch", (int*)&layer.green.src_channel, "Red\0Green\0Blue\0Alpha\0");
						ImGuiEx::Label("Invert");
						ImGui::Checkbox("##invg", &layer.green.invert);

						copyString(Span(tmp), layer.blue.path.c_str());
						ImGuiEx::Label("Blue");
						if (m_app.getAssetBrowser().resourceInput("b", Span(tmp), Texture::TYPE)) {
							layer.blue.path = tmp;
						}
						
						ImGuiEx::Label("Blue source channel");
						ImGui::Combo("##bsrcch", (int*)&layer.blue.src_channel, "Red\0Green\0Blue\0Alpha\0");
						ImGuiEx::Label("Invert");
						ImGui::Checkbox("##invb", &layer.blue.invert);
				
						copyString(Span(tmp), layer.alpha.path.c_str());
						ImGuiEx::Label("Alpha");
						if (m_app.getAssetBrowser().resourceInput("a", Span(tmp), Texture::TYPE)) {
							layer.alpha.path = tmp;
						}
						ImGuiEx::Label("Alpha source channel");
						ImGui::Combo("##asrch", (int*)&layer.alpha.src_channel, "Red\0Green\0Blue\0Alpha\0");
						ImGuiEx::Label("Invert");
						ImGui::Checkbox("##inva", &layer.alpha.invert);
					}
					else {
						copyString(Span(tmp), layer.red.path.c_str());
						ImGuiEx::Label("Source");
						if (m_app.getAssetBrowser().resourceInput("rgba", Span(tmp), Texture::TYPE)) {
							layer.red.path = tmp;
							layer.green.path = tmp;
							layer.blue.path = tmp;
							layer.alpha.path = tmp;
							layer.red.src_channel = 0;
							layer.green.src_channel = 1;
							layer.blue.src_channel = 2;
							layer.alpha.src_channel = 3;
						}
					}

					ImGui::TreePop();
				}
			}
			if (ImGui::Button("Add layer")) {
				m_composite.layers.emplace();
			}
			if (ImGui::Button(ICON_FA_SAVE "Save")) {
				if (!m_composite.save(fs, texture.getPath())) {
					logError("Failed to save ", texture.getPath());
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "Open externally")) {
				m_app.getAssetBrowser().openInExternalEditor(texture.getPath().c_str());
			}
		}
	}

	void onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return;

		auto* texture = static_cast<Texture*>(resources[0]);

		ImGuiEx::Label("Size");
		ImGui::Text("%dx%d", texture->width, texture->height);
		ImGuiEx::Label("Mips");
		ImGui::Text("%d", texture->mips);
		const char* format = "unknown";
		switch(texture->format) {
			case gpu::TextureFormat::R8: format = "R8"; break;
			case gpu::TextureFormat::RGBA8: format = "RGBA8"; break;
			case gpu::TextureFormat::RGBA16: format = "RGBA16"; break;
			case gpu::TextureFormat::R11G11B10F: format = "R11G11B10F"; break;
			case gpu::TextureFormat::RGBA16F: format = "RGBA16F"; break;
			case gpu::TextureFormat::RGBA32F: format = "RGBA32F"; break;
			case gpu::TextureFormat::R16F: format = "R16F"; break;
			case gpu::TextureFormat::R16: format = "R16"; break;
			case gpu::TextureFormat::R32F: format = "R32F"; break;
			case gpu::TextureFormat::SRGB: format = "SRGB"; break;
			case gpu::TextureFormat::SRGBA: format = "SRGBA"; break;
			default: ASSERT(false); break;
		}
		ImGuiEx::Label("Format");
		ImGui::TextUnformatted(format);
		if (texture->handle) {
			ImVec2 texture_size(200, 200);
			if (texture->width > texture->height) {
				texture_size.y = texture_size.x * texture->height / texture->width;
			}
			else {
				texture_size.x = texture_size.y * texture->width / texture->height;
			}

			if (texture != m_texture) {
				m_texture = texture;
				PluginManager& plugin_manager = m_app.getEngine().getPluginManager();
				auto* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
				renderer->runInRenderThread(this, [](Renderer& r, void* ptr){
					TexturePlugin* p = (TexturePlugin*)ptr;
					if (!p->m_texture_view) {
						p->m_texture_view = gpu::allocTextureHandle();
					}
					gpu::createTextureView(p->m_texture_view, p->m_texture->handle);
				});
			}

			ImGui::Image(m_texture_view, texture_size);

			if (ImGui::Button("Open")) m_app.getAssetBrowser().openInExternalEditor(texture);
		}

		if (Path::hasExtension(texture->getPath().c_str(), "ltc")) compositeGUI(*texture);
		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			
			if (texture->getPath().getHash() != m_meta_res) {
				m_meta = getMeta(texture->getPath());
				m_meta_res = texture->getPath().getHash();
			}
			
			ImGuiEx::Label("SRGB");
			ImGui::Checkbox("##srgb", &m_meta.srgb);
			ImGuiEx::Label("Mipmaps");
			ImGui::Checkbox("##mip", &m_meta.mips);
			if (m_meta.mips) {
				ImGuiEx::Label("Stochastic mipmap");
				ImGui::Checkbox("##stomip", &m_meta.stochastic_mipmap);
			}

			ImGuiEx::Label("Compress");
			ImGui::Checkbox("##cmprs", &m_meta.compress);
			
			if (m_meta.compress && (texture->width % 4 != 0 || texture->height % 4 != 0)) {
				ImGui::TextUnformatted(ICON_FA_EXCLAMATION_TRIANGLE " Block compression will not be used because texture size is not multiple of 4");
			}

			bool scale_coverage = m_meta.scale_coverage >= 0;
			ImGuiEx::Label("Mipmap scale coverage");
			if (ImGui::Checkbox("##mmapsccov", &scale_coverage)) {
				m_meta.scale_coverage *= -1;
			}
			if (m_meta.scale_coverage >= 0) {
				ImGuiEx::Label("Coverage alpha ref");
				ImGui::SliderFloat("##covaref", &m_meta.scale_coverage, 0, 1);
			}
			ImGuiEx::Label("Is normalmap");
			ImGui::Checkbox("##nrmmap", &m_meta.is_normalmap);

			ImGuiEx::Label("U Wrap mode");
			ImGui::Combo("##uwrp", (int*)&m_meta.wrap_mode_u, "Repeat\0Clamp\0");
			ImGuiEx::Label("V Wrap mode");
			ImGui::Combo("##vwrp", (int*)&m_meta.wrap_mode_v, "Repeat\0Clamp\0");
			ImGuiEx::Label("W Wrap mode");
			ImGui::Combo("##wwrp", (int*)&m_meta.wrap_mode_w, "Repeat\0Clamp\0");
			ImGuiEx::Label("Filter");
			ImGui::Combo("Filter", (int*)&m_meta.filter, "Linear\0Point\0Anisotropic\0");

			if (ImGui::Button(ICON_FA_CHECK "Apply")) {
				const StaticString<512> src("srgb = ", m_meta.srgb ? "true" : "false"
					, "\ncompress = ", m_meta.compress ? "true" : "false"
					, "\nstochastic_mip = ", m_meta.stochastic_mipmap ? "true" : "false"
					, "\nmip_scale_coverage = ", m_meta.scale_coverage
					, "\nmips = ", m_meta.mips ? "true" : "false"
					, "\nnormalmap = ", m_meta.is_normalmap ? "true" : "false"
					, "\nwrap_mode_u = \"", toString(m_meta.wrap_mode_u), "\""
					, "\nwrap_mode_v = \"", toString(m_meta.wrap_mode_v), "\""
					, "\nwrap_mode_w = \"", toString(m_meta.wrap_mode_w), "\""
					, "\nfilter = \"", toString(m_meta.filter), "\""
				);
				compiler.updateMeta(texture->getPath(), src);
			}
		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Texture"; }
	ResourceType getResourceType() const override { return Texture::TYPE; }

	StudioApp& m_app;
	struct {
		TextureTileJob* head = nullptr;
		TextureTileJob* tail = nullptr;
	} m_tiles_to_create;
	Texture* m_texture;
	gpu::TextureHandle m_texture_view = gpu::INVALID_TEXTURE;
	Meta m_meta;
	FilePathHash m_meta_res;
	CompositeTexture m_composite;
	void* m_composite_tag = nullptr;
};

struct ModelPropertiesPlugin final : PropertyGrid::IPlugin {
	ModelPropertiesPlugin(StudioApp& app) : m_app(app) {}
	
	void update() {}
	
	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) {
		if (cmp.type != MODEL_INSTANCE_TYPE) return;

		RenderScene* scene = (RenderScene*)cmp.scene;
		EntityRef entity = (EntityRef)cmp.entity;
		Model* model = scene->getModelInstanceModel(entity);
		if (!model || !model->isReady()) return;

		const i32 count = model->getMeshCount();
		if (count == 1) {
			ImGuiEx::Label("Material");
			char mat_path[LUMIX_MAX_PATH];
			Path path = scene->getModelInstanceMaterialOverride(entity);
			if (path.isEmpty()) {
				path = model->getMesh(0).material->getPath();
			}
			copyString(mat_path, path.c_str());
			if (m_app.getAssetBrowser().resourceInput("##mat", Span(mat_path), Material::TYPE)) {
				path = mat_path;
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
					m_app.getAssetBrowser().selectResource(material->getPath(), true, false);
				}
				ImGui::PopID();
			}
			if(count > 1) ImGui::TreePop();
		}
	}

	StudioApp& m_app;
};

struct ModelPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	struct Meta {
		Meta(IAllocator& allocator) : clips(allocator) {}

		float scale = 1.f;
		float culling_scale = 1.f;
		bool split = false;
		bool create_impostor = false;
		bool bake_impostor_normals = false;
		bool use_mikktspace = false;
		bool force_skin = false;
		bool import_vertex_colors = false;
		bool vertex_color_is_ao = false;
		u8 autolod_mask = 0;
		u32 lod_count = 1;
		float autolod_coefs[4] = { 0.75f, 0.5f, 0.25f, 0.125f };
		float lods_distances[4] = { 10'000, 0, 0, 0 };
		FBXImporter::ImportConfig::Origin origin = FBXImporter::ImportConfig::Origin::SOURCE;
		FBXImporter::ImportConfig::Physics physics = FBXImporter::ImportConfig::Physics::NONE;
		Array<FBXImporter::ImportConfig::Clip> clips;
	};

	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
		, m_mesh(INVALID_ENTITY)
		, m_universe(nullptr)
		, m_is_mouse_captured(false)
		, m_tile(app.getAllocator())
		, m_fbx_importer(app)
		, m_meta(app.getAllocator())
	{
		app.getAssetCompiler().registerExtension("fbx", Model::TYPE);
	}


	~ModelPlugin()
	{
		jobs::wait(&m_subres_signal);
		auto& engine = m_app.getEngine();
		engine.destroyUniverse(*m_universe);
		m_pipeline.reset();
		engine.destroyUniverse(*m_tile.universe);
		m_tile.pipeline.reset();
	}


	void init() {
		createPreviewUniverse();
		createTileUniverse();
		m_viewport.is_ortho = true;
		m_viewport.near = 0.f;
		m_viewport.far = 1000.f;
		m_fbx_importer.init();
	}


	Meta getMeta(const Path& path) const
	{
		Meta meta(m_app.getAllocator());
		m_app.getAssetCompiler().getMeta(path, [&](lua_State* L){
			LuaWrapper::DebugGuard guard(L);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "use_mikktspace", &meta.use_mikktspace);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "force_skin", &meta.force_skin);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "scale", &meta.scale);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "culling_scale", &meta.culling_scale);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "split", &meta.split);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "bake_impostor_normals", &meta.bake_impostor_normals);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "create_impostor", &meta.create_impostor);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "import_vertex_colors", &meta.import_vertex_colors);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "vertex_color_is_ao", &meta.vertex_color_is_ao);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "lod_count", &meta.lod_count);
			
			if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod0", &meta.autolod_coefs[0])) meta.autolod_mask |= 1;
			if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod1", &meta.autolod_coefs[1])) meta.autolod_mask |= 2;
			if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod2", &meta.autolod_coefs[2])) meta.autolod_mask |= 4;
			if (LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "autolod3", &meta.autolod_coefs[3])) meta.autolod_mask |= 8;

			if (LuaWrapper::getField(L, LUA_GLOBALSINDEX, "bake_vertex_ao") != LUA_TNIL) logWarning(path, ": `bake_vertex_ao` deprecated");
			if (LuaWrapper::getField(L, LUA_GLOBALSINDEX, "position_error") != LUA_TNIL) logWarning(path, ": `position_error` deprecated");
			if (LuaWrapper::getField(L, LUA_GLOBALSINDEX, "rotation_error") != LUA_TNIL) logWarning(path, ": `rotation_error` deprecated");
			if (LuaWrapper::getField(L, LUA_GLOBALSINDEX, "clips") == LUA_TTABLE) {
				const size_t count = lua_objlen(L, -1);
				for (int i = 0; i < count; ++i) {
					lua_rawgeti(L, -1, i + 1);
					if (lua_istable(L, -1)) {
						FBXImporter::ImportConfig::Clip& clip = meta.clips.emplace();
						char name[128];
						if (!LuaWrapper::checkStringField(L, -1, "name", Span(name)) 
							|| !LuaWrapper::checkField(L, -1, "from_frame", &clip.from_frame)
							|| !LuaWrapper::checkField(L, -1, "to_frame", &clip.to_frame))
						{
							logError(path, ": clip ", i, " is invalid");
							meta.clips.pop();
							continue;
						}
						clip.name = name;
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 4);

			char tmp[64];
			if (LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "physics", Span(tmp))) {
				if (equalIStrings(tmp, "trimesh")) meta.physics = FBXImporter::ImportConfig::Physics::TRIMESH;
				else if (equalIStrings(tmp, "convex")) meta.physics = FBXImporter::ImportConfig::Physics::CONVEX;
				else meta.physics = FBXImporter::ImportConfig::Physics::NONE;
			}

			for (u32 i = 0; i < lengthOf(meta.lods_distances); ++i) {
				LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, StaticString<32>("lod", i, "_distance"), &meta.lods_distances[i]);
			}
		});
		return meta;
	}

	void addSubresources(AssetCompiler& compiler, const char* _path) override {
		compiler.addResource(Model::TYPE, _path);

		Meta meta = getMeta(Path(_path));
		StaticString<LUMIX_MAX_PATH> pathstr = _path;
		jobs::runLambda([this, pathstr, meta = static_cast<Meta&&>(meta)]() {
			FBXImporter importer(m_app);
			AssetCompiler& compiler = m_app.getAssetCompiler();

			const char* path = pathstr[0] == '/' ? pathstr.data + 1 : pathstr.data;
			importer.setSource(path, true, false);

			if(meta.split) {
				const Array<FBXImporter::ImportMesh>& meshes = importer.getMeshes();
				for (int i = 0; i < meshes.size(); ++i) {
					char mesh_name[256];
					importer.getImportMeshName(meshes[i], mesh_name);
					StaticString<LUMIX_MAX_PATH> tmp(mesh_name, ".fbx:", path);
					compiler.addResource(Model::TYPE, tmp);
				}
			}

			if (meta.physics != FBXImporter::ImportConfig::Physics::NONE) {
				StaticString<LUMIX_MAX_PATH> tmp(".phy:", path);
				ResourceType physics_geom("physics_geometry");
				compiler.addResource(physics_geom, tmp);
			}

			if (meta.clips.empty()) {
				const Array<FBXImporter::ImportAnimation>& animations = importer.getAnimations();
				for (const FBXImporter::ImportAnimation& anim : animations) {
					StaticString<LUMIX_MAX_PATH> tmp(anim.name, ".ani:", path);
					compiler.addResource(ResourceType("animation"), tmp);
				}
			}
			else {
				for (const FBXImporter::ImportConfig::Clip& clip : meta.clips) {
					StaticString<LUMIX_MAX_PATH> tmp(clip.name, ".ani:", path);
					compiler.addResource(ResourceType("animation"), tmp);
				}
			}

		}, &m_subres_signal, 2);			
	}

	static const char* getResourceFilePath(const char* str)
	{
		const char* c = str;
		while (*c && *c != ':') ++c;
		return *c != ':' ? str : c + 1;
	}

	bool compile(const Path& src) override
	{
		ASSERT(Path::hasExtension(src.c_str(), "fbx"));
		const char* filepath = getResourceFilePath(src.c_str());
		FBXImporter::ImportConfig cfg;
		const Meta meta = getMeta(Path(filepath));
		cfg.autolod_mask = meta.autolod_mask;
		memcpy(cfg.autolod_coefs, meta.autolod_coefs, sizeof(meta.autolod_coefs));
		cfg.mikktspace_tangents = meta.use_mikktspace;
		cfg.mesh_scale = meta.scale;
		cfg.bounding_scale = meta.culling_scale;
		cfg.physics = meta.physics;
		cfg.import_vertex_colors = meta.import_vertex_colors;
		cfg.vertex_color_is_ao = meta.vertex_color_is_ao;
		cfg.lod_count = meta.lod_count;
		memcpy(cfg.lods_distances, meta.lods_distances, sizeof(meta.lods_distances));
		cfg.create_impostor = meta.create_impostor;
		cfg.clips = meta.clips;
		const PathInfo src_info(filepath);
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

		const StaticString<32> hash_str("", src.getHash());
		if (meta.split) {
			cfg.origin = FBXImporter::ImportConfig::Origin::CENTER;
			m_fbx_importer.writeSubmodels(filepath, cfg);
			m_fbx_importer.writePrefab(filepath, cfg);
		}
		cfg.origin = FBXImporter::ImportConfig::Origin::SOURCE;
		m_fbx_importer.writeModel(src.c_str(), cfg);
		m_fbx_importer.writeMaterials(filepath, cfg);
		m_fbx_importer.writeAnimations(filepath, cfg);
		m_fbx_importer.writePhysics(filepath, cfg);
		return true;
	}


	void createTileUniverse()
	{
		Engine& engine = m_app.getEngine();
		m_tile.universe = &engine.createUniverse(false);
		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_tile.pipeline = Pipeline::create(*renderer, pres, "PREVIEW", engine.getAllocator());

		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		const EntityRef env_probe = m_tile.universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_tile.universe->createComponent(ENVIRONMENT_PROBE_TYPE, env_probe);
		render_scene->getEnvironmentProbe(env_probe).outer_range = Vec3(1e3);
		render_scene->getEnvironmentProbe(env_probe).inner_range = Vec3(1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_tile.universe->createEntity({10, 10, 10}, mtx.getRotation());
		m_tile.universe->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_scene->getEnvironment(light_entity).direct_intensity = 5;
		render_scene->getEnvironment(light_entity).indirect_intensity = 1;
		
		m_tile.pipeline->setUniverse(m_tile.universe);
	}


	void createPreviewUniverse()
	{
		Engine& engine = m_app.getEngine();
		m_universe = &engine.createUniverse(false);
		auto* renderer = static_cast<Renderer*>(engine.getPluginManager().getPlugin("renderer"));
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PREVIEW",  engine.getAllocator());

		const EntityRef mesh_entity = m_universe->createEntity({0, 0, 0}, {0, 0, 0, 1});
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		m_mesh = mesh_entity;
		m_universe->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		const EntityRef env_probe = m_universe->createEntity({0, 0, 0}, Quat::IDENTITY);
		m_universe->createComponent(ENVIRONMENT_PROBE_TYPE, env_probe);
		render_scene->getEnvironmentProbe(env_probe).inner_range = Vec3(1e3);
		render_scene->getEnvironmentProbe(env_probe).outer_range = Vec3(1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_universe->createEntity({0, 0, 0}, mtx.getRotation());
		m_universe->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_scene->getEnvironment(light_entity).direct_intensity = 5;
		render_scene->getEnvironment(light_entity).indirect_intensity = 1;

		m_pipeline->setUniverse(m_universe);
	}


	void showPreview(Model& model)
	{
		auto* render_scene = static_cast<RenderScene*>(m_universe->getScene(MODEL_INSTANCE_TYPE));
		if (!render_scene) return;
		if (!model.isReady()) return;
		if (!m_mesh.isValid()) return;

		if (render_scene->getModelInstanceModel((EntityRef)m_mesh) != &model)
		{
			render_scene->setModelInstancePath((EntityRef)m_mesh, model.getPath());
			AABB aabb = model.getAABB();

			const Vec3 center = (aabb.max + aabb.min) * 0.5f;
			m_viewport.pos = DVec3(0) + center + Vec3(1, 1, 1) * length(aabb.max - aabb.min);
			
			Matrix mtx;
			Vec3 eye = center + Vec3(model.getCenterBoundingRadius() * 2);
			mtx.lookAt(eye, center, normalize(Vec3(1, -1, 1)));
			mtx = mtx.inverted();

			m_viewport.rot = mtx.getRotation();
		}
		render_scene->setModelInstanceLOD((EntityRef)m_mesh, 0);
		ImVec2 image_size(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x);

		m_viewport.w = (int)image_size.x;
		m_viewport.h = (int)image_size.y;
		m_viewport.ortho_size = model.getCenterBoundingRadius();
		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_preview = m_pipeline->getOutput();
		if (gpu::isOriginBottomLeft()) {
			ImGui::Image(m_preview, image_size);
		}
		else {
			ImGui::Image(m_preview, image_size, ImVec2(0, 1), ImVec2(1, 0));
		}
		
		bool mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1);
		if (m_is_mouse_captured && !mouse_down)
		{
			m_is_mouse_captured = false;
			os::showCursor(true);
			os::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
		}

		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("PreviewPopup");

		if (ImGui::BeginPopup("PreviewPopup"))
		{
			if (ImGui::Selectable("Save preview"))
			{
				model.incRefCount();
				renderTile(&model, &m_viewport.pos, &m_viewport.rot);
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemHovered() && mouse_down)
		{
			Vec2 delta(0, 0);
			const os::Event* events = m_app.getEvents();
			for (int i = 0, c = m_app.getEventsCount(); i < c; ++i) {
				const os::Event& e = events[i];
				if (e.type == os::Event::Type::MOUSE_MOVE) {
					delta += Vec2((float)e.mouse_move.xrel, (float)e.mouse_move.yrel);
				}
			}

			if (!m_is_mouse_captured)
			{
				m_is_mouse_captured = true;
				os::showCursor(false);
				const os::Point p = os::getMouseScreenPos();
				m_captured_mouse_x = p.x;
				m_captured_mouse_y = p.y;
			}

			if (delta.x != 0 || delta.y != 0)
			{
				const Vec2 MOUSE_SENSITIVITY(50, 50);
				DVec3 pos = m_viewport.pos;
				Quat rot = m_viewport.rot;

				float yaw = -signum(delta.x) * (powf(fabsf((float)delta.x / MOUSE_SENSITIVITY.x), 1.2f));
				Quat yaw_rot(Vec3(0, 1, 0), yaw);
				rot = normalize(yaw_rot * rot);

				Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
				float pitch =
					-signum(delta.y) * (powf(fabsf((float)delta.y / MOUSE_SENSITIVITY.y), 1.2f));
				Quat pitch_rot(pitch_axis, pitch);
				rot = normalize(pitch_rot * rot);

				Vec3 dir = rot.rotate(Vec3(0, 0, 1));
				Vec3 origin = (model.getAABB().max + model.getAABB().min) * 0.5f;

				float dist = length(origin - Vec3(pos));
				pos = DVec3(0) + origin + dir * dist;

				m_viewport.rot = rot;
				m_viewport.pos = pos;
			}
		}
	}


	static void postprocessImpostor(Array<u32>& gb0, Array<u32>& gb1, const IVec2& tile_size, IAllocator& allocator) {
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
		}
		else {
			// nothing was rendered
			memset(gb0.begin(), 0xff, gb0.byte_size());
			memset(gb1.begin(), 0xff, gb1.byte_size());
		}
	}

	static const char* toString(FBXImporter::ImportConfig::Physics value) {
		switch (value) {
			case FBXImporter::ImportConfig::Physics::TRIMESH: return "Triangle mesh";
			case FBXImporter::ImportConfig::Physics::CONVEX: return "Convex";
			case FBXImporter::ImportConfig::Physics::NONE: return "None";
			default: ASSERT(false); return "none";
		}
	}

	void onGUI(Span<Resource*> resources) override
	{
		if (resources.length() > 1) return;

		auto* model = static_cast<Model*>(resources[0]);

		if (model->isReady()) {
			ImGuiEx::Label("Bounding radius (from origin)");
			ImGui::Text("%f", model->getOriginBoundingRadius());
			ImGuiEx::Label("Bounding radius (from center)");
			ImGui::Text("%f", model->getCenterBoundingRadius());

			if (ImGui::CollapsingHeader("LODs")) {
				auto* lods = model->getLODIndices();
				auto* distances = model->getLODDistances();
				if (lods[0].to >= 0 && !model->isFailure())
				{
					ImGui::Separator();
					ImGui::Columns(4);
					ImGui::Text("LOD");
					ImGui::NextColumn();
					ImGui::Text("Distance");
					ImGui::NextColumn();
					ImGui::Text("# of meshes");
					ImGui::NextColumn();
					ImGui::Text("# of triangles");
					ImGui::NextColumn();
					ImGui::Separator();
					int lod_count = 1;
					for (int i = 0; i < Model::MAX_LOD_COUNT && lods[i].to >= 0; ++i)
					{
						ImGui::PushID(i);
						ImGui::Text("%d", i);
						ImGui::NextColumn();
						float dist = sqrtf(distances[i]);
						if (ImGui::DragFloat("", &dist)) {
							distances[i] = dist * dist;
						}
						ImGui::NextColumn();
						ImGui::Text("%d", lods[i].to - lods[i].from + 1);
						ImGui::NextColumn();
						int tri_count = 0;
						for (int j = lods[i].from; j <= lods[i].to; ++j)
						{
							i32 indices_count = (i32)model->getMesh(j).indices.size() >> 1;
							if (!model->getMesh(j).flags.isSet(Mesh::Flags::INDICES_16_BIT)) {
								indices_count >>= 1;
							}
							tri_count += indices_count / 3;

						}

						ImGui::Text("%d", tri_count);
						ImGui::NextColumn();
						++lod_count;
						ImGui::PopID();
					}

					ImGui::Columns(1);
				}
			}
		}

		if (ImGui::CollapsingHeader("Meshes")) {
			const float go_to_w = ImGui::CalcTextSize(ICON_FA_BULLSEYE).x;
			for (int i = 0; i < model->getMeshCount(); ++i)
			{
				auto& mesh = model->getMesh(i);
				if (ImGui::TreeNode(&mesh, "%s", mesh.name.length() > 0 ? mesh.name.c_str() : "N/A"))
				{
					ImGuiEx::Label("Triangle count");
					ImGui::Text("%d", ((i32)mesh.indices.size() >> (mesh.areIndices16() ? 1 : 2))/ 3);
					ImGuiEx::Label("Material");
					const float w = ImGui::GetContentRegionAvail().x - go_to_w;
					ImGuiEx::TextClipped(mesh.material->getPath().c_str(), w);
					ImGui::SameLine();
					if (ImGuiEx::IconButton(ICON_FA_BULLSEYE, "Go to"))
					{
						m_app.getAssetBrowser().selectResource(mesh.material->getPath(), true, false);
					}
					ImGui::TreePop();
				}
			}
		}

		if (model->isReady() && ImGui::CollapsingHeader("Bones")) {
			ImGuiEx::Label("Count");
			ImGui::Text("%d", model->getBoneCount());
			if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones")) {
				ImGui::Columns(4);
				for (int i = 0; i < model->getBoneCount(); ++i)
				{
					ImGui::Text("%s", model->getBone(i).name.c_str());
					ImGui::NextColumn();
					Vec3 pos = model->getBone(i).transform.pos;
					ImGui::Text("%f; %f; %f", pos.x, pos.y, pos.z);
					ImGui::NextColumn();
					Quat rot = model->getBone(i).transform.rot;
					ImGui::Text("%f; %f; %f; %f", rot.x, rot.y, rot.z, rot.w);
					ImGui::NextColumn();
					const i32 parent = model->getBone(i).parent_idx;
					if (parent >= 0) {
						ImGui::Text("%s", model->getBone(parent).name.c_str());
					}
					ImGui::NextColumn();
				}
			}
		}

		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			if(m_meta_res != model->getPath().getHash()) {
				m_meta = getMeta(model->getPath());
				m_meta_res = model->getPath().getHash();
			}
			ImGuiEx::Label("Mikktspace tangents");
			ImGui::Checkbox("##mikktspace", &m_meta.use_mikktspace);
			ImGuiEx::Label("Force skinned");
			ImGui::Checkbox("##frcskn", &m_meta.force_skin);
			ImGuiEx::Label("Scale");
			ImGui::InputFloat("##scale", &m_meta.scale);
			ImGuiEx::Label("Culling scale");
			ImGui::Text("(?)");
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("%s", "Use this for animated meshes if they are culled when still visible.");
			}
			ImGui::SameLine();
			ImGui::InputFloat("##cull_scale", &m_meta.culling_scale);
			ImGuiEx::Label("Split");
			ImGui::Checkbox("##split", &m_meta.split);
			ImGuiEx::Label("Create impostor mesh");
			ImGui::Checkbox("##creimp", &m_meta.create_impostor);
			if (m_meta.create_impostor) {
				ImGuiEx::Label("Bake impostor normals");
				ImGui::Checkbox("##impnrm", &m_meta.bake_impostor_normals);
			}
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
			}
			ImGuiEx::Label("Physics");
			if (ImGui::BeginCombo("##phys", toString(m_meta.physics))) {
				if (ImGui::Selectable("None")) m_meta.physics = FBXImporter::ImportConfig::Physics::NONE;
				if (ImGui::Selectable("Convex")) m_meta.physics = FBXImporter::ImportConfig::Physics::CONVEX;
				if (ImGui::Selectable("Triangle mesh")) m_meta.physics = FBXImporter::ImportConfig::Physics::TRIMESH;
				ImGui::EndCombo();
			}

			ImGuiEx::Label("LOD count");
			if (ImGui::SliderInt("##lodcount", (i32*)&m_meta.lod_count, 1, 4)) {
				m_meta.lods_distances[1] = maximum(m_meta.lods_distances[0] + 0.01f, m_meta.lods_distances[1]);
				m_meta.lods_distances[2] = maximum(m_meta.lods_distances[1] + 0.01f, m_meta.lods_distances[2]);
				m_meta.lods_distances[3] = maximum(m_meta.lods_distances[2] + 0.01f, m_meta.lods_distances[3]);
			}

			ImGui::NewLine();
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
					}

					ImGui::TableNextColumn();
					bool autolod = m_meta.autolod_mask & (1 << i);
					if (!m_meta.create_impostor || i < m_meta.lod_count - 1) {
						ImGui::SetNextItemWidth(-1);
						if (ImGui::Checkbox("##auto_lod", &autolod)) {
							m_meta.autolod_mask &= ~(1 << i);
							if (autolod) m_meta.autolod_mask |= 1 << i;
						}
					}

					ImGui::TableNextColumn();
					if ((!m_meta.create_impostor || i < m_meta.lod_count - 1) && autolod) {
						ImGui::SetNextItemWidth(-1);
						float f = m_meta.autolod_coefs[i] * 100;
						if (ImGui::DragFloat("##lodcoef", &f, 1, 0, 100, "%.1f", ImGuiSliderFlags_AlwaysClamp)) {
							m_meta.autolod_coefs[i] = f * 0.01f;
						}
					}
					
					ImGui::PopID();
				}

				ImGui::EndTable();
			}

			ImGui::NewLine();
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
					ImGui::InputText("##name", clip.name.data, sizeof(clip.name.data));
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##from", (i32*)&clip.from_frame);
					ImGui::TableNextColumn();
					ImGui::SetNextItemWidth(-1);
					ImGui::InputInt("##to", (i32*)&clip.to_frame);
					ImGui::TableNextColumn();
					if (ImGuiEx::IconButton(ICON_FA_TRASH, "Delete")) {
						m_meta.clips.erase(u32(&clip - m_meta.clips.begin()));
						ImGui::PopID();
						break;
					}
					ImGui::PopID();
				}

				ImGui::EndTable();
			}
			if (ImGui::Button(ICON_FA_PLUS " Add clip")) m_meta.clips.emplace();

			if (ImGui::Button(ICON_FA_CHECK "Apply")) {
				String src(m_app.getAllocator());
				src.cat("create_impostor = ").cat(m_meta.create_impostor ? "true" : "false")
					.cat("\nbake_impostor_normals = ").cat(m_meta.bake_impostor_normals ? "true" : "false")
					.cat("\nuse_mikktspace = ").cat(m_meta.use_mikktspace ? "true" : "false")
					.cat("\nforce_skin = ").cat(m_meta.force_skin ? "true" : "false")
					.cat("\nphysics = \"").cat(toString(m_meta.physics)).cat("\"")
					.cat("\nscale = ").cat(m_meta.scale)
					.cat("\nlod_count = ").cat(m_meta.lod_count)
					.cat("\nculling_scale = ").cat(m_meta.culling_scale)
					.cat("\nsplit = ").cat(m_meta.split ? "true" : "false")
					.cat("\nimport_vertex_colors = ").cat(m_meta.import_vertex_colors ? "true" : "false")
					.cat("\nvertex_color_is_ao = ").cat(m_meta.vertex_color_is_ao ? "true" : "false");

				if (!m_meta.clips.empty()) {
					src.cat("\nclips = {");
					for (const FBXImporter::ImportConfig::Clip& clip : m_meta.clips) {
						src.cat("\n\n{");
						src.cat("\n\n\nname = \"").cat(clip.name.data).cat("\",");
						src.cat("\n\n\nfrom_frame = ").cat(clip.from_frame).cat(",");
						src.cat("\n\n\nto_frame = ").cat(clip.to_frame);
						src.cat("\n\n},");
					}
					src.cat("\n}");
				}

				if (m_meta.autolod_mask & 1) src.cat("\nautolod0 = ").cat(m_meta.autolod_coefs[0]);
				if (m_meta.autolod_mask & 2) src.cat("\nautolod1 = ").cat(m_meta.autolod_coefs[1]);
				if (m_meta.autolod_mask & 4) src.cat("\nautolod2 = ").cat(m_meta.autolod_coefs[2]);
				if (m_meta.autolod_mask & 4) src.cat("\nautolod3 = ").cat(m_meta.autolod_coefs[3]);

				for (u32 i = 0; i < lengthOf(m_meta.lods_distances); ++i) {
					if (m_meta.lods_distances[i] > 0) {
						src.cat("\nlod").cat(i).cat("_distance").cat(" = ").cat(m_meta.lods_distances[i]);
					}
				}

				compiler.updateMeta(model->getPath(), src.c_str());
			}
			ImGui::SameLine();
			if (ImGui::Button("Create impostor texture")) {
				FBXImporter importer(m_app);
				importer.init();
				IAllocator& allocator = m_app.getAllocator();
				Array<u32> gb0(allocator); 
				Array<u32> gb1(allocator); 
				Array<u32> shadow(allocator); 
				IVec2 tile_size;
				importer.createImpostorTextures(model, gb0, gb1, shadow, tile_size, m_meta.bake_impostor_normals);
				postprocessImpostor(gb0, gb1, tile_size, allocator);
				const PathInfo fi(model->getPath().c_str());
				StaticString<LUMIX_MAX_PATH> img_path(fi.m_dir, fi.m_basename, "_impostor0.tga");
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

				if (m_meta.bake_impostor_normals) {
					img_path = fi.m_dir;
					img_path << fi.m_basename << "_impostor1.tga";
					if (fs.open(img_path, file)) {
						Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)gb1.begin(), gpu::isOriginBottomLeft(), Path(img_path), allocator);
						file.close();
					}
					else {
						logError("Failed to open ", img_path);
					}
				}

				img_path = fi.m_dir;
				img_path << fi.m_basename << "_impostor2.tga";
				if (fs.open(img_path, file)) {
					Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)shadow.begin(), gpu::isOriginBottomLeft(), Path(img_path), allocator);
					file.close();
				}
				else {
					logError("Failed to open ", img_path);
				}
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(?)");
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("%s", "To use impostors, check `Create impostor mesh` and press this button. "
				"When the mesh changes, you need to regenerate the impostor texture by pressing this button again.");
			}

		showPreview(*model);
	}

	Meta m_meta;
	FilePathHash m_meta_res;

	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Model"; }
	ResourceType getResourceType() const override { return Model::TYPE; }


	void pushTileQueue(const Path& path)
	{
		ASSERT(!m_tile.queue.full());
		Engine& engine = m_app.getEngine();
		ResourceManagerHub& resource_manager = engine.getResourceManager();

		Resource* resource;
		if (Path::hasExtension(path.c_str(), "fab")) {
			resource = resource_manager.load<PrefabResource>(path);
		}
		else if (Path::hasExtension(path.c_str(), "mat")) {
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
	
	static void destroyEntityRecursive(Universe& universe, EntityPtr entity)
	{
		if (!entity.isValid()) return;
			
		EntityRef e = (EntityRef)entity;
		destroyEntityRecursive(universe, universe.getFirstChild(e));
		destroyEntityRecursive(universe, universe.getNextSibling(e));

		universe.destroyEntity(e);
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
				destroyEntityRecursive(*m_tile.universe, (EntityRef)m_tile.entity);
				Engine& engine = m_app.getEngine();
				FileSystem& fs = engine.getFileSystem();
				StaticString<LUMIX_MAX_PATH> path(fs.getBasePath(), ".lumix/asset_tiles/", m_tile.path_hash, ".lbc");

				if (!gpu::isOriginBottomLeft()) {
					u32* p = (u32*)m_tile.data.getMutableData();
					for (u32 y = 0; y < AssetBrowser::TILE_SIZE >> 1; ++y) {
						for (u32 x = 0; x < AssetBrowser::TILE_SIZE; ++x) {
							swap(p[x + y * AssetBrowser::TILE_SIZE], p[x + (AssetBrowser::TILE_SIZE - y - 1) * AssetBrowser::TILE_SIZE]);
						}
					}
				}

				saveAsLBC(path, m_tile.data.data(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, false, gpu::isOriginBottomLeft(), m_app.getAllocator());
				memset(m_tile.data.getMutableData(), 0, m_tile.data.size());
				Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
				renderer->destroy(m_tile.texture);
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
				StaticString<LUMIX_MAX_PATH> out_path(".lumix/asset_tiles/", resource->getPath().getHash(), ".lbc");
				m_app.getAssetBrowser().copyTile("editor/textures/tile_animation.tga", out_path);
				m_app.getAssetBrowser().reloadTile(m_tile.path_hash);
			}
			popTileQueue();
			return;
		}
		if (!resource->isReady()) return;

		popTileQueue();

		if (resource->getType() == Model::TYPE) {
			renderTile((Model*)resource, nullptr, nullptr);
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


	void renderTile(PrefabResource* prefab)
	{
		Engine& engine = m_app.getEngine();
		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		EntityMap entity_map(m_app.getAllocator());
		if (!engine.instantiatePrefab(*m_tile.universe, *prefab, DVec3(0), Quat::IDENTITY, 1, entity_map)) return;
		if (entity_map.m_map.empty() || !entity_map.m_map[0].isValid()) return;

		m_tile.path_hash = prefab->getPath().getHash();
		prefab->decRefCount();
		m_tile.entity = entity_map.m_map[0];
		m_tile.waiting = true;
	}


	void renderPrefabSecondStage()
	{
		Engine& engine = m_app.getEngine();

		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		AABB aabb({0, 0, 0}, {0, 0, 0});

		float radius = 1;
		Universe& universe = *m_tile.universe;
		for (EntityPtr e = universe.getFirstEntity(); e.isValid(); e = universe.getNextEntity((EntityRef)e)) {
			EntityRef ent = (EntityRef)e;
			const DVec3 pos = universe.getPosition(ent);
			aabb.addPoint(Vec3(pos));
			if (universe.hasComponent(ent, MODEL_INSTANCE_TYPE)) {
				RenderScene* scene = (RenderScene*)universe.getScene(MODEL_INSTANCE_TYPE);
				Model* model = scene->getModelInstanceModel(ent);
				scene->setModelInstanceLOD(ent, 0);
				if (model->isReady()) {
					const Transform tr = universe.getTransform(ent);
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

		Renderer::MemRef mem;
		m_tile.texture = renderer->createTexture(AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, mem, "tile_final");
		gpu::TextureHandle tile_tmp = renderer->createTexture(AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, mem, "tile_tmp");
		renderer->copy(tile_tmp, m_tile.pipeline->getOutput());
		renderer->downscale(tile_tmp, AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, m_tile.texture, AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);

		renderer->getTextureImage(m_tile.texture
			, AssetBrowser::TILE_SIZE
			, AssetBrowser::TILE_SIZE
			, gpu::TextureFormat::RGBA8
			, Span(m_tile.data.getMutableData(), (u32)m_tile.data.size()));
		renderer->destroy(tile_tmp);

		m_tile.frame_countdown = 2;
	}


	void renderTile(Material* material) {
		if (material->getTextureCount() == 0) return;
		const char* in_path = material->getTexture(0)->getPath().c_str();
		Engine& engine = m_app.getEngine();
		StaticString<LUMIX_MAX_PATH> out_path(".lumix/asset_tiles/", material->getPath().getHash(), ".lbc");

		m_texture_plugin->createTile(in_path, out_path, Texture::TYPE);
	}

	void renderTile(Model* model, const DVec3* in_pos, const Quat* in_rot)
	{
		Engine& engine = m_app.getEngine();
		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		EntityRef mesh_entity = m_tile.universe->createEntity({ 0, 0, 0 }, { 0, 0, 0, 1 });
		m_tile.universe->createComponent(MODEL_INSTANCE_TYPE, mesh_entity);

		render_scene->setModelInstancePath(mesh_entity, model->getPath());
		render_scene->setModelInstanceLOD(mesh_entity, 0);
		const AABB aabb = model->getAABB();
		const float radius = model->getCenterBoundingRadius();

		Matrix mtx;
		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(radius * 2);
		Vec3 dir = normalize(center - eye);
		mtx.lookAt(eye, center, normalize(Vec3(1, -1, 1)));
		mtx = mtx.inverted();

		Viewport viewport;
		viewport.near = 0.01f;
		viewport.far = 8 * radius;
		viewport.is_ortho = true;
		viewport.ortho_size = radius * 1.1f;
		viewport.h = AssetBrowser::TILE_SIZE * 4;
		viewport.w = AssetBrowser::TILE_SIZE * 4;
		viewport.pos = DVec3(center - dir * 4 * radius);
		viewport.rot = in_rot ? *in_rot : mtx.getRotation();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);
		if (!m_tile.pipeline->getOutput()) {
			logError("Could not create ", model->getPath(), " thumbnail");
			model->decRefCount();
			m_tile.frame_countdown = -1;
			return;
		}

		Renderer::MemRef mem;
		m_tile.texture = renderer->createTexture(AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, mem, "tile_final");
		gpu::TextureHandle tile_tmp = renderer->createTexture(AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::COMPUTE_WRITE, mem, "tile_tmp");
		renderer->copy(tile_tmp, m_tile.pipeline->getOutput());
		renderer->downscale(tile_tmp, AssetBrowser::TILE_SIZE * 4, AssetBrowser::TILE_SIZE * 4, m_tile.texture, AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);

		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		renderer->getTextureImage(m_tile.texture 
			, AssetBrowser::TILE_SIZE
			, AssetBrowser::TILE_SIZE
			, gpu::TextureFormat::RGBA8
			, Span(m_tile.data.getMutableData(), (u32)m_tile.data.size()));
		
		renderer->destroy(tile_tmp);
		m_tile.entity = mesh_entity;
		m_tile.frame_countdown = 2;
		m_tile.path_hash = model->getPath().getHash();
		model->decRefCount();
	}


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (type == Shader::TYPE) return m_app.getAssetBrowser().copyTile("editor/textures/tile_shader.tga", out_path);

		if (type != Model::TYPE && type != Material::TYPE && type != PrefabResource::TYPE) return false;

		Path path(in_path);

		if (!m_tile.queue.full())
		{
			pushTileQueue(path);
			return true;
		}

		m_tile.paths.push(path);
		return true;
	}


	struct TileData
	{
		TileData(IAllocator& allocator)
			: data(allocator)
			, paths(allocator)
			, queue()
		{
		}

		Universe* universe = nullptr;
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
	gpu::TextureHandle m_preview;
	Universe* m_universe;
	Viewport m_viewport;
	UniquePtr<Pipeline> m_pipeline;
	EntityPtr m_mesh = INVALID_ENTITY;
	bool m_is_mouse_captured;
	int m_captured_mouse_x;
	int m_captured_mouse_y;
	TexturePlugin* m_texture_plugin;
	FBXImporter m_fbx_importer;
	jobs::Signal m_subres_signal;
};


struct ShaderPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit ShaderPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("shd", Shader::TYPE);
	}


	void findIncludes(const char* path)
	{
		lua_State* L = luaL_newstate();
		luaL_openlibs(L);

		os::InputFile file;
		if (!file.open(path[0] == '/' ? path + 1 : path)) return;
		
		IAllocator& allocator = m_app.getAllocator();
		OutputMemoryStream content(allocator);
		content.resize((int)file.size());
		if (!file.read(content.getMutableData(), content.size())) {
			logError("Could not read ", path);
			content.clear();
		}
		file.close();

		struct Context {
			const char* path;
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
			that->plugin->m_app.getAssetCompiler().registerDependency(Path(that->path), Path(path));
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

		if (lua_load(L, reader, &ctx, path) != 0) {
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

	void addSubresources(AssetCompiler& compiler, const char* path) override {
		compiler.addResource(Shader::TYPE, path);
		findIncludes(path);
	}

	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}


	void onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return;

		auto* shader = static_cast<Shader*>(resources[0]);
		if (ImGui::Button(ICON_FA_EXTERNAL_LINK_ALT "Open externally"))
		{
			m_app.getAssetBrowser().openInExternalEditor(shader->getPath().c_str());
		}

		if (shader->m_texture_slot_count > 0 &&
			ImGui::CollapsingHeader(
				"Texture slots", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
		{
			for (u32 i = 0; i < shader->m_texture_slot_count; ++i)
			{
				auto& slot = shader->m_texture_slots[i];
				ImGui::Text("%s", slot.name);
			}
		}
		if (!shader->m_uniforms.empty() &&
			ImGui::CollapsingHeader("Uniforms", nullptr, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_Framed))
		{
			ImGui::Columns(2);
			ImGui::Text("name");
			ImGui::NextColumn();
			ImGui::Text("type");
			ImGui::NextColumn();
			ImGui::Separator();
			for (int i = 0; i < shader->m_uniforms.size(); ++i)
			{
				auto& uniform = shader->m_uniforms[i];
				ImGui::Text("%s", uniform.name);
				ImGui::NextColumn();
				switch (uniform.type)
				{
					case Shader::Uniform::COLOR: ImGui::Text("Color"); break;
					case Shader::Uniform::FLOAT: ImGui::Text("Float"); break;
					case Shader::Uniform::INT: ImGui::Text("Int"); break;
					case Shader::Uniform::MATRIX4: ImGui::Text("Matrix 4x4"); break;
					case Shader::Uniform::VEC4: ImGui::Text("Vector4"); break;
					case Shader::Uniform::VEC3: ImGui::Text("Vector3"); break;
					case Shader::Uniform::VEC2: ImGui::Text("Vector2"); break;
					default: ASSERT(false); break;
				}
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Shader"; }
	ResourceType getResourceType() const override { return Shader::TYPE; }


	StudioApp& m_app;
};

template <typename F>
void captureCubemap(StudioApp& app
	, Universe& universe
	, Pipeline& pipeline
	, const u32 texture_size
	, const DVec3& position
	, Array<Vec4>& data
	, F&& f) {
	memoryBarrier();

	Engine& engine = app.getEngine();
	auto& plugin_manager = engine.getPluginManager();

	Viewport viewport;
	viewport.is_ortho = false;
	viewport.fov = degreesToRadians(90.f);
	viewport.near = 0.1f;
	viewport.far = 10'000;
	viewport.w = texture_size;
	viewport.h = texture_size;

	pipeline.setUniverse(&universe);
	pipeline.setViewport(viewport);

	Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
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
		renderer->getTextureImage(res
			, texture_size
			, texture_size
			, gpu::TextureFormat::RGBA32F
			, Span((u8*)(data.begin() + (i * texture_size * texture_size)), u32(texture_size * texture_size * sizeof(*data.begin())))
		);
	}

	struct RenderJob : Renderer::RenderJob {
		RenderJob(F&& f) : f(f) {}

		void setup() override {}
		void execute() override {
			f();
		}
		F f;
	};

	RenderJob& rjob = renderer->createJob<RenderJob>(static_cast<F&&>(f));
	renderer->queue(rjob, 0);
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
		PluginManager& plugin_manager = engine.getPluginManager();
		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		IAllocator& allocator = m_app.getAllocator();
		ResourceManagerHub& rm = engine.getResourceManager();
		PipelineResource* pres = rm.load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PROBE", allocator);
		m_ibl_filter_shader = rm.load<Shader>(Path("pipelines/ibl_filter.shd"));
	}

	bool saveCubemap(u64 probe_guid, const Vec4* data, u32 texture_size, u32 mips_count) {
		ASSERT(data);
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		StaticString<LUMIX_MAX_PATH> path(base_path, "universes");
		if (!os::makePath(path) && !os::dirExists(path)) {
			logError("Failed to create ", path);
		}
		path << "/probes_tmp/";
		if (!os::makePath(path) && !os::dirExists(path)) {
			logError("Failed to create ", path);
		}
		path << probe_guid << ".lbc";

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
		if (!file.open(path)) {
			logError("Failed to create ", path);
			return false;
		}
		bool res = file.write(blob.data(), blob.size());
		file.close();
		return res;
	}


	void generateCubemaps(bool bounce, Universe& universe) {
		ASSERT(m_probes.empty());

		m_pipeline->define("PROBE_BOUNCE", bounce);

		auto* scene = (RenderScene*)universe.getScene(ENVIRONMENT_PROBE_TYPE);
		const Span<EntityRef> env_probes = scene->getEnvironmentProbesEntities();
		const Span<EntityRef> reflection_probes = scene->getReflectionProbesEntities();
		m_probes.reserve(env_probes.length() + reflection_probes.length());
		IAllocator& allocator = m_app.getAllocator();
		for (EntityRef p : env_probes) {
			ProbeJob* job = LUMIX_NEW(m_app.getAllocator(), ProbeJob)(*this, universe, p, allocator);
			
			const EntityPtr env_entity = scene->getActiveEnvironment();
			job->env_probe = scene->getEnvironmentProbe(p);
			job->is_reflection = false;
			job->position = universe.getPosition(p);

			m_probes.push(job);
		}

		for (EntityRef p : reflection_probes) {
			ProbeJob* job = LUMIX_NEW(m_app.getAllocator(), ProbeJob)(*this, universe, p, allocator);
			
			job->reflection_probe = scene->getReflectionProbe(p);
			job->is_reflection = true;
			job->position = universe.getPosition(p);

			m_probes.push(job);
		}

		m_probe_counter += m_probes.size();
	}

	struct ProbeJob {
		ProbeJob(EnvironmentProbePlugin& plugin, Universe& universe, EntityRef& entity, IAllocator& allocator) 
			: entity(entity)
			, data(allocator)
			, plugin(plugin)
			, universe(universe)
		{}
		
		EntityRef entity;
		union {
			EnvironmentProbe env_probe;
			ReflectionProbe reflection_probe;
		};
		bool is_reflection = false;
		EnvironmentProbePlugin& plugin;
		DVec3 position;

		Universe& universe;
		Array<Vec4> data;
		SphericalHarmonics sh;
		bool render_dispatched = false;
		bool done = false;
		bool done_counted = false;
	};

	void render(ProbeJob& job) {
		const u32 texture_size = job.is_reflection ? job.reflection_probe.size : 128;

		captureCubemap(m_app, job.universe, *m_pipeline, texture_size, job.position, job.data, [&job](){
			jobs::runLambda([&job]() {
				job.plugin.processData(job);
			}, nullptr);

		});
	}

	void update() override
	{
		if (m_ibl_filter_shader->isReady() && !m_ibl_filter_program) {
			m_ibl_filter_program = m_ibl_filter_shader->getProgram(gpu::VertexDecl(), 0);
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
				ImGui::Text("%s", "Generating probes...");
				ImGui::Text("%s", "Manipulating with entities at this time can produce incorrect probes.");
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
			StaticString<LUMIX_MAX_PATH> path(base_path, "universes/");
			if (!os::dirExists(path) && !os::makePath(path)) {
				logError("Failed to create ", path);
			}
			path << "/probes/";
			if (!os::dirExists(path) && !os::makePath(path)) {
				logError("Failed to create ", path);
			}
			RenderScene* scene = nullptr;
			while (!m_probes.empty()) {
				ProbeJob& job = *m_probes.back();
				m_probes.pop();
				ASSERT(job.done);
				ASSERT(job.done_counted);

				if (job.is_reflection) {

					const u64 guid = job.reflection_probe.guid;

					const StaticString<LUMIX_MAX_PATH> tmp_path(base_path, "/universes/probes_tmp/", guid, ".lbc");
					const StaticString<LUMIX_MAX_PATH> path(base_path, "/universes/probes/", guid, ".lbc");
					if (!os::fileExists(tmp_path)) {
						if (scene) scene->reloadReflectionProbes();
						return;
					}
					if (!os::moveFile(tmp_path, path)) {
						logError("Failed to move file ", tmp_path, " to ", path);
					}
				}

				if (job.universe.hasComponent(job.entity, ENVIRONMENT_PROBE_TYPE)) {
					scene = (RenderScene*)job.universe.getScene(ENVIRONMENT_PROBE_TYPE);
					EnvironmentProbe& p = scene->getEnvironmentProbe(job.entity);
					static_assert(sizeof(p.sh_coefs) == sizeof(job.sh.coefs));
					memcpy(p.sh_coefs, job.sh.coefs, sizeof(p.sh_coefs));
				}

				IAllocator& allocator = m_app.getAllocator();
				LUMIX_DELETE(allocator, &job);
			}
			if (scene) scene->reloadReflectionProbes();
		}
	}

	void radianceFilter(const Vec4* data, u32 size, u64 guid) {
		PROFILE_FUNCTION();
		if (!m_ibl_filter_shader->isReady()) {
			logError(m_ibl_filter_shader->getPath(), "is not ready");
			return;
		}
		jobs::Signal finished;
		PluginManager& plugin_manager = m_app.getEngine().getPluginManager();
		Renderer* renderer = (Renderer*)plugin_manager.getPlugin("renderer");

		// TODO RenderJob
		jobs::runLambda([&](){
			renderer->beginProfileBlock("radiance_filter", 0);
			gpu::TextureHandle src = gpu::allocTextureHandle();
			gpu::TextureHandle dst = gpu::allocTextureHandle();
			bool created = gpu::createTexture(src, size, size, 1, gpu::TextureFormat::RGBA32F, gpu::TextureFlags::IS_CUBE, "env");
			ASSERT(created);
			for (u32 face = 0; face < 6; ++face) {
				gpu::update(src, 0, 0, 0, face, size, size, gpu::TextureFormat::RGBA32F, (void*)(data + size * size * face), size * size * sizeof(*data));
			}
			gpu::generateMipmaps(src);
			created = gpu::createTexture(dst, size, size, 1, gpu::TextureFormat::RGBA32F, gpu::TextureFlags::IS_CUBE, "env_filtered");
			ASSERT(created);
			gpu::BufferHandle buf = gpu::allocBufferHandle();
			gpu::createBuffer(buf, gpu::BufferFlags::UNIFORM_BUFFER, 256, nullptr);

			const u32 roughness_levels = 5;
			
			gpu::startCapture();
			gpu::useProgram(m_ibl_filter_program);
			gpu::bindTextures(&src, 0, 1);
			for (u32 mip = 0; mip < roughness_levels; ++mip) {
				const float roughness = float(mip) / (roughness_levels - 1);
				for (u32 face = 0; face < 6; ++face) {
					gpu::setFramebufferCube(dst, face, mip);
					gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, buf, 0, 256);
					struct {
						float roughness;
						u32 face;
						u32 mip;
					} drawcall = {roughness, face, mip};
					gpu::setState(gpu::StateFlags::NONE);
					gpu::viewport(0, 0, size >> mip, size >> mip);
					gpu::update(buf, &drawcall, sizeof(drawcall));
					gpu::drawArrays(gpu::PrimitiveType::TRIANGLE_STRIP, 0, 4);
				}
			}

			gpu::setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);

			gpu::TextureHandle staging = gpu::allocTextureHandle();
			const gpu::TextureFlags flags = gpu::TextureFlags::IS_CUBE | gpu::TextureFlags::READBACK;
			gpu::createTexture(staging, size, size, 1, gpu::TextureFormat::RGBA32F, flags, "staging_buffer");
			
			u32 data_size = 0;
			u32 mip_size = size;
			for (u32 mip = 0; mip < roughness_levels; ++mip) {
				data_size += mip_size * mip_size * sizeof(Vec4) * 6;
				mip_size >>= 1;
			}

			Array<u8> tmp(m_app.getAllocator());
			tmp.resize(data_size);

			gpu::copy(staging, dst, 0, 0);
			u8* tmp_ptr = tmp.begin();
			for (u32 mip = 0; mip < roughness_levels; ++mip) {
				const u32 mip_size = size >> mip;
				gpu::readTexture(staging, mip, Span(tmp_ptr, mip_size * mip_size * sizeof(Vec4) * 6));
				tmp_ptr += mip_size * mip_size * sizeof(Vec4) * 6;
			}
			gpu::stopCapture();

			saveCubemap(guid, (Vec4*)tmp.begin(), size, roughness_levels);
			gpu::destroy(staging);
			renderer->endProfileBlock();

			gpu::destroy(buf);
			gpu::destroy(src);
			gpu::destroy(dst);
		}, &finished, 1);
		jobs::wait(&finished);
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


	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override {
		Universe& universe = *editor.getUniverse();
		if (cmp.type == ENVIRONMENT_PROBE_TYPE) {
			const EntityRef e = (EntityRef)cmp.entity;
			auto* scene = static_cast<RenderScene*>(cmp.scene);
			if (m_probe_counter) ImGui::Text("Generating...");
			else {
				const EnvironmentProbe& probe = scene->getEnvironmentProbe(e);
				if (ImGui::CollapsingHeader("Generator")) {
					if (ImGui::Button("Generate")) generateCubemaps(false, universe);
					ImGui::SameLine();
					if (ImGui::Button("Add bounce")) generateCubemaps(true, universe);
				}
			}
		}

		if (cmp.type == REFLECTION_PROBE_TYPE) {
			const EntityRef e = (EntityRef)cmp.entity;
			auto* scene = static_cast<RenderScene*>(cmp.scene);
			if (m_probe_counter) ImGui::Text("Generating...");
			else {
				const ReflectionProbe& probe = scene->getReflectionProbe(e);
				if (probe.flags.isSet(ReflectionProbe::ENABLED)) {
					StaticString<LUMIX_MAX_PATH> path("universes/probes/", probe.guid, ".lbc");
					ImGuiEx::Label("Path");
					ImGui::TextUnformatted(path);
					if (ImGui::Button("View radiance")) m_app.getAssetBrowser().selectResource(Path(path), true, false);
				}
				if (ImGui::CollapsingHeader("Generator")) {
					if (ImGui::Button("Generate")) generateCubemaps(false, universe);
					ImGui::SameLine();
					if (ImGui::Button("Add bounce")) generateCubemaps(true, universe);
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
			RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(INSTANCED_MODEL_TYPE);
			InstancedModel& im = scene->beginInstancedModelEditing(entity);

			for (auto& i : im.instances) {
				if (memcmp(&i, &old_value, sizeof(old_value)) == 0) {
					i = new_value;
					break;
				}
			}

			scene->endInstancedModelEditing(entity);
			old_value = merge_value;
			return true;
		}

		void undo() override {
			RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(INSTANCED_MODEL_TYPE);
			InstancedModel& im = scene->beginInstancedModelEditing(entity);

			for (auto& i : im.instances) {
				if (memcmp(&i, &new_value, sizeof(new_value)) == 0) {
					i = old_value;
					break;
				}
			}

			scene->endInstancedModelEditing(entity);
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
			
			RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(INSTANCED_MODEL_TYPE);
			InstancedModel& im = scene->beginInstancedModelEditing(entity);
			for (i32 i = im.instances.size() - 1; i >= 0; --i) {
				const InstancedModel::InstanceData& id = im.instances[i];
				if (squaredLength(id.pos.xz() - center_xz) < radius_squared) {
					instances.push(im.instances[i]);
					im.instances.swapAndPop(i);
				}
			}
			
			scene->endInstancedModelEditing(entity);
			return true;
		}
		
		void undo() override {
			RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(INSTANCED_MODEL_TYPE);
			InstancedModel& im = scene->beginInstancedModelEditing(entity);
			
			for (const InstancedModel::InstanceData& id : instances) {
				im.instances.push(id);
			}
			scene->endInstancedModelEditing(entity);
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
			RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(INSTANCED_MODEL_TYPE);
			InstancedModel& im = scene->beginInstancedModelEditing(entity);
			for (const InstancedModel::InstanceData& i : instances) {
				im.instances.push(i);
			}
			scene->endInstancedModelEditing(entity);
			return true;
		}
		
		void undo() override {
			RenderScene* scene = (RenderScene*)editor.getUniverse()->getScene(INSTANCED_MODEL_TYPE);
			InstancedModel& im = scene->beginInstancedModelEditing(entity);
			for (u32 j = 0, cj = (u32)instances.size(); j < cj; ++j) {
				for (u32 i = 0, ci = (u32)im.instances.size(); i < ci; ++i) {
					if (memcmp(&instances[j], &im.instances[i], sizeof(im.instances[i])) == 0) {
						im.instances.swapAndPop(i);
						break;
					}
				}
			}
			scene->endInstancedModelEditing(entity);
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
		RenderScene* scene;
	};

	Component getComponent() {
		WorldEditor& editor = m_app.getWorldEditor();
		const Array<EntityRef>& selected_entities = editor.getSelectedEntities();
		if (selected_entities.size() != 1) return { nullptr };

		Universe& universe = *editor.getUniverse();
		RenderScene* scene = (RenderScene*)universe.getScene(INSTANCED_MODEL_TYPE);
		auto iter = scene->getInstancedModels().find(selected_entities[0]);
		if (!iter.isValid()) return { nullptr };
		return { &iter.value(), selected_entities[0], scene };
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
		const RayCastModelHit hit = m_brush != Brush::TERRAIN ? cmp.scene->castRay(ray_origin, ray_dir, INVALID_ENTITY) : cmp.scene->castRayTerrain(ray_origin, ray_dir);
		if (!hit.is_hit) return false;

		const DVec3 hit_pos = hit.origin + hit.t * hit.dir;
		const DVec3 origin = editor.getUniverse()->getPosition(cmp.entity);
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
				const Transform terrain_tr = editor.getUniverse()->getTransform(terrain);
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
						pos.y = cmp.scene->getTerrainHeightAt(terrain, terrain_pos.x, terrain_pos.z) + terrain_tr.pos.y;
						pos.y += randFloat(m_y_spread.x, m_y_spread.y);
						
						InstancedModel::InstanceData id;
						id.scale = randFloat(m_size_spread.x, m_size_spread.y);
						id.rot_quat = Vec3::ZERO;
						id.lod = 3;

						Quat rot = Quat::IDENTITY;
						if (m_is_rotate_x) {
							float angle = randFloat(m_rotate_x_spread.x, m_rotate_x_spread.y);
							Quat q(Vec3(1, 0, 0), angle);
							rot = q * rot;
						}

						if (m_is_rotate_y) {
							float angle = randFloat(m_rotate_y_spread.x, m_rotate_y_spread.y);
							Quat q(Vec3(0, 1, 0), angle);
							rot = q * rot;
						}

						if (m_is_rotate_z) {
							float angle = randFloat(m_rotate_z_spread.x, m_rotate_z_spread.y);
							Quat q(rot.rotate(Vec3(0, 0, 1)), angle);
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

	void onMouseMove(UniverseView& view, int x, int y, int, int) override {
		if (ImGui::GetIO().KeyShift && m_brush == Brush::TERRAIN) {
			paint(x, y);
		}
	}

	void onMouseUp(UniverseView& view, int x, int y, os::MouseButton button) override {
		if (m_can_lock_group) {
			m_app.getWorldEditor().lockGroupCommand();
			m_can_lock_group = false;
		}
	}

	bool onMouseDown(UniverseView& view, int x, int y) override {
		if (ImGui::GetIO().KeyShift) return paint(x, y);

		auto cmp = getComponent();
		if (!cmp.im) return false;
		if (!cmp.im->model || !cmp.im->model->isReady()) return false;

		DVec3 ray_origin;
		Vec3 ray_dir;
		view.getViewport().getRay(Vec2((float)x, (float)y), ray_origin, ray_dir);
		RayCastModelHit hit = cmp.scene->castRayInstancedModels(ray_origin, ray_dir, [](const RayCastModelHit&){ return true; });
		if (hit.is_hit && hit.entity == cmp.entity) {
			m_selected = cmp.scene->getInstancedModels()[cmp.entity].instances[hit.subindex];
			return true;
		}
		return false;
	}

	static void drawCircle(RenderScene& scene, const DVec3& center, float radius, u32 color) {
		constexpr i32 SLICE_COUNT = 30;
		constexpr float angle_step = PI * 2 / SLICE_COUNT;
		for (i32 i = 0; i < SLICE_COUNT + 1; ++i) {
			const float angle = i * angle_step;
			const float next_angle = i * angle_step + angle_step;
			const DVec3 from = center + DVec3(cosf(angle), 0, sinf(angle)) * radius;
			const DVec3 to = center + DVec3(cosf(next_angle), 0, sinf(next_angle)) * radius;
			scene.addDebugLine(from, to, color);
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

	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override {
		if (cmp.type != INSTANCED_MODEL_TYPE) return;
		RenderScene* render_scene = ((RenderScene*)cmp.scene);
		const InstancedModel& im = render_scene->getInstancedModels()[*cmp.entity];
		
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
			DVec3 origin = cmp.scene->getUniverse().getPosition(*cmp.entity);
			Transform tr;
			tr.rot = getInstanceQuat(m_selected.rot_quat);
			tr.scale = m_selected.scale;
			tr.pos = origin + DVec3(m_selected.pos);
			const Gizmo::Config& cfg = m_app.getGizmoConfig();
			bool changed = Gizmo::manipulate(u64(4) << 32 | cmp.entity.index, editor.getView(), tr, cfg);

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
			changed = ImGui::DragFloat("##scale", &tr.scale, 0.01f) || changed;

			if (changed) {
				tr.pos = tr.pos - origin;

				InstancedModel::InstanceData new_value;
				new_value.pos = Vec3(tr.pos);
				new_value.rot_quat = Vec3(tr.rot.x, tr.rot.y, tr.rot.z);
				if (tr.rot.w < 0) new_value.rot_quat *= -1;
				new_value.scale = tr.scale;
				new_value.lod = 3;
				
				UniquePtr<SetTransformCommand> cmd = UniquePtr<SetTransformCommand>::create(editor.getAllocator(), *cmp.entity, m_selected, new_value, editor);
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
					const RayCastModelHit hit = render_scene->castRayTerrain(ray_origin, ray_dir);
					if (hit.is_hit) {
						drawCircle(*render_scene, hit.origin + hit.t * hit.dir, m_brush_radius, 0xff880000);
					}
				}
				break;
			}
			default: ASSERT(false); break;
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

struct TerrainPlugin final : PropertyGrid::IPlugin
{
	explicit TerrainPlugin(StudioApp& app)
		: m_terrain_editor(app)
	{}


	void onGUI(PropertyGrid& grid, ComponentUID cmp, WorldEditor& editor) override {
		if (cmp.type != TERRAIN_TYPE) return;
		m_terrain_editor.onGUI(cmp, editor);
	}

	TerrainEditor m_terrain_editor;
};


struct RenderInterfaceImpl final : RenderInterface
{
	RenderInterfaceImpl(StudioApp& app, Renderer& renderer)
		: m_app(app)
		, m_textures(app.getAllocator())
		, m_renderer(renderer)
	{}

	void launchRenderDoc() override {
		gpu::launchRenderDoc();
	}

	bool saveTexture(Engine& engine, const char* path_cstr, const void* pixels, int w, int h, bool upper_left_origin) override
	{
		Path path(path_cstr);
		os::OutputFile file;
		if (!file.open(path_cstr)) return false;

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
		m_textures.insert(&texture->handle, texture);
		return (ImTextureID)(uintptr_t)texture->handle;
	}


	void destroyTexture(ImTextureID handle) override
	{
		auto& allocator = m_app.getAllocator();
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		auto* texture = iter.value();
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
		m_textures.insert(&texture->handle, texture);
		return &texture->handle;
	}


	void unloadTexture(ImTextureID handle) override
	{
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		auto* texture = iter.value();
		texture->decRefCount();
		m_textures.erase(iter);
	}


	UniverseView::RayHit castRay(Universe& universe, const DVec3& origin, const Vec3& dir, EntityPtr ignored) override
	{
		RenderScene* scene = (RenderScene*)universe.getScene(ENVIRONMENT_PROBE_TYPE);
		const RayCastModelHit hit = scene->castRay(origin, dir, ignored);

		return {hit.is_hit, hit.t, hit.entity, hit.origin + hit.dir * hit.t};
	}


	AABB getEntityAABB(Universe& universe, EntityRef entity, const DVec3& base) override
	{
		AABB aabb;

		if (universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) {
			RenderScene* scene = (RenderScene*)universe.getScene(ENVIRONMENT_PROBE_TYPE);
			Model* model = scene->getModelInstanceModel(entity);
			if (!model) return aabb;

			aabb = model->getAABB();
			aabb.transform(universe.getRelativeMatrix(entity, base));

			return aabb;
		}

		Vec3 pos = Vec3(universe.getPosition(entity) - base);
		aabb = AABB(pos, pos);

		return aabb;
	}


	Path getModelInstancePath(Universe& universe, EntityRef entity) override {
		RenderScene* scene = (RenderScene*)universe.getScene(ENVIRONMENT_PROBE_TYPE);
		return scene->getModelInstancePath(entity); 
	}


	StudioApp& m_app;
	Renderer& m_renderer;
	HashMap<void*, Texture*> m_textures;
};


struct EditorUIRenderPlugin final : StudioApp::GUIPlugin, IImGuiRenderer
{
	struct RenderJob : Renderer::RenderJob
	{
		struct CmdList
		{
			CmdList(LinearAllocator& allocator) : commands(allocator) {}

			Renderer::TransientSlice idx_buffer;
			Renderer::TransientSlice vtx_buffer;

			Array<ImDrawCmd> commands;
		};

		struct WindowDrawData {
			WindowDrawData(LinearAllocator& allocator) : cmd_lists(allocator) {}

			gpu::ProgramHandle program;
			os::WindowHandle window;
			u32 w, h;
			i32 x, y;
			bool new_program = false;
			Array<CmdList> cmd_lists;
			Renderer::TransientSlice ub;
			Vec2 scale;
			gpu::TextureHandle render_target = gpu::INVALID_TEXTURE;
			Vec4 clear_color = Vec4(0.2f, 0.2f, 0.2f, 1.f);
		};

		RenderJob(LinearAllocator& allocator)
			: allocator(allocator)
			, window_draw_data(allocator)
		{
		}

		gpu::ProgramHandle getProgram(void* window_handle, bool& new_program) {
			auto iter = plugin->m_programs.find(window_handle);
			if (!iter.isValid()) {
				plugin->m_programs.insert(window_handle, gpu::allocProgramHandle());
				iter = plugin->m_programs.find(window_handle);
				new_program = true;
			}

			return iter.value();
		}

		void setup() override
		{
			PROFILE_FUNCTION();
			PluginManager& plugin_manager = plugin->m_engine.getPluginManager();
			renderer = (Renderer*)plugin_manager.getPlugin("renderer");

			if (!should_setup) return;

			ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
			window_draw_data.reserve(platform_io.Viewports.size());
			for (ImGuiViewport* vp : platform_io.Viewports) {
				ImDrawData* draw_data = vp->DrawData;
				WindowDrawData& dd = window_draw_data.emplace(allocator);
				dd.w = u32(vp->Size.x);
				dd.h = u32(vp->Size.y);
				dd.x = i32(draw_data->DisplayPos.x);
				dd.y = i32(draw_data->DisplayPos.y);
				dd.scale = Vec2(1);
				dd.window = vp->PlatformHandle;
				dd.program = getProgram(dd.window, dd.new_program);
				dd.cmd_lists.reserve(draw_data->CmdListsCount);
				dd.ub = renderer->allocUniform(sizeof(Vec4) * 2);

				const Vec4 canvas_mtx[] = {
					Vec4(2.f / dd.w, 0, -1 + (float)-dd.x * 2.f / dd.w, 0),
					Vec4(0, -2.f / dd.h, 1 + (float)dd.y * 2.f / dd.h, 0)
				};
				memcpy(dd.ub.ptr, &canvas_mtx, sizeof(canvas_mtx));

				for (int i = 0; i < draw_data->CmdListsCount; ++i) {
					ImDrawList* cmd_list = draw_data->CmdLists[i];
					CmdList& out_cmd_list = dd.cmd_lists.emplace(allocator);

					out_cmd_list.idx_buffer = renderer->allocTransient(cmd_list->IdxBuffer.size_in_bytes());
					memcpy(out_cmd_list.idx_buffer.ptr, &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size_in_bytes());

					out_cmd_list.vtx_buffer = renderer->allocTransient(cmd_list->VtxBuffer.size_in_bytes());
					memcpy(out_cmd_list.vtx_buffer.ptr, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size_in_bytes());
			
					out_cmd_list.commands.reserve(cmd_list->CmdBuffer.size());
					for (int i = 0, c = cmd_list->CmdBuffer.size(); i < c; ++i) {
						if (cmd_list->CmdBuffer[i].UserCallback) {
							cmd_list->CmdBuffer[i].UserCallback(cmd_list, &cmd_list->CmdBuffer[i]);
						}
						else {
							out_cmd_list.commands.push(cmd_list->CmdBuffer[i]);
						}
					}
				}
			}
			
			default_texture = &plugin->m_texture;
		}


		void draw(const CmdList& cmd_list, const WindowDrawData& wdd) {
			const u32 num_indices = cmd_list.idx_buffer.size / sizeof(ImDrawIdx);
			const u32 num_vertices = cmd_list.vtx_buffer.size / sizeof(ImDrawVert);

			gpu::useProgram(wdd.program);

			gpu::bindIndexBuffer(cmd_list.idx_buffer.buffer);
			gpu::bindVertexBuffer(0, cmd_list.vtx_buffer.buffer, cmd_list.vtx_buffer.offset, sizeof(ImDrawVert));
			gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);

			const ImDrawCmd* pcmd_begin = cmd_list.commands.begin();
			const ImDrawCmd* pcmd_end = cmd_list.commands.end();

			const gpu::StateFlags blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
			gpu::setState(gpu::StateFlags::SCISSOR_TEST | blend_state);
			for (const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++) {
				ASSERT(!pcmd->UserCallback);
				if (0 == pcmd->ElemCount) continue;

				gpu::TextureHandle tex = (gpu::TextureHandle)(intptr_t)pcmd->TextureId;
				if (!tex) tex = *default_texture;
				gpu::bindTextures(&tex, 0, 1);

				const u32 h = u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y) * wdd.scale.y, 0.f, 65535.f));

				if (gpu::isOriginBottomLeft()) {
					gpu::scissor(u32(maximum((pcmd->ClipRect.x - wdd.x) * wdd.scale.x, 0.0f)),
						wdd.h - u32(maximum((pcmd->ClipRect.y - wdd.y) * wdd.scale.y, 0.0f)) - h,
						u32(clamp((pcmd->ClipRect.z - pcmd->ClipRect.x) * wdd.scale.x, 0.f, 65535.f)),
						u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y) * wdd.scale.y, 0.f, 65535.f)));
				} else {
					gpu::scissor(u32(maximum((pcmd->ClipRect.x - wdd.x) * wdd.scale.x, 0.0f)),
						u32(maximum((pcmd->ClipRect.y - wdd.y) * wdd.scale.y, 0.0f)),
						u32(clamp((pcmd->ClipRect.z - pcmd->ClipRect.x) * wdd.scale.x, 0.f, 65535.f)),
						u32(clamp((pcmd->ClipRect.w - pcmd->ClipRect.y) * wdd.scale.y, 0.f, 65535.f)));
				}

				gpu::drawIndexed(gpu::PrimitiveType::TRIANGLES, pcmd->IdxOffset * sizeof(u32) + cmd_list.idx_buffer.offset, pcmd->ElemCount, gpu::DataType::U32);
			}
			ib_offset += num_indices;
			vb_offset += num_vertices;
		}


		void execute() override
		{
			PROFILE_FUNCTION();

			renderer->beginProfileBlock("editor imgui", 0);

			vb_offset = 0;
			ib_offset = 0;
			for (WindowDrawData& dd : window_draw_data) {
				if (dd.render_target) {
					gpu::setFramebuffer(&dd.render_target, 1, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
				}
				else {
					gpu::setCurrentWindow(dd.window);
					gpu::setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);
				}
				gpu::viewport(0, 0, dd.w, dd.h);

				gpu::bindUniformBuffer(UniformBuffer::DRAWCALL, dd.ub.buffer, dd.ub.offset, dd.ub.size);

				gpu::clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH, &dd.clear_color.x, 1.0);
				if (dd.new_program) {
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
					gpu::VertexDecl decl;
					decl.addAttribute(0, 0, 2, gpu::AttributeType::FLOAT, 0);
					decl.addAttribute(1, 8, 2, gpu::AttributeType::FLOAT, 0);
					decl.addAttribute(2, 16, 4, gpu::AttributeType::U8, gpu::Attribute::NORMALIZED);
					gpu::createProgram(dd.program, decl, srcs, types, 2, nullptr, 0, "imgui shader");
				}
				for(const CmdList& cmd_list : dd.cmd_lists) {
					draw(cmd_list, dd);
				}
			}
			gpu::setCurrentWindow(nullptr);

			renderer->endProfileBlock();
		}
		
		LinearAllocator& allocator;
		Renderer* renderer;
		const gpu::TextureHandle* default_texture;
		Array<WindowDrawData> window_draw_data;
		u32 ib_offset;
		u32 vb_offset;
		EditorUIRenderPlugin* plugin;
		bool should_setup = true;
	};


	EditorUIRenderPlugin(StudioApp& app)
		: m_app(app)
		, m_engine(app.getEngine())
		, m_programs(app.getAllocator())
	{

		PluginManager& plugin_manager = m_engine.getPluginManager();
		Renderer* renderer = (Renderer*)plugin_manager.getPlugin("renderer");

		unsigned char* pixels;
		int width, height;
		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		atlas->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
		atlas->FontBuilderFlags = 0;
		atlas->Build();
		atlas->GetTexDataAsRGBA32(&pixels, &width, &height);

		const Renderer::MemRef mem = renderer->copy(pixels, width * height * 4);
		m_texture = renderer->createTexture(width, height, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NO_MIPS, mem, "editor_font_atlas");
		ImGui::GetIO().Fonts->TexID = m_texture;

		m_render_interface.create(app, *renderer);
		app.setRenderInterface(m_render_interface.get());
	}


	~EditorUIRenderPlugin()
	{
		m_app.setRenderInterface(nullptr);
		shutdownImGui();
		PluginManager& plugin_manager = m_engine.getPluginManager();
		Renderer* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
		for (gpu::ProgramHandle program : m_programs) {
			renderer->destroy(program);
		}
		if (m_texture) renderer->destroy(m_texture);
	}
	
	void render(gpu::TextureHandle rt, Vec2 rt_size, ImDrawData* draw_data, Vec2 scale) override {
		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		RenderJob& cmd = renderer->createJob<RenderJob>(renderer->getCurrentFrameAllocator());
		cmd.plugin = this;
		
		LinearAllocator& allocator = renderer->getCurrentFrameAllocator();
		RenderJob::WindowDrawData& dd = cmd.window_draw_data.emplace(allocator);
		dd.w = u32(rt_size.x);
		dd.h = u32(rt_size.y);
		dd.x = i32(0);
		dd.y = i32(0);
		dd.window = nullptr;
		dd.program = cmd.getProgram(dd.window, dd.new_program);
		dd.cmd_lists.reserve(draw_data->CmdListsCount);
		dd.ub = renderer->allocUniform(sizeof(Vec4) * 2);
		dd.render_target = rt;
		dd.scale = scale;
		dd.clear_color = Vec4(0);

		const Vec2 offset(0);
		const Vec4 canvas_mtx[] = {
			Vec4(2.f / dd.w * scale.x, 0, -1 + (float)-offset.x * 2.f / dd.w * scale.x, 0),
			Vec4(0, -2.f / dd.h * scale.y, 1 + (float)offset.y * 2.f / dd.h * scale.y, 0)
		};
		memcpy(dd.ub.ptr, &canvas_mtx, sizeof(canvas_mtx));

		for (int i = 0; i < draw_data->CmdListsCount; ++i) {
			ImDrawList* cmd_list = draw_data->CmdLists[i];
			RenderJob::CmdList& out_cmd_list = dd.cmd_lists.emplace(allocator);

			out_cmd_list.idx_buffer = renderer->allocTransient(cmd_list->IdxBuffer.size_in_bytes());
			memcpy(out_cmd_list.idx_buffer.ptr, &cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size_in_bytes());

			out_cmd_list.vtx_buffer = renderer->allocTransient(cmd_list->VtxBuffer.size_in_bytes());
			memcpy(out_cmd_list.vtx_buffer.ptr, &cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size_in_bytes());
			
			out_cmd_list.commands.reserve(cmd_list->CmdBuffer.size());
			for (int i = 0, c = cmd_list->CmdBuffer.size(); i < c; ++i) {
				if (cmd_list->CmdBuffer[i].UserCallback) {
					cmd_list->CmdBuffer[i].UserCallback(cmd_list, &cmd_list->CmdBuffer[i]);
				}
				else {
					out_cmd_list.commands.push(cmd_list->CmdBuffer[i]);
				}
			}
		}
			
		cmd.default_texture = &m_texture;
		cmd.should_setup = false;

		renderer->queue(cmd, 0);
	}

	void onWindowGUI() override {}


	const char* getName() const override { return "editor_ui_render"; }


	void shutdownImGui()
	{
		ImGui::DestroyContext();
	}


	void guiEndFrame() override
	{
		Renderer* renderer = static_cast<Renderer*>(m_engine.getPluginManager().getPlugin("renderer"));
		RenderJob& cmd = renderer->createJob<RenderJob>(renderer->getCurrentFrameAllocator());
		cmd.plugin = this;
		
		renderer->queue(cmd, 0);
		renderer->frame();
	}


	StudioApp& m_app;
	Engine& m_engine;
	HashMap<void*, gpu::ProgramHandle> m_programs;
	gpu::TextureHandle m_texture;
	Local<RenderInterfaceImpl> m_render_interface;
};


struct AddTerrainComponentPlugin final : StudioApp::IAddComponentPlugin
{
	explicit AddTerrainComponentPlugin(StudioApp& _app)
		: app(_app)
	{
	}


	bool createHeightmap(const char* material_path, int size)
	{
		char normalized_material_path[LUMIX_MAX_PATH];
		Path::normalize(material_path, Span(normalized_material_path));

		PathInfo info(normalized_material_path);
		StaticString<LUMIX_MAX_PATH> hm_path(info.m_dir, info.m_basename, ".raw");
		StaticString<LUMIX_MAX_PATH> albedo_path(info.m_dir, "albedo_detail.ltc");
		StaticString<LUMIX_MAX_PATH> normal_path(info.m_dir, "normal_detail.ltc");
		StaticString<LUMIX_MAX_PATH> splatmap_path(info.m_dir, "splatmap.tga");
		StaticString<LUMIX_MAX_PATH> splatmap_meta_path(info.m_dir, "splatmap.tga.meta");
		os::OutputFile file;
		FileSystem& fs = app.getEngine().getFileSystem();
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

		OutputMemoryStream splatmap(app.getAllocator());
		splatmap.resize(size * size * 4);
		memset(splatmap.getMutableData(), 0, size * size * 4);
		if (!Texture::saveTGA(&file, size, size, gpu::TextureFormat::RGBA8, splatmap.data(), true, Path(splatmap_path), app.getAllocator())) {
			logError("Failed to create texture ", splatmap_path);
			os::deleteFile(hm_path);
			return false;
		}
		file.close();

		if (!fs.open(albedo_path, file)) {
			logError("Failed to create texture ", albedo_path);
			os::deleteFile(hm_path);
			os::deleteFile(splatmap_path);
			os::deleteFile(splatmap_meta_path);
			return false;
		}
		file << R"#(
			layer {
				red = { path = "/textures/common/red.tga", channel = 0 },
				green = { path = "/textures/common/red.tga", channel = 1 },
				blue = { path = "/textures/common/red.tga", channel = 2 },
				alpha = { path = "/textures/common/red.tga", channel = 3 }
			}
			layer {
				red = { path = "/textures/common/green.tga", channel = 0 },
				green = { path = "/textures/common/green.tga", channel = 1 },
				blue = { path = "/textures/common/green.tga", channel = 2 },
				alpha = { path = "/textures/common/green.tga", channel = 3 }
			}
		)#";
		file.close();

		if (!fs.open(normal_path, file)) {
			logError("Failed to create texture ", normal_path);
			os::deleteFile(albedo_path);
			os::deleteFile(hm_path);
			os::deleteFile(splatmap_path);
			os::deleteFile(splatmap_meta_path);
			return false;
		}
		file << R"#(
			layer {
				red = { path = "/textures/common/default_normal.tga", channel = 0 },
				green = { path = "/textures/common/default_normal.tga", channel = 1 },
				blue = { path = "/textures/common/default_normal.tga", channel = 2 },
				alpha = { path = "/textures/common/default_normal.tga", channel = 3 }
			}
			layer {
				red = { path = "/textures/common/default_normal.tga", channel = 0 },
				green = { path = "/textures/common/default_normal.tga", channel = 1 },
				blue = { path = "/textures/common/default_normal.tga", channel = 2 },
				alpha = { path = "/textures/common/default_normal.tga", channel = 3 }
			}
		)#";
		file.close();

		if (!fs.open(normalized_material_path, file))
		{
			logError("Failed to create material ", normalized_material_path);
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
		file << info.m_basename;
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


	void onGUI(bool create_entity, bool from_filter, WorldEditor& editor) override
	{
		FileSystem& fs = app.getEngine().getFileSystem();

		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu("Terrain")) return;
		char buf[LUMIX_MAX_PATH];
		AssetBrowser& asset_browser = app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::BeginMenu("New"))
		{
			static int size = 1024;
			ImGui::InputInt("Size", &size);
			if (ImGui::Button("Create"))
			{
				char save_filename[LUMIX_MAX_PATH];
				if (os::getSaveFilename(Span(save_filename), "Material\0*.mat\0", "mat")) {
					if (fs.makeRelative(Span(buf), save_filename)) {
						new_created = createHeightmap(buf, size);
					}
					else {
						logError("Can not create ", save_filename, " because it's not in root directory.");
					}
				}
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);
		static FilePathHash selected_res_hash;
		if (asset_browser.resourceList(Span(buf), selected_res_hash, Material::TYPE, 0, false) || create_empty || new_created)
		{
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(Span(&entity, 1), false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, TERRAIN_TYPE))
			{
				editor.addComponent(Span(&entity, 1), TERRAIN_TYPE);
			}

			if (!create_empty)
			{
				editor.setProperty(TERRAIN_TYPE, "", -1, "Material", Span(&entity, 1), Path(buf));
			}

			ImGui::CloseCurrentPopup();
		}
		ImGui::EndMenu();
	}


	const char* getLabel() const override { return "Render / Terrain"; }


	StudioApp& app;
};


struct StudioAppPlugin : StudioApp::IPlugin
{
	StudioAppPlugin(StudioApp& app)
		: m_app(app)
		, m_pipeline_plugin(app)
		, m_font_plugin(app)
		, m_material_plugin(app)
		, m_particle_emitter_plugin(app)
		, m_particle_emitter_property_plugin(app)
		, m_spline_geom_plugin(app)
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
	{
	}

	const char* getName() const override { return "renderer"; }

	void init() override
	{
		IAllocator& allocator = m_app.getAllocator();

		AddTerrainComponentPlugin* add_terrain_plugin = LUMIX_NEW(allocator, AddTerrainComponentPlugin)(m_app);
		m_app.registerComponent(ICON_FA_MAP, "terrain", *add_terrain_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();

		const char* shader_exts[] = {"shd", nullptr};
		asset_compiler.addPlugin(m_shader_plugin, shader_exts);

		const char* texture_exts[] = { "png", "jpg", "jpeg", "tga", "raw", "ltc", nullptr};
		asset_compiler.addPlugin(m_texture_plugin, texture_exts);

		const char* pipeline_exts[] = {"pln", nullptr};
		asset_compiler.addPlugin(m_pipeline_plugin, pipeline_exts);

		const char* particle_emitter_exts[] = {"par", nullptr};
		asset_compiler.addPlugin(m_particle_emitter_plugin, particle_emitter_exts);

		const char* material_exts[] = {"mat", nullptr};
		asset_compiler.addPlugin(m_material_plugin, material_exts);

		m_model_plugin.m_texture_plugin = &m_texture_plugin;
		const char* model_exts[] = {"fbx", nullptr};
		asset_compiler.addPlugin(m_model_plugin, model_exts);

		const char* fonts_exts[] = {"ttf", nullptr};
		asset_compiler.addPlugin(m_font_plugin, fonts_exts);
		
		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(m_model_plugin);
		asset_browser.addPlugin(m_particle_emitter_plugin);
		asset_browser.addPlugin(m_material_plugin);
		asset_browser.addPlugin(m_font_plugin);
		asset_browser.addPlugin(m_shader_plugin);
		asset_browser.addPlugin(m_texture_plugin);

		m_app.addPlugin(m_scene_view);
		m_app.addPlugin(m_game_view);
		m_app.addPlugin(m_editor_ui_render_plugin);
		m_app.addPlugin(m_spline_geom_plugin);

		PropertyGrid& property_grid = m_app.getPropertyGrid();
		property_grid.addPlugin(m_model_properties_plugin);
		property_grid.addPlugin(m_env_probe_plugin);
		property_grid.addPlugin(m_terrain_plugin);
		property_grid.addPlugin(m_instanced_model_plugin);
		property_grid.addPlugin(m_particle_emitter_property_plugin);
		property_grid.addPlugin(m_spline_geom_plugin);

		m_scene_view.init();
		m_game_view.init();
		m_env_probe_plugin.init();
		m_model_plugin.init();

		m_particle_editor = ParticleEditor::create(m_app, m_editor_ui_render_plugin);
		m_app.addPlugin(*m_particle_editor.get());

		m_particle_emitter_plugin.m_particle_editor = m_particle_editor.get();
		m_particle_emitter_property_plugin.m_particle_editor = m_particle_editor.get();
	}

	void showEnvironmentProbeGizmo(UniverseView& view, ComponentUID cmp) {
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		const Universe& universe = scene->getUniverse();
		EntityRef e = (EntityRef)cmp.entity;
		EnvironmentProbe& p = scene->getEnvironmentProbe(e);
		Transform tr = universe.getTransform(e);
		const DVec3 pos = universe.getPosition(e);
		const Quat rot = universe.getRotation(e);

		/*Vec3 x = rot.rotate(Vec3(p.outer_range.x, 0, 0));
		Vec3 y = rot.rotate(Vec3(0, p.outer_range.y, 0));
		Vec3 z = rot.rotate(Vec3(0, 0, p.outer_range.z));

		addCube(view, pos, x, y, z, Color::BLUE);

		x = rot.rotate(Vec3(p.inner_range.x, 0, 0));
		y = rot.rotate(Vec3(0, p.inner_range.y, 0));
		z = rot.rotate(Vec3(0, 0, p.inner_range.z));

		addCube(view, pos, x, y, z, Color::BLUE);*/

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

	void showReflectionProbeGizmo(UniverseView& view, ComponentUID cmp) {
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		const Universe& universe = scene->getUniverse();
		EntityRef e = (EntityRef)cmp.entity;
		ReflectionProbe& p = scene->getReflectionProbe(e);
		Transform tr = universe.getTransform(e);
		const DVec3 pos = universe.getPosition(e);
		const Quat rot = universe.getRotation(e);

		const Gizmo::Config& cfg = m_app.getGizmoConfig();
		WorldEditor& editor = view.getEditor();
		if (Gizmo::box(u64(cmp.entity.index) | (u64(1) << 32), view, tr, p.half_extents, cfg, false)) {
			editor.beginCommandGroup("refl_probe_half_ext");
			editor.setProperty(ENVIRONMENT_PROBE_TYPE, "", -1, "Half extents", Span(&e, 1), p.half_extents);
			editor.setEntitiesPositions(&e, &tr.pos, 1);
			editor.endCommandGroup();
		}
	}

	void showPointLightGizmo(UniverseView& view, ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();

		const float range = scene->getLightRange((EntityRef)light.entity);
		const float fov = scene->getPointLight((EntityRef)light.entity).fov;

		const DVec3 pos = universe.getPosition((EntityRef)light.entity);
		if (fov > PI) {
			addSphere(view, pos, range, Color::BLUE);
		}
		else {
			const Quat rot = universe.getRotation((EntityRef)light.entity);
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

	void showGlobalLightGizmo(UniverseView& view, ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		const Universe& universe = scene->getUniverse();
		const EntityRef entity = (EntityRef)light.entity;
		const DVec3 pos = universe.getPosition(entity);

		const Vec3 dir = universe.getRotation(entity).rotate(Vec3(0, 0, 1));
		const Vec3 right = universe.getRotation(entity).rotate(Vec3(1, 0, 0));
		const Vec3 up = universe.getRotation(entity).rotate(Vec3(0, 1, 0));

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

	void showDecalGizmo(UniverseView& view, ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		const EntityRef e = (EntityRef)cmp.entity;
		Universe& universe = scene->getUniverse();
		Decal& decal = scene->getDecal(e);
		const Transform tr = universe.getTransform(e);
		const Vec3 x = tr.rot * Vec3(1, 0, 0) * decal.half_extents.x;
		const Vec3 y = tr.rot * Vec3(0, 1, 0) * decal.half_extents.y;
		const Vec3 z = tr.rot * Vec3(0, 0, 1) * decal.half_extents.z;
		addCube(view, tr.pos, x, y, z, Color::BLUE);
	}

	void showCurveDecalGizmo(UniverseView& view, ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		const EntityRef e = (EntityRef)cmp.entity;
		Universe& universe = scene->getUniverse();
		CurveDecal& decal = scene->getCurveDecal(e);
		const Transform tr = universe.getTransform(e);
		const Vec3 x = tr.rot * Vec3(1, 0, 0) * decal.half_extents.x;
		const Vec3 y = tr.rot * Vec3(0, 1, 0) * decal.half_extents.y;
		const Vec3 z = tr.rot * Vec3(0, 0, 1) * decal.half_extents.z;
		addCube(view, tr.pos, x, y, z, Color::BLUE);

		Gizmo::Config cfg;
		const DVec3 p0 = tr.transform(DVec3(decal.bezier_p0.x, 0, decal.bezier_p0.y));
		Transform p0_tr = { p0, Quat::IDENTITY, 1 };
		WorldEditor& editor = view.getEditor();
		if (Gizmo::manipulate((u64(1) << 32) | cmp.entity.index, view, p0_tr, cfg)) {
			const Vec2 p0 = Vec2(tr.inverted().transform(p0_tr.pos).xz());
			editor.setProperty(CURVE_DECAL_TYPE, "", 0, "Bezier P0", Span(&e, 1), p0);
		}

		const DVec3 p2 = tr.transform(DVec3(decal.bezier_p2.x, 0, decal.bezier_p2.y));
		Transform p2_tr = { p2, Quat::IDENTITY, 1 };
		if (Gizmo::manipulate((u64(2) << 32) | cmp.entity.index, view, p2_tr, cfg)) {
			const Vec2 p2 = Vec2(tr.inverted().transform(p2_tr.pos).xz());
			editor.setProperty(CURVE_DECAL_TYPE, "", 0, "Bezier P2", Span(&e, 1), p2);
		}

		addLine(view, tr.pos, p0_tr.pos, Color::BLUE);
		addLine(view, tr.pos, p2_tr.pos, Color::GREEN);
	}

	void showCameraGizmo(UniverseView& view, ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);

		addFrustum(view, scene->getCameraFrustum((EntityRef)cmp.entity), Color::BLUE);
	}

	bool showGizmo(UniverseView& view, ComponentUID cmp) override
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
		IAllocator& allocator = m_app.getAllocator();

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(m_model_plugin);
		asset_browser.removePlugin(m_particle_emitter_plugin);
		asset_browser.removePlugin(m_material_plugin);
		asset_browser.removePlugin(m_font_plugin);
		asset_browser.removePlugin(m_texture_plugin);
		asset_browser.removePlugin(m_shader_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();
		asset_compiler.removePlugin(m_font_plugin);
		asset_compiler.removePlugin(m_shader_plugin);
		asset_compiler.removePlugin(m_texture_plugin);
		asset_compiler.removePlugin(m_model_plugin);
		asset_compiler.removePlugin(m_material_plugin);
		asset_compiler.removePlugin(m_particle_emitter_plugin);
		asset_compiler.removePlugin(m_pipeline_plugin);

		m_app.removePlugin(*m_particle_editor.get());
		m_app.removePlugin(m_scene_view);
		m_app.removePlugin(m_game_view);
		m_app.removePlugin(m_editor_ui_render_plugin);
		m_app.removePlugin(m_spline_geom_plugin);

		PropertyGrid& property_grid = m_app.getPropertyGrid();

		property_grid.removePlugin(m_model_properties_plugin);
		property_grid.removePlugin(m_env_probe_plugin);
		property_grid.removePlugin(m_terrain_plugin);
		property_grid.removePlugin(m_instanced_model_plugin);
		property_grid.removePlugin(m_particle_emitter_property_plugin);
		property_grid.removePlugin(m_spline_geom_plugin);
	}

	StudioApp& m_app;
	UniquePtr<ParticleEditor> m_particle_editor;
	EditorUIRenderPlugin m_editor_ui_render_plugin;
	MaterialPlugin m_material_plugin;
	ParticleEmitterPlugin m_particle_emitter_plugin;
	ParticleEmitterPropertyPlugin m_particle_emitter_property_plugin;
	SplineGeometryPlugin m_spline_geom_plugin;
	PipelinePlugin m_pipeline_plugin;
	FontPlugin m_font_plugin;
	ShaderPlugin m_shader_plugin;
	ModelPropertiesPlugin m_model_properties_plugin;
	TexturePlugin m_texture_plugin;
	GameView m_game_view;
	SceneView m_scene_view;
	EnvironmentProbePlugin m_env_probe_plugin;
	TerrainPlugin m_terrain_plugin;
	InstancedModelPlugin m_instanced_model_plugin;
	ModelPlugin m_model_plugin;
};


LUMIX_STUDIO_ENTRY(renderer)
{
	auto& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
