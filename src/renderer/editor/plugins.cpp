#include <imgui/imgui_freetype.h>

#include "animation/animation.h"
#include "editor/asset_browser.h"
#include "editor/asset_compiler.h"
#include "editor/property_grid.h"
#include "editor/render_interface.h"
#include "editor/settings.h"
#include "editor/studio_app.h"
#include "editor/utils.h"
#include "editor/world_editor.h"
#include "engine/crc32.h"
#include "engine/engine.h"
#include "engine/file_system.h"
#include "engine/geometry.h"
#include "engine/mt/atomic.h"
#include "engine/job_system.h"
#include "engine/log.h"
#include "engine/lua_wrapper.h"
#include "engine/lumix.h"
#include "engine/mt/thread.h"
#include "engine/os.h"
#include "engine/path_utils.h"
#include "engine/plugin_manager.h"
#include "engine/prefab.h"
#include "engine/profiler.h"
#include "engine/queue.h"
#include "engine/reflection.h"
#include "engine/resource_manager.h"
#include "engine/universe/universe.h"
#include "fbx_importer.h"
#include "game_view.h"
#include "renderer/culling_system.h"
#include "renderer/gpu/gpu.h"
#include "renderer/font.h"
#include "renderer/material.h"
#include "renderer/model.h"
#include "renderer/particle_system.h"
#include "renderer/pipeline.h"
#include "renderer/render_scene.h"
#include "renderer/renderer.h"
#include "renderer/shader.h"
#include "renderer/texture.h"
#include "scene_view.h"
#include "stb/stb_image.h"
#include "stb/stb_image_resize.h"
#include "terrain_editor.h"
#include <cmft/cubemapfilter.h>
#include <nvtt.h>


using namespace Lumix;


static const ComponentType PARTICLE_EMITTER_TYPE = Reflection::getComponentType("particle_emitter");
static const ComponentType TERRAIN_TYPE = Reflection::getComponentType("terrain");
static const ComponentType CAMERA_TYPE = Reflection::getComponentType("camera");
static const ComponentType DECAL_TYPE = Reflection::getComponentType("decal");
static const ComponentType POINT_LIGHT_TYPE = Reflection::getComponentType("point_light");
static const ComponentType ENVIRONMENT_TYPE = Reflection::getComponentType("environment");
static const ComponentType MODEL_INSTANCE_TYPE = Reflection::getComponentType("model_instance");
static const ComponentType TEXT_MESH_TYPE = Reflection::getComponentType("text_mesh");
static const ComponentType ENVIRONMENT_PROBE_TYPE = Reflection::getComponentType("environment_probe");
static const ComponentType LIGHT_PROBE_GRID_TYPE = Reflection::getComponentType("light_probe_grid");



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
			case 0: return Vec3(1.f, v, -u).normalized();
			case 1: return Vec3(-1.f, v, u).normalized();
			case 2: return Vec3(u, 1.f, -v).normalized();
			case 3: return Vec3(u, -1.f, v).normalized();
			case 4: return Vec3(u, v, 1.f).normalized();
			case 5: return Vec3(-u, v, -1.f).normalized();
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

static bool saveAsDDS(const char* path, const u8* data, int w, int h) {
	ASSERT(data);
	OS::OutputFile file;
	if (!file.open(path)) return false;
	
	nvtt::Context context;
		
	nvtt::InputOptions input;
	input.setMipmapGeneration(true);
	input.setAlphaMode(nvtt::AlphaMode_Transparency);
	input.setAlphaCoverageMipScale(0.3f, 3);
	input.setNormalMap(false);
	input.setTextureLayout(nvtt::TextureType_2D, w, h);
	input.setMipmapData(data, w, h);
		
	nvtt::OutputOptions output;
	output.setSrgbFlag(false);
	struct : nvtt::OutputHandler {
		bool writeData(const void * data, int size) override { return dst->write(data, size); }
		void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
		void endImage() override {}

		OS::OutputFile* dst;
	} output_handler;
	output_handler.dst = &file;
	output.setOutputHandler(&output_handler);

	nvtt::CompressionOptions compression;
	compression.setFormat(nvtt::Format_DXT5);
	compression.setQuality(nvtt::Quality_Fastest);

	if (!context.process(input, compression, output)) {
		file.close();
		return false;
	}
	file.close();
	return true;
}


struct FontPlugin final : public AssetBrowser::IPlugin, AssetCompiler::IPlugin
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


struct ParticleEmitterPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	explicit ParticleEmitterPlugin(StudioApp& app)
		: m_app(app)
	{
		app.getAssetCompiler().registerExtension("par", ParticleEmitterResource::TYPE);
	}


	bool compile(const Path& src) override
	{
		return m_app.getAssetCompiler().copyCompile(src);
	}
	
	
	void onGUI(Span<Resource*> resources) override {}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Particle Emitter"; }
	ResourceType getResourceType() const override { return ParticleEmitterResource::TYPE; }


	StudioApp& m_app;
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
		OS::OutputFile file;
		if (!file.open(path)) {
			logError("Renderer") << "Failed to create " << path;
			return false;
		}

		file << "shader \"/pipelines/standard.shd\"";
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
				logError("Editor") << "Could not save file " << material->getPath().c_str();
			}
			m_app.getAssetBrowser().endSaveResource(*material, *file, success);
		}
	}

	void onGUIMultiple(Span<Resource*> resources) {
		if (ImGui::Button("Open in external editor")) {
			for (Resource* res : resources) {
				m_app.getAssetBrowser().openInExternalEditor(res);
			}
		}

		for (Resource* res : resources) {
			if (!res->isReady()) {
				ImGui::Text("%s is not ready", res->getPath().c_str());
				return;
			}
		}
		ImGui::SameLine();
		if (ImGui::Button("Save")) {
			for (Resource* res : resources) {
				saveMaterial(static_cast<Material*>(res));
			}
		}

		char buf[MAX_PATH_LENGTH];
		auto* first = static_cast<Material*>(resources[0]);

		bool same_shader = true;
		for (Resource* res : resources) {
			if (static_cast<Material*>(res)->getShader() != first->getShader()) {
				same_shader = false;
			}
		}

		if (same_shader) {
			copyString(buf, first->getShader() ? first->getShader()->getPath().c_str() : "");
		}
		else {
			copyString(buf, "<different values>");
		}

		if (m_app.getAssetBrowser().resourceInput("Shader", "shader", Span(buf), Shader::TYPE)) {
			for (Resource* res : resources) {
				static_cast<Material*>(res)->setShader(Path(buf));
			}
		}

		if (!same_shader) return;

	}

	void onGUI(Span<Resource*> resources) override {
		if (resources.length() > 1) {
			onGUIMultiple(resources);
			return;
		}

		Material* material = static_cast<Material*>(resources[0]);
		if (ImGui::Button("Open in external editor")) m_app.getAssetBrowser().openInExternalEditor(material);

		if (ImGui::Button("Save")) saveMaterial(material);
		ImGui::SameLine();

		auto* plugin = m_app.getEngine().getPluginManager().getPlugin("renderer");
		auto* renderer = static_cast<Renderer*>(plugin);

		int alpha_cutout_define = renderer->getShaderDefineIdx("ALPHA_CUTOUT");

		bool b = material->isBackfaceCulling();
		if (ImGui::Checkbox("Backface culling", &b)) material->enableBackfaceCulling(b);

		if (material->getShader() 
			&& material->getShader()->isReady() 
			&& material->getShader()->hasDefine(alpha_cutout_define))
		{
			b = material->isDefined(alpha_cutout_define);
			if (ImGui::Checkbox("Is alpha cutout", &b)) material->setDefine(alpha_cutout_define, b);
			if (b) {
				float tmp = material->getAlphaRef();
				if (ImGui::DragFloat("Alpha reference value", &tmp, 0.01f, 0.0f, 1.0f)) {
					material->setAlphaRef(tmp);
				}
			}
		}
		
		const Shader* shader = material->getShader() && material->getShader()->isReady() ? material->getShader() : nullptr;
		if (shader) {
			if (!shader->isIgnored(Shader::COLOR)) {
				Vec4 color = material->getColor();
				if (ImGui::ColorEdit4("Color", &color.x)) {
					material->setColor(color);
				}
			}

			if (!shader->isIgnored(Shader::ROUGHNESS)) {
				float roughness = material->getRoughness();
				if (ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f)) {
					material->setRoughness(roughness);
				}
			}

			if (!shader->isIgnored(Shader::METALLIC)) {
				float metallic = material->getMetallic();
				if (ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f)) {
					material->setMetallic(metallic);
				}
			}
			
			if (!shader->isIgnored(Shader::EMISSION)) {
				float emission = material->getEmission();
				if (ImGui::DragFloat("Emission", &emission, 0.01f, 0.0f)) {
					material->setEmission(emission);
				}
			}
		}

		char buf[MAX_PATH_LENGTH];
		copyString(buf, material->getShader() ? material->getShader()->getPath().c_str() : "");
		if (m_app.getAssetBrowser().resourceInput("Shader", "shader", Span(buf), Shader::TYPE)) {
			material->setShader(Path(buf));
		}

		const char* current_layer_name = renderer->getLayerName(material->getLayer());
		if (ImGui::BeginCombo("Layer", current_layer_name)) {
			for (u8 i = 0, c = renderer->getLayersCount(); i < c; ++i) {
				const char* name = renderer->getLayerName(i);
				if (ImGui::Selectable(name)) {
					material->setLayer(i);
				}
			}
			ImGui::EndCombo();
		}

		for (u32 i = 0; material->getShader() && i < material->getShader()->m_texture_slot_count; ++i) {
			auto& slot = material->getShader()->m_texture_slots[i];
			Texture* texture = material->getTexture(i);
			copyString(buf, texture ? texture->getPath().c_str() : "");
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
			if (m_app.getAssetBrowser().resourceInput(
					slot.name, StaticString<30>("", (u64)&slot), Span(buf), Texture::TYPE))
			{
				material->setTexturePath(i, Path(buf));
			}
			if (!texture && is_node_open) {
				ImGui::TreePop();
				continue;
			}

			if (is_node_open) {
				ImGui::Image((void*)(uintptr_t)texture->handle.value, ImVec2(96, 96));

				for (int i = 0; i < Material::getCustomFlagCount(); ++i) {
					bool b = material->isCustomFlag(1 << i);
					if (ImGui::Checkbox(Material::getCustomFlagName(i), &b)) {
						if (b)
							material->setCustomFlag(1 << i);
						else
							material->unsetCustomFlag(1 << i);
					}
				}

				ImGui::TreePop();
			}
		}

		if (material->getShader() && material->isReady()) {
			const Shader* shader = material->getShader();
			for (int i = 0; i < shader->m_uniforms.size(); ++i) {
				const Shader::Uniform& shader_uniform = shader->m_uniforms[i];
				Material::Uniform* uniform = material->findUniform(shader_uniform.name_hash);
				if (!uniform) continue;

				switch (shader_uniform.type) {
					case Shader::Uniform::FLOAT:
						if (ImGui::DragFloat(shader_uniform.name, &uniform->float_value)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::VEC3:
						if (ImGui::DragFloat3(shader_uniform.name, uniform->vec3)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::VEC4:
						if (ImGui::DragFloat4(shader_uniform.name, uniform->vec4)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::VEC2:
						if (ImGui::DragFloat2(shader_uniform.name, uniform->vec2)) {
							material->updateRenderData(false);
						}
						break;
					case Shader::Uniform::COLOR:
						if (ImGui::ColorEdit3(shader_uniform.name, uniform->vec3)) {
							material->updateRenderData(false);
						}
						break;
					default: ASSERT(false); break;
				}
			}
			
			if (ImGui::CollapsingHeader("Defines")) {
				for (int i = 0; i < renderer->getShaderDefinesCount(); ++i) {
					const char* define = renderer->getShaderDefine(i);
					if (!shader->hasDefine(i)) continue;
					bool value = material->isDefined(i);

					auto isBuiltinDefine = [](const char* define) {
						const char* BUILTIN_DEFINES[] = {"HAS_SHADOWMAP", "ALPHA_CUTOUT", "SKINNED"};
						for (const char* builtin_define : BUILTIN_DEFINES)
						{
							if (equalStrings(builtin_define, define)) return true;
						}
						return false;
					};

					bool is_texture_define = material->isTextureDefine(i);
					if (!is_texture_define && !isBuiltinDefine(define) && ImGui::Checkbox(define, &value)) {
						material->setDefine(i, value);
					}
				}
			}

		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Material"; }
	ResourceType getResourceType() const override { return Material::TYPE; }


	StudioApp& m_app;
};


struct ModelPlugin final : AssetBrowser::IPlugin, AssetCompiler::IPlugin
{
	struct Meta
	{
		float scale = 1;
		bool split = false;
		bool create_impostor = false;
		float lods_distances[4] = { -1, -1, -1, -1 };
		float position_error = 0.02f;
		float rotation_error = 0.001f;
	};

	explicit ModelPlugin(StudioApp& app)
		: m_app(app)
		, m_mesh(INVALID_ENTITY)
		, m_pipeline(nullptr)
		, m_universe(nullptr)
		, m_is_mouse_captured(false)
		, m_tile(app.getAllocator())
		, m_fbx_importer(app)
	{
		app.getAssetCompiler().registerExtension("fbx", Model::TYPE);
		createPreviewUniverse();
		createTileUniverse();
		m_viewport.is_ortho = false;
		m_viewport.fov = degreesToRadians(60.f);
		m_viewport.near = 0.1f;
		m_viewport.far = 10000.f;
	}


	~ModelPlugin()
	{
		JobSystem::wait(m_subres_signal);
		auto& engine = m_app.getEngine();
		engine.destroyUniverse(*m_universe);
		Pipeline::destroy(m_pipeline);
		engine.destroyUniverse(*m_tile.universe);
		Pipeline::destroy(m_tile.pipeline);
	}


	Meta getMeta(const Path& path) const
	{
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "position_error", &meta.position_error);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "rotation_error", &meta.rotation_error);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "scale", &meta.scale);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "split", &meta.split);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "create_impostor", &meta.create_impostor);
			
			for (u32 i = 0; i < lengthOf(meta.lods_distances); ++i) {
				LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, StaticString<32>("lod", i, "_distance"), &meta.lods_distances[i]);
			}
		});
		return meta;
	}

	void addSubresources(AssetCompiler& compiler, const char* path) override {
		compiler.addResource(Model::TYPE, path);

		const Meta meta = getMeta(Path(path));
		struct JobData {
			ModelPlugin* plugin;
			StaticString<MAX_PATH_LENGTH> path;
			Meta meta;
		};
		JobData* data = LUMIX_NEW(m_app.getAllocator(), JobData);
		data->plugin = this;
		data->path = path;
		data->meta = meta;
		JobSystem::runEx(data, [](void* ptr) {
			JobData* data = (JobData*)ptr;
			ModelPlugin* plugin = data->plugin;
			FBXImporter importer(plugin->m_app);
			AssetCompiler& compiler = plugin->m_app.getAssetCompiler();

			const char* path = data->path[0] == '/' ? data->path.data + 1 : data->path;
			importer.setSource(path, true);

			if(data->meta.split) {
				const Array<FBXImporter::ImportMesh>& meshes = importer.getMeshes();
				for (int i = 0; i < meshes.size(); ++i) {
					char mesh_name[256];
					importer.getImportMeshName(meshes[i], mesh_name);
					StaticString<MAX_PATH_LENGTH> tmp(mesh_name, ".fbx:", path);
					compiler.addResource(Model::TYPE, tmp);
				}
			}

			const Array<FBXImporter::ImportAnimation>& animations = importer.getAnimations();
			for (const FBXImporter::ImportAnimation& anim : animations) {
				StaticString<MAX_PATH_LENGTH> tmp(anim.name, ".ani:", path);
				compiler.addResource(ResourceType("animation"), tmp);
			}

			LUMIX_DELETE(plugin->m_app.getAllocator(), data);
		}, &m_subres_signal, JobSystem::INVALID_HANDLE, 2);			
	}

	static const char* getResourceFilePath(const char* str)
	{
		const char* c = str;
		while (*c && *c != ':') ++c;
		return *c != ':' ? str : c + 1;
	}

	bool compile(const Path& src) override
	{
		ASSERT(PathUtils::hasExtension(src.c_str(), "fbx"));
		const char* filepath = getResourceFilePath(src.c_str());
		FBXImporter::ImportConfig cfg;
		const Meta meta = getMeta(Path(filepath));
		cfg.rotation_error = meta.rotation_error;
		cfg.position_error = meta.position_error;
		cfg.mesh_scale = meta.scale;
		memcpy(cfg.lods_distances, meta.lods_distances, sizeof(meta.lods_distances));
		cfg.create_impostor = meta.create_impostor;
		const PathUtils::FileInfo src_info(filepath);
		m_fbx_importer.setSource(filepath, false);
		if (m_fbx_importer.getMeshes().empty() && m_fbx_importer.getAnimations().empty()) {
			if (m_fbx_importer.getOFBXScene()) {
				if (m_fbx_importer.getOFBXScene()->getMeshCount() > 0) {
					logError("Editor") << "No meshes with materials found in " << src;
				}
				else {
					logError("Editor") << "No meshes or animations found in " << src;
				}
			}
		}

		const StaticString<32> hash_str("", src.getHash());
		if (meta.split) {
			//cfg.origin = FBXImporter::ImportConfig::Origin::CENTER;
			m_fbx_importer.writeSubmodels(filepath, cfg);
			m_fbx_importer.writePrefab(filepath, cfg);
		}
		m_fbx_importer.writeModel(src.c_str(), cfg);
		m_fbx_importer.writeMaterials(filepath, cfg);
		m_fbx_importer.writeAnimations(filepath, cfg);
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
		render_scene->getEnvironmentProbe(env_probe).half_extents = Vec3(1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_tile.universe->createEntity({10, 10, 10}, mtx.getRotation());
		m_tile.universe->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_scene->getEnvironment(light_entity).diffuse_intensity = 1;
		render_scene->getEnvironment(light_entity).indirect_intensity = 1;
		
		m_tile.pipeline->setUniverse(m_tile.universe);
	}


	void createPreviewUniverse()
	{
		auto& engine = m_app.getEngine();
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
		render_scene->getEnvironmentProbe(env_probe).half_extents = Vec3(1e3);

		Matrix mtx;
		mtx.lookAt({10, 10, 10}, Vec3::ZERO, {0, 1, 0});
		const EntityRef light_entity = m_universe->createEntity({0, 0, 0}, mtx.getRotation());
		m_universe->createComponent(ENVIRONMENT_TYPE, light_entity);
		render_scene->getEnvironment(light_entity).diffuse_intensity = 1;
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
			m_viewport.pos = DVec3(0) + center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length();
			m_viewport.rot = Quat::vec3ToVec3({0, 0, 1}, {1, 1, 1});
		}
		ImVec2 image_size(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().x);

		m_viewport.w = (int)image_size.x;
		m_viewport.h = (int)image_size.y;
		m_pipeline->setViewport(m_viewport);
		m_pipeline->render(false);
		m_preview = m_pipeline->getOutput();
		ImGui::Image((void*)(uintptr_t)m_preview.value, image_size);
		bool mouse_down = ImGui::IsMouseDown(0) || ImGui::IsMouseDown(1);
		if (m_is_mouse_captured && !mouse_down)
		{
			m_is_mouse_captured = false;
			OS::showCursor(true);
			OS::setMouseScreenPos(m_captured_mouse_x, m_captured_mouse_y);
		}

		if (ImGui::GetIO().MouseClicked[1] && ImGui::IsItemHovered()) ImGui::OpenPopup("PreviewPopup");

		if (ImGui::BeginPopup("PreviewPopup"))
		{
			if (ImGui::Selectable("Save preview"))
			{
				model.getResourceManager().load(model);
				renderTile(&model, &m_viewport.pos, &m_viewport.rot);
			}
			ImGui::EndPopup();
		}

		if (ImGui::IsItemHovered() && mouse_down)
		{
			const Vec2 delta = m_app.getMouseMove();

			if (!m_is_mouse_captured)
			{
				m_is_mouse_captured = true;
				OS::showCursor(false);
				const OS::Point p = OS::getMouseScreenPos();
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
				rot = yaw_rot * rot;
				rot.normalize();

				Vec3 pitch_axis = rot.rotate(Vec3(1, 0, 0));
				float pitch =
					-signum(delta.y) * (powf(fabsf((float)delta.y / MOUSE_SENSITIVITY.y), 1.2f));
				Quat pitch_rot(pitch_axis, pitch);
				rot = pitch_rot * rot;
				rot.normalize();

				Vec3 dir = rot.rotate(Vec3(0, 0, 1));
				Vec3 origin = (model.getAABB().max + model.getAABB().min) * 0.5f;

				float dist = (origin - pos.toFloat()).length();
				pos = DVec3(0) + origin + dir * dist;

				m_viewport.rot = rot;
				m_viewport.pos = pos;
			}
		}
	}


	static void postprocessImpostor(Ref<Array<u32>> gb0, Ref<Array<u32>> gb1, const IVec2& tile_size, IAllocator& allocator) {
		struct Cell {
			i16 x, y;
		};
		const IVec2 size = tile_size * 9;
		Array<Cell> cells(allocator);
		cells.resize(gb0->size());
		const u32* data = gb0->begin();
		for (i32 j = 0; j < size.y; ++j) {
			for (i32 i = 0; i < size.x; ++i) {
				const u32 idx = i + j * size.x;
				if (data[idx] & 0x000000ff) {
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
				if (data[idx] & 0x000000ff) {
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
				if (data[idx] & 0xff) {
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
		tmp.resize(gb0->size());
		for (i32 j = 0; j < size.y; ++j) {
			for (i32 i = 0; i < size.x; ++i) {
				const u32 idx = i + j * size.x;
				const u8 alpha = data[idx] >> 24;
				tmp[idx] = data[cells[idx].x + cells[idx].y * size.x];
				tmp[idx] = alpha << 24 | tmp[idx] & 0xffFFff;
			}
		}
		memcpy(gb0->begin(), tmp.begin(), tmp.byte_size());

		const u32* gb1_data = gb1->begin();
		for (i32 j = 0; j < size.y; ++j) {
			for (i32 i = 0; i < size.x; ++i) {
				const u32 idx = i + j * size.x;
				tmp[idx] = gb1_data[cells[idx].x + cells[idx].y * size.x];
			}
		}
		memcpy(gb1->begin(), tmp.begin(), tmp.byte_size());
	}

	void onGUI(Span<Resource*> resources) override
	{
		if (resources.length() > 1) return;

		auto* model = static_cast<Model*>(resources[0]);

		if (model->isReady()) {
			ImGui::LabelText("Bounding radius", "%f", model->getBoundingRadius());

			auto* lods = model->getLODs();
			if (lods[0].to_mesh >= 0 && !model->isFailure())
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
				for (int i = 0; i < Model::MAX_LOD_COUNT && lods[i].to_mesh >= 0; ++i)
				{
					ImGui::PushID(i);
					ImGui::Text("%d", i);
					ImGui::NextColumn();
					if (lods[i].distance == FLT_MAX)
					{
						ImGui::Text("Infinite");
					}
					else
					{
						float dist = sqrtf(lods[i].distance);
						if (ImGui::DragFloat("", &dist))
						{
							lods[i].distance = dist * dist;
						}
					}
					ImGui::NextColumn();
					ImGui::Text("%d", lods[i].to_mesh - lods[i].from_mesh + 1);
					ImGui::NextColumn();
					int tri_count = 0;
					for (int j = lods[i].from_mesh; j <= lods[i].to_mesh; ++j)
					{
						int indices_count = model->getMesh(j).indices.size() >> 1;
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

			ImGui::Separator();
		}

		for (int i = 0; i < model->getMeshCount(); ++i)
		{
			auto& mesh = model->getMesh(i);
			if (ImGui::TreeNode(&mesh, "%s", mesh.name.length() > 0 ? mesh.name.c_str() : "N/A"))
			{
				ImGui::LabelText("Triangle count", "%d", (mesh.indices.size() >> (mesh.areIndices16() ? 1 : 2))/ 3);
				ImGui::LabelText("Material", "%s", mesh.material->getPath().c_str());
				ImGui::SameLine();
				if (ImGui::Button("->"))
				{
					m_app.getAssetBrowser().selectResource(mesh.material->getPath(), true, false);
				}
				ImGui::TreePop();
			}
		}

		if (model->isReady()) {
			ImGui::LabelText("Bone count", "%d", model->getBoneCount());
			if (model->getBoneCount() > 0 && ImGui::CollapsingHeader("Bones")) {
				ImGui::Columns(3);
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
				}
			}
		}

		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			if(m_meta_res != model->getPath().getHash()) {
				m_meta = getMeta(model->getPath());
				m_meta_res = model->getPath().getHash();
			}
			ImGui::InputFloat("Max position error", &m_meta.position_error);
			ImGui::InputFloat("Max rotation error", &m_meta.rotation_error);
			ImGui::InputFloat("Scale", &m_meta.scale);
			ImGui::Checkbox("Split", &m_meta.split);
			ImGui::Checkbox("Create impostor", &m_meta.create_impostor);
			for(u32 i = 0; i < lengthOf(m_meta.lods_distances); ++i) {
				bool infinite = m_meta.lods_distances[i] <= 0;
				if(ImGui::Checkbox(StaticString<32>("Infinite LOD ", i), &infinite)) {
					m_meta.lods_distances[i] *= -1;
				}
				if (m_meta.lods_distances[i] > 0) {
					ImGui::SameLine();
					ImGui::PushItemWidth(-1);
					ImGui::DragFloat(StaticString<32>("##lod", i), &m_meta.lods_distances[i]);
					ImGui::PopItemWidth();
				}
			}
			
			if (ImGui::Button("Apply")) {
				String src(m_app.getAllocator());
				src.cat("create_impostor=").cat(m_meta.create_impostor ? "true" : "false")
					.cat("\nposition_error = ").cat(m_meta.position_error)
					.cat("\nrotation_error = ").cat(m_meta.rotation_error)
					.cat("\nscale = ").cat(m_meta.scale)
					.cat("\nscale = ").cat(m_meta.scale)
					.cat("\nsplit = ").cat(m_meta.split ? "true\n" : "false\n");

				for (u32 i = 0; i < lengthOf(m_meta.lods_distances); ++i) {
					if (m_meta.lods_distances[i] > 0) {
						src.cat("lod").cat(i).cat("_distance").cat(" = ").cat(m_meta.lods_distances[i]).cat("\n");
					}
				}

				compiler.updateMeta(model->getPath(), src.c_str());
				if (compiler.compile(model->getPath())) {
					model->getResourceManager().reload(*model);
				}
			}
			if (ImGui::Button("Create impostor texture")) {
				FBXImporter importer(m_app);
				IAllocator& allocator = m_app.getAllocator();
				Array<u32> gb0(allocator); 
				Array<u32> gb1(allocator); 
				IVec2 tile_size;
				importer.createImpostorTextures(model, Ref(gb0), Ref(gb1), Ref(tile_size));
				postprocessImpostor(Ref(gb0), Ref(gb1), tile_size, allocator);
				const PathUtils::FileInfo fi(model->getPath().c_str());
				StaticString<MAX_PATH_LENGTH> img_path(fi.m_dir, fi.m_basename, "_impostor0.tga");
				ASSERT(gb0.size() == tile_size.x * 9 * tile_size.y * 9);
				
				OS::OutputFile file;
				if (file.open(img_path)) {
					Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)gb0.begin(), true, Path(img_path), allocator);
					file.close();
				}
				else {
					logError("Renderer") << "Failed to open " << img_path;
				}

				img_path = fi.m_dir;
				img_path << fi.m_basename << "_impostor1.tga";
				if (file.open(img_path)) {
					Texture::saveTGA(&file, tile_size.x * 9, tile_size.y * 9, gpu::TextureFormat::RGBA8, (const u8*)gb1.begin(), true, Path(img_path), allocator);
					file.close();
				}
				else {
					logError("Renderer") << "Failed to open " << img_path;
				}
			}
		}

		showPreview(*model);
	}

	Meta m_meta;
	u32 m_meta_res = 0;

	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Model"; }
	ResourceType getResourceType() const override { return Model::TYPE; }


	void pushTileQueue(const Path& path)
	{
		ASSERT(!m_tile.queue.full());
		Engine& engine = m_app.getEngine();
		ResourceManagerHub& resource_manager = engine.getResourceManager();

		Resource* resource;
		if (PathUtils::hasExtension(path.c_str(), "fab")) {
			resource = resource_manager.load<PrefabResource>(path);
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
		if (m_tile.frame_countdown >= 0) {
			--m_tile.frame_countdown;
			if (m_tile.frame_countdown == -1) {
				destroyEntityRecursive(*m_tile.universe, (EntityRef)m_tile.entity);
				Engine& engine = m_app.getEngine();
				FileSystem& fs = engine.getFileSystem();
				StaticString<MAX_PATH_LENGTH> path(fs.getBasePath(), ".lumix/asset_tiles/", m_tile.path_hash, ".dds");
				
				for (u32 i = 0; i < u32(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE); ++i) {
					swap(m_tile.data[i * 4 + 0], m_tile.data[i * 4 + 2]);
				}

				saveAsDDS(path, &m_tile.data[0], AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE);
				memset(m_tile.data.begin(), 0, m_tile.data.byte_size());
				Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
				renderer->destroy(m_tile.texture);
				m_tile.entity = INVALID_ENTITY;
			}
			return;
		}

		if (m_tile.entity.isValid()) return;
		if (m_tile.queue.empty()) return;

		Resource* resource = m_tile.queue.front();
		if (resource->isFailure()) {
			logError("Editor") << "Failed to load " << resource->getPath();
			popTileQueue();
			return;
		}
		if (!resource->isReady()) return;

		popTileQueue();

		if (resource->getType() == Model::TYPE) {
			renderTile((Model*)resource, nullptr, nullptr);
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
		if (!engine.instantiatePrefab(*m_tile.universe, *prefab, DVec3(0), Quat::IDENTITY, 1, Ref(entity_map))) return;
		// TODO there can be more than one model or model not in root
		const EntityPtr mesh_entity = entity_map.m_map[0];
		if (!mesh_entity.isValid()) return;

		if (!render_scene->getUniverse().hasComponent((EntityRef)mesh_entity, MODEL_INSTANCE_TYPE)) return;

		Model* model = render_scene->getModelInstanceModel((EntityRef)mesh_entity);
		if (!model) return;

		m_tile.path_hash = prefab->getPath().getHash();
		prefab->getResourceManager().unload(*prefab);
		m_tile.entity = mesh_entity;
		model->onLoaded<&ModelPlugin::renderPrefabSecondStage>(this);
	}


	void renderPrefabSecondStage(Resource::State old_state, Resource::State new_state, Resource& resource)
	{
		Engine& engine = m_app.getEngine();

		RenderScene* render_scene = (RenderScene*)m_tile.universe->getScene(MODEL_INSTANCE_TYPE);
		if (!render_scene) return;

		Renderer* renderer = (Renderer*)engine.getPluginManager().getPlugin("renderer");
		if (!renderer) return;

		if (new_state != Resource::State::READY) return;
		Model* model = (Model*)&resource;
		if (!model->isReady()) return;

		AABB aabb = model->getAABB();

		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length() / SQRT2;
		Matrix mtx;
		mtx.lookAt(eye, center, Vec3(-1, 1, -1).normalized());
		mtx.inverse();
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.far = 10000.f;
		viewport.near = 0.1f;
		viewport.fov = degreesToRadians(60.f);
		viewport.h = AssetBrowser::TILE_SIZE;
		viewport.w = AssetBrowser::TILE_SIZE;
		viewport.pos = DVec3(eye.x, eye.y, eye.z);
		viewport.rot = mtx.getRotation();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);

		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		m_tile.texture = gpu::allocTextureHandle(); 
		renderer->getTextureImage(m_tile.pipeline->getOutput(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, gpu::TextureFormat::RGBA8,  Span(m_tile.data.begin(), m_tile.data.end()));
		
		m_tile.frame_countdown = 2;
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
		AABB aabb = model->getAABB();

		Matrix mtx;
		Vec3 center = (aabb.max + aabb.min) * 0.5f;
		Vec3 eye = center + Vec3(1, 1, 1) * (aabb.max - aabb.min).length() / SQRT2;
		mtx.lookAt(eye, center, Vec3(1, -1, 1).normalized());
		mtx.inverse();
		Viewport viewport;
		viewport.is_ortho = false;
		viewport.far = 10000.f;
		viewport.near = 0.1f;
		viewport.fov = degreesToRadians(60.f);
		viewport.h = AssetBrowser::TILE_SIZE;
		viewport.w = AssetBrowser::TILE_SIZE;
		viewport.pos = in_pos ? *in_pos : DVec3(eye.x, eye.y, eye.z);
		viewport.rot = in_rot ? *in_rot : mtx.getRotation();
		m_tile.pipeline->setViewport(viewport);
		m_tile.pipeline->render(false);

		m_tile.texture = gpu::allocTextureHandle(); 
		m_tile.data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
		renderer->getTextureImage(m_tile.pipeline->getOutput(), AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE, gpu::TextureFormat::RGBA8, Span(m_tile.data.begin(), m_tile.data.end()));
		
		m_tile.entity = mesh_entity;
		m_tile.frame_countdown = 2;
		m_tile.path_hash = model->getPath().getHash();
		model->getResourceManager().unload(*model);
	}


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (type == Material::TYPE) return fs.copyFile("models/editor/tile_material.dds", out_path);
		if (type == Shader::TYPE) return fs.copyFile("models/editor/tile_shader.dds", out_path);

		if (type != Model::TYPE && type != PrefabResource::TYPE) return false;

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
			, queue(allocator)
			, paths(allocator)
		{
		}

		Universe* universe = nullptr;
		Pipeline* pipeline = nullptr;
		EntityPtr entity = INVALID_ENTITY;
		int frame_countdown = -1;
		u32 path_hash;
		Array<u8> data;
		gpu::TextureHandle texture = gpu::INVALID_TEXTURE;
		Queue<Resource*, 8> queue;
		Array<Path> paths;
	} m_tile;
	

	StudioApp& m_app;
	gpu::TextureHandle m_preview;
	Universe* m_universe;
	Viewport m_viewport;
	Pipeline* m_pipeline;
	EntityPtr m_mesh = INVALID_ENTITY;
	bool m_is_mouse_captured;
	int m_captured_mouse_x;
	int m_captured_mouse_y;
	FBXImporter m_fbx_importer;
	JobSystem::SignalHandle m_subres_signal = JobSystem::INVALID_HANDLE;
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
			POINT
		};
		bool srgb = false;
		bool is_normalmap = false;
		float scale_coverage = -1;
		bool convert_to_raw = false;
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
		app.getAssetCompiler().registerExtension("jpg", Texture::TYPE);
		app.getAssetCompiler().registerExtension("tga", Texture::TYPE);
		app.getAssetCompiler().registerExtension("dds", Texture::TYPE);
		app.getAssetCompiler().registerExtension("raw", Texture::TYPE);
		app.getAssetCompiler().registerExtension("ltc", Texture::TYPE);
	}


	~TexturePlugin() {
		PluginManager& plugin_manager = m_app.getEngine().getPluginManager();
		auto* renderer = (Renderer*)plugin_manager.getPlugin("renderer");
		if(m_texture_view.isValid()) {
			renderer->destroy(m_texture_view);
		}
	}

	const char* getDefaultExtension() const override { return "ltc"; }
	const char* getFileDialogFilter() const override { return "Composite texture\0*.ltc\0"; }
	const char* getFileDialogExtensions() const override { return "ltc"; }
	bool canCreateResource() const override { return true; }
	bool createResource(const char* path) override { 
		OS::OutputFile file;
		if (!file.open(path)) {
			logError("Renderer") << "Failed to create " << path;
			return false;
		}

		file.close();
		return true;
	}

	struct TextureTileJob
	{
		TextureTileJob(FileSystem& filesystem, IAllocator& allocator) 
			: m_allocator(allocator) 
			, m_filesystem(filesystem)
		{}

		void execute() {
			IAllocator& allocator = m_allocator;

			u32 hash = crc32(m_in_path);
			StaticString<MAX_PATH_LENGTH> out_path(".lumix/asset_tiles/", hash, ".dds");
			Array<u8> resized_data(allocator);
			resized_data.resize(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE * 4);
			if (PathUtils::hasExtension(m_in_path, "dds")) {
				OS::InputFile file;
				if (!file.open(m_in_path)) {
					m_filesystem.copyFile("models/editor/tile_texture.dds", out_path);
					logError("Editor") << "Failed to load " << m_in_path;
					return;
				}
				Array<u8> data(allocator);
				data.resize((int)file.size());
				file.read(&data[0], data.size());
				file.close();

				nvtt::Surface surface;
				if (!surface.load(m_in_path, data.begin(), data.byte_size())) {
					logError("Editor") << "Failed to load " << m_in_path;
					m_filesystem.copyFile("models/editor/tile_texture.dds", out_path);
					return;
				}

				Array<u8> decompressed(allocator);
				const int w = surface.width();
				const int h = surface.height();
				decompressed.resize(4 * w * h);
				for (int c = 0; c < 4; ++c) {
					const float* data = surface.channel(c);
					for (int j = 0; j < h; ++j) {
						for (int i = 0; i < w; ++i) {
							const u8 p = u8(data[j * w + i] * 255.f + 0.5f);
							decompressed[(j * w + i) * 4 + c] = p;
						}
					}
				}

				stbir_resize_uint8((const u8*)decompressed.begin(),
					w,
					h,
					0,
					&resized_data[0],
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
			}
			else
			{
				int image_comp;
				int w, h;
				auto data = stbi_load(m_in_path, &w, &h, &image_comp, 4);
				if (!data)
				{
					logError("Editor") << "Failed to load " << m_in_path;
					m_filesystem.copyFile("models/editor/tile_texture.dds", out_path);
					return;
				}
				stbir_resize_uint8(data,
					w,
					h,
					0,
					&resized_data[0],
					AssetBrowser::TILE_SIZE,
					AssetBrowser::TILE_SIZE,
					0,
					4);
				for (u32 i = 0; i < u32(AssetBrowser::TILE_SIZE * AssetBrowser::TILE_SIZE); ++i) {
					swap(resized_data[i * 4 + 0], resized_data[i * 4 + 2]);
				}
				stbi_image_free(data);
			}

			if (!saveAsDDS(m_out_path, &resized_data[0], AssetBrowser::TILE_SIZE, AssetBrowser::TILE_SIZE)) {
				logError("Editor") << "Failed to save " << m_out_path;
			}
		}

		static void execute(void* data) {
			PROFILE_FUNCTION();
			TextureTileJob* that = (TextureTileJob*)data;
			that->execute();
			LUMIX_DELETE(that->m_allocator, that);
		}

		IAllocator& m_allocator;
		FileSystem& m_filesystem;
		StaticString<MAX_PATH_LENGTH> m_in_path; 
		StaticString<MAX_PATH_LENGTH> m_out_path; 
	};


	bool createTile(const char* in_path, const char* out_path, ResourceType type) override
	{
		if (type == Texture::TYPE && !PathUtils::hasExtension(in_path, "ltc") && !PathUtils::hasExtension(in_path, "raw")) {
			IAllocator& allocator = m_app.getAllocator();
			FileSystem& fs = m_app.getEngine().getFileSystem();
			auto* job = LUMIX_NEW(allocator, TextureTileJob)(fs, allocator);
			job->m_in_path = fs.getBasePath();
			job->m_in_path << in_path;
			job->m_out_path = fs.getBasePath();
			job->m_out_path << out_path;
			JobSystem::SignalHandle signal = JobSystem::INVALID_HANDLE;
			JobSystem::runEx(job, &TextureTileJob::execute, &signal, m_tile_signal, JobSystem::getWorkersCount() - 1);
			m_tile_signal = signal;
			return true;
		}
		return false;
	}

	struct TextureComposite {
		struct ChannelSource {
			Path path;
			u32 src_channel = 0;
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

		static TextureComposite& getThis(lua_State* L) {
			lua_getfield(L, LUA_GLOBALSINDEX, "this");
			TextureComposite* tc = (TextureComposite*)lua_touserdata(L, -1);
			lua_pop(L, 1);
			ASSERT(tc);
			return *tc;
		}

		static ChannelSource toChannelSource(lua_State* L, int idx) {
			ChannelSource res;
			if (!lua_istable(L, idx)) {
				luaL_argerror(L, 1, "unexpected form");
			}
			const size_t l = lua_objlen(L, idx);
			if (l == 0) {
				luaL_argerror(L, 1, "unexpected form");
			}
			lua_rawgeti(L, idx, 1);
			if (!lua_isstring(L, -1)) {
				luaL_argerror(L, 1, "unexpected form");
			}
			res.path = lua_tostring(L, -1);
			lua_pop(L, 1);

			if (l > 1) {
				lua_rawgeti(L, idx, 2);
				if (!lua_isnumber(L, -1)) {
					luaL_argerror(L, 1, "unexpected form");
				}
				res.src_channel = (u32)lua_tointeger(L, -1);
				lua_pop(L, 1);
			}

			return res;
		}

		static int LUA_layer(lua_State* L) {
			LuaWrapper::DebugGuard guard(L);
			LuaWrapper::checkTableArg(L, 1);
			TextureComposite& that = getThis(L);

			Layer& layer = that.layers.emplace();

			lua_pushnil(L);
			while (lua_next(L, 1)) {
				const char* key = LuaWrapper::toType<const char*>(L, -2);
				if (equalIStrings(key, "rgb")) {
					layer.red = toChannelSource(L, -1);
					layer.green = layer.red;
					layer.blue = layer.red;
					layer.red.src_channel = 0;
					layer.green.src_channel = 1;
					layer.blue.src_channel = 2;
				}
				else if (equalIStrings(key, "alpha")) {
					layer.alpha = toChannelSource(L, -1);
				}
				else if (equalIStrings(key, "red")) {
					layer.red = toChannelSource(L, -1);
				}
				else if (equalIStrings(key, "green")) {
					layer.green = toChannelSource(L, -1);
				}
				else if (equalIStrings(key, "blue")) {
					layer.blue = toChannelSource(L, -1);
				}
				else {
					luaL_argerror(L, 1, StaticString<128>("unknown key ", key));
				}
				lua_pop(L, 1);
			}
			return 0;
		}

		static int LUA_output(lua_State* L) {
			const char* type = LuaWrapper::checkArg<const char*>(L, 1);
			
			TextureComposite& that = getThis(L);
			if (equalIStrings(type, "bc1")) {
				that.output = Output::BC1;
			}
			else if (equalIStrings(type, "bc3")) {
				that.output = Output::BC3;
			}
			else {
				luaL_argerror(L, 1, "unknown value");
			}
			return 0;
		}

		TextureComposite(IAllocator& allocator)
			: layers(allocator)
		{}

		bool init(Span<u8> data, const char* src_path) {
			layers.clear();
			output = Output::BC1;

			lua_State* L = luaL_newstate();
			luaL_openlibs(L);

			lua_pushlightuserdata(L, this);
			lua_setfield(L, LUA_GLOBALSINDEX, "this");
		
			#define DEFINE_LUA_FUNC(func) \
				lua_pushcfunction(L, TextureComposite::LUA_##func); \
				lua_setfield(L, LUA_GLOBALSINDEX, #func); 

			DEFINE_LUA_FUNC(layer)
			DEFINE_LUA_FUNC(output)
		
			#undef DEFINE_LUA_FUNC

			bool success = LuaWrapper::execute(L, Span((const char*)data.begin(), (const char*)data.end()), src_path, 0);
			lua_close(L);
			return success;
		}

		Array<Layer> layers;
		Output output = Output::BC1;
	};

	bool createComposite(const Array<u8>& src_data, OutputMemoryStream& dst, const Meta& meta, const char* src_path) {
		IAllocator& allocator = m_app.getAllocator();
		TextureComposite tc(allocator);
		if (!tc.init(Span(src_data.begin(), src_data.end()), src_path)) return false;

		HashMap<u32, stbi_uc*> sources(allocator);
		FileSystem& fs = m_app.getEngine().getFileSystem();
		Array<u8> tmp_src(allocator);
		int w = -1, h = -1;

		auto prepare_source =[&](const TextureComposite::ChannelSource& ch){
			if (!ch.path.isValid()) return false;
			if (sources.find(ch.path.getHash()).isValid()) return true;

			tmp_src.clear();
			if (!fs.getContentSync(ch.path, Ref(tmp_src))) {
				return false;
			}

			int cmp;
			stbi_uc* data = stbi_load_from_memory(tmp_src.begin(), tmp_src.byte_size(), &w, &h, &cmp, 4);
			if (!data) {
				return false;
			}

			sources.insert(ch.path.getHash(), data);
			return true;
		};

		bool success = true;
		for (TextureComposite::Layer& layer : tc.layers) {
			success = success && prepare_source(layer.red);
			success = success && prepare_source(layer.green);
			success = success && prepare_source(layer.blue);
			success = success && prepare_source(layer.alpha);
		}

		if (!success) return false;

		if (tc.layers.empty()) {
			return true;
		}

		nvtt::InputOptions input;
		input.setMipmapGeneration(true);
		input.setAlphaCoverageMipScale(meta.scale_coverage, 4);
		input.setAlphaMode(nvtt::AlphaMode_Transparency);
		input.setNormalMap(meta.is_normalmap);
		input.setTextureLayout(nvtt::TextureType_Array, w, h, 1, tc.layers.size());
		
		Array<u8> out_data(allocator);
		out_data.resize(w * h * 4);
		for (TextureComposite::Layer& layer : tc.layers) {
			const u32 idx = u32(&layer - tc.layers.begin());
			for (u32 ch = 0; ch < 4; ++ch) {
				const Path& p = layer.getChannel(ch).path;
				if (!p.isValid()) continue;
				stbi_uc* from = sources[p.getHash()];
				if (!from) continue;
				u32 from_ch = layer.getChannel(ch).src_channel;
				if (from_ch == 0) from_ch = 2;
				else if (from_ch == 2) from_ch = 0;

				for (u32 j = 0; j < (u32)h; ++j) {
					for (u32 i = 0; i < (u32)w; ++i) {
						out_data[(j * w + i) * 4 + ch] = from[(i + j * w) * 4 + from_ch];
					}
				}
			}

			input.setMipmapData(out_data.begin(), w, h, 1, idx);
		}

		for (stbi_uc* i : sources) {
			free(i);
		}

		nvtt::OutputOptions output;
		output.setSrgbFlag(meta.srgb);
		output.setContainer(nvtt::Container_DDS10);
		struct : nvtt::OutputHandler {
			bool writeData(const void * data, int size) override { return dst->write(data, size); }
			void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
			void endImage() override {}

			OutputMemoryStream* dst;
		} output_handler;
		output_handler.dst = &dst;
		output.setOutputHandler(&output_handler);

		nvtt::CompressionOptions compression;
		// TODO format
		compression.setFormat(nvtt::Format_BC3);
		compression.setQuality(nvtt::Quality_Fastest);

		dst.write("dds", 3);
		u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
		flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
		flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
		flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
		flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
		dst.write(&flags, sizeof(flags));

		nvtt::Context context;
		if (!context.process(input, compression, output)) {
			return false;
		}
		return true;
	}

	bool compileImage(const Path& path, const Array<u8>& src_data, OutputMemoryStream& dst, const Meta& meta)
	{
		PROFILE_FUNCTION();
		int w, h, comps;
		const bool is_16_bit = stbi_is_16_bit_from_memory(src_data.begin(), src_data.byte_size());
		if (is_16_bit) {
			logError("Renderer") << path << ": 16bit images not yet supported.";
		}

		stbi_uc* data = stbi_load_from_memory(src_data.begin(), src_data.byte_size(), &w, &h, &comps, 4);
		if (!data) return false;

		if(meta.convert_to_raw) {
			dst.write("raw", 3);
			u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
			flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
			flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
			flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
			flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
			dst.write(&flags, sizeof(flags));
			for (int j = 0; j < h; ++j) {
				for (int i = 0; i < w; ++i) {
					const u16 tmp = (u16)data[(i + j * w) * 4] * 256;
					dst.write(tmp);
				}
			}
			return true;
		}

		for (u32 i = 0; i < u32(w * h); ++i) {
			swap(data[i * 4 + 0], data[i * 4 + 2]);
		}

		dst.write("dds", 3);
		u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
		flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
		flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
		flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
		flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
		dst.write(&flags, sizeof(flags));

		nvtt::Context context;
		
		const bool has_alpha = comps == 4;
		nvtt::InputOptions input;
		input.setMipmapGeneration(true);
		input.setAlphaCoverageMipScale(meta.scale_coverage, comps == 4 ? 3 : 0);
		input.setAlphaMode(has_alpha ? nvtt::AlphaMode_Transparency : nvtt::AlphaMode_None);
		input.setNormalMap(meta.is_normalmap);
		input.setTextureLayout(nvtt::TextureType_2D, w, h);
		input.setMipmapData(data, w, h);
		stbi_image_free(data);
		
		nvtt::OutputOptions output;
		output.setSrgbFlag(meta.srgb);
		struct : nvtt::OutputHandler {
			bool writeData(const void * data, int size) override { return dst->write(data, size); }
			void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
			void endImage() override {}

			OutputMemoryStream* dst;
		} output_handler;
		output_handler.dst = &dst;
		output.setOutputHandler(&output_handler);

		nvtt::CompressionOptions compression;
		compression.setFormat(meta.is_normalmap ? nvtt::Format_DXT5n : (has_alpha ? nvtt::Format_DXT5 :  nvtt::Format_DXT1));
		compression.setQuality(nvtt::Quality_Normal);

		if (!context.process(input, compression, output)) {
			return false;
		}
		return true;
	}


	Meta getMeta(const Path& path) const
	{
		Meta meta;
		m_app.getAssetCompiler().getMeta(path, [&meta](lua_State* L){
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "srgb", &meta.srgb);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "convert_to_raw", &meta.convert_to_raw);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "mip_scale_coverage", &meta.scale_coverage);
			LuaWrapper::getOptionalField(L, LUA_GLOBALSINDEX, "normalmap", &meta.is_normalmap);
			char tmp[32];
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "filter", Span(tmp))) {
				if (stricmp(tmp, "point") == 0) {
					meta.filter = Meta::Filter::POINT;
				}
				else {
					meta.filter = Meta::Filter::LINEAR;
				}
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_u", Span(tmp))) {
				meta.wrap_mode_u = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_v", Span(tmp))) {
				meta.wrap_mode_v = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
			if(LuaWrapper::getOptionalStringField(L, LUA_GLOBALSINDEX, "wrap_mode_w", Span(tmp))) {
				meta.wrap_mode_w = stricmp(tmp, "repeat") == 0 ? Meta::WrapMode::REPEAT : Meta::WrapMode::CLAMP;
			}
		});
		return meta;
	}

	bool compile(const Path& src) override
	{
		char ext[4] = {};
		PathUtils::getExtension(Span(ext), Span(src.c_str(), src.length()));

		FileSystem& fs = m_app.getEngine().getFileSystem();
		Array<u8> src_data(m_app.getAllocator());
		if (!fs.getContentSync(src, Ref(src_data))) return false;
		
		OutputMemoryStream out(m_app.getAllocator());
		Meta meta = getMeta(src);
		if (equalStrings(ext, "dds") || equalStrings(ext, "raw") || equalStrings(ext, "tga")) {
			if (meta.scale_coverage < 0 || !equalStrings(ext, "tga")) {
				out.write(ext, sizeof(ext) - 1);
				u32 flags = meta.srgb ? (u32)Texture::Flags::SRGB : 0;
				flags |= meta.wrap_mode_u == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_U : 0;
				flags |= meta.wrap_mode_v == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_V : 0;
				flags |= meta.wrap_mode_w == Meta::WrapMode::CLAMP ? (u32)Texture::Flags::CLAMP_W : 0;
				flags |= meta.filter == Meta::Filter::POINT ? (u32)Texture::Flags::POINT : 0;
				out.write(flags);
				out.write(src_data.begin(), src_data.byte_size());
			}
			else {
				compileImage(src, src_data, out, meta);
			}
		}
		else if(equalStrings(ext, "jpg") || equalStrings(ext, "png")) {
			compileImage(src, src_data, out, meta);
		}
		else if (equalStrings(ext, "ltc")) {
			if (!createComposite(src_data, out, meta, src.c_str())) {
				return false;
			}
		}
		else {
			ASSERT(false);
		}

		return m_app.getAssetCompiler().writeCompiledResource(src.c_str(), Span((u8*)out.getData(), (i32)out.getPos()));
	}

	const char* toString(Meta::Filter filter) {
		switch (filter) {
			case Meta::Filter::POINT: return "point";
			case Meta::Filter::LINEAR: return "linear";
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

	void compositeGUI(Texture& texture) {
		FileSystem& fs = m_app.getEngine().getFileSystem();
		if (m_composite_tag != &texture) {
			m_composite_tag = &texture;
			IAllocator& allocator = m_app.getAllocator();
			Array<u8> content(allocator);
			fs.getContentSync(texture.getPath(), Ref(content));
			m_composite.init(Span(content.begin(), content.end()), texture.getPath().c_str());
		}

		if (ImGui::CollapsingHeader("Edit")) {
			static bool show_channels = false;
			bool same_channels = true;
			for (TextureComposite::Layer& layer : m_composite.layers) {
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
			for (TextureComposite::Layer& layer : m_composite.layers) {
				const u32 idx = u32(&layer - m_composite.layers.begin());
				if (ImGui::TreeNodeEx(&layer, 0, "%d", idx)) {
					if (ImGui::Button("Remove")) {
						m_composite.layers.erase(idx);
						ImGui::TreePop();
						break;
					}

					char tmp[MAX_PATH_LENGTH];
					
					if (show_channels) {
						copyString(Span(tmp), layer.red.path.c_str());
						if (m_app.getAssetBrowser().resourceInput("Red", "r", Span(tmp), Texture::TYPE)) {
							layer.red.path = tmp;
						}
						ImGui::Combo("Red source channel", (int*)&layer.red.src_channel, "Red\0Green\0Blue\0Alpha\0");

						copyString(Span(tmp), layer.green.path.c_str());
						if (m_app.getAssetBrowser().resourceInput("Green", "g", Span(tmp), Texture::TYPE)) {
							layer.green.path = tmp;
						}
						ImGui::Combo("Green source channel", (int*)&layer.green.src_channel, "Red\0Green\0Blue\0Alpha\0");

						copyString(Span(tmp), layer.blue.path.c_str());
						if (m_app.getAssetBrowser().resourceInput("Blue", "b", Span(tmp), Texture::TYPE)) {
							layer.blue.path = tmp;
						}
						ImGui::Combo("Blue source channel", (int*)&layer.blue.src_channel, "Red\0Green\0Blue\0Alpha\0");
				
						copyString(Span(tmp), layer.alpha.path.c_str());
						if (m_app.getAssetBrowser().resourceInput("Alpha", "a", Span(tmp), Texture::TYPE)) {
							layer.alpha.path = tmp;
						}
						ImGui::Combo("Alpha source channel", (int*)&layer.alpha.src_channel, "Red\0Green\0Blue\0Alpha\0");
					}
					else {
						copyString(Span(tmp), layer.red.path.c_str());
						if (m_app.getAssetBrowser().resourceInput("Source", "rgba", Span(tmp), Texture::TYPE)) {
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
			if (ImGui::Button("Save")) {
				const StaticString<MAX_PATH_LENGTH> out_path(fs.getBasePath(), "/", texture.getPath().c_str());
				OS::OutputFile file;
				if (file.open(out_path)) {
					for (TextureComposite::Layer& layer : m_composite.layers) {
						file << "layer {\n";
						file << "\tred = { \"" << layer.red.path.c_str() << "\", " << layer.red.src_channel << " },\n";
						file << "\tgreen = { \"" << layer.green.path.c_str() << "\", " << layer.green.src_channel << " },\n";
						file << "\tblue = { \"" << layer.blue.path.c_str() << "\", " << layer.blue.src_channel << " },\n";
						file << "\talpha = { \"" << layer.alpha.path.c_str() << "\", " << layer.alpha.src_channel << " },\n";
						file << "}\n";
					}
					file.close();
				}
				else {
					logError("Renderer") << "Failed to create " << out_path;
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("Open in external editor")) {
				m_app.getAssetBrowser().openInExternalEditor(texture.getPath().c_str());
			}
		}
	}

	void onGUI(Span<Resource*> resources) override
	{
		if(resources.length() > 1) return;

		auto* texture = static_cast<Texture*>(resources[0]);

		ImGui::LabelText("Size", "%dx%d", texture->width, texture->height);
		ImGui::LabelText("Mips", "%d", texture->mips);
		const char* format = "unknown";
		switch(texture->format) {
			case gpu::TextureFormat::R8: format = "R8"; break;
			case gpu::TextureFormat::RGBA8: format = "RGBA8"; break;
			case gpu::TextureFormat::RGBA16: format = "RGBA16"; break;
			case gpu::TextureFormat::RGBA16F: format = "RGBA16F"; break;
			case gpu::TextureFormat::RGBA32F: format = "RGBA32F"; break;
			case gpu::TextureFormat::R16F: format = "R16F"; break;
			case gpu::TextureFormat::R16: format = "R16"; break;
			case gpu::TextureFormat::R32F: format = "R32F"; break;
			case gpu::TextureFormat::SRGB: format = "SRGB"; break;
			case gpu::TextureFormat::SRGBA: format = "SRGBA"; break;
			default: ASSERT(false); break;
		}
		ImGui::LabelText("Format", "%s", format);
		if (texture->handle.isValid()) {
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
					if (!p->m_texture_view.isValid()) {
						p->m_texture_view = gpu::allocTextureHandle();
					}
					gpu::createTextureView(p->m_texture_view, p->m_texture->handle);
				});
			}

			ImGui::Image((void*)(uintptr_t)m_texture_view.value, texture_size);

			if (ImGui::Button("Open")) m_app.getAssetBrowser().openInExternalEditor(texture);
		}

		if (PathUtils::hasExtension(texture->getPath().c_str(), "ltc")) compositeGUI(*texture);
		if (ImGui::CollapsingHeader("Import")) {
			AssetCompiler& compiler = m_app.getAssetCompiler();
			
			if(texture->getPath().getHash() != m_meta_res) {
				m_meta = getMeta(texture->getPath());
				m_meta_res = texture->getPath().getHash();
			}
			
			ImGui::Checkbox("SRGB", &m_meta.srgb);
			ImGui::Checkbox("Convert to RAW", &m_meta.convert_to_raw);
			bool scale_coverage = m_meta.scale_coverage >= 0;
			if (ImGui::Checkbox("Mipmap scale coverage", &scale_coverage)) {
				m_meta.scale_coverage *= -1;
			}
			if (m_meta.scale_coverage >= 0) {
				ImGui::SliderFloat("Coverage alpha ref", &m_meta.scale_coverage, 0, 1);
			}
			ImGui::Checkbox("Is normalmap", &m_meta.is_normalmap);
			ImGui::Combo("U Wrap mode", (int*)&m_meta.wrap_mode_u, "Repeat\0Clamp\0");
			ImGui::Combo("V Wrap mode", (int*)&m_meta.wrap_mode_v, "Repeat\0Clamp\0");
			ImGui::Combo("W Wrap mode", (int*)&m_meta.wrap_mode_w, "Repeat\0Clamp\0");
			ImGui::Combo("Filter", (int*)&m_meta.filter, "Linear\0Point\0");

			if (ImGui::Button("Apply")) {
				const StaticString<512> src("srgb = ", m_meta.srgb ? "true" : "false"
					, "\nconvert_to_raw = ", m_meta.convert_to_raw ? "true" : "false"
					, "\nmip_scale_coverage = ", m_meta.scale_coverage
					, "\nnormalmap = ", m_meta.is_normalmap ? "true" : "false"
					, "\nwrap_mode_u = \"", toString(m_meta.wrap_mode_u), "\""
					, "\nwrap_mode_v = \"", toString(m_meta.wrap_mode_v), "\""
					, "\nwrap_mode_w = \"", toString(m_meta.wrap_mode_w), "\""
					, "\nfilter = \"", toString(m_meta.filter), "\""
				);
				compiler.updateMeta(texture->getPath(), src);
				if (compiler.compile(texture->getPath())) {
					texture->getResourceManager().reload(*texture);
				}
			}
		}
	}


	void onResourceUnloaded(Resource* resource) override {}
	const char* getName() const override { return "Texture"; }
	ResourceType getResourceType() const override { return Texture::TYPE; }


	StudioApp& m_app;
	Texture* m_texture;
	gpu::TextureHandle m_texture_view = gpu::INVALID_TEXTURE;
	JobSystem::SignalHandle m_tile_signal = JobSystem::INVALID_HANDLE;
	Meta m_meta;
	u32 m_meta_res = 0;
	TextureComposite m_composite;
	void* m_composite_tag = nullptr;
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

		OS::InputFile file;
		if (!file.open(path[0] == '/' ? path + 1 : path)) return;
		
		IAllocator& allocator = m_app.getAllocator();
		Array<u8> content(allocator);
		content.resize((int)file.size());
		file.read(content.begin(), content.byte_size());
		file.close();

		struct Context {
			const char* path;
			ShaderPlugin* plugin;
			u8* content;
			u32 content_len;
			int idx;
		} ctx = { path, this, content.begin(), content.byte_size(), 0 };

		lua_pushlightuserdata(L, &ctx);
		lua_setfield(L, LUA_GLOBALSINDEX, "this");

		auto include = [](lua_State* L) -> int {
			lua_getfield(L, LUA_GLOBALSINDEX, "this");
			Context* that = LuaWrapper::toType<Context*>(L, -1);
			lua_pop(L, 1);
			const char* path = LuaWrapper::checkArg<const char*>(L, 1);
			that->plugin->m_app.getAssetCompiler().registerDependency(Path(that->path), Path(path));
			return 0;
		};

		lua_pushcclosure(L, include, 0);
		lua_setfield(L, LUA_GLOBALSINDEX, "include");

		static const char* preface = 
			"local new_g = setmetatable({include = include}, {__index = function() return function() end end })\n"
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
			logError("Engine") << path << ": " << lua_tostring(L, -1);
			lua_pop(L, 2);
			lua_close(L);
			return;
		}

		if (lua_pcall(L, 0, 0, -2) != 0) {
			logError("Engine") << lua_tostring(L, -1);
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
		char basename[MAX_PATH_LENGTH];
		PathUtils::getBasename(Span(basename), shader->getPath().c_str());
		if (ImGui::Button("Open in external editor"))
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
	, Pipeline& pipeline
	, const u32 texture_size
	, const DVec3& position
	, Ref<Array<Vec4>> data
	, F&& f) {
	MT::memoryBarrier();

	WorldEditor& world_editor = app.getWorldEditor();
	Engine& engine = app.getEngine();
	auto& plugin_manager = engine.getPluginManager();

	Viewport viewport;
	viewport.is_ortho = false;
	viewport.fov = degreesToRadians(90.f);
	viewport.near = 0.1f;
	viewport.far = 10'000;
	viewport.w = texture_size;
	viewport.h = texture_size;

	Universe* universe = world_editor.getUniverse();
	pipeline.setUniverse(universe);
	pipeline.setViewport(viewport);

	Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
	Vec3 dirs[] = {{-1, 0, 0}, {1, 0, 0}, {0, -1, 0}, {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};
	Vec3 ups[] = {{0, 1, 0}, {0, 1, 0}, {0, 0, 1}, {0, 0, -1}, {0, 1, 0}, {0, 1, 0}};
	Vec3 ups_opengl[] = { { 0, -1, 0 },{ 0, -1, 0 },{ 0, 0, 1 },{ 0, 0, -1 },{ 0, -1, 0 },{ 0, -1, 0 } };

	data->resize(6 * texture_size * texture_size);

	const bool ndc_bottom_left = gpu::isOriginBottomLeft();
	for (int i = 0; i < 6; ++i) {
		Vec3 side = crossProduct(ndc_bottom_left ? ups_opengl[i] : ups[i], dirs[i]);
		Matrix mtx = Matrix::IDENTITY;
		mtx.setZVector(dirs[i]);
		mtx.setYVector(ndc_bottom_left ? ups_opengl[i] : ups[i]);
		mtx.setXVector(side);
		viewport.pos = position;
		viewport.rot = mtx.getRotation();
		pipeline.setViewport(viewport);
		pipeline.render(false);

		const gpu::TextureHandle res = pipeline.getOutput();
		ASSERT(res.isValid());
		renderer->getTextureImage(res
			, texture_size
			, texture_size
			, gpu::TextureFormat::RGBA32F
			, Span((u8*)(data->begin() + (i * texture_size * texture_size)), u32(texture_size * texture_size * sizeof(*data->begin())))
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

	RenderJob* rjob = LUMIX_NEW(renderer->getAllocator(), RenderJob)(static_cast<F&&>(f));
	renderer->queue(rjob, 0);
}

struct LightProbeGridPlugin final : public PropertyGrid::IPlugin {
	struct Job {
		Job(LightProbeGridPlugin& plugin, u32 idx, IAllocator& allocator) 
			: plugin(plugin)
			, data(allocator)
			, index(idx)
		{}

		u32 index;
		LightProbeGridPlugin& plugin;
		Array<Vec4> data;
		bool done = false;
	};
	
	LightProbeGridPlugin(StudioApp& app)
		: m_app(app)
		, m_jobs(app.getAllocator())
		, m_result(app.getAllocator())
	{
		Engine& engine = app.getEngine();
		PluginManager& plugin_manager = engine.getPluginManager();
		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		IAllocator& allocator = app.getAllocator();
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PROBE", allocator);
	}
	
	~LightProbeGridPlugin() {
		Pipeline::destroy(m_pipeline);
	}

	void onGUI(PropertyGrid& grid, ComponentUID cmp) override {
		if (cmp.type != LIGHT_PROBE_GRID_TYPE) return;

		if (ImGui::CollapsingHeader("Generator")) {
			if (ImGui::Button("Generate")) generate(cmp, false);
			if (ImGui::Button("Add bounce")) generate(cmp, true);
		}
	}

	void generate(ComponentUID cmp, bool bounce) {
		m_bounce = bounce;
		RenderScene* scene = (RenderScene*)cmp.scene;
		EntityRef e = (EntityRef)cmp.entity;
		Universe& universe = scene->getUniverse();
		m_grid = scene->getLightProbeGrid(e);
		m_to_dispatch = m_grid.resolution.x * m_grid.resolution.y * m_grid.resolution.z;
		m_total = m_to_dispatch;
		m_position = universe.getPosition(e);
		m_result.resize(m_total);
	}

	void update() override {
		IAllocator& allocator = m_app.getAllocator();
		if (m_to_dispatch > 0) {
			--m_to_dispatch;
			Job* job = LUMIX_NEW(allocator, Job)(*this, m_to_dispatch, allocator);

			m_pipeline->define("PROBE_BOUNCE", m_bounce);
			const Vec3 cell_size = 2.f * m_grid.half_extents / m_grid.resolution;
			const DVec3 origin = m_position - m_grid.half_extents + cell_size * 0.5f;
			const IVec3 loc(m_to_dispatch % m_grid.resolution.x
				, (m_to_dispatch / m_grid.resolution.x) % m_grid.resolution.y
				, m_to_dispatch / (m_grid.resolution.x * m_grid.resolution.y));
			const DVec3 pos = origin + cell_size * loc;
			captureCubemap(m_app, *m_pipeline, 32, pos, Ref(job->data), [job](){
				JobSystem::run(job, [](void* ptr) {
					Job* pjob = (Job*)ptr;

					const bool ndc_bottom_left = gpu::isOriginBottomLeft();
					if (!ndc_bottom_left) {
						const u32 texture_size = (u32)sqrtf(pjob->data.size() / 6.f);
						for (int i = 0; i < 6; ++i) {
							Vec4* tmp = &pjob->data[i * texture_size * texture_size];
							if (i == 2 || i == 3) {
								flipY(tmp, texture_size);
							}
							else {
								flipX(tmp, texture_size);
							}
						}
					}

					pjob->plugin.m_result[pjob->index].compute(pjob->data);
					MT::memoryBarrier();
					pjob->done = true;
				}, nullptr);

			});
			m_jobs.push(job);
		}

		if (m_total > 0) {
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
			if (ImGui::Begin("Light probe grid generation", nullptr, flags)) {
				ImGui::Text("%s", "Generating light probe grid...");
				ImGui::Text("%s", "Manipulating with entities at this time can produce incorrect probes.");
				ImGui::ProgressBar(((float)m_total - m_to_dispatch) / m_total, ImVec2(-1, 0), StaticString<64>(m_total - m_to_dispatch, " / ", m_total));
			}
			ImGui::End();
			ImGui::PopStyleVar();
		}

		for (int i = m_jobs.size() - 1; i >= 0; --i) {
			if (m_jobs[i]->done) {
				LUMIX_DELETE(allocator, m_jobs[i]);
				m_jobs.swapAndPop(i);
			}
		}

		if (m_total > 0) {
			if (m_to_dispatch == 0 && m_jobs.empty()) {
				Universe* universe = m_app.getWorldEditor().getUniverse();
				const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
				StaticString<MAX_PATH_LENGTH> dir(base_path, "universes/", universe->getName(), "/probes/");
				if (!OS::makePath(dir) && !OS::dirExists(dir)) {
					logError("Editor") << "Failed to create " << dir;
				}

				for (u32 sh_idx = 0; sh_idx < 7; ++sh_idx) {
					StaticString<MAX_PATH_LENGTH> path(dir, m_grid.guid, "_grid", sh_idx, ".raw");
					StaticString<MAX_PATH_LENGTH> meta_path(dir, m_grid.guid, "_grid", sh_idx, ".raw.meta");
					
					OS::OutputFile meta_file;
					if (!meta_file.open(meta_path)) {
						logError("Editor") << "Failed to create " << path;
					}
					else {
						meta_file << "wrap_mode_u = \"clamp\"\n";
						meta_file << "wrap_mode_v = \"clamp\"\n";
						meta_file << "wrap_mode_w = \"clamp\"\n";
					}
					meta_file.close();

					OS::OutputFile file;
					if (!file.open(path)) {
						logError("Editor") << "Failed to create " << path;
						return;
					}
					RawTextureHeader header;
					header.is_array = false;
					header.width = m_grid.resolution.x;
					header.height = m_grid.resolution.y;
					header.depth = m_grid.resolution.z;
					header.channels_count = 4;
					header.channel_type = RawTextureHeader::ChannelType::FLOAT;
					file.write(&header, sizeof(header));
					const u32 c0 = (sh_idx * 4 + 0) / 3;
					const u32 c1 = (sh_idx * 4 + 1) / 3;
					const u32 c2 = (sh_idx * 4 + 2) / 3;
					const u32 c3 = (sh_idx * 4 + 3) / 3;
					const u32 c00 = (sh_idx * 4 + 0) % 3;
					const u32 c10 = (sh_idx * 4 + 1) % 3;
					const u32 c20 = (sh_idx * 4 + 2) % 3;
					const u32 c30 = (sh_idx * 4 + 3) % 3;
					if (sh_idx == 6) {
						for (u32 i = 0; i < m_total; ++i) {
							Vec4 v;
							v.x = (&m_result[i].coefs[c0].x)[c00];
							v.y = (&m_result[i].coefs[c1].x)[c10];
							v.z = (&m_result[i].coefs[c2].x)[c20];
							v.w = 0;
							file.write(&v, sizeof(v));
						}
					}
					else {
						for (u32 i = 0; i < m_total; ++i) {
							Vec4 v;
							v.x = (&m_result[i].coefs[c0].x)[c00];
							v.y = (&m_result[i].coefs[c1].x)[c10];
							v.z = (&m_result[i].coefs[c2].x)[c20];
							v.w = (&m_result[i].coefs[c3].x)[c30];
							file.write(&v, sizeof(v));
						}
					}
					file.close();
				}

				m_total = 0;
				m_to_dispatch = 0;
			}
		}
	}

	Pipeline* m_pipeline;
	StudioApp& m_app;
	bool m_bounce;
	LightProbeGrid m_grid;
	Array<SphericalHarmonics> m_result;
	u32 m_to_dispatch = 0;
	u32 m_total = 0;
	DVec3 m_position;
	Array<Job*> m_jobs;
};

struct EnvironmentProbePlugin final : public PropertyGrid::IPlugin
{
	explicit EnvironmentProbePlugin(StudioApp& app)
		: m_app(app)
		, m_probes(app.getAllocator())
	{
		Engine& engine = app.getEngine();
		PluginManager& plugin_manager = engine.getPluginManager();
		Renderer* renderer = static_cast<Renderer*>(plugin_manager.getPlugin("renderer"));
		IAllocator& allocator = app.getAllocator();
		PipelineResource* pres = engine.getResourceManager().load<PipelineResource>(Path("pipelines/main.pln"));
		m_pipeline = Pipeline::create(*renderer, pres, "PROBE", allocator);
	}


	~EnvironmentProbePlugin()
	{
		Pipeline::destroy(m_pipeline);
	}


	bool saveCubemap(u64 probe_guid, const u8* data, int texture_size, const char* postfix, nvtt::Format format)
	{
		ASSERT(data);
		const char* base_path = m_app.getEngine().getFileSystem().getBasePath();
		StaticString<MAX_PATH_LENGTH> path(base_path, "universes/", m_app.getWorldEditor().getUniverse()->getName());
		if (!OS::makePath(path) && !OS::dirExists(path)) {
			logError("Editor") << "Failed to create " << path;
		}
		path << "/probes_tmp/";
		if (!OS::makePath(path) && !OS::dirExists(path)) {
			logError("Editor") << "Failed to create " << path;
		}
		path << probe_guid << postfix << ".dds";
		OS::OutputFile file;
		if (!file.open(path)) {
			logError("Editor") << "Failed to create " << path;
			return false;
		}

		nvtt::Context context;
		
		nvtt::InputOptions input;
		input.setMipmapGeneration(true);
		input.setAlphaMode(nvtt::AlphaMode_None);
		input.setNormalMap(false);
		input.setTextureLayout(nvtt::TextureType_Cube, texture_size, texture_size);
		for (int i = 0; i < 6; ++i) {
			const int step = texture_size * texture_size * 4;
			input.setMipmapData(data + step * i, texture_size, texture_size, 1, i);
		}
		
		nvtt::OutputOptions output;
		output.setSrgbFlag(false);
		struct : nvtt::OutputHandler {
			bool writeData(const void * data, int size) override { return dst->write(data, size); }
			void beginImage(int size, int width, int height, int depth, int face, int miplevel) override {}
			void endImage() override {}

			OS::OutputFile* dst;
		} output_handler;
		output_handler.dst = &file;
		output.setOutputHandler(&output_handler);

		nvtt::CompressionOptions compression;
		compression.setFormat(format);
		compression.setQuality(nvtt::Quality_Fastest);

		if (!context.process(input, compression, output)) {
			file.close();
			return false;
		}
		file.close();
		return true;
	}


	void generateCubemaps(bool bounce, bool fast_filter) {
		ASSERT(m_probes.empty());

		Universe* universe = m_app.getWorldEditor().getUniverse();
		if (universe->getName()[0] == '\0') {
			logError("Editor") << "Universe must be saved before environment probe can be generated.";
			return;
		}
		
		m_pipeline->define("PROBE_BOUNCE", bounce);

		auto* scene = static_cast<RenderScene*>(universe->getScene(ENVIRONMENT_PROBE_TYPE));
		const Span<EntityRef> probes = scene->getAllEnvironmentProbes();
		m_probes.reserve(probes.length());
		IAllocator& allocator = m_app.getAllocator();
		for (EntityRef p : probes) {
			ProbeJob* job = LUMIX_NEW(m_app.getAllocator(), ProbeJob)(*this, p, allocator);
			
			const EntityPtr env_entity = scene->getActiveEnvironment();
			job->probe = scene->getEnvironmentProbe(p);
			job->position = universe->getPosition(p);
			job->universe_name = universe->getName();
			if (env_entity.isValid()) {
				job->fast_filter = fast_filter;
			}

			m_probes.push(job);
		}
		m_probe_counter += m_probes.size();
	}

	struct ProbeJob {
		ProbeJob(EnvironmentProbePlugin& plugin, EntityRef& entity, IAllocator& allocator) 
			: entity(entity)
			, data(allocator)
			, plugin(plugin)
		{}
		
		StaticString<MAX_PATH_LENGTH> universe_name;
		EntityRef entity;
		EnvironmentProbe probe;
		EnvironmentProbePlugin& plugin;
		DVec3 position;
		bool fast_filter = false;

		Array<Vec4> data;
		SphericalHarmonics sh;
		bool render_dispatched = false;
		bool done = false;
		bool done_counted = false;
	};

	void render(ProbeJob& job) {
		bool diffuse_only = job.probe.flags.isSet(EnvironmentProbe::DIFFUSE);
		diffuse_only = diffuse_only && !job.probe.flags.isSet(EnvironmentProbe::SPECULAR);
		diffuse_only = diffuse_only && !job.probe.flags.isSet(EnvironmentProbe::REFLECTION);
		const u32 texture_size = diffuse_only ? 32 : 1024;

		captureCubemap(m_app, *m_pipeline, texture_size, job.position, Ref(job.data), [&job](){
			JobSystem::run(&job, [](void* ptr) {
				ProbeJob* pjob = (ProbeJob*)ptr;
				pjob->plugin.processData(*pjob);
			}, nullptr);

		});
	}


	void update() override
	{
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

		MT::memoryBarrier();
		for (ProbeJob* j : m_probes) {
			if (j->done && !j->done_counted) {
				j->done_counted = true;
				++m_done_counter;
			}
		}

		if (m_done_counter == m_probe_counter) {
			while (!m_probes.empty()) {
				ProbeJob& job = *m_probes.back();
				m_probes.pop();
				ASSERT(job.done);
				ASSERT(job.done_counted);

				auto move = [job](u64 guid, const char* postfix){
					const StaticString<MAX_PATH_LENGTH> tmp_path("universes/", job.universe_name, "/probes_tmp/", guid, postfix, ".dds");
					const StaticString<MAX_PATH_LENGTH> path("universes/", job.universe_name, "/probes/", guid, postfix, ".dds");
					if (!OS::fileExists(tmp_path)) return;
					if (!OS::moveFile(tmp_path, path)) {
						logError("Editor") << "Failed to move file " << tmp_path;
					}
				};

				move(job.probe.guid, "");
				move(job.probe.guid, "_radiance");

				Universe* universe = m_app.getWorldEditor().getUniverse();
				if (universe->hasComponent(job.entity, ENVIRONMENT_PROBE_TYPE)) {
					RenderScene* scene = (RenderScene*)universe->getScene(ENVIRONMENT_PROBE_TYPE);
					EnvironmentProbe& p = scene->getEnvironmentProbe(job.entity);
					static_assert(sizeof(p.sh_coefs == job.sh.coefs));
					memcpy(p.sh_coefs, job.sh.coefs, sizeof(p.sh_coefs));
				}

				IAllocator& allocator = m_app.getAllocator();
				LUMIX_DELETE(allocator, &job);
			}
		}
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

		if (job.probe.flags.isSet(EnvironmentProbe::DIFFUSE)) {
			job.sh.compute(data);
		}

		// TODO do not override mipmaps if slow filter is used
		if (job.probe.flags.isSet(EnvironmentProbe::SPECULAR)) {
			cmft::Image image;	
			cmft::imageCreate(image, texture_size, texture_size, 0x303030ff, 1, 6, cmft::TextureFormat::RGBA32F);
			memcpy(image.m_data, data.begin(), data.byte_size());
			if (job.fast_filter) {
				cmft::imageResize(image, 128, 128);
			}
			else {
				PROFILE_BLOCK("radiance");
				cmft::imageRadianceFilter(
					image
					, 128
					, cmft::LightingModel::BlinnBrdf
					, false
					, 1
					, 10
					, 1
					, cmft::EdgeFixup::None
					, MT::getCPUsCount()
				);
			}
			cmft::imageFromRgba32f(image, cmft::TextureFormat::RGBA8);
			saveCubemap(job.probe.guid, (u8*)image.m_data, 128, "_radiance", nvtt::Format_DXT1);
		}

		// TODO save reflection, careful, job.data is float
		/*if (job.probe.flags.isSet(EnvironmentProbe::REFLECTION)) {
			for (int i = 3; i < job.data.size(); i += 4) job.data[i] = 0xff; 
			saveCubemap(job.probe.guid, &job.data[0], texture_size, "", nvtt::Format_DXT1);
		}*/

		MT::memoryBarrier();
		job.done = true;
	}


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override {
		if (cmp.type != ENVIRONMENT_PROBE_TYPE) return;

		const EntityRef e = (EntityRef)cmp.entity;
		auto* scene = static_cast<RenderScene*>(cmp.scene);
		if (m_probe_counter) ImGui::Text("Generating...");
		else {
			const EnvironmentProbe& probe = scene->getEnvironmentProbe(e);
			if (probe.reflection && probe.flags.isSet(EnvironmentProbe::REFLECTION)) {
				ImGui::LabelText("Reflection path", "%s", probe.reflection->getPath().c_str());
				if (ImGui::Button("View reflection")) m_app.getAssetBrowser().selectResource(probe.reflection->getPath(), true, false);
			}
			if (probe.radiance && probe.flags.isSet(EnvironmentProbe::SPECULAR)) {
				ImGui::LabelText("Radiance path", "%s", probe.radiance->getPath().c_str());
				if (ImGui::Button("View radiance")) m_app.getAssetBrowser().selectResource(probe.radiance->getPath(), true, false);
			}
			if (ImGui::CollapsingHeader("Generator")) {
				ImGui::Checkbox("Fast filter", &m_fast_filter);
				if (ImGui::Button("Generate")) generateCubemaps(false, m_fast_filter);
				if (ImGui::Button("Add bounce")) generateCubemaps(true, m_fast_filter);
			}
		}
	}


	StudioApp& m_app;
	Pipeline* m_pipeline;
	
	// TODO to be used with http://casual-effects.blogspot.com/2011/08/plausible-environment-lighting-in-two.html
	Array<ProbeJob*> m_probes;
	u32 m_done_counter = 0;
	u32 m_probe_counter = 0;
	bool m_fast_filter = true;
};


struct TerrainPlugin final : public PropertyGrid::IPlugin
{
	explicit TerrainPlugin(StudioApp& app)
		: m_app(app)
	{
		WorldEditor& editor = app.getWorldEditor();
		m_terrain_editor = LUMIX_NEW(app.getAllocator(), TerrainEditor)(editor, app);
	}


	~TerrainPlugin() { LUMIX_DELETE(m_app.getAllocator(), m_terrain_editor); }


	void onGUI(PropertyGrid& grid, ComponentUID cmp) override
	{
		if (cmp.type != TERRAIN_TYPE) return;

		m_terrain_editor->setComponent(cmp);
		m_terrain_editor->onGUI();
	}


	StudioApp& m_app;
	TerrainEditor* m_terrain_editor;
};


struct RenderInterfaceImpl final : public RenderInterfaceBase
{
	RenderInterfaceImpl(WorldEditor& editor, Pipeline& pipeline, Renderer& renderer)
		: m_pipeline(pipeline)
		, m_editor(editor)
		, m_models(editor.getAllocator())
		, m_textures(editor.getAllocator())
		, m_renderer(renderer)
	{
		m_model_index = 0;
		auto& rm = m_editor.getEngine().getResourceManager();
		
		Path shader_path("pipelines/debug_shape.shd");
		m_shader = rm.load<Shader>(shader_path);
		
		Path font_path("editor/fonts/OpenSans-Regular.ttf");
		m_font_res = rm.load<FontResource>(font_path);
		m_font = m_font_res->addRef(16);

		editor.universeCreated().bind<&RenderInterfaceImpl::onUniverseCreated>(this);
		editor.universeDestroyed().bind<&RenderInterfaceImpl::onUniverseDestroyed>(this);
	}


	~RenderInterfaceImpl()
	{
		m_shader->getResourceManager().unload(*m_shader);
		m_font_res->getResourceManager().unload(*m_font_res);

		m_editor.universeCreated().unbind<&RenderInterfaceImpl::onUniverseCreated>(this);
		m_editor.universeDestroyed().unbind<&RenderInterfaceImpl::onUniverseDestroyed>(this);
	}


	void addText2D(float x, float y, u32 color, const char* text) override
	{
		if (m_font) m_pipeline.getDraw2D().addText(*m_font, {x, y}, Color(color), text);
	}


	void addRect2D(const Vec2& a, const Vec2& b, u32 color) override
	{
		m_pipeline.getDraw2D().addRect(a, b, *(Color*)&color, 1);
	}


	void addRectFilled2D(const Vec2& a, const Vec2& b, u32 color) override
	{
		m_pipeline.getDraw2D().addRectFilled(a, b, *(Color*)&color);
	}


	DVec3 getClosestVertex(Universe* universe, EntityRef entity, const DVec3& wpos) override
	{
		const Transform tr = universe->getTransform(entity);
		const Vec3 lpos = tr.rot.conjugated() * (wpos - tr.pos).toFloat();
		auto* scene = (RenderScene*)universe->getScene(MODEL_INSTANCE_TYPE);
		if (!universe->hasComponent(entity, MODEL_INSTANCE_TYPE)) return wpos;

		Model* model = scene->getModelInstanceModel(entity);

		float min_dist_squared = FLT_MAX;
		Vec3 closest_vertex = lpos;
		auto processVertex = [&](const Vec3& vertex) {
			float dist_squared = (vertex - lpos).squaredLength();
			if (dist_squared < min_dist_squared)
			{
				min_dist_squared = dist_squared;
				closest_vertex = vertex;
			}
		};

		for (int i = 0, c = model->getMeshCount(); i < c; ++i)
		{
			Mesh& mesh = model->getMesh(i);

			if (mesh.areIndices16())
			{
				const u16* indices = (const u16*)&mesh.indices[0];
				for (int i = 0, c = mesh.indices.size() >> 1; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
			else
			{
				const u32* indices = (const u32*)&mesh.indices[0];
				for (int i = 0, c = mesh.indices.size() >> 2; i < c; ++i)
				{
					Vec3 vertex = mesh.vertices[indices[i]];
					processVertex(vertex);
				}
			}
		}
		return tr.pos + tr.rot * closest_vertex;
	}


	ImFont* addFont(const char* filename, int size) override
	{
		ImGuiIO& io = ImGui::GetIO();
		ImFont* font = io.Fonts->AddFontFromFileTTF(filename, (float)size);

		Engine& engine = m_editor.getEngine();
		unsigned char* pixels;
		int width, height;

		ImGuiFreeType::BuildFontAtlas(ImGui::GetIO().Fonts);
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		Material* material = engine.getResourceManager().load<Material>(Path("pipelines/imgui/imgui.mat"));

		Texture* old_texture = material->getTexture(0);
		Texture* texture = LUMIX_NEW(engine.getAllocator(), Texture)(
			Path("font"), *engine.getResourceManager().get(Texture::TYPE), m_renderer, engine.getAllocator());

		texture->create(width, height, gpu::TextureFormat::RGBA8, pixels, width * height * 4);
		material->setTexture(0, texture);
		if (old_texture)
		{
			old_texture->destroy();
			LUMIX_DELETE(engine.getAllocator(), old_texture);
		}

		return font;
	}


	ModelHandle loadModel(Path& path) override
	{
		m_models.insert(m_model_index, m_editor.getEngine().getResourceManager().load<Model>(path));
		++m_model_index;
		return m_model_index - 1;
	}


	bool saveTexture(Engine& engine, const char* path_cstr, const void* pixels, int w, int h, bool upper_left_origin) override
	{
		Path path(path_cstr);
		OS::OutputFile file;
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
		Engine& engine = m_editor.getEngine();
		auto& rm = engine.getResourceManager();
		auto& allocator = m_editor.getAllocator();

		Texture* texture = LUMIX_NEW(allocator, Texture)(Path(name), *rm.get(Texture::TYPE), m_renderer, allocator);
		texture->create(w, h, gpu::TextureFormat::RGBA8, pixels, w * h * 4);
		m_textures.insert(&texture->handle, texture);
		return (ImTextureID)(uintptr_t)texture->handle.value;
	}


	void destroyTexture(ImTextureID handle) override
	{
		auto& allocator = m_editor.getAllocator();
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		auto* texture = iter.value();
		m_textures.erase(iter);
		texture->destroy();
		LUMIX_DELETE(allocator, texture);
	}


	bool isValid(ImTextureID texture) override
	{
		return texture && ((gpu::TextureHandle*)texture)->isValid();
	}


	ImTextureID loadTexture(const Path& path) override
	{
		auto& rm = m_editor.getEngine().getResourceManager();
		auto* texture = rm.load<Texture>(path);
		m_textures.insert(&texture->handle, texture);
		return &texture->handle;
	}


	void unloadTexture(ImTextureID handle) override
	{
		auto iter = m_textures.find(handle);
		if (iter == m_textures.end()) return;
		auto* texture = iter.value();
		texture->getResourceManager().unload(*texture);
		m_textures.erase(iter);
	}


	void addDebugCross(const DVec3& pos, float size, u32 color) override
	{
		m_render_scene->addDebugCross(pos, size, color);
	}


	WorldEditor::RayHit castRay(const DVec3& origin, const Vec3& dir, EntityPtr ignored) override
	{
		const RayCastModelHit hit = m_render_scene->castRay(origin, dir, ignored);

		return {hit.is_hit, hit.t, hit.entity, hit.origin + hit.dir * hit.t};
	}


	void addDebugLine(const DVec3& from, const DVec3& to, u32 color) override
	{
		m_render_scene->addDebugLine(from, to, color);
	}


	void addDebugCube(const DVec3& minimum, const DVec3& maximum, u32 color) override
	{
		m_render_scene->addDebugCube(minimum, maximum, color);
	}


	void addDebugCube(const DVec3& pos, const Vec3& dir, const Vec3& up, const Vec3& right, u32 color) override
	{
		m_render_scene->addDebugCube(pos, dir, right, up, color);
	}


	AABB getEntityAABB(Universe& universe, EntityRef entity, const DVec3& base) override
	{
		AABB aabb;

		if (universe.hasComponent(entity, MODEL_INSTANCE_TYPE)) {
			Model* model = m_render_scene->getModelInstanceModel(entity);
			if (!model) return aabb;

			aabb = model->getAABB();
			aabb.transform(universe.getRelativeMatrix(entity, base));

			return aabb;
		}

		Vec3 pos = (universe.getPosition(entity) - base).toFloat();
		aabb.set(pos, pos);

		return aabb;
	}


	Model* getModel(ModelHandle handle) override {
		return m_models[handle];
	}

	void unloadModel(ModelHandle handle) override
	{
		auto* model = m_models[handle];
		model->getResourceManager().unload(*model);
		m_models.erase(handle);
	}


	Vec2 getCameraScreenSize(EntityRef entity) override { return m_render_scene->getCameraScreenSize(entity); }


	float getCameraOrthoSize(EntityRef entity) override { return m_render_scene->getCamera(entity).ortho_size; }


	bool isCameraOrtho(EntityRef entity) override { return m_render_scene->getCamera(entity).is_ortho; }


	float getCameraFOV(EntityRef entity) override { return m_render_scene->getCamera(entity).fov; }


	float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Pose* pose) override
	{
		RayCastModelHit hit = m_models[model]->castRay(origin, dir, pose);
		return hit.is_hit ? hit.t : -1;
	}


	void onUniverseCreated()
	{
		m_render_scene = static_cast<RenderScene*>(m_editor.getUniverse()->getScene(MODEL_INSTANCE_TYPE));
	}


	void onUniverseDestroyed() { m_render_scene = nullptr; }


	Vec3 getModelCenter(EntityRef entity) override
	{
		if (!m_render_scene->getUniverse().hasComponent(entity, MODEL_INSTANCE_TYPE)) return Vec3::ZERO;
		Model* model = m_render_scene->getModelInstanceModel(entity);
		if (!model) return Vec3(0, 0, 0);
		return (model->getAABB().min + model->getAABB().max) * 0.5f;
	}


	Path getModelInstancePath(EntityRef entity) override { return m_render_scene->getModelInstancePath(entity); }


	ShiftedFrustum getFrustum(EntityRef camera, const Vec2& viewport_min, const Vec2& viewport_max) override
	{
		return m_render_scene->getCameraFrustum(camera, viewport_min, viewport_max);
	}


	void getRenderables(Array<EntityRef>& entities, const ShiftedFrustum& frustum) override
	{
		for (int i = 0; i < (int)RenderableTypes::COUNT; ++i) {
			CullResult* renderables = m_render_scene->getRenderables(frustum, (RenderableTypes)i);
			while (renderables) {
				for (u32 i = 0; i < renderables->header.count; ++i) {
					entities.push(renderables->entities[i]);
				}
				renderables = renderables->header.next;
			}
		}
	}


	WorldEditor& m_editor;
	Shader* m_shader;
	FontResource* m_font_res;
	Font* m_font;
	RenderScene* m_render_scene;
	Renderer& m_renderer;
	Pipeline& m_pipeline;
	HashMap<int, Model*> m_models;
	HashMap<void*, Texture*> m_textures;
	int m_model_index;
};


struct EditorUIRenderPlugin final : public StudioApp::GUIPlugin
{
	struct RenderCommand : Renderer::RenderJob
	{
		struct CmdList
		{
			CmdList(IAllocator& allocator) : commands(allocator) {}

			Renderer::MemRef idx_buffer;
			Renderer::MemRef vtx_buffer;

			Array<ImDrawCmd> commands;
		};

		struct WindowDrawData {
			WindowDrawData(IAllocator& allocator) : cmd_lists(allocator) {}

			gpu::ProgramHandle program;
			OS::WindowHandle window;
			u32 w, h;
			i32 x, y;
			bool new_program = false;
			Array<CmdList> cmd_lists;
		};

		RenderCommand(IAllocator& allocator)
			: allocator(allocator)
			, window_draw_data(allocator)
		{
		}

		gpu::ProgramHandle getProgram(void* window_handle, Ref<bool> new_program) {
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

			ImGuiPlatformIO& platform_io = ImGui::GetPlatformIO();
			window_draw_data.reserve(platform_io.Viewports.size());
			for (ImGuiViewport* vp : platform_io.Viewports) {
				ImDrawData* draw_data = vp->DrawData;
				WindowDrawData& dd = window_draw_data.emplace(allocator);
				dd.w = u32(vp->Size.x);
				dd.h = u32(vp->Size.y);
				dd.x = i32(draw_data->DisplayPos.x);
				dd.y = i32(draw_data->DisplayPos.y);
				dd.window = vp->PlatformHandle;
				dd.program = getProgram(dd.window, Ref(dd.new_program));
				dd.cmd_lists.reserve(draw_data->CmdListsCount);
				for (int i = 0; i < draw_data->CmdListsCount; ++i) {
					ImDrawList* cmd_list = draw_data->CmdLists[i];
					CmdList& out_cmd_list = dd.cmd_lists.emplace(allocator);

					out_cmd_list.idx_buffer = renderer->copy(&cmd_list->IdxBuffer[0], cmd_list->IdxBuffer.size() * sizeof(cmd_list->IdxBuffer[0]));
					out_cmd_list.vtx_buffer = renderer->copy(&cmd_list->VtxBuffer[0], cmd_list->VtxBuffer.size() * sizeof(cmd_list->VtxBuffer[0]));
			
					out_cmd_list.commands.resize(cmd_list->CmdBuffer.size());
					for (int i = 0, c = out_cmd_list.commands.size(); i < c; ++i) {
						out_cmd_list.commands[i] = cmd_list->CmdBuffer[i];
					}
				}
			}
			
			if (!plugin->m_index_buffer.isValid()) {
				init_render = true;
				plugin->m_index_buffer = gpu::allocBufferHandle();
				plugin->m_vertex_buffer = gpu::allocBufferHandle();
				plugin->m_uniform_buffer = gpu::allocBufferHandle();
			}
		
			default_texture = &plugin->m_texture;
			vb = plugin->m_vertex_buffer;
			ib = plugin->m_index_buffer;
			ub = plugin->m_uniform_buffer;
		}


		void draw(const CmdList& cmd_list, const WindowDrawData& wdd)
		{
			const u32 num_indices = cmd_list.idx_buffer.size / sizeof(ImDrawIdx);
			const u32 num_vertices = cmd_list.vtx_buffer.size / sizeof(ImDrawVert);

			const bool use_big_buffers = num_vertices * sizeof(ImDrawVert) > 256 * 1024 ||
				num_indices * sizeof(ImDrawIdx) > 256 * 1024;
			gpu::useProgram(wdd.program);

			gpu::BufferHandle big_ib, big_vb;
			if (use_big_buffers) {
				big_vb = gpu::allocBufferHandle();
				big_ib = gpu::allocBufferHandle();
				gpu::createBuffer(big_vb, (u32)gpu::BufferFlags::IMMUTABLE, num_vertices * sizeof(ImDrawVert), cmd_list.vtx_buffer.data);
				gpu::createBuffer(big_ib, (u32)gpu::BufferFlags::IMMUTABLE, num_indices * sizeof(ImDrawIdx), cmd_list.idx_buffer.data);
				gpu::bindIndexBuffer(big_ib);
				gpu::bindVertexBuffer(0, big_vb, 0, sizeof(ImDrawVert));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			}
			else {
				gpu::update(ib, cmd_list.idx_buffer.data, num_indices * sizeof(ImDrawIdx));
				gpu::update(vb, cmd_list.vtx_buffer.data, num_vertices * sizeof(ImDrawVert));

				gpu::bindIndexBuffer(ib);
				gpu::bindVertexBuffer(0, vb, 0, sizeof(ImDrawVert));
				gpu::bindVertexBuffer(1, gpu::INVALID_BUFFER, 0, 0);
			}
			renderer->free(cmd_list.vtx_buffer);
			renderer->free(cmd_list.idx_buffer);
			u32 elem_offset = 0;
			const ImDrawCmd* pcmd_begin = cmd_list.commands.begin();
			const ImDrawCmd* pcmd_end = cmd_list.commands.end();
			// TODO enable only when dc.textures[0].value != m_scene_view.getTextureHandle().value);
			const u64 blend_state = gpu::getBlendStateBits(gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA, gpu::BlendFactors::SRC_ALPHA, gpu::BlendFactors::ONE_MINUS_SRC_ALPHA);
			gpu::setState((u64)gpu::StateFlags::SCISSOR_TEST | blend_state);
			for (const ImDrawCmd* pcmd = pcmd_begin; pcmd != pcmd_end; pcmd++)
			{
				ASSERT(!pcmd->UserCallback);
				if (0 == pcmd->ElemCount) continue;

				gpu::TextureHandle tex = gpu::TextureHandle{(u32)(intptr_t)pcmd->TextureId};
				if (!tex.isValid()) tex = *default_texture;
				gpu::bindTextures(&tex, 0, 1);

				const u32 h = u32(clamp(pcmd->ClipRect.w - pcmd->ClipRect.y, 0.f, 65535.f));

				if (gpu::isOriginBottomLeft()) {
					gpu::scissor(u32(maximum(pcmd->ClipRect.x - wdd.x, 0.0f)),
						wdd.h - u32(maximum(pcmd->ClipRect.y - wdd.y, 0.0f)) - h,
						u32(clamp(pcmd->ClipRect.z - pcmd->ClipRect.x, 0.f, 65535.f)),
						u32(clamp(pcmd->ClipRect.w - pcmd->ClipRect.y, 0.f, 65535.f)));
				}
				else {
					gpu::scissor(u32(maximum(pcmd->ClipRect.x - wdd.x, 0.0f)),
						u32(maximum(pcmd->ClipRect.y - wdd.y, 0.0f)),
						u32(clamp(pcmd->ClipRect.z - pcmd->ClipRect.x, 0.f, 65535.f)),
						u32(clamp(pcmd->ClipRect.w - pcmd->ClipRect.y, 0.f, 65535.f)));
				}

				gpu::drawElements(elem_offset * sizeof(u32), pcmd->ElemCount, gpu::PrimitiveType::TRIANGLES, gpu::DataType::U32);
		
				elem_offset += pcmd->ElemCount;
			}
			if (use_big_buffers) {
				gpu::destroy(big_ib);
				gpu::destroy(big_vb);
			}
			else {
				ib_offset += num_indices;
				vb_offset += num_vertices;
			}
		}


		void execute() override
		{
			PROFILE_FUNCTION();

			if (init_render) {
				gpu::createBuffer(ub, (u32)gpu::BufferFlags::UNIFORM_BUFFER, 256, nullptr);
				gpu::createBuffer(ib, 0, 256 * 1024, nullptr);
				gpu::createBuffer(vb, 0, 256 * 1024, nullptr);
			}

			gpu::pushDebugGroup("imgui");

			vb_offset = 0;
			ib_offset = 0;
			for (WindowDrawData& dd : window_draw_data) {
				// TODO window could be destroyed by now
				gpu::setCurrentWindow(dd.window);
				gpu::setFramebuffer(nullptr, 0, 0);
				gpu::viewport(0, 0, dd.w, dd.h);
				
				Vec4 canvas_mtx[] = {
					Vec4(2.f / dd.w, 0, -1 + (float)-dd.x * 2.f / dd.w, 0),
					Vec4(0, -2.f / dd.h, 1 + (float)dd.y * 2.f / dd.h, 0)
				};
				gpu::update(ub, canvas_mtx, sizeof(canvas_mtx));
				gpu::bindUniformBuffer(4, ub, 0, sizeof(canvas_mtx));
				Vec4 cc = {1, 0, 1, 1};
				const float clear_color[] = {0.2f, 0.2f, 0.2f, 1.f};
				gpu::clear((u32)gpu::ClearFlags::COLOR | (u32)gpu::ClearFlags::DEPTH, clear_color, 1.0);
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
						uniform sampler2D u_texture;
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

			gpu::popDebugGroup();
		}
		
		Renderer* renderer;
		const gpu::TextureHandle* default_texture;
		Array<WindowDrawData> window_draw_data;
		u32 ib_offset;
		u32 vb_offset;
		IAllocator& allocator;
		gpu::BufferHandle ib;
		gpu::BufferHandle vb;
		gpu::BufferHandle ub;
		bool init_render = false;
		EditorUIRenderPlugin* plugin;
	};


	EditorUIRenderPlugin(StudioApp& app, SceneView& scene_view, GameView& game_view)
		: m_app(app)
		, m_scene_view(scene_view)
		, m_game_view(game_view)
		, m_engine(app.getEngine())
		, m_index_buffer(gpu::INVALID_BUFFER)
		, m_vertex_buffer(gpu::INVALID_BUFFER)
		, m_uniform_buffer(gpu::INVALID_BUFFER)
		, m_programs(app.getAllocator())
	{

		PluginManager& plugin_manager = m_engine.getPluginManager();
		Renderer* renderer = (Renderer*)plugin_manager.getPlugin("renderer");

		unsigned char* pixels;
		int width, height;
		ImGuiFreeType::BuildFontAtlas(ImGui::GetIO().Fonts);
		ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		const Renderer::MemRef mem = renderer->copy(pixels, width * height * 4);
		m_texture = renderer->createTexture(width, height, 1, gpu::TextureFormat::RGBA8, 0, mem, "editor_font_atlas");
		ImGui::GetIO().Fonts->TexID = (void*)(intptr_t)m_texture.value;

		IAllocator& allocator = app.getAllocator();
		WorldEditor& editor = app.getWorldEditor();
		RenderInterface* render_interface =
			LUMIX_NEW(allocator, RenderInterfaceImpl)(editor, *scene_view.getPipeline(), *renderer);
		editor.setRenderInterface(render_interface);
	}


	~EditorUIRenderPlugin()
	{
		shutdownImGui();
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
		RenderCommand* cmd = LUMIX_NEW(renderer->getAllocator(), RenderCommand)(renderer->getAllocator());
		cmd->plugin = this;
		
		renderer->queue(cmd, 0);
		renderer->frame();
	}


	StudioApp& m_app;
	Engine& m_engine;
	SceneView& m_scene_view;
	GameView& m_game_view;
	HashMap<void*, gpu::ProgramHandle> m_programs;
	gpu::TextureHandle m_texture;
	gpu::BufferHandle m_index_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_vertex_buffer = gpu::INVALID_BUFFER;
	gpu::BufferHandle m_uniform_buffer = gpu::INVALID_BUFFER;
};


struct GizmoPlugin final : public WorldEditor::Plugin
{
	void showLightProbeGridGizmo(ComponentUID cmp) {
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		const Universe& universe = scene->getUniverse();
		EntityRef e = (EntityRef)cmp.entity;
		const LightProbeGrid& lpg = scene->getLightProbeGrid(e);
		const DVec3 pos = universe.getPosition(e);

		scene->addDebugCube(pos - lpg.half_extents, pos + lpg.half_extents, 0xff0000ff);
	}

	void showEnvironmentProbeGizmo(ComponentUID cmp) {
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		const Universe& universe = scene->getUniverse();
		EntityRef e = (EntityRef)cmp.entity;
		const EnvironmentProbe& p = scene->getEnvironmentProbe(e);
		const DVec3 pos = universe.getPosition(e);

		scene->addDebugCube(pos - p.half_extents, pos + p.half_extents, 0xff0000ff);
	}

	void showPointLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		Universe& universe = scene->getUniverse();

		const float range = scene->getLightRange((EntityRef)light.entity);

		const DVec3 pos = universe.getPosition((EntityRef)light.entity);
		scene->addDebugSphere(pos, range, 0xff0000ff);
	}


	static Vec3 minCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(minimum(a.x, b.x), minimum(a.y, b.y), minimum(a.z, b.z));
	}


	static Vec3 maxCoords(const Vec3& a, const Vec3& b)
	{
		return Vec3(maximum(a.x, b.x), maximum(a.y, b.y), maximum(a.z, b.z));
	}


	void showGlobalLightGizmo(ComponentUID light)
	{
		RenderScene* scene = static_cast<RenderScene*>(light.scene);
		const Universe& universe = scene->getUniverse();
		const EntityRef entity = (EntityRef)light.entity;
		const DVec3 pos = universe.getPosition(entity);

		const Vec3 dir = universe.getRotation(entity).rotate(Vec3(0, 0, 1));
		const Vec3 right = universe.getRotation(entity).rotate(Vec3(1, 0, 0));
		const Vec3 up = universe.getRotation(entity).rotate(Vec3(0, 1, 0));

		scene->addDebugLine(pos, pos + dir, 0xff0000ff);
		scene->addDebugLine(pos + right, pos + dir + right, 0xff0000ff);
		scene->addDebugLine(pos - right, pos + dir - right, 0xff0000ff);
		scene->addDebugLine(pos + up, pos + dir + up, 0xff0000ff);
		scene->addDebugLine(pos - up, pos + dir - up, 0xff0000ff);

		scene->addDebugLine(pos + right + up, pos + dir + right + up, 0xff0000ff);
		scene->addDebugLine(pos + right - up, pos + dir + right - up, 0xff0000ff);
		scene->addDebugLine(pos - right - up, pos + dir - right - up, 0xff0000ff);
		scene->addDebugLine(pos - right + up, pos + dir - right + up, 0xff0000ff);

		scene->addDebugSphere(pos - dir, 0.1f, 0xff0000ff);
	}


	void showDecalGizmo(ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);
		Universe& universe = scene->getUniverse();
		Vec3 half_extents = scene->getDecalHalfExtents((EntityRef)cmp.entity);
		const RigidTransform tr = universe.getTransform((EntityRef)cmp.entity).getRigidPart();
		const Vec3 x = tr.rot * Vec3(1, 0, 0) * half_extents.x;
		const Vec3 y = tr.rot * Vec3(0, 1, 0) * half_extents.y;
		const Vec3 z = tr.rot * Vec3(0, 0, 1) * half_extents.z;
		scene->addDebugCube(tr.pos, x, y, z, 0xff0000ff);
	}


	void showCameraGizmo(ComponentUID cmp)
	{
		RenderScene* scene = static_cast<RenderScene*>(cmp.scene);

		scene->addDebugFrustum(scene->getCameraFrustum((EntityRef)cmp.entity), 0xffff0000);
	}


	bool showGizmo(ComponentUID cmp) override
	{
		if (cmp.type == CAMERA_TYPE)
		{
			showCameraGizmo(cmp);
			return true;
		}
		if (cmp.type == DECAL_TYPE)
		{
			showDecalGizmo(cmp);
			return true;
		}
		if (cmp.type == POINT_LIGHT_TYPE)
		{
			showPointLightGizmo(cmp);
			return true;
		}
		if (cmp.type == ENVIRONMENT_TYPE)
		{
			showGlobalLightGizmo(cmp);
			return true;
		}
		if (cmp.type == ENVIRONMENT_PROBE_TYPE) {
			showEnvironmentProbeGizmo(cmp);
			return true;
		}
		if (cmp.type == LIGHT_PROBE_GRID_TYPE) {
			showLightProbeGridGizmo(cmp);
			return true;
		}
		return false;
	}
};


struct AddTerrainComponentPlugin final : public StudioApp::IAddComponentPlugin
{
	explicit AddTerrainComponentPlugin(StudioApp& _app)
		: app(_app)
	{
	}


	bool createHeightmap(const char* material_path, int size)
	{
		char normalized_material_path[MAX_PATH_LENGTH];
		PathUtils::normalize(material_path, Span(normalized_material_path));

		PathUtils::FileInfo info(normalized_material_path);
		StaticString<MAX_PATH_LENGTH> hm_path(info.m_dir, info.m_basename, ".raw");
		OS::OutputFile file;
		if (!file.open(hm_path))
		{
			logError("Editor") << "Failed to create heightmap " << hm_path;
			return false;
		}
		else
		{
			RawTextureHeader header;
			header.width = size;
			header.height = size;
			header.depth = 1;
			header.is_array = false;
			header.channel_type = RawTextureHeader::ChannelType::U16;
			header.channels_count = 1;
			file.write(&header, sizeof(header));
			u16 tmp = 0xffff >> 1;
			for (int i = 0; i < size * size; ++i) {
				file.write(&tmp, sizeof(tmp));
			}
			file.close();
		}

		if (!file.open(normalized_material_path))
		{
			logError("Editor") << "Failed to create material " << normalized_material_path;
			OS::deleteFile(hm_path);
			return false;
		}

		file << R"#(
			shader "pipelines/terrain.shd"
			texture ")#";
		file << info.m_basename;
		file << R"#(.raw"
			texture "/textures/common/white.tga"
			texture ""
			texture ""
		)#";

		file.close();
		return true;
	}


	void onGUI(bool create_entity, bool from_filter) override
	{
		WorldEditor& editor = app.getWorldEditor();
		FileSystem& fs = app.getEngine().getFileSystem();

		ImGui::SetNextWindowSize(ImVec2(300, 300));
		if (!ImGui::BeginMenu("Terrain")) return;
		char buf[MAX_PATH_LENGTH];
		AssetBrowser& asset_browser = app.getAssetBrowser();
		bool new_created = false;
		if (ImGui::BeginMenu("New"))
		{
			static int size = 1024;
			ImGui::InputInt("Size", &size);
			if (ImGui::Button("Create"))
			{
				char save_filename[MAX_PATH_LENGTH];
				if (OS::getSaveFilename(Span(save_filename), "Material\0*.mat\0", "mat")) {
					fs.makeRelative(Span(buf), save_filename);
					new_created = createHeightmap(buf, size);
				}
			}
			ImGui::EndMenu();
		}
		bool create_empty = ImGui::Selectable("Empty", false);
		static u32 selected_res_hash = 0;
		if (asset_browser.resourceList(Span(buf), Ref(selected_res_hash), Material::TYPE, 0, false) || create_empty || new_created)
		{
			if (create_entity)
			{
				EntityRef entity = editor.addEntity();
				editor.selectEntities(&entity, 1, false);
			}
			if (editor.getSelectedEntities().empty()) return;
			EntityRef entity = editor.getSelectedEntities()[0];

			if (!editor.getUniverse()->hasComponent(entity, TERRAIN_TYPE))
			{
				editor.addComponent(Span(&entity, 1), TERRAIN_TYPE);
			}

			if (!create_empty)
			{
				auto* prop = Reflection::getProperty(TERRAIN_TYPE, "Material");
				editor.setProperty(TERRAIN_TYPE, -1, *prop, &entity, 1, buf, stringLength(buf) + 1);
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
	{
	}


	const char* getName() const override { return "renderer"; }


	void init() override
	{
		IAllocator& allocator = m_app.getAllocator();

		m_app.registerComponent("camera", "Render / Camera");
		m_app.registerComponent("environment", "Render / Environment");
		m_app.registerComponent("light_probe_grid", "Render / Light probe grid");

		m_app.registerComponentWithResource(
			"model_instance", "Render / Mesh", Model::TYPE, *Reflection::getProperty(MODEL_INSTANCE_TYPE, "Source"));
		m_app.registerComponentWithResource("particle_emitter",
			"Render / Particle emitter",
			ParticleEmitterResource::TYPE,
			*Reflection::getProperty(PARTICLE_EMITTER_TYPE, "Resource"));
		m_app.registerComponent("point_light", "Render / Point light");
		m_app.registerComponent("decal", "Render / Decal");
		m_app.registerComponent("bone_attachment", "Render / Bone attachment");
		m_app.registerComponent("environment_probe", "Render / Environment probe");
		m_app.registerComponentWithResource(
			"text_mesh", "Render / Text 3D", FontResource::TYPE, *Reflection::getProperty(TEXT_MESH_TYPE, "Font"));

		m_add_terrain_plugin = LUMIX_NEW(allocator, AddTerrainComponentPlugin)(m_app);
		m_app.registerComponent("terrain", *m_add_terrain_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();

		m_shader_plugin = LUMIX_NEW(allocator, ShaderPlugin)(m_app);
		const char* shader_exts[] = {"shd", nullptr};
		asset_compiler.addPlugin(*m_shader_plugin, shader_exts);

		m_texture_plugin = LUMIX_NEW(allocator, TexturePlugin)(m_app);
		const char* texture_exts[] = { "png", "jpg", "dds", "tga", "raw", "ltc", nullptr};
		asset_compiler.addPlugin(*m_texture_plugin, texture_exts);

		m_pipeline_plugin = LUMIX_NEW(allocator, PipelinePlugin)(m_app);
		const char* pipeline_exts[] = {"pln", nullptr};
		asset_compiler.addPlugin(*m_pipeline_plugin, pipeline_exts);

		m_particle_emitter_plugin = LUMIX_NEW(allocator, ParticleEmitterPlugin)(m_app);
		const char* particle_emitter_exts[] = {"par", nullptr};
		asset_compiler.addPlugin(*m_particle_emitter_plugin, particle_emitter_exts);

		m_material_plugin = LUMIX_NEW(allocator, MaterialPlugin)(m_app);
		const char* material_exts[] = {"mat", nullptr};
		asset_compiler.addPlugin(*m_material_plugin, material_exts);

		m_model_plugin = LUMIX_NEW(allocator, ModelPlugin)(m_app);
		const char* model_exts[] = {"fbx", nullptr};
		asset_compiler.addPlugin(*m_model_plugin, model_exts);

		m_font_plugin = LUMIX_NEW(allocator, FontPlugin)(m_app);
		const char* fonts_exts[] = {"ttf", nullptr};
		asset_compiler.addPlugin(*m_font_plugin, fonts_exts);
		
		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.addPlugin(*m_model_plugin);
		asset_browser.addPlugin(*m_particle_emitter_plugin);
		asset_browser.addPlugin(*m_material_plugin);
		asset_browser.addPlugin(*m_font_plugin);
		asset_browser.addPlugin(*m_shader_plugin);
		asset_browser.addPlugin(*m_texture_plugin);

		m_env_probe_plugin = LUMIX_NEW(allocator, EnvironmentProbePlugin)(m_app);
		m_light_probe_grid_plugin = LUMIX_NEW(allocator, LightProbeGridPlugin)(m_app);
		m_terrain_plugin = LUMIX_NEW(allocator, TerrainPlugin)(m_app);
		PropertyGrid& property_grid = m_app.getPropertyGrid();
		property_grid.addPlugin(*m_env_probe_plugin);
		property_grid.addPlugin(*m_light_probe_grid_plugin);
		property_grid.addPlugin(*m_terrain_plugin);

		m_scene_view = LUMIX_NEW(allocator, SceneView)(m_app);
		m_game_view = LUMIX_NEW(allocator, GameView)(m_app);
		m_editor_ui_render_plugin = LUMIX_NEW(allocator, EditorUIRenderPlugin)(m_app, *m_scene_view, *m_game_view);
		m_app.addPlugin(*m_scene_view);
		m_app.addPlugin(*m_game_view);
		m_app.addPlugin(*m_editor_ui_render_plugin);

		m_gizmo_plugin = LUMIX_NEW(allocator, GizmoPlugin)();
		m_app.getWorldEditor().addPlugin(*m_gizmo_plugin);
	}


	~StudioAppPlugin()
	{
		IAllocator& allocator = m_app.getAllocator();

		AssetBrowser& asset_browser = m_app.getAssetBrowser();
		asset_browser.removePlugin(*m_model_plugin);
		asset_browser.removePlugin(*m_particle_emitter_plugin);
		asset_browser.removePlugin(*m_material_plugin);
		asset_browser.removePlugin(*m_font_plugin);
		asset_browser.removePlugin(*m_texture_plugin);
		asset_browser.removePlugin(*m_shader_plugin);

		AssetCompiler& asset_compiler = m_app.getAssetCompiler();
		asset_compiler.removePlugin(*m_font_plugin);
		asset_compiler.removePlugin(*m_shader_plugin);
		asset_compiler.removePlugin(*m_texture_plugin);
		asset_compiler.removePlugin(*m_model_plugin);
		asset_compiler.removePlugin(*m_material_plugin);
		asset_compiler.removePlugin(*m_particle_emitter_plugin);
		asset_compiler.removePlugin(*m_pipeline_plugin);

		LUMIX_DELETE(allocator, m_model_plugin);
		LUMIX_DELETE(allocator, m_material_plugin);
		LUMIX_DELETE(allocator, m_particle_emitter_plugin);
		LUMIX_DELETE(allocator, m_pipeline_plugin);
		LUMIX_DELETE(allocator, m_font_plugin);
		LUMIX_DELETE(allocator, m_texture_plugin);
		LUMIX_DELETE(allocator, m_shader_plugin);

		PropertyGrid& property_grid = m_app.getPropertyGrid();

		property_grid.removePlugin(*m_env_probe_plugin);
		property_grid.removePlugin(*m_light_probe_grid_plugin);
		property_grid.removePlugin(*m_terrain_plugin);

		LUMIX_DELETE(allocator, m_env_probe_plugin);
		LUMIX_DELETE(allocator, m_light_probe_grid_plugin);
		LUMIX_DELETE(allocator, m_terrain_plugin);

		m_app.removePlugin(*m_scene_view);
		m_app.removePlugin(*m_game_view);
		m_app.removePlugin(*m_editor_ui_render_plugin);

		LUMIX_DELETE(allocator, m_scene_view);
		LUMIX_DELETE(allocator, m_game_view);
		LUMIX_DELETE(allocator, m_editor_ui_render_plugin);

		m_app.getWorldEditor().removePlugin(*m_gizmo_plugin);
		LUMIX_DELETE(allocator, m_gizmo_plugin);
	}


	StudioApp& m_app;
	AddTerrainComponentPlugin* m_add_terrain_plugin;
	ModelPlugin* m_model_plugin;
	MaterialPlugin* m_material_plugin;
	ParticleEmitterPlugin* m_particle_emitter_plugin;
	PipelinePlugin* m_pipeline_plugin;
	FontPlugin* m_font_plugin;
	TexturePlugin* m_texture_plugin;
	ShaderPlugin* m_shader_plugin;
	EnvironmentProbePlugin* m_env_probe_plugin;
	LightProbeGridPlugin* m_light_probe_grid_plugin;
	TerrainPlugin* m_terrain_plugin;
	SceneView* m_scene_view;
	GameView* m_game_view;
	EditorUIRenderPlugin* m_editor_ui_render_plugin;
	GizmoPlugin* m_gizmo_plugin;
};


LUMIX_STUDIO_ENTRY(renderer)
{
	auto& allocator = app.getAllocator();
	return LUMIX_NEW(allocator, StudioAppPlugin)(app);
}
