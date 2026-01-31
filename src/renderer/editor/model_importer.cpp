#include "animation/animation.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/os.h"
#include "core/profiler.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
#include "engine/component_types.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/plugin.h"
#include "engine/world.h"
#include "meshoptimizer/meshoptimizer.h"
#include "model_importer.h"
#include "physics/physics_module.h"
#include "physics/physics_resources.h"
#include "physics/physics_system.h"
#include "renderer/editor/model_meta.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/pipeline.h"
#include "renderer/render_module.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/voxels.h"

namespace black {

static constexpr u32 IMPOSTOR_TILE_SIZE = 512;
static constexpr u32 IMPOSTOR_COLS = 9;

namespace {

struct BitWriter {
	BitWriter(OutputMemoryStream& blob, u32 total_bits)
		: blob(blob)
	{
		const u64 offset = blob.size();
		blob.resize(blob.size() + (total_bits + 7) / 8);
		ptr = blob.getMutableData() + offset;
		memset(ptr, 0, (total_bits + 7) / 8);
	}

	static u32 quantize(float v, float min, float max, u32 bitsize) {
		return u32(double(v - min) / (max - min) * (1 << bitsize) + 0.5f);
	}

	void write(float v, float min, float max, u32 bitsize) {
		ASSERT(bitsize < 32);
		write(quantize(v, min, max, bitsize), bitsize);
	}

	void write(u64 v, u32 bitsize) {
		u64 tmp;
		memcpy(&tmp, &ptr[cursor / 8], sizeof(tmp));
		tmp |= v << (cursor & 7);
		memcpy(&ptr[cursor / 8], &tmp, sizeof(tmp));
		cursor += bitsize;
	};

	OutputMemoryStream& blob;
	u32 cursor = 0;
	u8* ptr;
};

struct TranslationTrack {
	Vec3 min, max;
	u8 bitsizes[4] = {};
	bool is_const = false;
};

struct RotationTrack {
	Quat min, max;
	u8 bitsizes[4];
	bool is_const;
	u8 skipped_channel;
};

u64 pack(float v, float min, float range, u32 bitsize) {
	double normalized = double(v - min) / range;
	return u64(normalized * double((1 << bitsize) - 1) + 0.5f);
}

u64 pack(const Quat& r, const RotationTrack& track) {
	u64 res = 0;
	if (track.skipped_channel != 3) {
		res |= pack(r.w, track.min.w, track.max.w - track.min.w, track.bitsizes[3]);
	}
	
	if (track.skipped_channel != 2) {
		res <<= track.bitsizes[2];
		res |= pack(r.z, track.min.z, track.max.z - track.min.z, track.bitsizes[2]);
	}

	if (track.skipped_channel != 1) {
		res <<= track.bitsizes[1];
		res |= pack(r.y, track.min.y, track.max.y - track.min.y, track.bitsizes[1]);
	}

	if (track.skipped_channel != 0) {
		res <<= track.bitsizes[0];
		res |= pack(r.x, track.min.x, track.max.x - track.min.x, track.bitsizes[0]);
	}
	return res;
}

u64 pack(const Vec3& p, const TranslationTrack& track) {
	u64 res = 0;
	res |= pack(p.z, track.min.z, track.max.z - track.min.z, track.bitsizes[2]);
	res <<= track.bitsizes[1];

	res |= pack(p.y, track.min.y, track.max.y - track.min.y, track.bitsizes[1]);
	res <<= track.bitsizes[0];

	res |= pack(p.x, track.min.x, track.max.x - track.min.x, track.bitsizes[0]);
	return res;
}

bool clampBitsizes(Span<u8> values) {
	u32 total = 0;
	for (u8 v : values) total += v;
	if (total > 64) {
		u32 over = total - 64;
		u32 i =  0;
		while (over) {
			if (values[i] > 0) {
				--values[i];
				--over;
			}
			i = (i + 1) % values.length();
		}
		
		return true;
	}
	return false;
}

} // anonymous namespace

/*
static bool isBindPoseRotationTrack(u32 count, const Array<FBXImporter::Key>& keys, const Quat& bind_rot, float error) {
	if (count != 2) return false;
	for (const FBXImporter::Key& key : keys) {
		if (key.flags & 1) continue;
		if (fabs(key.rot.x - bind_rot.x) > error) return false;
		if (fabs(key.rot.y - bind_rot.y) > error) return false;
		if (fabs(key.rot.z - bind_rot.z) > error) return false;
		if (fabs(key.rot.w - bind_rot.w) > error) return false;
	}
	return true;
}
*/
static bool isBindPosePositionTrack(u32 count, const Array<ModelImporter::Key>& keys, const Vec3& bind_pos) {
	const float ERROR = 0.00001f;
	for (const ModelImporter::Key& key : keys) {
		const Vec3 d = key.pos - bind_pos;
		if (fabsf(d.x) > ERROR || fabsf(d.y) > ERROR || fabsf(d.z) > ERROR) return false;
	}
	return true;
}

static const ModelImporter::Bone* getParent(Span<const ModelImporter::Bone> bones, const ModelImporter::Bone& bone) {
	if (bone.parent_id == 0) return nullptr;
	for (const ModelImporter::Bone& b : bones) {
		if (b.id == bone.parent_id) return &b;
	}
	ASSERT(false);
	return nullptr;
}

static i32 getParentIndex(Span<const ModelImporter::Bone> bones, const ModelImporter::Bone& bone) {
	if (bone.parent_id == 0) return -1;
	for (const ModelImporter::Bone& b : bones) {
		if (b.id == bone.parent_id) return i32(&b - bones.begin());
	}
	ASSERT(false);
	return -1;
}

static bool hasAutoLOD(const ModelMeta& meta, u32 idx) {
	return meta.autolod_mask & (1 << idx);
}

static bool areIndices16Bit(const ModelImporter::ImportGeometry& mesh) {
	int vertex_size = mesh.vertex_size;
	return mesh.vertex_buffer.size() / vertex_size < (1 << 16);
}

static Vec3 impostorToWorld(Vec2 uv) {
	uv = uv * 2 - 1;
	Vec3 position= Vec3(
		uv.x + uv.y,
		0.f,
		uv.x - uv.y
	) * 0.5f;

	position.y = -(1.f - fabsf(position.x) - fabsf(position.z));
	return position;
};

static Vec2 computeBoundingCylinder(const Model& model, Vec3 center) {
	center.x = 0;
	center.z = 0;
	i32 mesh_count = model.getMeshCount();
	Vec2 bcylinder(0);
	for (i32 mesh_idx = 0; mesh_idx < mesh_count; ++mesh_idx) {
		const Mesh& mesh = model.getMesh(mesh_idx);
		if (mesh.lod != 0) continue;
		i32 vertex_count = mesh.vertices.size();
		for (i32 i = 0; i < vertex_count; ++i) {
			const Vec3 p = mesh.vertices[i] - center;
			bcylinder.x = maximum(bcylinder.x, p.x * p.x + p.z * p.z);
			bcylinder.y = maximum(bcylinder.y, fabsf(p.y));
		}
	}

	bcylinder.x = sqrtf(bcylinder.x);
	return bcylinder;
}

static Vec2 computeImpostorHalfExtents(Vec2 bounding_cylinder) {
	return {
		bounding_cylinder.x,
		sqrtf(bounding_cylinder.x * bounding_cylinder.x + bounding_cylinder.y * bounding_cylinder.y)
	};
}

ModelImporter::ModelImporter(struct StudioApp& app)
	: m_app(app)
	, m_allocator(app.getAllocator())
	, m_materials(app.getAllocator())
	, m_out_file(app.getAllocator())
	, m_bones(app.getAllocator())
	, m_meshes(app.getAllocator())
	, m_animations(app.getAllocator())
	, m_geometries(app.getAllocator())
	, m_lights(app.getAllocator())
{}


u32 ModelImporter::packF4u(const Vec3& vec) {
	const i8 xx = i8(clamp((vec.x * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
	const i8 yy = i8(clamp((vec.y * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
	const i8 zz = i8(clamp((vec.z * 0.5f + 0.5f) * 255, 0.f, 255.f) - 128);
	const i8 ww = i8(0);

	union {
		u32 ui32;
		i8 arr[4];
	} un;

	un.arr[0] = xx;
	un.arr[1] = yy;
	un.arr[2] = zz;
	un.arr[3] = ww;

	return un.ui32;
}

Vec3 ModelImporter::unpackF4u(u32 packed) {
	union {
		u32 ui32;
		i8 arr[4];
	} un;

	un.ui32 = packed;
	Vec3 res;
	res.x = un.arr[0];
	res.y = un.arr[1];
	res.z = un.arr[2];
	return ((res + Vec3(128.f)) / 255) * 2.f - 1.f;
}



void ModelImporter::writeString(const char* str) { m_out_file.write(str, stringLength(str)); }

static i32 getAttributeOffset(const ModelImporter::ImportGeometry& mesh, AttributeSemantic semantic) {
	i32 offset = 0;
	for (const auto& attr : mesh.attributes) {
		if (attr.semantic == semantic) return offset;
		offset += gpu::getSize(attr.type) * attr.num_components;
	}
	return -1;
}

void ModelImporter::postprocessCommon(const ModelMeta& meta, StringView src_filepath) {
	StringView src_dir = Path::getDir(src_filepath);
	FileSystem& filesystem = m_app.getEngine().getFileSystem();

	for (ImportMaterial& mat : m_materials) {
		// we don't support dds, but try it as last option, so user can get error message with filepath
		const char* exts[] = { "png", "jpg", "jpeg", "tga", "bmp", "dds" };
		for (ImportTexture& tex : mat.textures) {
			if (tex.path.empty()) continue;
			tex.src = tex.path;

			const bool exists = filesystem.fileExists(tex.src);
			StringView tex_ext = Path::getExtension(tex.path);

			if (!exists && (equalStrings(tex_ext, "dds") || !findTexture(src_dir, tex_ext, tex))) {
				for (const char*& ext : exts) {
					if (findTexture(src_dir, ext, tex)) {
						// we assume all texture have the same extension,
						// so we move it to the beginning, so it's checked first
						swap(ext, exts[0]);
						break;
					}
				}
			}

			if (tex.src.empty()) {
				logInfo(src_filepath, ": texture ", tex.path, " not found");
				continue;
			}
			
			Path::normalize(tex.src.data);
		}
	}
	
	jobs::forEach(m_meshes.size(), 1, [&](i32 mesh_idx, i32){
		// TODO this can process the same geom multiple times

		PROFILE_FUNCTION();
		ImportMesh& mesh = m_meshes[mesh_idx];
		ImportGeometry& geom = m_geometries[mesh.geometry_idx];

		for (u32 i = 0; i < meta.lod_count; ++i) {
			if ((meta.autolod_mask & (1 << i)) == 0) continue;
			if (mesh.lod != 0) continue;
			
			geom.autolod_indices[i].create(m_allocator);
			geom.autolod_indices[i]->resize(geom.indices.size());
			const size_t lod_index_count = meshopt_simplifySloppy(geom.autolod_indices[i]->begin()
				, geom.indices.begin()
				, geom.indices.size()
				, (const float*)geom.vertex_buffer.data()
				, u32(geom.vertex_buffer.size() / geom.vertex_size)
				, geom.vertex_size
				, size_t(geom.indices.size() * meta.autolod_coefs[i])
				, 0.5f
				);
			geom.autolod_indices[i]->resize((u32)lod_index_count);
		}
	});

	// TODO check this
	if (meta.bake_vertex_ao) bakeVertexAO(meta.min_bake_vertex_ao);

	u32 mesh_data_size = 0;
	for (const ImportGeometry& g : m_geometries) {
		mesh_data_size += u32(g.vertex_buffer.size() + g.indices.byte_size());
	}
	m_out_file.reserve(128 * 1024 + mesh_data_size);
}

bool ModelImporter::writeSubmodels(const Path& src, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	HashMap<u64, bool> map(m_allocator);
	map.reserve(m_geometries.size());
	
	for (i32 i = 0; i < m_geometries.size(); ++i) {
		m_out_file.clear();
		writeModelHeader();
		const BoneNameHash root_motion_bone(meta.root_motion_bone.c_str());
		write(root_motion_bone);
		writeSubmesh(src, i, meta);
		writeGeometry(i);
		write((i32)0);

		// lods
		const i32 lod_count = 1;
		const i32 to_mesh = 0;
		const float factor = FLT_MAX;
		write(lod_count);
		write(to_mesh);
		write(factor);

		Path path(m_geometries[i].name, ".fbx:", src);

		AssetCompiler& compiler = m_app.getAssetCompiler();
		if (!compiler.writeCompiledResource(path, Span(m_out_file.data(), (i32)m_out_file.size()))) {
			return false;
		}
	}
	return true;
}

// TODO move this to the constructor?
void ModelImporter::init() {
	m_impostor_shadow_shader = m_app.getEngine().getResourceManager().load<Shader>(Path("engine/shaders/impostor_shadow.hlsl"));
}

void ModelImporter::createImpostorTextures(Model* model, ImpostorTexturesContext& ctx, bool bake_normals)
{
	ASSERT(model->isReady());
	ASSERT(m_impostor_shadow_shader->isReady());

	ctx.path = model->getPath();
	Engine& engine = m_app.getEngine();
	Renderer* renderer = (Renderer*)engine.getSystemManager().getSystem("renderer");
	ASSERT(renderer);

	const u32 capture_define = 1 << renderer->getShaderDefineIdx("DEFERRED");
	const u32 bake_normals_define = 1 << renderer->getShaderDefineIdx("BAKE_NORMALS");
	Array<u32> depth_tmp(m_allocator);

	renderer->pushJob("create impostor textures", [&](DrawStream& stream) {
		const AABB& aabb = model->getAABB();
		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		center.x = center.z = 0;
		const float radius = model->getCenterBoundingRadius();

		const Vec2 bounding_cylinder = computeBoundingCylinder(*model, center);
		Vec2 min, max;
		const Vec2 half_extents = computeImpostorHalfExtents(bounding_cylinder);
		min = -half_extents;
		max = half_extents;

		gpu::TextureHandle gbs[] = {gpu::allocTextureHandle(), gpu::allocTextureHandle(), gpu::allocTextureHandle()};

		const Vec2 padding = Vec2(1.f) / Vec2(IMPOSTOR_TILE_SIZE) * (max - min);
		min += -padding;
		max += padding;
		const Vec2 size = max - min;

		IVec2& tile_size = ctx.tile_size;
		tile_size = IVec2(int(IMPOSTOR_TILE_SIZE * size.x / size.y), IMPOSTOR_TILE_SIZE);
		tile_size.x = (tile_size.x + 3) & ~3;
		tile_size.y = (tile_size.y + 3) & ~3;
		const IVec2 texture_size = tile_size * IMPOSTOR_COLS;
		stream.beginProfileBlock("create impostor textures", 0, false);
		stream.createTexture(gbs[0], texture_size.x, texture_size.y, 1, gpu::TextureFormat::SRGBA, gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET, "impostor_gb0");
		stream.createTexture(gbs[1], texture_size.x, texture_size.y, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET, "impostor_gb1");
		stream.createTexture(gbs[2], texture_size.x, texture_size.y, 1, gpu::TextureFormat::D32, gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::RENDER_TARGET, "impostor_gbd");

		stream.setFramebuffer(gbs, 2, gbs[2], gpu::FramebufferFlags::SRGB);
		const float color[] = {0, 0, 0, 0};
		stream.clear(gpu::ClearFlags::COLOR | gpu::ClearFlags::DEPTH | gpu::ClearFlags::STENCIL, color, 0);

		PassState pass_state;
		pass_state.view = Matrix::IDENTITY;
		pass_state.projection.setOrtho(min.x, max.x, min.y, max.y, 0, 2.02f * radius, true);
		pass_state.inv_projection = pass_state.projection.inverted();
		pass_state.inv_view = pass_state.view.fastInverted();
		pass_state.view_projection = pass_state.projection * pass_state.view;
		pass_state.inv_view_projection = pass_state.view_projection.inverted();
		pass_state.view_dir = Vec4(pass_state.view.inverted().transformVector(Vec3(0, 0, -1)), 0);
		pass_state.camera_up = Vec4(pass_state.view.inverted().transformVector(Vec3(0, 1, 0)), 0);
		UniformPool& uniform_pool = renderer->getUniformPool();
		const TransientSlice pass_buf = alloc(uniform_pool, &pass_state, sizeof(pass_state));
		stream.bindUniformBuffer(UniformBuffer::PASS, pass_buf.buffer, pass_buf.offset, pass_buf.size);

		for (u32 j = 0; j < IMPOSTOR_COLS; ++j) {
			for (u32 col = 0; col < IMPOSTOR_COLS; ++col) {
				if (gpu::isOriginBottomLeft()) {
					stream.viewport(col * tile_size.x, j * tile_size.y, tile_size.x, tile_size.y);
				} else {
					stream.viewport(col * tile_size.x, (IMPOSTOR_COLS - j - 1) * tile_size.y, tile_size.x, tile_size.y);
				}
				const Vec3 v = normalize(impostorToWorld({col / (float)(IMPOSTOR_COLS - 1), j / (float)(IMPOSTOR_COLS - 1)}));

				Matrix model_mtx;
				Vec3 up = Vec3(0, 1, 0);
				if (col == IMPOSTOR_COLS >> 1 && j == IMPOSTOR_COLS >> 1) up = Vec3(1, 0, 0);
				model_mtx.lookAt(center - v * 1.01f * radius, center, up);
				const TransientSlice ub = alloc(uniform_pool, &model_mtx, sizeof(model_mtx));
				stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);

				for (u32 i = 0; i <= (u32)model->getLODIndices()[0].to; ++i) {
					const MeshMaterial& mesh_mat = model->getMeshMaterial(i);
					const Mesh& mesh = model->getMesh(i);
					const Material* material = mesh_mat.material;
					Shader* shader = material->getShader();
					const gpu::StateFlags state = gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE | material->m_render_states;
					const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, capture_define | material->getDefineMask(), mesh.semantics_defines);

					ASSERT(false);
					// TODO
					// material->bind(stream);
					stream.useProgram(program);
					stream.bindIndexBuffer(mesh.index_buffer_handle);
					stream.bindVertexBuffer(0, mesh.vertex_buffer_handle, 0, mesh.vb_stride);
					stream.bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
					stream.drawIndexed(0, mesh.indices_count, mesh.index_type);
				}
			}
		}

		stream.setFramebuffer(nullptr, 0, gpu::INVALID_TEXTURE, gpu::FramebufferFlags::NONE);

		gpu::TextureHandle shadow = gpu::allocTextureHandle();
		stream.createTexture(shadow, texture_size.x, texture_size.y, 1, gpu::TextureFormat::RGBA8, gpu::TextureFlags::NO_MIPS | gpu::TextureFlags::COMPUTE_WRITE, "impostor_shadow");
		gpu::ProgramHandle shadow_program = m_impostor_shadow_shader->getProgram(bake_normals ? bake_normals_define : 0);
		stream.useProgram(shadow_program);
		// stream.bindImageTexture(shadow, 0);
		// stream.bindTextures(&gbs[1], 1, 2);
		struct {
			Matrix projection;
			Matrix proj_to_model;
			Matrix inv_view;
			Vec4 center;
			IVec2 tile;
			IVec2 tile_size;
			int size;
			float radius;
			gpu::BindlessHandle depth;
			gpu::BindlessHandle normalmap;
			gpu::RWBindlessHandle output;
		} data;
		for (u32 j = 0; j < IMPOSTOR_COLS; ++j) {
			for (u32 i = 0; i < IMPOSTOR_COLS; ++i) {
				Matrix view, projection;
				const Vec3 v = normalize(impostorToWorld({i / (float)(IMPOSTOR_COLS - 1), j / (float)(IMPOSTOR_COLS - 1)}));
				Vec3 up = Vec3(0, 1, 0);
				if (i == IMPOSTOR_COLS >> 1 && j == IMPOSTOR_COLS >> 1) up = Vec3(1, 0, 0);
				view.lookAt(center - v * 1.01f * radius, center, up);
				projection.setOrtho(min.x, max.x, min.y, max.y, 0, 2.02f * radius, true);
				data = {
					.projection = projection,
					.proj_to_model = (projection * view).inverted(),
					.inv_view = view.inverted(),
					.center = Vec4(center, 1),
					.tile = IVec2(i, j),
					.tile_size = tile_size,
					.size = IMPOSTOR_COLS,
					.radius = radius,
					.depth = gpu::getBindlessHandle(gbs[2]),
					.normalmap = gpu::getBindlessHandle(gbs[1]),
					.output = gpu::getRWBindlessHandle(shadow),
				};
				const TransientSlice ub = alloc(uniform_pool, &data, sizeof(data));
				stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);
				stream.dispatch((tile_size.x + 15) / 16, (tile_size.y + 15) / 16, 1);
			}
		}

		ctx.start();
		stream.readTexture(gbs[0], makeDelegate<&ImpostorTexturesContext::readCallback0>(&ctx));
		stream.readTexture(gbs[1], makeDelegate<&ImpostorTexturesContext::readCallback1>(&ctx));
		stream.readTexture(gbs[2], makeDelegate<&ImpostorTexturesContext::readCallback2>(&ctx));
		stream.readTexture(shadow, makeDelegate<&ImpostorTexturesContext::readCallback3>(&ctx));
		stream.destroy(shadow);
		stream.destroy(gbs[0]);
		stream.destroy(gbs[1]);
		stream.destroy(gbs[2]);
		stream.endProfileBlock();
	});

	renderer->frame();
	renderer->waitForRender();

	const PathInfo src_info(model->getPath());
	const Path mat_src(src_info.dir, src_info.basename, "_impostor.mat");
	os::OutputFile f;
	FileSystem& filesystem = m_app.getEngine().getFileSystem(); 
	if (!filesystem.fileExists(mat_src)) {
		if (!filesystem.open(mat_src, f)) {
			logError("Failed to create ", mat_src);
		}
		else {
			const AABB& aabb = model->getAABB();
			const Vec3 center = (aabb.max + aabb.min) * 0.5f;
			f << "shader \"/shaders/impostor.hlsl\"\n";
			f << "texture \"" << src_info.basename << "_impostor0.tga\"\n";
			f << "texture \"" << src_info.basename << "_impostor1.tga\"\n";
			f << "texture \"" << src_info.basename << "_impostor2.tga\"\n";
			f << "texture \"" << src_info.basename << "_impostor_depth.raw\"\n";
			f << "define \"ALPHA_CUTOUT\"\n";
			f << "layer \"impostor\"\n";
			f << "backface_culling false \n";
			f << "uniform \"Center\", { 0, " << center.y << ", 0 }\n";
			f << "uniform \"Radius\", " << model->getCenterBoundingRadius() << "\n";
			f.close();
		}
	}
	
	const Path albedo_meta(src_info.dir, src_info.basename, "_impostor0.tga.meta");
	if (!filesystem.fileExists(albedo_meta)) {
		if (!filesystem.open(albedo_meta, f)) {
			logError("Failed to create ", albedo_meta);
		}
		else {
			f << "srgb = true";
			f.close();
		}
	}
}

bool ModelImporter::write(const Path& src, const ModelMeta& meta) {
	const Path filepath = Path(ResourcePath::getResource(src));
	if (meta.split) {
		if (!writeSubmodels(filepath, meta)) return false;
		if (!writeDummyModel(src)) return false;
	}
	else {
		if (!writeModel(src, meta)) return false;
	}
	if (!writeMaterials(filepath, meta, false)) return false;
	if (!writeAnimations(filepath, meta)) return false;
	if (!writePhysics(filepath, meta)) return false;
	if (meta.split || meta.create_prefab_with_physics) {
		jobs::moveJobToWorker(0);
		bool res = writePrefab(filepath, meta);
		jobs::yield();
		if (!res) return false;
	}
	return true;
}

bool ModelImporter::writeMaterials(const Path& src, const ModelMeta& meta, bool force) {
	PROFILE_FUNCTION()

	FileSystem& filesystem = m_app.getEngine().getFileSystem();
	bool failed = false;
	StringView dir = Path::getDir(src);
	OutputMemoryStream blob(m_app.getAllocator());
	for (const ImportMaterial& material : m_materials) {
		const Path mat_src(dir, material.name, ".mat");
		if (filesystem.fileExists(mat_src) && !force) continue;

		os::OutputFile f;
		if (!filesystem.open(mat_src, f)) {
			failed = true;
			logError("Failed to create ", mat_src);
			continue;
		}
		blob.clear();

		blob << "shader \"/engine/shaders/standard.hlsl\"\n";
		if (!material.textures[2].src.empty()) blob << "uniform \"Metallic\", 1.000000\n";

		auto writeTexture = [&](const ImportTexture& texture, u32 idx) {
			if (!texture.src.empty() && idx < 2) {
				const Path meta_path(texture.src, ".meta");
				if (!filesystem.fileExists(meta_path)) {
					os::OutputFile file;
					if (filesystem.open(meta_path, file)) {
						file << (idx == 0 ? "srgb = true\n" : "normalmap = true\n");
						file.close();
					}
				}
			}
			if (!texture.src.empty()) {
				blob << "texture \"/"
					 << texture.src
					 << "\"\n";
			}
			else {
				blob << "texture \"\"\n";
			}
		};

		writeTexture(material.textures[0], 0);
		writeTexture(material.textures[1], 1);
		if (meta.use_specular_as_roughness) {
			writeTexture(material.textures[2], 2);
		}
		else {
			blob << "texture \"\"\n";
		}
		if (meta.use_specular_as_metallic) {
			writeTexture(material.textures[2], 3);
		}
		else {
			blob << "texture \"\"\n";
		}

		if (material.textures[0].src.empty() && !meta.ignore_material_colors) {
			const Vec3 color = material.diffuse_color;
			blob << "uniform \"Material color\", {" << color.x
				<< "," << color.y
				<< "," << color.z
				<< ",1}\n";
		}

		if (!f.write(blob.data(), blob.size())) {
			failed = true;
			logError("Failed to write ", mat_src);
		}
		f.close();
	}
	return !failed;
}

bool ModelImporter::findTexture(StringView src_dir, StringView ext, ModelImporter::ImportTexture& tex) const {
	FileSystem& filesystem = m_app.getEngine().getFileSystem();
	PathInfo file_info(tex.path);
	tex.src = src_dir;
	tex.src.append(file_info.basename, ".", ext);
	if (filesystem.fileExists(tex.src)) return true;

	tex.src = src_dir;
	tex.src.append(file_info.dir, "/", file_info.basename, ".", ext);
	if (filesystem.fileExists(tex.src)) return true;
					
	tex.src = src_dir;
	tex.src.append("textures/", file_info.basename, ".", ext);
	if (filesystem.fileExists(tex.src)) return true;

	tex.src = "";
	return false;
}

void ModelImporter::writeImpostorVertices(float center_y, Vec2 bounding_cylinder) {
	struct Vertex {
		Vec3 pos;
		Vec2 uv;
	};

	Vec2 min, max;
	const Vec2 half_extents = computeImpostorHalfExtents(bounding_cylinder);
	min = -half_extents;
	max = half_extents;

	const Vertex vertices[] = {
		{{ min.x, center_y + min.y, 0}, {0, 0}},
		{{ min.x, center_y + max.y, 0}, {0, 1}},
		{{ max.x, center_y + max.y, 0}, {1, 1}},
		{{ max.x, center_y + min.y, 0}, {1, 0}}
	};

	const u32 vertex_data_size = sizeof(vertices);
	write(vertex_data_size);
	for (const Vertex& vertex : vertices) {
		write(vertex.pos);
		write(vertex.uv);
	}
}

void ModelImporter::writeGeometry(u32 geom_idx) {
	PROFILE_FUNCTION();
	// TODO lods
	const ImportGeometry& geom = m_geometries[geom_idx];
	
	const bool are_indices_16_bit = geom.index_size == sizeof(u16);
	write(geom.index_size);
	if (are_indices_16_bit) {
		write(geom.indices.size());
		for (int i : geom.indices) {
			ASSERT(i <= (1 << 16));
			write((u16)i);
		}
	}
	else {
		ASSERT(geom.index_size == sizeof(u32));
		write(geom.indices.size());
		write(&geom.indices[0], sizeof(geom.indices[0]) * geom.indices.size());
	}
	
	AABB aabb = {{FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX}};
	float origin_radius_squared = 0;
	const u8* positions = geom.vertex_buffer.data();
	const i32 vertex_size = geom.vertex_size;
	const u32 vertex_count = u32(geom.vertex_buffer.size() / vertex_size);
	for (u32 i = 0; i < vertex_count; ++i) {
		Vec3 p;
		memcpy(&p, positions, sizeof(p));
		positions += vertex_size;
		const float d = squaredLength(p);
		origin_radius_squared = maximum(d, origin_radius_squared);
		aabb.addPoint(p);
	}

	float center_radius_squared = 0;
	const Vec3 center = (aabb.max + aabb.min) * 0.5f;

	positions = geom.vertex_buffer.data();
	for (u32 i = 0; i < vertex_count; ++i) {
		Vec3 p;
		memcpy(&p, positions, sizeof(p));
		positions += vertex_size;
		const float d = squaredLength(p - center);
		center_radius_squared = maximum(d, center_radius_squared);
	}

	write((i32)geom.vertex_buffer.size());
	write(geom.vertex_buffer.data(), geom.vertex_buffer.size());

	write(sqrtf(origin_radius_squared));
	write(sqrtf(center_radius_squared));
	write(aabb);
}

bool ModelImporter::writePrefab(const Path& src, const ModelMeta& meta) {
	Engine& engine = m_app.getEngine();
	World& world = engine.createWorld();

	os::OutputFile file;
	PathInfo file_info(src);
	Path tmp(file_info.dir, "/", file_info.basename, ".fab");
	FileSystem& fs = engine.getFileSystem();
	if (!fs.open(tmp, file)) {
		logError("Could not create ", tmp);
		return false;
	}

	OutputMemoryStream blob(m_allocator);
	bool with_physics = meta.physics != ModelMeta::Physics::NONE;
	RenderModule* rmodule = (RenderModule*)world.getModule(types::model_instance);
	PhysicsModule* pmodule = (PhysicsModule*)world.getModule("physics");
	if (!pmodule) with_physics = false;
	
	const EntityRef root = world.createEntity({0, 0, 0}, Quat::IDENTITY);
	if (meta.split) {
		for(int i  = 0; i < m_meshes.size(); ++i) {
			Vec3 pos;
			Quat rot;
			Vec3 scale;
			m_meshes[i].matrix.decompose(pos, rot, scale);
			const EntityRef e = world.createEntity(DVec3(pos), rot);
			world.setScale(e, scale);
			world.createComponent(types::model_instance, e);
			world.setParent(root, e);
			const ImportGeometry& geom = m_geometries[m_meshes[i].geometry_idx];
			Path mesh_path(geom.name, ".fbx:", src);
			rmodule->setModelInstancePath(e, mesh_path);
	
			if (with_physics) {
				world.createComponent(types::rigid_actor, e);
				pmodule->setActorMesh(e, Path(geom.name, ".phy:", src));
			}
		}

		for (i32 i = 0, c = (i32)m_lights.size(); i < c; ++i) {
			const DVec3 pos = m_lights[i];
			const EntityRef e = world.createEntity(pos, Quat::IDENTITY);
			world.createComponent(types::point_light, e);
			world.setParent(root, e);
			world.setEntityName(e, "light");
		}
	}
	else {
		world.createComponent(types::model_instance, root);
		rmodule->setModelInstancePath(root, src);

		ASSERT(with_physics);
		world.createComponent(types::rigid_actor, root);
		pmodule->setActorMesh(root, Path(".phy:", src));
	}

	world.serialize(blob, WorldSerializeFlags::NONE);
	engine.destroyWorld(world);
	if (!file.write(blob.data(), blob.size())) {
		logError("Could not write ", tmp);
		file.close();
		return false;
	}
	file.close();
	return true;
}

static bool isIdentity(const Matrix& mtx) {
	for (u32 i = 0; i < 4; ++i) {
		for (u32 j = 0; j < 4; ++j) {
			if (fabs(mtx.columns[i][j] - Matrix::IDENTITY.columns[i][j]) > 0.001f) return false;
		}
	}
	return true;
}

void ModelImporter::writeGeometry(const ModelMeta& meta) {
	PROFILE_FUNCTION();
	float center_radius_squared = 0;

	Vec2 bounding_cylinder = Vec2(0);
	
	for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
		for (const ImportMesh& import_mesh : m_meshes) {
			const ImportGeometry& geom = m_geometries[import_mesh.geometry_idx];
			const bool are_indices_16_bit = areIndices16Bit(geom);
			
			if (import_mesh.lod == lod && !hasAutoLOD(meta, lod)) { 
				if (are_indices_16_bit) {
					const i32 index_size = sizeof(u16);
					write(index_size);
					write(geom.indices.size());
					for (u32 i : geom.indices) {
						ASSERT(i <= (1 << 16));
						u16 index = (u16)i;
						write(index);
					}
				}
				else {
					int index_size = sizeof(geom.indices[0]);
					write(index_size);
					write(geom.indices.size());
					write(&geom.indices[0], sizeof(geom.indices[0]) * geom.indices.size());
				}
			}
			else if (import_mesh.lod == 0 && hasAutoLOD(meta, lod)) {
				const auto& lod_indices = *geom.autolod_indices[lod].get();
				if (are_indices_16_bit) {
					const i32 index_size = sizeof(u16);
					write(index_size);
					write(lod_indices.size());
					for (u32 i : lod_indices) {
						ASSERT(i <= (1 << 16));
						u16 index = (u16)i;
						write(index);
					}
				}
				else {
					i32 index_size = sizeof(lod_indices[0]);
					write(index_size);
					write(lod_indices.size());
					write(lod_indices.begin(), geom.autolod_indices[lod]->byte_size());
				}
			}
		}
	}

	if (meta.create_impostor) {
		const int index_size = sizeof(u16);
		write(index_size);
		const u16 indices[] = {0, 1, 2, 0, 2, 3};
		const u32 len = lengthOf(indices);
		write(len);
		write(indices, sizeof(indices));
	}

	float origin_radius_squared = 0;
	AABB aabb = { {FLT_MAX, FLT_MAX, FLT_MAX}, {-FLT_MAX, -FLT_MAX, -FLT_MAX} };

	const u64 output_vertex_data_offset = m_out_file.size();
	for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
		for (const ImportMesh& import_mesh : m_meshes) {
			if (!((import_mesh.lod == lod && !hasAutoLOD(meta, lod)) || (import_mesh.lod == 0 && hasAutoLOD(meta, lod)))) continue;
			
			const ImportGeometry& geom = m_geometries[import_mesh.geometry_idx];
			write((i32)geom.vertex_buffer.size());

			const u32 vertex_size = geom.vertex_size;
			const u32 vertex_count = u32(geom.vertex_buffer.size() / geom.vertex_size);
			const i32 normal_offset = getAttributeOffset(geom, AttributeSemantic::NORMAL);
			const i32 tangent_offset = getAttributeOffset(geom, AttributeSemantic::TANGENT);
			const i32 bitangent_offset = getAttributeOffset(geom, AttributeSemantic::BITANGENT);
			const u8* in = geom.vertex_buffer.data();
			u8* out = (u8*)m_out_file.skip(geom.vertex_buffer.size());
			const Matrix mtx = import_mesh.matrix;

 			memcpy(out, in, geom.vertex_buffer.size());
			if (isIdentity(mtx)) {
				for (u32 i = 0; i < vertex_count; ++i) {
					Vec3 p;
					memcpy(&p, out + vertex_size * i, sizeof(p));
					aabb.addPoint(p);
					const float d = squaredLength(p);
					origin_radius_squared = maximum(d, origin_radius_squared);
				}
			}
			else {
				Vec3 scale;
				Vec3 pos;
				Quat rot;
				mtx.decompose(pos, rot, scale);
				ASSERT(fabsf(rot.x * rot.x + rot.y * rot.y + rot.z * rot.z + rot.w * rot.w - 1) < 0.0001f);

				auto transform_vector = [&](u32 offset){
					u32 packed_vec;
					memcpy(&packed_vec, out + offset, sizeof(packed_vec));
					Vec3 vec = unpackF4u(packed_vec);
					vec = rot.rotate(vec);
					packed_vec = packF4u(vec);
					memcpy(out + offset, &packed_vec, sizeof(packed_vec));
				};

				for (u32 i = 0; i < vertex_count; ++i) {
					Vec3 p;
					memcpy(&p, out + vertex_size * i, sizeof(p));
					p = mtx.transformPoint(p);
					memcpy(out + i * vertex_size, &p, sizeof(p));

					aabb.addPoint(p);
					const float d = squaredLength(p);
					origin_radius_squared = maximum(d, origin_radius_squared);

					if (normal_offset >= 0) transform_vector(normal_offset + vertex_size * i);
					if (tangent_offset >= 0) transform_vector(tangent_offset + vertex_size * i);
					if (bitangent_offset >= 0) transform_vector(bitangent_offset + vertex_size * i);
				}
			}
		}
	}

	Vec3 center = (aabb.min + aabb.max) * 0.5f;
	Vec3 center_xz0(0, center.y, 0);

	if (meta.origin != ModelMeta::Origin::SOURCE) {
		u8* out = m_out_file.getMutableData() + output_vertex_data_offset;
		for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
			for (const ImportMesh& import_mesh : m_meshes) {
				if (!((import_mesh.lod == lod && !hasAutoLOD(meta, lod)) || (import_mesh.lod == 0 && hasAutoLOD(meta, lod)))) continue;
				
				const ImportGeometry& geom = m_geometries[import_mesh.geometry_idx];
				const u32 vertex_size = geom.vertex_size;
				const u32 vertex_count = u32(geom.vertex_buffer.size() / geom.vertex_size);
				out += sizeof(i32);
				
				for (u32 i = 0; i < vertex_count; ++i) {
					Vec3 p;
					memcpy(&p, out + vertex_size * i, sizeof(p));
					p.x -= center.x;
					p.z -= center.z;
					if (meta.origin == ModelMeta::Origin::CENTER) {
						p.y -= center.y;
					}
					memcpy(out + vertex_size * i, &p, sizeof(p));
				}
				out += geom.vertex_buffer.size();
			}
		}

		aabb.min -= center;
		aabb.max -= center;
		if (meta.origin == ModelMeta::Origin::BOTTOM) {
			aabb.min.y += center.y;
			aabb.max.y += center.y;
		}
		center = (aabb.min + aabb.max) * 0.5f;
		center_xz0 = Vec3(0, center.y, 0);
	}

	const u8* out = m_out_file.getMutableData() + output_vertex_data_offset;
	for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
		for (const ImportMesh& import_mesh : m_meshes) {
			if (!((import_mesh.lod == lod && !hasAutoLOD(meta, lod)) || (import_mesh.lod == 0 && hasAutoLOD(meta, lod)))) continue;
			
			const ImportGeometry& geom = m_geometries[import_mesh.geometry_idx];
			const u32 vertex_size = geom.vertex_size;
			const u32 vertex_count = u32(geom.vertex_buffer.size() / geom.vertex_size);
			out += sizeof(i32);
			
			for (u32 i = 0; i < vertex_count; ++i) {
				Vec3 p;
				memcpy(&p, out + vertex_size * i, sizeof(p));

				float d = squaredLength(p - center);
				center_radius_squared = maximum(d, center_radius_squared);

				p -= center_xz0;
				float xz_squared = p.x * p.x + p.z * p.z;
				bounding_cylinder.x = maximum(bounding_cylinder.x, xz_squared);
				bounding_cylinder.y = maximum(bounding_cylinder.y, fabsf(p.y));
			}
			out += geom.vertex_buffer.size();
		}
	}
	bounding_cylinder.x = sqrtf(bounding_cylinder.x);

	if (meta.create_impostor) writeImpostorVertices((aabb.max.y + aabb.min.y) * 0.5f, bounding_cylinder);

	if (m_meshes.empty()) {
		for (const Bone& bone : m_bones) {
			const Vec3 p = bone.bind_pose_matrix.getTranslation();
			origin_radius_squared = maximum(origin_radius_squared, squaredLength(p));
			aabb.addPoint(p);
		}
		center_radius_squared = squaredLength(aabb.max - aabb.min) * 0.5f;
	}

	write(sqrtf(origin_radius_squared) * meta.culling_scale);
	write(sqrtf(center_radius_squared) * meta.culling_scale);
	write(aabb * meta.culling_scale);
}


void ModelImporter::writeImpostorMesh(StringView dir, StringView model_name)
{
	const i32 attribute_count = 2;
	write(attribute_count);

	write(AttributeSemantic::POSITION);
	write(gpu::AttributeType::FLOAT);
	write((u8)3);

	write(AttributeSemantic::TEXCOORD0);
	write(gpu::AttributeType::FLOAT);
	write((u8)2);

	const Path material_name(dir, model_name, "_impostor.mat");
	u32 length = material_name.length();
	write(length);
	write(material_name.c_str(), length);

	const char* mesh_name = "impostor";
	length = stringLength(mesh_name);
	write(length);
	write(mesh_name, length);
}


void ModelImporter::writeSubmesh(const Path& src, i32 geom_idx, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	const PathInfo src_info(src);
	write((u32)1);

	const ImportGeometry& geom = m_geometries[geom_idx];
	write((u32)geom.attributes.size());

	for (const AttributeDesc& desc : geom.attributes) {
		write(desc.semantic);
		write(desc.type);
		write(desc.num_components);
	}

	const ImportMaterial& material = m_materials[geom.material_index];
	const Path mat_path(src_info.dir, material.name, ".mat");
	const i32 len = mat_path.length();
	write(len);
	write(mat_path.c_str(), len);

	write((u32)geom.name.length());
	write(geom.name.c_str(), geom.name.length());
}

void ModelImporter::writeMeshes(const Path& src, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	const PathInfo src_info(src);
	i32 mesh_count = 0;
	for (ImportMesh& mesh : m_meshes) {
		if (mesh.lod >= meta.lod_count - (meta.create_impostor ? 1 : 0)) continue;
		if (mesh.lod == 0 || !hasAutoLOD(meta, mesh.lod)) ++mesh_count;
		for (u32 i = 1; i < meta.lod_count - (meta.create_impostor ? 1 : 0); ++i) {
			if (mesh.lod == 0 && hasAutoLOD(meta, i)) ++mesh_count;
		}
	}
	if (meta.create_impostor) ++mesh_count;
	write(mesh_count);
	
	auto writeMesh = [&](const ImportMesh& mesh ) {
		const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
		write((u32)geom.attributes.size());

		for (const AttributeDesc& desc : geom.attributes) {
			write(desc.semantic);
			write(desc.type);
			write(desc.num_components);
		}

		const ImportMaterial& material = m_materials[geom.material_index];
		const Path mat_path(src_info.dir, material.name, ".mat");
		const i32 len = mat_path.length();
		write(len);
		write(mat_path.c_str(), len);

		write((u32)mesh.name.length());
		write(mesh.name.c_str(), mesh.name.length());
	};

	for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
		for (ImportMesh& import_mesh : m_meshes) {
			if (import_mesh.lod == lod && !hasAutoLOD(meta, lod)) writeMesh(import_mesh);
			else if (import_mesh.lod == 0 && hasAutoLOD(meta, lod)) writeMesh(import_mesh);
		}
	}

	if (meta.create_impostor) {
		writeImpostorMesh(src_info.dir, src_info.basename);
	}
}


void ModelImporter::writeSkeleton(const ModelMeta& meta) {
	write(m_bones.size());

	for (i32 idx = 0, num = m_bones.size(); idx < num; ++idx) {
		const Bone& node = m_bones[idx];
		const char* name = node.name.c_str();
		const i32 len = stringLength(name);
		write(len);
		writeString(name);

		const i32 parent_index = getParentIndex(m_bones, node);
		// m_bones must have parents before children
		// i.e. importers must sort them that way
		ASSERT(parent_index < idx);
		write(parent_index);

		const Matrix& tr = node.bind_pose_matrix;

		const Quat q = tr.getRotation();
		const Vec3 t = tr.getTranslation();
		write(t);
		write(q);
	}
}


void ModelImporter::writeLODs(const ModelMeta& meta) {
	i32 lods[4] = {};
	for (auto& mesh : m_meshes) {
		if (mesh.lod >= meta.lod_count - (meta.create_impostor ? 1 : 0)) continue;

		if (mesh.lod == 0 || !hasAutoLOD(meta, mesh.lod)) {
			++lods[mesh.lod];
		}
		for (u32 i = 1; i < meta.lod_count - (meta.create_impostor ? 1 : 0); ++i) {
			if (mesh.lod == 0 && hasAutoLOD(meta, i)) {
				++lods[i];
			}
		}
	}

	if (meta.create_impostor) {
		lods[meta.lod_count - 1] = 1;
	}

	write(meta.lod_count);

	u32 to_mesh = 0;
	for (u32 i = 0; i < meta.lod_count; ++i) {
		to_mesh += lods[i];
		const i32 tmp = to_mesh - 1;
		write((const char*)&tmp, sizeof(tmp));
		float factor = meta.lods_distances[i] < 0 ? FLT_MAX : meta.lods_distances[i] * meta.lods_distances[i];
		write((const char*)&factor, sizeof(factor));
	}
}

void ModelImporter::bakeVertexAO(float min_ao) {
	PROFILE_FUNCTION();

	AABB aabb(Vec3(FLT_MAX), Vec3(-FLT_MAX));
	for (ImportMesh& mesh : m_meshes) {
		const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
		const u8* positions = geom.vertex_buffer.data();
		const i32 vertex_size = geom.vertex_size;
		const i32 vertex_count = i32(geom.vertex_buffer.size() / vertex_size);
		for (i32 i = 0; i < vertex_count; ++i) {
			Vec3 p;
			memcpy(&p, positions + i * vertex_size, sizeof(p));
			aabb.addPoint(p);
		}
	}

	Voxels voxels(m_allocator);
	voxels.beginRaster(aabb, 64);
	for (ImportMesh& mesh : m_meshes) {
		const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
		const u8* positions = geom.vertex_buffer.data();
		const i32 vertex_size = geom.vertex_size;
		const i32 count = geom.indices.size();
		const u32* indices = geom.indices.data();

		for (i32 i = 0; i < count; i += 3) {
			Vec3 p[3];
			memcpy(&p[0], positions + indices[i + 0] * vertex_size, sizeof(p[0]));
			memcpy(&p[1], positions + indices[i + 1] * vertex_size, sizeof(p[1]));
			memcpy(&p[2], positions + indices[i + 2] * vertex_size, sizeof(p[2]));
			voxels.raster(p[0], p[1], p[2]);
		}
	}
	voxels.computeAO(32);
	voxels.blurAO();

	for (ImportMesh& mesh : m_meshes) {
		ImportGeometry& geom = m_geometries[mesh.geometry_idx];
		const u8* positions = geom.vertex_buffer.data();
		u32 ao_offset = 0;
		for (const AttributeDesc& desc :  geom.attributes) {
			if (desc.semantic == AttributeSemantic::AO) break;
			ao_offset += desc.num_components * gpu::getSize(desc.type);
		}

		u8* AOs = geom.vertex_buffer.getMutableData() + ao_offset;
		const i32 vertex_size = geom.vertex_size;
		const i32 vertex_count = i32(geom.vertex_buffer.size() / vertex_size);

		for (i32 i = 0; i < vertex_count; ++i) {
			Vec3 p;
			memcpy(&p, positions + i * vertex_size, sizeof(p));
			float ao;
			bool res = voxels.sampleAO(p, &ao);
			ASSERT(res);
			if (res) {
				const u8 ao8 = u8(clamp((ao + min_ao) * 255, 0.f, 255.f) + 0.5f);
				memcpy(AOs + i * vertex_size, &ao8, sizeof(ao8));
			}
		}
	}
}

void ModelImporter::writeModelHeader()
{
	Model::FileHeader header;
	header.magic = 0x5f4c4d4f;
	header.version = Model::FileVersion::LATEST;
	write(header);
}

bool ModelImporter::writePhysics(const Path& src, const ModelMeta& meta) {
	PhysicsSystem* ps = (PhysicsSystem*)m_app.getEngine().getSystemManager().getSystem("physics");
	if (!ps) return true;
	
	if (m_meshes.empty()) return true;
	if (meta.physics == ModelMeta::Physics::NONE) return true;

	Array<Vec3> verts(m_allocator);
	PhysicsGeometry::Header header;
	header.m_magic = PhysicsGeometry::HEADER_MAGIC;
	header.m_version = (u32)PhysicsGeometry::Versions::LAST;
	const bool to_convex = meta.physics == ModelMeta::Physics::CONVEX;
	header.m_convex = (u32)to_convex;

	if (meta.split) {
		for (const ImportMesh& mesh : m_meshes) {
			const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
			m_out_file.clear();
			m_out_file.write(&header, sizeof(header));

			verts.clear();
			int vertex_size = geom.vertex_size;
			int vertex_count = (i32)(geom.vertex_buffer.size() / vertex_size);

			const u8* vd = geom.vertex_buffer.data();

			for (int i = 0; i < vertex_count; ++i) {
				Vec3 p;
				memcpy(&p, vd + i * vertex_size, sizeof(p));
				p = mesh.matrix.transformPoint(p);
				verts.push(p);
			}

			if (to_convex) {
				if (!ps->cookConvex(verts, m_out_file)) {
					logError("Failed to cook ", src);
					return false;
				}
			}
			else {
				if (!ps->cookTriMesh(verts, geom.indices, m_out_file)) {
					logError("Failed to cook ", src);
					return false;
				}
			}

			Path phy_path(mesh.name, ".phy:", src);
			AssetCompiler& compiler = m_app.getAssetCompiler();
			if (!compiler.writeCompiledResource(phy_path, Span(m_out_file.data(), (i32)m_out_file.size()))) {
				return false;
			}
		}
		return true;
	}

	m_out_file.clear();
	m_out_file.write(&header, sizeof(header));

	i32 total_vertex_count = 0;
	for (const ImportMesh& mesh : m_meshes)	{
		const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
		total_vertex_count += (i32)(geom.vertex_buffer.size() / geom.vertex_size);
	}
	verts.reserve(total_vertex_count);

	for (const ImportMesh& mesh : m_meshes) {
		const ImportGeometry& geom = m_geometries[mesh.geometry_idx];

		int vertex_size = geom.vertex_size;
		int vertex_count = (i32)(geom.vertex_buffer.size() / vertex_size);

		const u8* src = geom.vertex_buffer.data();

		for (int i = 0; i < vertex_count; ++i) {
			Vec3 p;
			memcpy(&p, src + i * vertex_size, sizeof(p));
			p = mesh.matrix.transformPoint(p);
			verts.push(p);
		}
	}

	if (to_convex) {
		if (!ps->cookConvex(verts, m_out_file)) {
			logError("Failed to cook ", src);
			return false;
		}
	} else {
		Array<u32> indices(m_allocator);
		i32 count = 0;
		for (const ImportMesh& mesh : m_meshes) {
			const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
			count += geom.indices.size();
		}
		indices.reserve(count);
		int offset = 0;
		for (const ImportMesh& mesh : m_meshes) {
			const ImportGeometry& geom = m_geometries[mesh.geometry_idx];
			for (unsigned int j = 0, c = geom.indices.size(); j < c; ++j) {
				u32 index = geom.indices[j] + offset;
				indices.push(index);
			}
			int vertex_count = (i32)(geom.vertex_buffer.size() / geom.vertex_size);
			offset += vertex_count;
		}

		if (!ps->cookTriMesh(verts, indices, m_out_file)) {
			logError("Failed to cook ", src);
			return false;
		}
	}

	Path phy_path(".phy:", src);
	AssetCompiler& compiler = m_app.getAssetCompiler();
	return compiler.writeCompiledResource(phy_path, Span(m_out_file.data(), (i32)m_out_file.size()));
}

// if we split the model into multiple meshes, we still create a dummy file for the asset
// so that we can edit source's metadata
bool ModelImporter::writeDummyModel(const Path& src) {
	m_out_file.clear();
	writeModelHeader();
	write(BoneNameHash());
	write((u32)0);
	write((u32)0);
	write((u32)0);

	AssetCompiler& compiler = m_app.getAssetCompiler();
	return compiler.writeCompiledResource(Path(src), Span(m_out_file.data(), m_out_file.size()));
}

bool ModelImporter::writeModel(const Path& src, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	if (m_meshes.empty() && m_animations.empty()) return false;

	m_out_file.clear();
	writeModelHeader();
	const BoneNameHash root_motion_bone(meta.root_motion_bone.c_str());
	write(root_motion_bone);
	writeMeshes(src, meta);
	writeGeometry(meta);
	writeSkeleton(meta);
	writeLODs(meta);

	AssetCompiler& compiler = m_app.getAssetCompiler();
	return compiler.writeCompiledResource(Path(src), Span(m_out_file.data(), m_out_file.size()));
}

bool ModelImporter::writeAnimations(const Path& src, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	bool any_failed = false;
	for (const ImportAnimation& anim : m_animations) { 
		if (anim.length <= 0) continue;

		Array<TranslationTrack> translation_tracks(m_allocator);
		Array<RotationTrack> rotation_tracks(m_allocator);
		translation_tracks.resize(m_bones.size());
		rotation_tracks.resize(m_bones.size());

		auto write_animation = [&](StringView name, u32 from_sample, u32 samples_count) {
			m_out_file.clear();
			Animation::Header header;
			header.magic = Animation::HEADER_MAGIC;
			header.version = Animation::Version::LAST;
			write(header);
			m_out_file.writeString(meta.skeleton.c_str());
			write(anim.fps);
			write(samples_count - 1);
			write(meta.root_motion_flags);

			Array<Array<Key>> all_keys(m_allocator);
			fillTracks(anim, all_keys, from_sample, samples_count);

			{
				u32 total_bits = 0;
				u32 translation_curves_count = 0;
				u64 toffset = m_out_file.size();
				u16 offset_bits = 0;
				write(translation_curves_count);
				for (const Bone& bone : m_bones) {
					const u32 bone_idx = u32(&bone - m_bones.begin());
					Array<Key>& keys = all_keys[bone_idx];
					if (keys.empty()) continue;

					Vec3 bind_pos;
					if (bone.parent_id == 0) {
						bind_pos = m_bones[bone_idx].bind_pose_matrix.getTranslation();
					}
					else {
						const int parent_idx = getParentIndex(m_bones, bone);
						bind_pos = (m_bones[parent_idx].bind_pose_matrix.inverted() * m_bones[bone_idx].bind_pose_matrix).getTranslation();
					}

					if (isBindPosePositionTrack(keys.size(), keys, bind_pos)) continue;
			
					const BoneNameHash name_hash(bone.name.c_str());
					write(name_hash);

					Vec3 min(FLT_MAX), max(-FLT_MAX);
					for (const Key& k : keys) {
						const Vec3 p = k.pos;
						min = minimum(p, min);
						max = maximum(p, max);
					}
					const u8 bitsizes[] = {
						(u8)log2(u32((max.x - min.x) / 0.00005f / meta.anim_translation_error)),
						(u8)log2(u32((max.y - min.y) / 0.00005f / meta.anim_translation_error)),
						(u8)log2(u32((max.z - min.z) / 0.00005f / meta.anim_translation_error))
					};
					const u8 bitsize = (bitsizes[0] + bitsizes[1] + bitsizes[2]);

					if (bitsize == 0) {
						translation_tracks[bone_idx].is_const = true;
						write(Animation::TrackType::CONSTANT);
						write(keys[0].pos);
					}
					else {
						translation_tracks[bone_idx].is_const = false;
						write(Animation::TrackType::ANIMATED);

						write(min);
						write((max.x - min.x) / ((1 << bitsizes[0]) - 1));
						write((max.y - min.y) / ((1 << bitsizes[1]) - 1));
						write((max.z - min.z) / ((1 << bitsizes[2]) - 1));
						write(bitsizes);
						write(offset_bits);
						offset_bits += bitsize;

						memcpy(translation_tracks[bone_idx].bitsizes, bitsizes, sizeof(bitsizes));
						translation_tracks[bone_idx].max = max;
						translation_tracks[bone_idx].min = min;
						total_bits += bitsize * keys.size();
					}				

					++translation_curves_count;
				}

				BitWriter bit_writer(m_out_file, total_bits);

				for (u32 i = 0; i < samples_count; ++i) {
					for (u32 bone_idx = 0, num_bones = m_bones.size(); bone_idx < num_bones; ++bone_idx) {
						Array<Key>& keys = all_keys[bone_idx];
						const TranslationTrack& track = translation_tracks[bone_idx];

						if (!keys.empty() && !track.is_const) {
							const Key& k = keys[i];
							Vec3 p = k.pos;
							const u64 packed = pack(p, track);
							const u32 bitsize = (track.bitsizes[0] + track.bitsizes[1] + track.bitsizes[2]);
							ASSERT(bitsize <= 64);
							bit_writer.write(packed, bitsize);
						}
					}
				}

				memcpy(m_out_file.getMutableData() + toffset, &translation_curves_count, sizeof(translation_curves_count));
			}

			u32 rotation_curves_count = 0;
			u64 roffset = m_out_file.size();
			write(rotation_curves_count);

			u32 total_bits = 0;
			u16 offset_bits = 0;
			for (const Bone& bone : m_bones) {
				const u32 bone_idx = u32(&bone - m_bones.begin());
				Array<Key>& keys = all_keys[bone_idx];
				if (keys.empty()) continue;
			
				Quat bind_rot;
				if (bone.parent_id == 0) {
					bind_rot = m_bones[bone_idx].bind_pose_matrix.getRotation();
				}
				else {
					const i32 parent_idx = getParentIndex(m_bones, bone);
					bind_rot = (m_bones[parent_idx].bind_pose_matrix.inverted() * m_bones[bone_idx].bind_pose_matrix).getRotation();
				}

				//if (isBindPoseRotationTrack(count, keys, bind_rot, meta.rotation_error)) continue;

				const BoneNameHash name_hash(bone.name.c_str());
				write(name_hash);

				Quat min(FLT_MAX, FLT_MAX, FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX, -FLT_MAX, -FLT_MAX);
				for (const Key& k : keys) {
					const Quat r = k.rot;
					min.x = minimum(min.x, r.x); max.x = maximum(max.x, r.x);
					min.y = minimum(min.y, r.y); max.y = maximum(max.y, r.y);
					min.z = minimum(min.z, r.z); max.z = maximum(max.z, r.z);
					min.w = minimum(min.w, r.w); max.w = maximum(max.w, r.w);
				}
				
				u8 bitsizes[] = {
					(u8)log2(u32((max.x - min.x) / 0.000001f / meta.anim_rotation_error)),
					(u8)log2(u32((max.y - min.y) / 0.000001f / meta.anim_rotation_error)),
					(u8)log2(u32((max.z - min.z) / 0.000001f / meta.anim_rotation_error)),
					(u8)log2(u32((max.w - min.w) / 0.000001f / meta.anim_rotation_error))
				};
				if (clampBitsizes(bitsizes)) {
					logWarning("Clamping bone ", bone.name, " in ", src);
				}

				if (bitsizes[0] + bitsizes[1] + bitsizes[2] + bitsizes[3] == 0) {
					rotation_tracks[bone_idx].is_const = true;
					write(Animation::TrackType::CONSTANT);
					write(keys[0].rot);
				}
				else {
					rotation_tracks[bone_idx].is_const = false;
					write(Animation::TrackType::ANIMATED);

					u8 skipped_channel = 0;
					for (u32 i = 1; i < 4; ++i) {
						if (bitsizes[i] > bitsizes[skipped_channel]) skipped_channel = i;
					}

					for (u32 i = 0; i < 4; ++i) {
						if (skipped_channel == i) continue;
						write((&min.x)[i]);
					}
					for (u32 i = 0; i < 4; ++i) {
						if (skipped_channel == i) continue;
						write(((&max.x)[i] - (&min.x)[i]) / ((1 << bitsizes[i]) - 1));
					}
					for (u32 i = 0; i < 4; ++i) {
						if (skipped_channel == i) continue;
						write(bitsizes[i]);
					}
					u8 bitsize = bitsizes[0] + bitsizes[1] + bitsizes[2] + bitsizes[3] + 1;
					bitsize -= bitsizes[skipped_channel];
					write(offset_bits);
					write(skipped_channel);

					offset_bits += bitsize;
					ASSERT(bitsize > 0 && bitsize <= 64);
				
					memcpy(rotation_tracks[bone_idx].bitsizes, bitsizes, sizeof(bitsizes));
					rotation_tracks[bone_idx].max = max;
					rotation_tracks[bone_idx].min = min;
					rotation_tracks[bone_idx].skipped_channel = skipped_channel;
					total_bits += bitsize * keys.size();
				}
				++rotation_curves_count;
			}
			memcpy(m_out_file.getMutableData() + roffset, &rotation_curves_count, sizeof(rotation_curves_count));

			BitWriter bit_writer(m_out_file, total_bits);

			for (u32 i = 0; i < samples_count; ++i) {
				for (u32 bone_idx = 0, num_bones = m_bones.size(); bone_idx < num_bones; ++bone_idx) {
					Array<Key>& keys = all_keys[bone_idx];
					const RotationTrack& track = rotation_tracks[bone_idx];

					if (!keys.empty() && !track.is_const) {
						const Key& k = keys[i];
						Quat q = k.rot;
						u32 bitsize = (track.bitsizes[0] + track.bitsizes[1] + track.bitsizes[2] + track.bitsizes[3]);
						bitsize -= track.bitsizes[track.skipped_channel];
						++bitsize; // sign bit
						ASSERT(bitsize <= 64);
						u64 packed = pack(q, track);
						packed <<= 1;
						packed |= (&q.x)[track.skipped_channel] < 0 ? 1 : 0;
						bit_writer.write(packed, bitsize);
					}
				}
			}

			Path anim_path(name, ".ani:", src);
			AssetCompiler& compiler = m_app.getAssetCompiler();
			if (!compiler.writeCompiledResource(anim_path, Span(m_out_file.data(), (i32)m_out_file.size()))) {
				any_failed = true;
			}
		};
		if (meta.clips.empty()) {
			write_animation(anim.name, 0, u32(anim.length * anim.fps + 0.5f) + 1);
		}
		else {
			for (const ModelMeta::Clip& clip : meta.clips) {
				write_animation(clip.name, clip.from_frame, clip.to_frame - clip.from_frame + 1);
			}
		}
	}
	return !any_failed;
}

} // namespace black