#include "renderer.h"

#include "engine/array.h"
#include "engine/command_line_parser.h"
#include "engine/crc32.h"
#include "engine/debug/debug.h"
#include "engine/engine.h"
#include "engine/fs/os_file.h"
#include "engine/log.h"
#include "engine/mt/sync.h"
#include "engine/mt/task.h"
#include "engine/profiler.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/string.h"
#include "engine/system.h"
#include "engine/universe/component.h"
#include "engine/universe/universe.h"
#include "renderer/draw2d.h"
#include "renderer/font_manager.h"
#include "renderer/global_state_uniforms.h"
#include "renderer/material.h"
#include "renderer/material_manager.h"
#include "renderer/model.h"
#include "renderer/model_manager.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/shader.h"
#include "renderer/shader_manager.h"
#include "renderer/terrain.h"
#include "renderer/texture.h"
#include "renderer/texture_manager.h"

#include <Windows.h>
#include "gl/GL.h"
#include "ffr/ffr.h"
#include <cstdio>

#define FFR_GL_IMPORT(prototype, name) static prototype name;
#define FFR_GL_IMPORT_TYPEDEFS

#include "ffr/gl_ext.h"

#define CHECK_GL(gl) \
	do { \
		gl; \
		GLenum err = glGetError(); \
		if (err != GL_NO_ERROR) { \
			g_log_error.log("Renderer") << "OpenGL error " << err; \
		} \
	} while(0)

namespace Lumix
{

namespace DDS
{

static const uint DDS_MAGIC = 0x20534444; //  little-endian
static const uint DDSD_CAPS = 0x00000001;
static const uint DDSD_HEIGHT = 0x00000002;
static const uint DDSD_WIDTH = 0x00000004;
static const uint DDSD_PITCH = 0x00000008;
static const uint DDSD_PIXELFORMAT = 0x00001000;
static const uint DDSD_MIPMAPCOUNT = 0x00020000;
static const uint DDSD_LINEARSIZE = 0x00080000;
static const uint DDSD_DEPTH = 0x00800000;
static const uint DDPF_ALPHAPIXELS = 0x00000001;
static const uint DDPF_FOURCC = 0x00000004;
static const uint DDPF_INDEXED = 0x00000020;
static const uint DDPF_RGB = 0x00000040;
static const uint DDSCAPS_COMPLEX = 0x00000008;
static const uint DDSCAPS_TEXTURE = 0x00001000;
static const uint DDSCAPS_MIPMAP = 0x00400000;
static const uint DDSCAPS2_CUBEMAP = 0x00000200;
static const uint DDSCAPS2_CUBEMAP_POSITIVEX = 0x00000400;
static const uint DDSCAPS2_CUBEMAP_NEGATIVEX = 0x00000800;
static const uint DDSCAPS2_CUBEMAP_POSITIVEY = 0x00001000;
static const uint DDSCAPS2_CUBEMAP_NEGATIVEY = 0x00002000;
static const uint DDSCAPS2_CUBEMAP_POSITIVEZ = 0x00004000;
static const uint DDSCAPS2_CUBEMAP_NEGATIVEZ = 0x00008000;
static const uint DDSCAPS2_VOLUME = 0x00200000;
static const uint D3DFMT_DXT1 = '1TXD';
static const uint D3DFMT_DXT2 = '2TXD';
static const uint D3DFMT_DXT3 = '3TXD';
static const uint D3DFMT_DXT4 = '4TXD';
static const uint D3DFMT_DXT5 = '5TXD';

struct PixelFormat {
	uint dwSize;
	uint dwFlags;
	uint dwFourCC;
	uint dwRGBBitCount;
	uint dwRBitMask;
	uint dwGBitMask;
	uint dwBBitMask;
	uint dwAlphaBitMask;
};

struct Caps2 {
	uint dwCaps1;
	uint dwCaps2;
	uint dwDDSX;
	uint dwReserved;
};

struct Header {
	uint dwMagic;
	uint dwSize;
	uint dwFlags;
	uint dwHeight;
	uint dwWidth;
	uint dwPitchOrLinearSize;
	uint dwDepth;
	uint dwMipMapCount;
	uint dwReserved1[11];

	PixelFormat pixelFormat;
	Caps2 caps2;

	uint dwReserved2;
};

struct LoadInfo {
	bool compressed;
	bool swap;
	bool palette;
	uint divSize;
	uint blockBytes;
	GLenum internalFormat;
	GLenum internalSRGBFormat;
	GLenum externalFormat;
	GLenum type;
};

static uint sizeDXTC(uint w, uint h, GLuint format) {
    const bool is_dxt1 = format == GL_COMPRESSED_RGBA_S3TC_DXT1_EXT || format == GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT;
	return ((w + 3) / 4) * ((h + 3) / 4) * (is_dxt1 ? 8 : 16);
}

static bool isDXT1(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT1));
}

static bool isDXT3(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT3));

}

static bool isDXT5(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_FOURCC) && (pf.dwFourCC == D3DFMT_DXT5));
}

static bool isBGRA8(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& (pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 32)
		&& (pf.dwRBitMask == 0xff0000)
		&& (pf.dwGBitMask == 0xff00)
		&& (pf.dwBBitMask == 0xff)
		&& (pf.dwAlphaBitMask == 0xff000000U));
}

static bool isBGR8(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_ALPHAPIXELS)
		&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 24)
		&& (pf.dwRBitMask == 0xff0000)
		&& (pf.dwGBitMask == 0xff00)
		&& (pf.dwBBitMask == 0xff));
}

static bool isBGR5A1(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& (pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 16)
		&& (pf.dwRBitMask == 0x00007c00)
		&& (pf.dwGBitMask == 0x000003e0)
		&& (pf.dwBBitMask == 0x0000001f)
		&& (pf.dwAlphaBitMask == 0x00008000));
}

static bool isBGR565(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_RGB)
		&& !(pf.dwFlags & DDPF_ALPHAPIXELS)
		&& (pf.dwRGBBitCount == 16)
		&& (pf.dwRBitMask == 0x0000f800)
		&& (pf.dwGBitMask == 0x000007e0)
		&& (pf.dwBBitMask == 0x0000001f));
}

static bool isINDEX8(PixelFormat& pf)
{
	return ((pf.dwFlags & DDPF_INDEXED) && (pf.dwRGBBitCount == 8));
}

static LoadInfo loadInfoDXT1 = {
	true, false, false, 4, 8, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
};
static LoadInfo loadInfoDXT3 = {
	true, false, false, 4, 16, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
};
static LoadInfo loadInfoDXT5 = {
	true, false, false, 4, 16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
};
static LoadInfo loadInfoBGRA8 = {
	false, false, false, 1, 4, GL_RGBA8, GL_SRGB8_ALPHA8, GL_BGRA, GL_UNSIGNED_BYTE
};
static LoadInfo loadInfoBGR8 = {
	false, false, false, 1, 3, GL_RGB8, GL_SRGB8, GL_BGR, GL_UNSIGNED_BYTE
};
static LoadInfo loadInfoBGR5A1 = {
	false, true, false, 1, 2, GL_RGB5_A1, GL_ZERO, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV
};
static LoadInfo loadInfoBGR565 = {
	false, true, false, 1, 2, GL_RGB5, GL_ZERO, GL_RGB, GL_UNSIGNED_SHORT_5_6_5
};
static LoadInfo loadInfoIndex8 = {
	false, false, true, 1, 1, GL_RGB8, GL_SRGB8, GL_BGRA, GL_UNSIGNED_BYTE
};

struct DXTColBlock
{
	u16 col0;
	u16 col1;
	u8 row[4];
};

struct DXT3AlphaBlock
{
	u16 row[4];
};

struct DXT5AlphaBlock
{
	u8 alpha0;
	u8 alpha1;
	u8 row[6];
};

static LUMIX_FORCE_INLINE void swapMemory(void* mem1, void* mem2, int size, IAllocator& allocator)
{
	if(size < 2048)
	{
		u8 tmp[2048];
		memcpy(tmp, mem1, size);
		memcpy(mem1, mem2, size);
		memcpy(mem2, tmp, size);
	}
	else
	{
		Array<u8> tmp(allocator);
		tmp.resize(size);
		memcpy(&tmp[0], mem1, size);
		memcpy(mem1, mem2, size);
		memcpy(mem2, &tmp[0], size);
	}
}

static void flipBlockDXTC1(DXTColBlock *line, int numBlocks, IAllocator& allocator)
{
	DXTColBlock *curblock = line;

	for (int i = 0; i < numBlocks; i++)
	{
		swapMemory(&curblock->row[0], &curblock->row[3], sizeof(u8), allocator);
		swapMemory(&curblock->row[1], &curblock->row[2], sizeof(u8), allocator);
		++curblock;
	}
}

static void flipBlockDXTC3(DXTColBlock *line, int numBlocks, IAllocator& allocator)
{
	DXTColBlock *curblock = line;
	DXT3AlphaBlock *alphablock;

	for (int i = 0; i < numBlocks; i++)
	{
		alphablock = (DXT3AlphaBlock*)curblock;

		swapMemory(&alphablock->row[0], &alphablock->row[3], sizeof(u16), allocator);
		swapMemory(&alphablock->row[1], &alphablock->row[2], sizeof(u16), allocator);
		++curblock;

		swapMemory(&curblock->row[0], &curblock->row[3], sizeof(u8), allocator);
		swapMemory(&curblock->row[1], &curblock->row[2], sizeof(u8), allocator);
		++curblock;
	}
}

static void flipDXT5Alpha(DXT5AlphaBlock *block)
{
	u8 tmp_bits[4][4];

	const uint mask = 0x00000007;
	uint bits = 0;
	memcpy(&bits, &block->row[0], sizeof(u8) * 3);

	tmp_bits[0][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[0][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[0][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[0][3] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[1][3] = (u8)(bits & mask);

	bits = 0;
	memcpy(&bits, &block->row[3], sizeof(u8) * 3);

	tmp_bits[2][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[2][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[2][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[2][3] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][0] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][1] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][2] = (u8)(bits & mask);
	bits >>= 3;
	tmp_bits[3][3] = (u8)(bits & mask);

	uint *out_bits = (uint*)&block->row[0];

	*out_bits = *out_bits | (tmp_bits[3][0] << 0);
	*out_bits = *out_bits | (tmp_bits[3][1] << 3);
	*out_bits = *out_bits | (tmp_bits[3][2] << 6);
	*out_bits = *out_bits | (tmp_bits[3][3] << 9);

	*out_bits = *out_bits | (tmp_bits[2][0] << 12);
	*out_bits = *out_bits | (tmp_bits[2][1] << 15);
	*out_bits = *out_bits | (tmp_bits[2][2] << 18);
	*out_bits = *out_bits | (tmp_bits[2][3] << 21);

	out_bits = (uint*)&block->row[3];

	*out_bits &= 0xff000000;

	*out_bits = *out_bits | (tmp_bits[1][0] << 0);
	*out_bits = *out_bits | (tmp_bits[1][1] << 3);
	*out_bits = *out_bits | (tmp_bits[1][2] << 6);
	*out_bits = *out_bits | (tmp_bits[1][3] << 9);

	*out_bits = *out_bits | (tmp_bits[0][0] << 12);
	*out_bits = *out_bits | (tmp_bits[0][1] << 15);
	*out_bits = *out_bits | (tmp_bits[0][2] << 18);
	*out_bits = *out_bits | (tmp_bits[0][3] << 21);
}

static void flipBlockDXTC5(DXTColBlock *line, int numBlocks, IAllocator& allocator)
{
	DXTColBlock *curblock = line;
	DXT5AlphaBlock *alphablock;

	for (int i = 0; i < numBlocks; i++)
	{
		alphablock = (DXT5AlphaBlock*)curblock;

		flipDXT5Alpha(alphablock);

		++curblock;

		swapMemory(&curblock->row[0], &curblock->row[3], sizeof(u8), allocator);
		swapMemory(&curblock->row[1], &curblock->row[2], sizeof(u8), allocator);

		++curblock;
	}
}

/// from gpu gems
static void flipCompressedTexture(int w, int h, int format, void* surface, IAllocator& allocator)
{
	void (*flipBlocksFunction)(DXTColBlock*, int, IAllocator&);
	int xblocks = w >> 2;
	int yblocks = h >> 2;
	int blocksize;

	switch (format)
	{
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
			blocksize = 8;
			flipBlocksFunction = &flipBlockDXTC1;
			break;
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
			blocksize = 16;
			flipBlocksFunction = &flipBlockDXTC3;
			break;
		case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
		case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
			blocksize = 16;
			flipBlocksFunction = &flipBlockDXTC5;
			break;
		default:
			ASSERT(false);
			return;
	}

	int linesize = xblocks * blocksize;

	DXTColBlock *top = (DXTColBlock*)surface;
	DXTColBlock *bottom = (DXTColBlock*)((u8*)surface + ((yblocks - 1) * linesize));

	while (top < bottom)
	{
		(*flipBlocksFunction)(top, xblocks, allocator);
		(*flipBlocksFunction)(bottom, xblocks, allocator);
		swapMemory(bottom, top, linesize, allocator);

		top = (DXTColBlock*)((u8*)top + linesize);
		bottom = (DXTColBlock*)((u8*)bottom - linesize);
	}
}


} // namespace DDS


static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("renderable");

struct FrameContext
{
	struct GenericCommand
	{
		virtual ~GenericCommand() {}
		virtual void execute() = 0;
	};

	FrameContext(IAllocator& allocator)
		: allocator(allocator)
		, pre_commands(allocator)
		, post_commands(allocator)
	{}

	~FrameContext()
	{
		for(auto* cmd : pre_commands) {
			LUMIX_DELETE(allocator, cmd);
		}
		for(auto* cmd : post_commands) {
			LUMIX_DELETE(allocator, cmd);
		}
	}

	IAllocator& allocator;
	Array<GenericCommand*> pre_commands;
	Array<GenericCommand*> post_commands;

	struct {
		Renderer::RenderCommandBase* cmd;
		void* setup_data;
	} commands[256];
	int commands_count;
};


struct RenderTask : MT::Task
{
	RenderTask(Renderer& renderer, IAllocator& allocator) 
		: MT::Task(allocator)
		, m_allocator(allocator)
		, m_renderer(renderer)
		, m_ready_event(false)
		, m_can_push_event(false)
	{}


	void init()
	{
		void* window_handle = m_renderer.getEngine().getPlatformData().window_handle;
		ffr::init(window_handle, m_allocator);
		m_framebuffer = ffr::createFramebuffer(0, nullptr);
		m_renderer.getGlobalStateUniforms().create();
	}

	int task() override {
		init();
		m_can_push_event.trigger();
		for(;;) {
			m_ready_event.wait();
			FrameContext* ctx = m_frame_context;
			m_frame_context = nullptr;
			m_can_push_event.trigger();

			for(FrameContext::GenericCommand* cmd : ctx->pre_commands) {
				cmd->execute();
			}

			for(int i = 0, c = ctx->commands_count; i < c; ++i) {
				ctx->commands[i].cmd->execute(ctx->commands[i].setup_data);
			}

			for(FrameContext::GenericCommand* cmd : ctx->post_commands) {
				cmd->execute();
			}

			LUMIX_DELETE(m_allocator, ctx);
		}
		return 0;
	}

	void push(FrameContext* ctx)
	{
		m_can_push_event.wait();
		m_frame_context = ctx;
		m_ready_event.trigger();
	}

	IAllocator& m_allocator;
	Renderer& m_renderer;
	FrameContext* m_frame_context;
	MT::Event m_ready_event;
	MT::Event m_can_push_event;
	ffr::FramebufferHandle m_framebuffer;
};

struct BoneProperty : Reflection::IEnumProperty
{
	BoneProperty() 
	{ 
		name = "Bone"; 
		getter_code = "RenderScene::getBoneAttachmentBone";
		setter_code = "RenderScene::setBoneAttachmentBone";
	}


	void getValue(ComponentUID cmp, int index, OutputBlob& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = scene->getBoneAttachmentBone(cmp.entity);
		stream.write(value);
	}


	void setValue(ComponentUID cmp, int index, InputBlob& stream) const override
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		int value = stream.read<int>();
		scene->setBoneAttachmentBone(cmp.entity, value);
	}


	Entity getModelInstance(RenderScene* render_scene, Entity bone_attachment) const
	{
		Entity parent_entity = render_scene->getBoneAttachmentParent(bone_attachment);
		if (parent_entity == INVALID_ENTITY) return INVALID_ENTITY;
		return render_scene->getUniverse().hasComponent(parent_entity, MODEL_INSTANCE_TYPE) ? parent_entity : INVALID_ENTITY;
	}


	int getEnumValueIndex(ComponentUID cmp, int value) const override  { return value; }
	int getEnumValue(ComponentUID cmp, int index) const override { return index; }


	int getEnumCount(ComponentUID cmp) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		Entity model_instance = getModelInstance(render_scene, cmp.entity);
		if (!model_instance.isValid()) return 0;

		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model || !model->isReady()) return 0;

		return model->getBoneCount();
	}


	const char* getEnumName(ComponentUID cmp, int index) const override
	{
		RenderScene* render_scene = static_cast<RenderScene*>(cmp.scene);
		Entity model_instance = getModelInstance(render_scene, cmp.entity);
		if (!model_instance.isValid()) return "";

		auto* model = render_scene->getModelInstanceModel(model_instance);
		if (!model) return "";

		return model->getBone(index).name.c_str();
	}
};


static void registerProperties(IAllocator& allocator)
{
	using namespace Reflection;

	static auto rotationModeDesc = enumDesciptor<Terrain::GrassType::RotationMode>(
		LUMIX_ENUM_VALUE(Terrain::GrassType::RotationMode::ALL_RANDOM),
		LUMIX_ENUM_VALUE(Terrain::GrassType::RotationMode::Y_UP),
		LUMIX_ENUM_VALUE(Terrain::GrassType::RotationMode::ALIGN_WITH_NORMAL)
	);
	registerEnum(rotationModeDesc);

	static auto render_scene = scene("renderer", 
		component("bone_attachment",
			property("Parent", LUMIX_PROP(RenderScene, BoneAttachmentParent)),
			property("Relative position", LUMIX_PROP(RenderScene, BoneAttachmentPosition)),
			property("Relative rotation", LUMIX_PROP(RenderScene, BoneAttachmentRotation), 
				RadiansAttribute()),
			BoneProperty()
		),
		component("particle_emitter_spawn_shape",
			property("Radius", LUMIX_PROP(RenderScene, ParticleEmitterShapeRadius))
		),
		component("particle_emitter_plane",
			property("Bounce", LUMIX_PROP(RenderScene, ParticleEmitterPlaneBounce),
				ClampAttribute(0, 1)),
			array("Planes", &RenderScene::getParticleEmitterPlaneCount, &RenderScene::addParticleEmitterPlane, &RenderScene::removeParticleEmitterPlane, 
				property("Entity", LUMIX_PROP(RenderScene, ParticleEmitterPlaneEntity))
			)
		),
		component("particle_emitter_attractor",
			property("Force", LUMIX_PROP(RenderScene, ParticleEmitterAttractorForce)),
			array("Attractors", &RenderScene::getParticleEmitterAttractorCount, &RenderScene::addParticleEmitterAttractor, &RenderScene::removeParticleEmitterAttractor,
				property("Entity", LUMIX_PROP(RenderScene, ParticleEmitterAttractorEntity))
			)
		),
		component("particle_emitter_alpha",
			sampled_func_property("Alpha", LUMIX_PROP(RenderScene, ParticleEmitterAlpha), &RenderScene::getParticleEmitterAlphaCount, 1)
		),
		component("particle_emitter_random_rotation"),
		component("environment_probe",
			property("Enabled reflection", LUMIX_PROP_FULL(RenderScene, isEnvironmentProbeReflectionEnabled, enableEnvironmentProbeReflection)),
			property("Override global size", LUMIX_PROP_FULL(RenderScene, isEnvironmentProbeCustomSize, enableEnvironmentProbeCustomSize)),
			property("Radiance size", LUMIX_PROP(RenderScene, EnvironmentProbeRadianceSize)),
			property("Irradiance size", LUMIX_PROP(RenderScene, EnvironmentProbeIrradianceSize))
		),
		component("particle_emitter_force",
			property("Acceleration", LUMIX_PROP(RenderScene, ParticleEmitterAcceleration))
		),
		component("particle_emitter_subimage",
			property("Rows", LUMIX_PROP(RenderScene, ParticleEmitterSubimageRows)),
			property("Columns", LUMIX_PROP(RenderScene, ParticleEmitterSubimageCols))
		),
		component("particle_emitter_size",
			sampled_func_property("Size", LUMIX_PROP(RenderScene, ParticleEmitterSize), &RenderScene::getParticleEmitterSizeCount, 1)
		),
		component("scripted_particle_emitter",
			property("Material", LUMIX_PROP(RenderScene, ScriptedParticleEmitterMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE))
		),
		component("particle_emitter",
			property("Life", LUMIX_PROP(RenderScene, ParticleEmitterInitialLife)),
			property("Initial size", LUMIX_PROP(RenderScene, ParticleEmitterInitialSize)),
			property("Spawn period", LUMIX_PROP(RenderScene, ParticleEmitterSpawnPeriod)),
			property("Autoemit", LUMIX_PROP(RenderScene, ParticleEmitterAutoemit)),
			property("Local space", LUMIX_PROP(RenderScene, ParticleEmitterLocalSpace)),
			property("Material", LUMIX_PROP(RenderScene, ParticleEmitterMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("Spawn count", LUMIX_PROP(RenderScene, ParticleEmitterSpawnCount))
		),
		component("particle_emitter_linear_movement",
			property("x", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementX)),
			property("y", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementY)),
			property("z", LUMIX_PROP(RenderScene, ParticleEmitterLinearMovementZ))
		),
		component("camera",
			property("Orthographic size", LUMIX_PROP(RenderScene, CameraOrthoSize), 
				MinAttribute(0)),
			property("Orthographic", LUMIX_PROP_FULL(RenderScene, isCameraOrtho, setCameraOrtho)),
			property("FOV", LUMIX_PROP(RenderScene, CameraFOV),
				RadiansAttribute()),
			property("Near", LUMIX_PROP(RenderScene, CameraNearPlane), 
				MinAttribute(0)),
			property("Far", LUMIX_PROP(RenderScene, CameraFarPlane), 
				MinAttribute(0))
		),
		component("renderable",
			property("Enabled", LUMIX_PROP_FULL(RenderScene, isModelInstanceEnabled, enableModelInstance)),
			property("Source", LUMIX_PROP(RenderScene, ModelInstancePath),
				ResourceAttribute("Mesh (*.msh)", Model::TYPE)),
			const_array("Materials", &RenderScene::getModelInstanceMaterialsCount, 
				property("Source", LUMIX_PROP(RenderScene, ModelInstanceMaterial),
					ResourceAttribute("Material (*.mat)", Material::TYPE))
			)
		),
		component("global_light",
			property("Color", LUMIX_PROP(RenderScene, GlobalLightColor),
				ColorAttribute()),
			property("Intensity", LUMIX_PROP(RenderScene, GlobalLightIntensity), 
				MinAttribute(0)),
			property("Indirect intensity", LUMIX_PROP(RenderScene, GlobalLightIndirectIntensity), MinAttribute(0)),
			property("Fog density", LUMIX_PROP(RenderScene, FogDensity),
				ClampAttribute(0, 1)),
			property("Fog bottom", LUMIX_PROP(RenderScene, FogBottom)),
			property("Fog height", LUMIX_PROP(RenderScene, FogHeight), 
				MinAttribute(0)),
			property("Fog color", LUMIX_PROP(RenderScene, FogColor),
				ColorAttribute()),
			property("Shadow cascades", LUMIX_PROP(RenderScene, ShadowmapCascades))
		),
		component("point_light",
			property("Diffuse color", LUMIX_PROP(RenderScene, PointLightColor),
				ColorAttribute()),
			property("Specular color", LUMIX_PROP(RenderScene, PointLightSpecularColor),
				ColorAttribute()),
			property("Diffuse intensity", LUMIX_PROP(RenderScene, PointLightIntensity), 
				MinAttribute(0)),
			property("Specular intensity", LUMIX_PROP(RenderScene, PointLightSpecularIntensity)),
			property("FOV", LUMIX_PROP(RenderScene, LightFOV), 
				ClampAttribute(0, 360),
				RadiansAttribute()),
			property("Attenuation", LUMIX_PROP(RenderScene, LightAttenuation),
				ClampAttribute(0, 1000)),
			property("Range", LUMIX_PROP(RenderScene, LightRange), 
				MinAttribute(0)),
			property("Cast shadows", LUMIX_PROP(RenderScene, LightCastShadows), 
				MinAttribute(0))
		),
		component("text_mesh",
			property("Text", LUMIX_PROP(RenderScene, TextMeshText)),
			property("Font", LUMIX_PROP(RenderScene, TextMeshFontPath),
				ResourceAttribute("Font (*.ttf)", FontResource::TYPE)),
			property("Font Size", LUMIX_PROP(RenderScene, TextMeshFontSize)),
			property("Color", LUMIX_PROP(RenderScene, TextMeshColorRGBA),
				ColorAttribute()),
			property("Camera-oriented", LUMIX_PROP_FULL(RenderScene, isTextMeshCameraOriented, setTextMeshCameraOriented))
		),
		component("decal",
			property("Material", LUMIX_PROP(RenderScene, DecalMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("Scale", LUMIX_PROP(RenderScene, DecalScale), 
				MinAttribute(0))
		),
		component("terrain",
			property("Material", LUMIX_PROP(RenderScene, TerrainMaterialPath),
				ResourceAttribute("Material (*.mat)", Material::TYPE)),
			property("XZ scale", LUMIX_PROP(RenderScene, TerrainXZScale), 
				MinAttribute(0)),
			property("Height scale", LUMIX_PROP(RenderScene, TerrainYScale), 
				MinAttribute(0)),
			array("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass,
				property("Mesh", LUMIX_PROP(RenderScene, GrassPath),
					ResourceAttribute("Mesh (*.msh)", Model::TYPE)),
				property("Distance", LUMIX_PROP(RenderScene, GrassDistance),
					MinAttribute(1)),
				property("Density", LUMIX_PROP(RenderScene, GrassDensity)),
				enum_property("Mode", LUMIX_PROP(RenderScene, GrassRotationMode), rotationModeDesc)
			)
		)
	);
	registerScene(render_scene);
}


struct RendererImpl LUMIX_FINAL : public Renderer
{
	struct TextureRecord
	{
		ffr::TextureHandle handle;
		uint w;
		uint h;
	};

	explicit RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(*this, m_allocator)
		, m_model_manager(*this, m_allocator)
		, m_material_manager(*this, m_allocator)
		, m_shader_manager(*this, m_allocator)
		, m_font_manager(nullptr)
		, m_shader_defines(m_allocator)
		, m_layers(m_allocator)
		, m_vsync(true)
		, m_main_pipeline(nullptr)
		, m_render_task(*this, m_allocator)
		, m_textures(m_allocator)
		, m_commands(m_allocator)
		, m_first_free_texture(0)
	{
		m_frame_context = LUMIX_NEW(m_allocator, FrameContext)(m_allocator);
		m_textures.resize(4096);
		for(int i = 0; i < m_textures.size() - 1; ++i) {
			m_textures[i].handle.value = i + 1;
		}
		m_textures.back().handle.value = -1;

		registerProperties(engine.getAllocator());
		char cmd_line[4096];
		getCommandLine(cmd_line, lengthOf(cmd_line));
		CommandLineParser cmd_line_parser(cmd_line);
		m_vsync = true;
		while (cmd_line_parser.next())
		{
			if (cmd_line_parser.currentEquals("-no_vsync"))
			{
				m_vsync = false;
				break;
			}
		}

		ResourceManager& manager = engine.getResourceManager();
		m_texture_manager.create(Texture::TYPE, manager);
		m_model_manager.create(Model::TYPE, manager);
		m_material_manager.create(Material::TYPE, manager);
		m_shader_manager.create(Shader::TYPE, manager);
		m_font_manager = LUMIX_NEW(m_allocator, FontManager)(*this, m_allocator);
		m_font_manager->create(FontResource::TYPE, manager);

		m_default_shader = static_cast<Shader*>(m_shader_manager.load(Path("pipelines/standard.shd")));
		RenderScene::registerLuaAPI(m_engine.getState());
		m_layers.emplace("default");
		m_layers.emplace("transparent");
		m_layers.emplace("water");
		m_layers.emplace("fur");

		m_render_task.create("render task");
	}


	~RendererImpl()
	{
		m_shader_manager.unload(*m_default_shader);
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_font_manager->destroy();
		LUMIX_DELETE(m_allocator, m_font_manager);

		m_global_state_uniforms.destroy();

		frame(false);
		frame(false);
	}


	MemRef copy(const void* data, uint size) override
	{
		MemRef mem = allocate(size);
		copyMemory(mem.data, data, size);
		return mem;
	}


	IAllocator& getAllocator() override
	{
		return m_allocator;
	}


	MemRef allocate(uint size) override
	{
		MemRef ret;
		ret.size = size;
		ret.own = true;
		ret.data = m_allocator.allocate(size);
		return ret;
	}


	ffr::FramebufferHandle getFramebuffer() const override
	{
		return m_render_task.m_framebuffer;
	}


	TextureHandle loadTexture(const MemRef& memory, u32 flags, ffr::TextureInfo* info) override
	{
		// TODO
		TextureHandle t;
		t.reset();
		return t;
	/*	ASSERT(memory.size > 0);
		TextureHandle t;
		if (m_first_free_texture < 0) {
			g_log_error.log("Renderer") << "Out of texture slots.";
			t.reset();
			return t;
		}

		t.value = m_first_free_texture;
		TextureRecord& rec = m_textures[m_first_free_texture];
		m_first_free_texture = rec.handle;

		const DDS::Header* header = (DDS::Header*)memory.data;
		rec.w = header->dwWidth;
		rec.h = header->dwHeight;
		rec.handle = 0;

		struct Cmd : RenderTask::GenericCommand {
			void execute() override {
			}

			TextureRecord* texture;
			MemRef memory;
		};

		Cmd* cmd = LUMIX_NEW(m_render_task.m_allocator, Cmd);
		cmd->texture = &rec;
		cmd->memory = memory;
		m_render_task.pushPreframe(cmd);


		return t;*/
	}


	TextureHandle createTexture(uint w, uint h, ffr::TextureFormat format, u32 flags, const MemRef& memory) override
	{
		TextureHandle t;
		if(m_first_free_texture < 0) {
			g_log_error.log("Renderer") << "Out of texture slots.";
			t.reset();
			return t;
		}
		t.value = m_first_free_texture;
		TextureRecord& rec = m_textures[m_first_free_texture];
		m_first_free_texture = rec.handle.value;
		rec.handle = ffr::INVALID_TEXTURE;
		rec.w = w;
		rec.h = h;

		struct Cmd : FrameContext::GenericCommand {
			void execute() override {
				texture->handle = ffr::createTexture(texture->w, texture->h, format, 0, memory.data);
			}

			TextureRecord* texture;
			MemRef memory;
			ffr::TextureFormat format;
		};

		Cmd* cmd = LUMIX_NEW(m_allocator, Cmd);
		cmd->texture = &rec;
		cmd->memory = memory;
		cmd->format = format;
		m_frame_context->pre_commands.push(cmd);

		return t;
	}


	ffr::TextureHandle getFFRHandle(TextureHandle tex) const override
	{
		return m_textures[tex.value].handle;
	}


	void destroy(TextureHandle tex)
	{
		
	}


	void push(RenderCommandBase* cmd) override
	{
		m_commands.push(cmd);
	}


	void setMainPipeline(Pipeline* pipeline) override
	{
		m_main_pipeline = pipeline;
	}


	GlobalStateUniforms& getGlobalStateUniforms() override
	{
		return m_global_state_uniforms;
	}


	Pipeline* getMainPipeline() override
	{
		return m_main_pipeline;
	}


	int getLayer(const char* name) override
	{
		for (int i = 0; i < m_layers.size(); ++i)
		{
			if (m_layers[i] == name) return i;
		}
		ASSERT(m_layers.size() < 64);
		m_layers.emplace() = name;
		return m_layers.size() - 1;
	}


	int getLayersCount() const override { return m_layers.size(); }
	const char* getLayerName(int idx) const override { return m_layers[idx]; }


	ModelManager& getModelManager() override { return m_model_manager; }
	MaterialManager& getMaterialManager() override { return m_material_manager; }
	ShaderManager& getShaderManager() override { return m_shader_manager; }
	TextureManager& getTextureManager() override { return m_texture_manager; }
	FontManager& getFontManager() override { return *m_font_manager; }
// TODO
	/*
	const bgfx::VertexDecl& getBasicVertexDecl() const override { static bgfx::VertexDecl v; return v; }
	const bgfx::VertexDecl& getBasic2DVertexDecl() const override { static bgfx::VertexDecl v; return v; }
	*/

	void createScenes(Universe& ctx) override
	{
		auto* scene = RenderScene::createInstance(*this, m_engine, ctx, m_allocator);
		ctx.addScene(scene);
	}


	void destroyScene(IScene* scene) override { RenderScene::destroyInstance(static_cast<RenderScene*>(scene)); }
	const char* getName() const override { return "renderer"; }
	Engine& getEngine() override { return m_engine; }
	int getShaderDefinesCount() const override { return m_shader_defines.size(); }
	const char* getShaderDefine(int define_idx) const override { return m_shader_defines[define_idx]; }
// TODO
	/*
	const bgfx::UniformHandle& getMaterialColorUniform() const override { static bgfx::UniformHandle v; return v; }
	const bgfx::UniformHandle& getRoughnessMetallicEmissionUniform() const override { static bgfx::UniformHandle v; return v; }
	*/
	void makeScreenshot(const Path& filename) override {  }
	void resize(int w, int h) override {  }
	Shader* getDefaultShader() override { return m_default_shader; }


	u8 getShaderDefineIdx(const char* define) override
	{
		for (int i = 0; i < m_shader_defines.size(); ++i)
		{
			if (m_shader_defines[i] == define)
			{
				return i;
			}
		}

		if (m_shader_defines.size() >= MAX_SHADER_DEFINES) {
			ASSERT(false);
			g_log_error.log("Renderer") << "Too many shader defines.";
		}

		m_shader_defines.emplace(define);
		return m_shader_defines.size() - 1;
	}


	void frame(bool capture) override
	{
		m_frame_context->commands_count = m_commands.size();
		for (int i = 0, c = m_commands.size(); i < c; ++i) {
			RenderCommandBase* cmd = m_commands[i];
			m_frame_context->commands[i].cmd = cmd;
			m_frame_context->commands[i].setup_data = cmd->setup();
		}
		m_render_task.push(m_frame_context);
		m_frame_context = LUMIX_NEW(m_allocator, FrameContext)(m_allocator);
	}


	using ShaderDefine = StaticString<32>;
	using Layer = StaticString<32>;


	Engine& m_engine;
	IAllocator& m_allocator;
	Array<ShaderDefine> m_shader_defines;
	Array<Layer> m_layers;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	FontManager* m_font_manager;
	ShaderManager m_shader_manager;
	ModelManager m_model_manager;
	bool m_vsync;
	Shader* m_default_shader;
	Pipeline* m_main_pipeline;
	GlobalStateUniforms m_global_state_uniforms;
	RenderTask m_render_task;
	Array<TextureRecord> m_textures;
	int m_first_free_texture;
	FrameContext* m_frame_context;
	Array<RenderCommandBase*> m_commands;
};


extern "C"
{
	LUMIX_PLUGIN_ENTRY(renderer)
	{
		return LUMIX_NEW(engine.getAllocator(), RendererImpl)(engine);
	}
}


} // namespace Lumix



