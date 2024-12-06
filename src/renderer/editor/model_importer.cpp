#include "animation/animation.h"
#include "core/job_system.h"
#include "core/log.h"
#include "core/os.h"
#include "core/profiler.h"
#include "editor/asset_compiler.h"
#include "editor/studio_app.h"
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

namespace Lumix {

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

static bool areIndices16Bit(const ModelImporter::ImportMesh& mesh) {
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

static void computeBoundingShapes(ModelImporter::ImportMesh& mesh) {
	PROFILE_FUNCTION();
	AABB aabb(Vec3(FLT_MAX), Vec3(-FLT_MAX));
	float max_squared_dist = 0;
	const u32 vertex_count = u32(mesh.vertex_buffer.size() / mesh.vertex_size);
	const u8* positions = mesh.vertex_buffer.data();
	for (u32 i = 0; i < vertex_count; ++i) {
		Vec3 p;
		memcpy(&p, positions, sizeof(p));
		positions += mesh.vertex_size;
		aabb.addPoint(p);
		float d = squaredLength(p);
		max_squared_dist = maximum(d, max_squared_dist);
	}

	mesh.aabb = aabb;
	mesh.origin_radius_squared = max_squared_dist;
}

static AABB computeMeshAABB(const ModelImporter::ImportMesh& mesh) {
	const u32 vertex_count = u32(mesh.vertex_buffer.size() / mesh.vertex_size);
	if (vertex_count <= 0) return { Vec3(0), Vec3(0) };

	const u8* ptr = mesh.vertex_buffer.data();
	
	Vec3 min(FLT_MAX);
	Vec3 max(-FLT_MAX);

	for (u32 i = 0; i < vertex_count; ++i) {
		Vec3 v;
		memcpy(&v, ptr + mesh.vertex_size * i, sizeof(v));

		min = minimum(min, v);
		max = maximum(max, v);
	}

	return { min, max };
}

static void offsetMesh(ModelImporter::ImportMesh& mesh, const Vec3& offset) {
	const u32 vertex_count = u32(mesh.vertex_buffer.size() / mesh.vertex_size);
	if (vertex_count <= 0) return;

	u8* ptr = mesh.vertex_buffer.getMutableData();
	mesh.origin = offset;
	
	for (u32 i = 0; i < vertex_count; ++i) {
		Vec3 v;
		memcpy(&v, ptr + mesh.vertex_size * i, sizeof(v));
		v -= offset;
		memcpy(ptr + mesh.vertex_size * i, &v, sizeof(v));
	}

	computeBoundingShapes(mesh);
}

ModelImporter::ModelImporter(struct StudioApp& app)
	: m_app(app)
	, m_allocator(app.getAllocator())
	, m_materials(app.getAllocator())
	, m_out_file(app.getAllocator())
	, m_bones(app.getAllocator())
	, m_meshes(app.getAllocator())
	, m_animations(app.getAllocator())
	, m_lights(app.getAllocator())
{}

void ModelImporter::writeString(const char* str) { m_out_file.write(str, stringLength(str)); }

void ModelImporter::centerMeshes() {
	jobs::forEach(m_meshes.size(), 1, [&](i32 mesh_idx, i32){
		ImportMesh& mesh = m_meshes[mesh_idx];
		const AABB aabb = computeMeshAABB(mesh);
		const Vec3 offset = (aabb.max + aabb.min) * 0.5f;
		offsetMesh(mesh, offset);
	});
}	

void ModelImporter::postprocessCommon(const ModelMeta& meta) {
	jobs::forEach(m_meshes.size(), 1, [&](i32 mesh_idx, i32){
		computeBoundingShapes(m_meshes[mesh_idx]);
	});
	
	AABB merged_aabb(Vec3(FLT_MAX), Vec3(-FLT_MAX));
	for (const ImportMesh& m : m_meshes) {
		merged_aabb.merge(m.aabb);
	}

	jobs::forEach(m_meshes.size(), 1, [&](i32 mesh_idx, i32){
		ImportMesh& mesh = m_meshes[mesh_idx];

		if (meta.origin != ModelMeta::Origin::SOURCE) {
			Vec3 offset = (merged_aabb.max + merged_aabb.min) * 0.5f;
			if (meta.origin == ModelMeta::Origin::BOTTOM) offset.y = merged_aabb.min.y;
			offsetMesh(mesh, offset);
		}

		for (u32 i = 0; i < meta.lod_count; ++i) {
			if ((meta.autolod_mask & (1 << i)) == 0) continue;
			if (mesh.lod != 0) continue;
			
			mesh.autolod_indices[i].create(m_allocator);
			mesh.autolod_indices[i]->resize(mesh.indices.size());
			const size_t lod_index_count = meshopt_simplifySloppy(mesh.autolod_indices[i]->begin()
				, mesh.indices.begin()
				, mesh.indices.size()
				, (const float*)mesh.vertex_buffer.data()
				, u32(mesh.vertex_buffer.size() / mesh.vertex_size)
				, mesh.vertex_size
				, size_t(mesh.indices.size() * meta.autolod_coefs[i])
				);
			mesh.autolod_indices[i]->resize((u32)lod_index_count);
		}
	});

	// TODO check this
	if (meta.bake_vertex_ao) bakeVertexAO(meta.min_bake_vertex_ao);

	u32 mesh_data_size = 0;
	for (const ImportMesh& m : m_meshes) {
		mesh_data_size += u32(m.vertex_buffer.size() + m.indices.byte_size());
	}
	m_out_file.reserve(128 * 1024 + mesh_data_size);
}

bool ModelImporter::writeSubmodels(const Path& src, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	for (int i = 0; i < m_meshes.size(); ++i) {
		m_out_file.clear();
		writeModelHeader();
		const BoneNameHash root_motion_bone(meta.root_motion_bone.c_str());
		write(root_motion_bone);
		writeMeshes(src, i, meta);
		writeGeometry(i);
		if (m_meshes[i].is_skinned) {
			writeSkeleton(meta);
		}
		else {
			write((i32)0);
		}

		// lods
		const i32 lod_count = 1;
		const i32 to_mesh = 0;
		const float factor = FLT_MAX;
		write(lod_count);
		write(to_mesh);
		write(factor);

		Path path(m_meshes[i].name, ".fbx:", src);

		AssetCompiler& compiler = m_app.getAssetCompiler();
		if (!compiler.writeCompiledResource(path, Span(m_out_file.data(), (i32)m_out_file.size()))) {
			return false;
		}
	}
	return true;
}

// TODO move this to the constructor?
void ModelImporter::init() {
	m_impostor_shadow_shader = m_app.getEngine().getResourceManager().load<Shader>(Path("shaders/impostor_shadow.hlsl"));
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
		const Renderer::TransientSlice pass_buf = renderer->allocUniform(&pass_state, sizeof(pass_state));
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
				const Renderer::TransientSlice ub = renderer->allocUniform(&model_mtx, sizeof(model_mtx));
				stream.bindUniformBuffer(UniformBuffer::DRAWCALL, ub.buffer, ub.offset, ub.size);

				for (u32 i = 0; i <= (u32)model->getLODIndices()[0].to; ++i) {
					const Mesh& mesh = model->getMesh(i);
					Shader* shader = mesh.material->getShader();
					const Material* material = mesh.material;
					const gpu::StateFlags state = gpu::StateFlags::DEPTH_FN_GREATER | gpu::StateFlags::DEPTH_WRITE | material->m_render_states;
					const gpu::ProgramHandle program = shader->getProgram(state, mesh.vertex_decl, capture_define | material->getDefineMask(), mesh.semantics_defines);

					material->bind(stream);
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
				const Renderer::TransientSlice ub = renderer->allocUniform(&data, sizeof(data));
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
	if (!meta.split && !writeModel(src, meta)) return false;
	if (!writeMaterials(filepath, meta, false)) return false;
	if (!writeAnimations(filepath, meta)) return false;
	if (meta.split) {
		centerMeshes();
		if (!writeSubmodels(filepath, meta)) return false;
	}
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

		blob << "shader \"/shaders/standard.hlsl\"\n";
		if (material.textures[2].import) blob << "uniform \"Metallic\", 1.000000\n";

		auto writeTexture = [&](const ImportTexture& texture, u32 idx) {
			if (texture.import && idx < 2) {
				const Path meta_path(texture.src, ".meta");
				if (!filesystem.fileExists(meta_path)) {
					os::OutputFile file;
					if (filesystem.open(meta_path, file)) {
						file << (idx == 0 ? "srgb = true\n" : "normalmap = true\n");
						file.close();
					}
				}
			}
			if (texture.import) {
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

		if (!material.textures[0].import) {
			const Vec3 color = material.diffuse_color;
			blob << "uniform \"Material color\", {" << powf(color.x, 2.2f) 
				<< "," << powf(color.x, 2.2f)
				<< "," << powf(color.z, 2.2f)
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
	tex.import = filesystem.fileExists(tex.src);

	if (!tex.import) {
		tex.src = src_dir;
		tex.src.append(file_info.dir, "/", file_info.basename, ".", ext);
		tex.import = filesystem.fileExists(tex.src);
					
		if (!tex.import) {
			tex.src = src_dir;
			tex.src.append("textures/", file_info.basename, ".", ext);
			tex.import = filesystem.fileExists(tex.src);
		}
	}
	return tex.import;
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

void ModelImporter::writeGeometry(int mesh_idx) {
	PROFILE_FUNCTION();
	// TODO lods
	OutputMemoryStream vertices_blob(m_allocator);
	const ImportMesh& import_mesh = m_meshes[mesh_idx];
	
	const bool are_indices_16_bit = import_mesh.index_size == sizeof(u16);
	write(import_mesh.index_size);
	if (are_indices_16_bit) {
		write(import_mesh.indices.size());
		for (int i : import_mesh.indices) {
			ASSERT(i <= (1 << 16));
			write((u16)i);
		}
	}
	else {
		ASSERT(import_mesh.index_size == sizeof(u32));
		write(import_mesh.indices.size());
		write(&import_mesh.indices[0], sizeof(import_mesh.indices[0]) * import_mesh.indices.size());
	}
	
	const Vec3 center = (import_mesh.aabb.max + import_mesh.aabb.min) * 0.5f;
	float max_center_dist_squared = 0;
	const u8* positions = import_mesh.vertex_buffer.data();
	const i32 vertex_size = import_mesh.vertex_size;
	const u32 vertex_count = u32(import_mesh.vertex_buffer.size() / vertex_size);
	for (u32 i = 0; i < vertex_count; ++i) {
		Vec3 p;
		memcpy(&p, positions, sizeof(p));
		positions += vertex_size;
		float d = squaredLength(p - center);
		max_center_dist_squared = maximum(d, max_center_dist_squared);
	}

	write((i32)import_mesh.vertex_buffer.size());
	write(import_mesh.vertex_buffer.data(), import_mesh.vertex_buffer.size());

	write(sqrtf(import_mesh.origin_radius_squared));
	write(sqrtf(max_center_dist_squared));
	write(import_mesh.aabb);
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
	static const ComponentType RIGID_ACTOR_TYPE = reflection::getComponentType("rigid_actor");
	static const ComponentType MODEL_INSTANCE_TYPE = reflection::getComponentType("model_instance");
	const bool with_physics = meta.physics != ModelMeta::Physics::NONE;
	RenderModule* rmodule = (RenderModule*)world.getModule(MODEL_INSTANCE_TYPE);
	PhysicsModule* pmodule = (PhysicsModule*)world.getModule(RIGID_ACTOR_TYPE);
	
	const EntityRef root = world.createEntity({0, 0, 0}, Quat::IDENTITY);
	if (!meta.split) {
		world.createComponent(MODEL_INSTANCE_TYPE, root);
		rmodule->setModelInstancePath(root, src);

		ASSERT(with_physics);
		world.createComponent(RIGID_ACTOR_TYPE, root);
		pmodule->setMeshGeomPath(root, Path(".phy:", src));
	}
	else {
		for(int i  = 0; i < m_meshes.size(); ++i) {
			const EntityRef e = world.createEntity(DVec3(m_meshes[i].origin), Quat::IDENTITY);
			world.createComponent(MODEL_INSTANCE_TYPE, e);
			world.setParent(root, e);
			Path mesh_path(m_meshes[i].name, ".fbx:", src);
			rmodule->setModelInstancePath(e, mesh_path);
	
			if (with_physics) {
				world.createComponent(RIGID_ACTOR_TYPE, e);
				pmodule->setMeshGeomPath(e, Path(m_meshes[i].name, ".phy:", src));
			}
		}

		static const ComponentType POINT_LIGHT_TYPE = reflection::getComponentType("point_light");
		for (i32 i = 0, c = (i32)m_lights.size(); i < c; ++i) {
			const DVec3 pos = m_lights[i];
			const EntityRef e = world.createEntity(pos, Quat::IDENTITY);
			world.createComponent(POINT_LIGHT_TYPE, e);
			world.setParent(root, e);
		}
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

void ModelImporter::writeGeometry(const ModelMeta& meta) {
	PROFILE_FUNCTION();
	AABB aabb = {{0, 0, 0}, {0, 0, 0}};
	float origin_radius_squared = 0;
	float center_radius_squared = 0;
	OutputMemoryStream vertices_blob(m_allocator);

	Vec2 bounding_cylinder = Vec2(0);
	for (const ImportMesh& import_mesh : m_meshes) {
		if (import_mesh.lod != 0) continue;

		origin_radius_squared = maximum(origin_radius_squared, import_mesh.origin_radius_squared);
		aabb.merge(import_mesh.aabb);
	}
	
	const Vec3 center = (aabb.min + aabb.max) * 0.5f;
	const Vec3 center_xz0(0, center.y, 0);
	for (const ImportMesh& import_mesh : m_meshes) {
		if (import_mesh.lod != 0) continue;

		const u8* positions = import_mesh.vertex_buffer.data();
		const i32 vertex_size = import_mesh.vertex_size;
		const u32 vertex_count = u32(import_mesh.vertex_buffer.size() / vertex_size);
		for (u32 i = 0; i < vertex_count; ++i) {
			Vec3 p;
			memcpy(&p, positions, sizeof(p));
			positions += vertex_size;
			float d = squaredLength(p - center);
			center_radius_squared = maximum(d, center_radius_squared);
			
			p -= center_xz0;
			float xz_squared = p.x * p.x + p.z * p.z;
			bounding_cylinder.x = maximum(bounding_cylinder.x, xz_squared);
			bounding_cylinder.y = maximum(bounding_cylinder.y, fabsf(p.y));
		}
	}
	bounding_cylinder.x = sqrtf(bounding_cylinder.x);

	for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
		for (const ImportMesh& import_mesh : m_meshes) {

			const bool are_indices_16_bit = areIndices16Bit(import_mesh);
			
			if (import_mesh.lod == lod && !hasAutoLOD(meta, lod)) { 
				if (are_indices_16_bit) {
					const i32 index_size = sizeof(u16);
					write(index_size);
					write(import_mesh.indices.size());
					for (int i : import_mesh.indices)
					{
						ASSERT(i <= (1 << 16));
						u16 index = (u16)i;
						write(index);
					}
				}
				else {
					int index_size = sizeof(import_mesh.indices[0]);
					write(index_size);
					write(import_mesh.indices.size());
					write(&import_mesh.indices[0], sizeof(import_mesh.indices[0]) * import_mesh.indices.size());
				}
			}
			else if (import_mesh.lod == 0 && hasAutoLOD(meta, lod)) {
				const auto& lod_indices = *import_mesh.autolod_indices[lod].get();
				if (are_indices_16_bit) {
					const i32 index_size = sizeof(u16);
					write(index_size);
					write(lod_indices.size());
					for (u32 i : lod_indices)
					{
						ASSERT(i <= (1 << 16));
						u16 index = (u16)i;
						write(index);
					}
				}
				else {
					i32 index_size = sizeof(lod_indices[0]);
					write(index_size);
					write(lod_indices.size());
					write(lod_indices.begin(), import_mesh.autolod_indices[lod]->byte_size());
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

	for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
		for (const ImportMesh& import_mesh : m_meshes) {
			if ((import_mesh.lod == lod && !hasAutoLOD(meta, lod)) || (import_mesh.lod == 0 && hasAutoLOD(meta, lod))) {
				write((i32)import_mesh.vertex_buffer.size());
				write(import_mesh.vertex_buffer.data(), import_mesh.vertex_buffer.size());
			}
		}
	}

	if (meta.create_impostor) writeImpostorVertices((aabb.max.y + aabb.min.y) * 0.5f, bounding_cylinder);

	if (m_meshes.empty()) {
		for (const Bone& bone : m_bones) {
			// TODO check if this works with different values of m_orientation
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


void ModelImporter::writeMeshes(const Path& src, int mesh_idx, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	const PathInfo src_info(src);
	i32 mesh_count = 0;
	if (mesh_idx >= 0) {
		mesh_count = 1;
	}
	else {
		for (ImportMesh& mesh : m_meshes) {
			if (mesh.lod >= meta.lod_count - (meta.create_impostor ? 1 : 0)) continue;
			if (mesh.lod == 0 || !hasAutoLOD(meta, mesh.lod)) ++mesh_count;
			for (u32 i = 1; i < meta.lod_count - (meta.create_impostor ? 1 : 0); ++i) {
				if (mesh.lod == 0 && hasAutoLOD(meta, i)) ++mesh_count;
			}
		}
		if (meta.create_impostor) ++mesh_count;
	}
	write(mesh_count);
	
	auto writeMesh = [&](const ImportMesh& mesh ) {
		write((u32)mesh.attributes.size());

		for (const AttributeDesc& desc : mesh.attributes) {
			write(desc.semantic);
			write(desc.type);
			write(desc.num_components);
		}

		const ImportMaterial& material = m_materials[mesh.material_index];
		const Path mat_path(src_info.dir, material.name, ".mat");
		const i32 len = mat_path.length();
		write(len);
		write(mat_path.c_str(), len);

		write((u32)mesh.name.length());
		write(mesh.name.c_str(), mesh.name.length());
	};

	if(mesh_idx >= 0) {
		writeMesh(m_meshes[mesh_idx]);
	}
	else {
		for (u32 lod = 0; lod < meta.lod_count - (meta.create_impostor ? 1 : 0); ++lod) {
			for (ImportMesh& import_mesh : m_meshes) {
				if (import_mesh.lod == lod && !hasAutoLOD(meta, lod)) writeMesh(import_mesh);
				else if (import_mesh.lod == 0 && hasAutoLOD(meta, lod)) writeMesh(import_mesh);
			}
		}
	}

	if (mesh_idx < 0 && meta.create_impostor) {
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
		const u8* positions = mesh.vertex_buffer.data();
		const i32 vertex_size = mesh.vertex_size;
		const i32 vertex_count = i32(mesh.vertex_buffer.size() / vertex_size);
		for (i32 i = 0; i < vertex_count; ++i) {
			Vec3 p;
			memcpy(&p, positions + i * vertex_size, sizeof(p));
			aabb.addPoint(p);
		}
	}

	Voxels voxels(m_allocator);
	voxels.beginRaster(aabb, 64);
	for (ImportMesh& mesh : m_meshes) {
		const u8* positions = mesh.vertex_buffer.data();
		const i32 vertex_size = mesh.vertex_size;
		const i32 count = mesh.indices.size();
		const u32* indices = mesh.indices.data();

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
		const u8* positions = mesh.vertex_buffer.data();
		u32 ao_offset = 0;
		for (const AttributeDesc& desc :  mesh.attributes) {
			if (desc.semantic == AttributeSemantic::AO) break;
			ao_offset += desc.num_components * gpu::getSize(desc.type);
		}

		u8* AOs = mesh.vertex_buffer.getMutableData() + ao_offset;
		const i32 vertex_size = mesh.vertex_size;
		const i32 vertex_count = i32(mesh.vertex_buffer.size() / vertex_size);

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
	if (m_meshes.empty()) return true;
	if (meta.physics == ModelMeta::Physics::NONE) return true;

	Array<Vec3> verts(m_allocator);
	PhysicsSystem* ps = (PhysicsSystem*)m_app.getEngine().getSystemManager().getSystem("physics");
	if (!ps) {
		logError(src, ": no physics system found while trying to cook physics data");
		return false;
	}

	PhysicsGeometry::Header header;
	header.m_magic = PhysicsGeometry::HEADER_MAGIC;
	header.m_version = (u32)PhysicsGeometry::Versions::LAST;
	const bool to_convex = meta.physics == ModelMeta::Physics::CONVEX;
	header.m_convex = (u32)to_convex;

	if (meta.split) {
		for (const ImportMesh& mesh : m_meshes) {
			m_out_file.clear();
			m_out_file.write(&header, sizeof(header));

			verts.clear();
			int vertex_size = mesh.vertex_size;
			int vertex_count = (i32)(mesh.vertex_buffer.size() / vertex_size);

			const u8* vd = mesh.vertex_buffer.data();

			for (int i = 0; i < vertex_count; ++i) {
				verts.push(*(Vec3*)(vd + i * vertex_size));
			}

			if (to_convex) {
				if (!ps->cookConvex(verts, m_out_file)) {
					logError("Failed to cook ", src);
					return false;
				}
			}
			else {
				if (!ps->cookTriMesh(verts, mesh.indices, m_out_file)) {
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
		total_vertex_count += (i32)(mesh.vertex_buffer.size() / mesh.vertex_size);
	}
	verts.reserve(total_vertex_count);

	for (const ImportMesh& mesh : m_meshes) {
		int vertex_size = mesh.vertex_size;
		int vertex_count = (i32)(mesh.vertex_buffer.size() / vertex_size);

		const u8* src = mesh.vertex_buffer.data();

		for (int i = 0; i < vertex_count; ++i) {
			verts.push(*(Vec3*)(src + i * vertex_size));
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
		for (auto& mesh : m_meshes) {
			count += mesh.indices.size();
		}
		indices.reserve(count);
		int offset = 0;
		for (const ImportMesh& mesh : m_meshes) {
			for (unsigned int j = 0, c = mesh.indices.size(); j < c; ++j) {
				u32 index = mesh.indices[j] + offset;
				indices.push(index);
			}
			int vertex_count = (i32)(mesh.vertex_buffer.size() / mesh.vertex_size);
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

bool ModelImporter::writeModel(const Path& src, const ModelMeta& meta) {
	PROFILE_FUNCTION();
	if (m_meshes.empty() && m_animations.empty()) return false;

	m_out_file.clear();
	writeModelHeader();
	const BoneNameHash root_motion_bone(meta.root_motion_bone.c_str());
	write(root_motion_bone);
	writeMeshes(src, -1, meta);
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

					// TODO check if this works with different values of m_orientation
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

} // namespace Lumix