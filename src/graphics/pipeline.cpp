#include "pipeline.h"

#include "graphics/pipeline.h"
#include "core/array.h"
#include "core/associative_array.h"
#include "core/crc32.h"
#include "core/frustum.h"
#include "core/fs/ifile.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/string.h"
#include "engine/engine.h"
#include "engine/plugin_manager.h"
#include "graphics/bitmap_font.h"
#include "graphics/frame_buffer.h"
#include "graphics/geometry.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/terrain.h"
#include "graphics/texture.h"
#include <bgfx.h>
#include <lua.hpp>
#include <lauxlib.h>


namespace Lumix
{


	struct PipelineImpl;
	struct PipelineInstanceImpl;


	static const uint32_t LIGHT_DIR_HASH = crc32("light_dir");
	static const uint32_t TERRAIN_SCALE_HASH = crc32("terrain_scale");
	static const uint32_t BONE_MATRICES_HASH = crc32("bone_matrices");
	static const uint32_t CAMERA_POS_HASH = crc32("camera_pos");
	static const uint32_t MAP_SIZE_HASH = crc32("map_size");
	static const uint32_t POINT_LIGHT_HASH = crc32("point_light");
	static const uint32_t BRUSH_SIZE_HASH = crc32("brush_size");
	static const uint32_t BRUSH_POSITION_HASH = crc32("brush_position");
	static float split_distances[] = { 0.01f, 5, 20, 100, 300 };
	static const float SHADOW_CAM_NEAR = 0.1f;
	static const float SHADOW_CAM_FAR = 10000.0f;
	
	class InstanceData
	{
		public:
			static const int MAX_INSTANCE_COUNT = 32;

		public:
			const bgfx::InstanceDataBuffer* m_buffer;
			int m_instance_count;
			RenderableMesh m_mesh;
	};

	class BaseVertex
	{
		public:
			float m_x, m_y, m_z;
			uint32_t m_rgba;
			float m_u;
			float m_v;

			static bgfx::VertexDecl s_vertex_decl;
	};


	bgfx::VertexDecl BaseVertex::s_vertex_decl;


	struct PipelineImpl : public Pipeline
	{
		PipelineImpl(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
			: Pipeline(path, resource_manager, allocator)
			, m_allocator(allocator)
			, m_framebuffers(allocator)
			, m_lua_state(nullptr)
		{
		}


		Renderer& getRenderer()
		{
			return static_cast<PipelineManager*>(m_resource_manager.get(ResourceManager::PIPELINE))->getRenderer();
		}


		virtual ~PipelineImpl() override
		{
			if (m_lua_state)
			{
				lua_close(m_lua_state);
			}
			ASSERT(isEmpty());
		}


		virtual void doUnload(void) override
		{
			if (m_lua_state)
			{
				lua_close(m_lua_state);
				m_lua_state = nullptr;
			}
			onEmpty();
		}


		void parseRenderbuffers(lua_State* L, FrameBuffer::Declaration& decl)
		{
			decl.m_renderbuffers_count = 0;
			int len = (int)lua_rawlen(L, -1);
			for (int i = 0; i < len; ++i)
			{
				if (lua_rawgeti(L, -1, 1 + i) == LUA_TTABLE)
				{
					FrameBuffer::RenderBuffer& buf = decl.m_renderbuffers[decl.m_renderbuffers_count];
					buf.parse(L);
					++decl.m_renderbuffers_count;
				}
				lua_pop(L, 1);
			}
		}


		void parseFramebuffers(lua_State* L)
		{
			if (lua_getglobal(L, "framebuffers") == LUA_TTABLE)
			{
				int len = (int)lua_rawlen(L, -1);
				m_framebuffers.resize(len);
				for (int i = 0; i < len; ++i)
				{
					if (lua_rawgeti(L, -1, 1 + i) == LUA_TTABLE)
					{
						FrameBuffer::Declaration& decl = m_framebuffers[i];
						if (lua_getfield(L, -1, "name") == LUA_TSTRING)
						{
							decl.m_name = lua_tostring(L, -1);
						}
						lua_pop(L, 1);
						if (lua_getfield(L, -1, "width") == LUA_TNUMBER)
						{
							decl.m_width = (int)lua_tointeger(L, -1);
						}
						lua_pop(L, 1);
						if (lua_getfield(L, -1, "height") == LUA_TNUMBER)
						{
							decl.m_height = (int)lua_tointeger(L, -1);
						}
						lua_pop(L, 1);
						if (lua_getfield(L, -1, "renderbuffers") == LUA_TTABLE)
						{
							parseRenderbuffers(L, decl);
						}
						lua_pop(L, 1);
					}
					lua_pop(L, 1);
				}
			}
			lua_pop(L, 1);
		}


		void registerCFunction(const char* name, lua_CFunction function)
		{
			lua_pushcfunction(m_lua_state, function);
			lua_setglobal(m_lua_state, name);
		}


		void registerCFunctions();


		virtual void loaded(FS::IFile* file, bool success, FS::FileSystem& fs) override
		{
			if (m_lua_state)
			{
				lua_close(m_lua_state);
				m_lua_state = nullptr;
			}
			if (success)
			{
				m_lua_state = luaL_newstate();
				luaL_openlibs(m_lua_state);
				bool errors = luaL_loadbuffer(m_lua_state, (const char*)file->getBuffer(), file->size(), "") != LUA_OK;
				errors = errors || lua_pcall(m_lua_state, 0, LUA_MULTRET, 0) != LUA_OK;
				if (errors)
				{
					g_log_error.log("lua") << getPath().c_str() << ": " << lua_tostring(m_lua_state, -1);
					onFailure();
				}
				else
				{
					parseFramebuffers(m_lua_state);
					registerCFunctions();
					decrementDepCount();
				}

			}
			else
			{
				onFailure();
			}

			fs.close(file);
		}

		lua_State* m_lua_state;
		IAllocator& m_allocator;
		Array<FrameBuffer::Declaration> m_framebuffers;
	};


	struct PipelineInstanceImpl : public PipelineInstance
	{
		PipelineInstanceImpl(Pipeline& pipeline, IAllocator& allocator)
			: m_source(static_cast<PipelineImpl&>(pipeline))
			, m_active_camera(Component::INVALID)
			, m_custom_commands_handlers(allocator)
			, m_allocator(allocator)
			, m_tmp_terrains(allocator)
			, m_tmp_grasses(allocator)
			, m_tmp_meshes(allocator)
			, m_framebuffers(allocator)
			, m_uniforms(allocator)
			, m_global_textures(allocator)
			, m_frame_allocator(allocator, 10 * 1024 * 1024)
			, m_renderer(static_cast<PipelineImpl&>(pipeline).getRenderer())
			, m_screen_space_material(nullptr)
			, m_debug_line_material(nullptr)
			, m_debug_flags(BGFX_DEBUG_TEXT)
		{
			m_terrain_scale_uniform = bgfx::createUniform("u_terrainScale", bgfx::UniformType::Vec4);
			m_morph_const_uniform = bgfx::createUniform("u_morphConst", bgfx::UniformType::Vec4);
			m_rel_camera_pos_uniform = bgfx::createUniform("u_relCamPos", bgfx::UniformType::Vec4);
			m_map_size_uniform = bgfx::createUniform("u_mapSize", bgfx::UniformType::Vec4);
			m_quad_min_and_size_uniform = bgfx::createUniform("u_quadMinAndSize", bgfx::UniformType::Vec4);
			m_fog_color_density_uniform = bgfx::createUniform("u_fogColorDensity", bgfx::UniformType::Vec4);
			m_light_pos_radius_uniform = bgfx::createUniform("u_lightPosRadius", bgfx::UniformType::Vec4);
			m_light_color_uniform = bgfx::createUniform("u_lightRgbInnerR", bgfx::UniformType::Vec4);
			m_light_dir_fov_uniform = bgfx::createUniform("u_lightDirFov", bgfx::UniformType::Vec4);
			m_light_specular_uniform = bgfx::createUniform("u_lightSpecular", bgfx::UniformType::Mat4, 64);
			m_ambient_color_uniform = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);
			m_shadowmap_matrices_uniform = bgfx::createUniform("u_shadowmapMatrices", bgfx::UniformType::Mat4, 4);
			m_shadowmap_splits_uniform = bgfx::createUniform("u_shadowmapSplits", bgfx::UniformType::Vec4);
			m_bone_matrices_uniform = bgfx::createUniform("u_boneMatrices", bgfx::UniformType::Mat4, 64);
			m_specular_shininess_uniform = bgfx::createUniform("u_materialSpecularShininess", bgfx::UniformType::Vec4);

			ResourceManagerBase* material_manager = pipeline.getResourceManager().get(ResourceManager::MATERIAL);
			m_screen_space_material = static_cast<Material*>(material_manager->load(Lumix::Path("models/editor/screen_space.mat"))); 
			m_debug_line_material = static_cast<Material*>(material_manager->load(Lumix::Path("models/editor/debug_line.mat")));

			m_scene = NULL;
			m_width = m_height = -1;
			m_framebuffer_width = m_framebuffer_height = -1;
			pipeline.onLoaded<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
		}


		~PipelineInstanceImpl()
		{
			ResourceManagerBase* material_manager = m_source.getResourceManager().get(ResourceManager::MATERIAL);
			material_manager->unload(*m_screen_space_material);
			material_manager->unload(*m_debug_line_material);

			bgfx::destroyUniform(m_specular_shininess_uniform);
			bgfx::destroyUniform(m_bone_matrices_uniform);
			bgfx::destroyUniform(m_terrain_scale_uniform);
			bgfx::destroyUniform(m_morph_const_uniform);
			bgfx::destroyUniform(m_rel_camera_pos_uniform);
			bgfx::destroyUniform(m_map_size_uniform);
			bgfx::destroyUniform(m_quad_min_and_size_uniform);
			bgfx::destroyUniform(m_fog_color_density_uniform);
			bgfx::destroyUniform(m_light_pos_radius_uniform);
			bgfx::destroyUniform(m_light_color_uniform);
			bgfx::destroyUniform(m_light_dir_fov_uniform);
			bgfx::destroyUniform(m_ambient_color_uniform);
			bgfx::destroyUniform(m_shadowmap_matrices_uniform);
			bgfx::destroyUniform(m_shadowmap_splits_uniform);
			bgfx::destroyUniform(m_light_specular_uniform);

			for (int i = 0; i < m_uniforms.size(); ++i)
			{
				bgfx::destroyUniform(m_uniforms[i]);
			}

			m_source.getObserverCb().unbind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
			m_source.getResourceManager().get(ResourceManager::PIPELINE)->unload(m_source);
			for (int i = 0; i < m_framebuffers.size(); ++i)
			{
				m_allocator.deleteObject(m_framebuffers[i]);
			}
		}

		void finishInstances(int idx)
		{
			if (m_instances_data[idx].m_buffer)
			{
				const RenderableMesh& info = m_instances_data[idx].m_mesh;
				const Mesh& mesh = *info.m_mesh;
				const Model& model = *info.m_model;
				const Geometry& geometry = model.getGeometry();
				const Material* material = mesh.getMaterial();

				setMaterial(material);
				bgfx::setProgram(info.m_mesh->getMaterial()->getShaderInstance().m_program_handles[m_pass_idx]);
				bgfx::setVertexBuffer(geometry.getAttributesArrayID(), mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(), mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
				bgfx::setIndexBuffer(geometry.getIndicesArrayID(), mesh.getIndicesOffset(), mesh.getIndexCount());
				bgfx::setState(m_render_state | material->getRenderStates());
				bgfx::setInstanceDataBuffer(m_instances_data[idx].m_buffer, m_instances_data[idx].m_instance_count);
				bgfx::submit(m_view_idx);
				m_instances_data[idx].m_buffer = nullptr;
				m_instances_data[idx].m_instance_count = 0;
				m_instances_data[idx].m_mesh.m_mesh->setInstanceIdx(-1);
			}
		}


		void finishInstances()
		{
			for (int i = 0; i < lengthOf(m_instances_data); ++i)
			{
				finishInstances(i);
			}
			m_instance_data_idx = 0;
		}


		void setPass(const char* name)
		{
			finishInstances();
			m_pass_idx = m_renderer.getPassIdx(name);
			for (int i = 0; i < lengthOf(m_view2pass_map); ++i)
			{
				if (m_view2pass_map[i] == m_pass_idx)
				{
					m_view_idx = i;
					break;
				}
				else if (m_view2pass_map[i] == 0xff)
				{
					m_view2pass_map[i] = m_pass_idx;
					m_view_idx = i;
					break;
				}
			}
		}

		
		void setActiveCamera(const Component& cmp)
		{
			m_active_camera = cmp;
		}


		CustomCommandHandler& addCustomCommandHandler(const char* name) override
		{
			return m_custom_commands_handlers[crc32(name)];
		}


		FrameBuffer* getFramebuffer(const char* framebuffer_name)
		{
			for (int i = 0, c = m_framebuffers.size(); i < c; ++i)
			{
				if (strcmp(m_framebuffers[i]->getName(), framebuffer_name) == 0)
				{
					return m_framebuffers[i];
				}
			}
			return nullptr;
		}


		void setCurrentFramebuffer(const char* framebuffer_name)
		{
			m_current_framebuffer = getFramebuffer(framebuffer_name);
			if (!m_current_framebuffer)
			{
				g_log_warning.log("renderer") << "Framebuffer " << framebuffer_name << " not found";
			}
		}


		void bindFramebufferTexture(const char* framebuffer_name, int renderbuffer_idx, int uniform_idx)
		{
			FrameBuffer* fb = getFramebuffer(framebuffer_name);
			if (fb)
			{
				GlobalTexture& t = m_global_textures.pushEmpty();
				t.m_texture = fb->getRenderbufferHandle(renderbuffer_idx);
				t.m_uniform = m_uniforms[uniform_idx];
			}
		}


		virtual int getWidth() override
		{
			return m_width;
		}


		virtual int getHeight() override
		{
			return m_height;
		}

		
		void sourceLoaded(Resource::State old_state, Resource::State new_state)
		{
			if (old_state != Resource::State::READY && new_state == Resource::State::READY)
			{
				for (int i = 0; i < m_framebuffers.size(); ++i)
				{
					m_allocator.deleteObject(m_framebuffers[i]);
				}
				m_framebuffers.clear();

				m_framebuffers.reserve(m_source.m_framebuffers.size());
				for (int i = 0; i < m_source.m_framebuffers.size(); ++i)
				{
					FrameBuffer::Declaration& decl = m_source.m_framebuffers[i];
					m_framebuffers.push(m_allocator.newObject<FrameBuffer>(decl));
				}
				
				if (lua_getglobal(m_source.m_lua_state, "init") == LUA_TFUNCTION)
				{
					lua_pushlightuserdata(m_source.m_lua_state, this);
					if (lua_pcall(m_source.m_lua_state, 1, 0, 0) != LUA_OK)
					{
						g_log_error.log("lua") << lua_tostring(m_source.m_lua_state, -1);
					}
				}
			}
		}


		void executeCustomCommand(uint32_t name)
		{
			CustomCommandHandler handler;
			if (m_custom_commands_handlers.find(name, handler))
			{
				handler.invoke();
			}
		}


		void renderShadowmap(Component camera, int64_t layer_mask)
		{
			Component light_cmp = m_scene->getActiveGlobalLight();
			if (!light_cmp.isValid() || !camera.isValid())
			{
				return;
			}
			Matrix light_mtx = light_cmp.entity.getMatrix();

			float shadowmap_height = (float)m_current_framebuffer->getHeight();
			float shadowmap_width = (float)m_current_framebuffer->getWidth();
			float viewports[] =
			{ 0, 0
			, 0.5f, 0
			, 0, 0.5f
			, 0.5f, 0.5f };

			float camera_fov = m_scene->getCameraFOV(camera);
			float camera_ratio = m_scene->getCameraWidth(camera) / m_scene->getCameraHeight(camera);
			for (int split_index = 0; split_index < 4; ++split_index)
			{
				if (split_index > 0)
				{
					++m_view_idx;
					m_view2pass_map[m_view_idx] = m_pass_idx;
				}
				
				bgfx::setViewFrameBuffer(m_view_idx, m_current_framebuffer->getHandle());
				bgfx::setViewClear(m_view_idx, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
				bgfx::submit(m_view_idx);
				float* viewport = viewports + split_index * 2;
				bgfx::setViewRect(m_view_idx
					, (uint16_t)(1 + shadowmap_width * viewport[0])
					, (uint16_t)(1 + shadowmap_height * viewport[1])
					, (uint16_t)(0.5f * shadowmap_width - 2), (uint16_t)(0.5f * shadowmap_height - 2));

				Frustum frustum;
				Matrix camera_matrix = camera.entity.getMatrix();
				frustum.computePerspective(camera_matrix.getTranslation()
					, camera_matrix.getZVector()
					, camera_matrix.getYVector()
					, camera_fov
					, camera_ratio
					, split_distances[split_index]
					, split_distances[split_index + 1]
					);
				(&m_shadowmap_splits.x)[split_index] = split_distances[split_index + 1];

				Vec3 shadow_cam_pos = frustum.getCenter();
				float bb_size = frustum.getRadius();
				Matrix projection_matrix;
				projection_matrix.setOrtho(-bb_size, bb_size, -bb_size, bb_size, SHADOW_CAM_NEAR, SHADOW_CAM_FAR);

				Vec3 light_forward = light_mtx.getZVector();
				shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
				Matrix view_matrix;
				view_matrix.lookAt(shadow_cam_pos, shadow_cam_pos + light_forward, light_mtx.getYVector());
				bgfx::setViewTransform(m_view_idx, &view_matrix.m11, &projection_matrix.m11);
				static const Matrix biasMatrix(
					0.5, 0.0, 0.0, 0.0,
					0.0, 0.5, 0.0, 0.0,
					0.0, 0.0, 0.5, 0.0,
					0.5, 0.5, 0.5, 1.0
					);
				m_shadow_modelviewprojection[split_index] = biasMatrix * (projection_matrix * view_matrix);

				Frustum shadow_camera_frustum;
				shadow_camera_frustum.computeOrtho(shadow_cam_pos, -light_forward, light_mtx.getYVector(), bb_size * 2, bb_size * 2, SHADOW_CAM_NEAR, SHADOW_CAM_FAR);
				renderAll(shadow_camera_frustum, layer_mask, true);
			}
		}


		void renderDebugLines()
		{
			const Array<DebugLine>& lines = m_scene->getDebugLines();
			if (lines.empty() || !m_debug_line_material->isReady())
			{
				return;
			}
			bgfx::TransientVertexBuffer tvb;
			bgfx::TransientIndexBuffer tib;
			if (bgfx::allocTransientBuffers(&tvb, BaseVertex::s_vertex_decl, lines.size() * 2, &tib, lines.size() * 2))
			{
				BaseVertex* vertex = (BaseVertex*)tvb.data;
				uint16_t* indices = (uint16_t*)tib.data;
				for (int i = 0; i < lines.size(); ++i)
				{
					const DebugLine& line = lines[i];
					vertex[0].m_rgba = line.m_color;
					vertex[0].m_x = line.m_from.x;
					vertex[0].m_y = line.m_from.y;
					vertex[0].m_z = line.m_from.z;
					vertex[0].m_u = vertex[0].m_v = 0;

					vertex[1].m_rgba = line.m_color;
					vertex[1].m_x = line.m_to.x;
					vertex[1].m_y = line.m_to.y;
					vertex[1].m_z = line.m_to.z;
					vertex[1].m_u = vertex[0].m_v = 0;

					indices[0] = i * 2;
					indices[1] = i * 2 + 1;
					vertex += 2;
					indices += 2;
				}

				bgfx::setProgram(m_debug_line_material->getShaderInstance().m_program_handles[m_pass_idx]);
				bgfx::setVertexBuffer(&tvb);
				bgfx::setIndexBuffer(&tib);
				bgfx::setState(m_render_state 
					| m_debug_line_material->getRenderStates()
					| BGFX_STATE_PT_LINES
					);
				bgfx::submit(m_view_idx);
			}
		}


		void setPointLightUniforms(const Component& light_cmp)
		{
			Vec4 light_pos_radius(light_cmp.entity.getPosition(), m_scene->getLightRange(light_cmp));
			bgfx::setUniform(m_light_pos_radius_uniform, &light_pos_radius);

			float innerRadius = 0;
			Vec4 light_color(m_scene->getPointLightColor(light_cmp) * m_scene->getPointLightIntensity(light_cmp), innerRadius);
			bgfx::setUniform(m_light_color_uniform, &light_color);

			Vec4 light_dir_fov(light_cmp.entity.getRotation() * Vec3(0, 0, 1), m_scene->getLightFOV(light_cmp));
			bgfx::setUniform(m_light_dir_fov_uniform, &light_dir_fov);

			Vec4 light_specular(m_scene->getPointLightSpecularColor(light_cmp), 1.0);
			bgfx::setUniform(m_light_specular_uniform, &light_specular);
		}


		void setDirectionalLightUniforms(const Component& light_cmp)
		{
			Vec4 diffuse_light_color(m_scene->getGlobalLightColor(light_cmp) * m_scene->getGlobalLightIntensity(light_cmp), 1);
			bgfx::setUniform(m_light_color_uniform, &diffuse_light_color);

			Vec4 ambient_light_color(m_scene->getLightAmbientColor(light_cmp) * m_scene->getLightAmbientIntensity(light_cmp), 1);
			bgfx::setUniform(m_ambient_color_uniform, &ambient_light_color);

			Vec4 light_dir_fov(light_cmp.entity.getRotation() * Vec3(0, 0, 1), 0);
			bgfx::setUniform(m_light_dir_fov_uniform, &light_dir_fov);

			bgfx::setUniform(m_shadowmap_matrices_uniform, &m_shadow_modelviewprojection, 4);
			bgfx::setUniform(m_shadowmap_splits_uniform, &m_shadowmap_splits);

			Vec4 fog_color_density(m_scene->getFogColor(light_cmp), m_scene->getFogDensity(light_cmp));
			bgfx::setUniform(m_fog_color_density_uniform, &fog_color_density);
		}

		
		void setLightUniforms(const Component& light_cmp)
		{
			if (light_cmp.isValid())
			{
				if (light_cmp.type == POINT_LIGHT_HASH)
				{
					setPointLightUniforms(light_cmp);
				}
				else
				{
					setDirectionalLightUniforms(light_cmp);
				}
			}
		}

		
		void enableBlending()
		{
			m_render_state |= BGFX_STATE_BLEND_ADD;
		}


		void disableBlending()
		{
			m_render_state &= ~BGFX_STATE_BLEND_MASK;
		}


		void renderPointLightInfluencedGeometry(const Frustum& frustum, int64_t layer_mask)
		{
			PROFILE_FUNCTION();

			Array<Component> lights(m_allocator);
			m_scene->getPointLights(frustum, lights);
			for (int i = 0; i < lights.size(); ++i)
			{
				m_tmp_grasses.clear();
				m_tmp_meshes.clear();
				m_tmp_terrains.clear();

				Component light = lights[i];
				m_scene->getPointLightInfluencedGeometry(light, frustum, m_tmp_meshes, layer_mask);
				m_scene->getTerrainInfos(m_tmp_terrains, layer_mask, m_scene->getAppliedCamera().entity.getPosition(), m_frame_allocator);
				m_scene->getGrassInfos(frustum, m_tmp_grasses, layer_mask);
				setLightUniforms(light);
				renderMeshes(m_tmp_meshes);
				renderTerrains(m_tmp_terrains);
				renderGrasses(m_tmp_grasses);
				finishInstances();
			}
		}


		void drawQuad(float x, float y, float w, float h)
		{
			if (m_screen_space_material->isReady() && bgfx::checkAvailTransientVertexBuffer(3, BaseVertex::s_vertex_decl))
			{
				Matrix projection_mtx;
				projection_mtx.setOrtho(-1, 1, -1, 1, 0, 30);
				bgfx::setViewTransform(m_view_idx, &Matrix::IDENTITY.m11, &projection_mtx.m11);
				bgfx::setViewRect(m_view_idx, 0, 0, (uint16_t)m_width, (uint16_t)m_height);

				bgfx::TransientVertexBuffer vb;
				bgfx::allocTransientVertexBuffer(&vb, 6, BaseVertex::s_vertex_decl);
				BaseVertex* vertex = (BaseVertex*)vb.data;
				float x2 = x + w;
				float y2 = y + h;

				vertex[0].m_x = x;
				vertex[0].m_y = y;
				vertex[0].m_z = 0;
				vertex[0].m_rgba = 0xffffffff;
				vertex[0].m_u = 0;
				vertex[0].m_v = 0;

				vertex[1].m_x = x2;
				vertex[1].m_y = y;
				vertex[1].m_z = 0;
				vertex[1].m_rgba = 0xffffffff;
				vertex[1].m_u = 1;
				vertex[1].m_v = 0;

				vertex[2].m_x = x2;
				vertex[2].m_y = y2;
				vertex[2].m_z = 0;
				vertex[2].m_rgba = 0xffffffff;
				vertex[2].m_u = 1;
				vertex[2].m_v = 1;

				vertex[3].m_x = x;
				vertex[3].m_y = y;
				vertex[3].m_z = 0;
				vertex[3].m_rgba = 0xffffffff;
				vertex[3].m_u = 0;
				vertex[3].m_v = 0;

				vertex[4].m_x = x2;
				vertex[4].m_y = y2;
				vertex[4].m_z = 0;
				vertex[4].m_rgba = 0xffffffff;
				vertex[4].m_u = 1;
				vertex[4].m_v = 1;

				vertex[5].m_x = x;
				vertex[5].m_y = y2;
				vertex[5].m_z = 0;
				vertex[5].m_rgba = 0xffffffff;
				vertex[5].m_u = 0;
				vertex[5].m_v = 1;

				for (int i = 0; i < m_global_textures.size(); ++i)
				{
					const GlobalTexture& t = m_global_textures[i];
					bgfx::setTexture(i, t.m_uniform, t.m_texture);
				}
				
				bgfx::setProgram(m_screen_space_material->getShaderInstance().m_program_handles[m_pass_idx]);
				bgfx::setVertexBuffer(&vb);
				bgfx::submit(m_view_idx);
			}
		}


		void renderAll(const Frustum& frustum, int64_t layer_mask, bool is_shadowmap)
		{
			PROFILE_FUNCTION();
			
			if (m_scene->getAppliedCamera().isValid())
			{
				m_tmp_grasses.clear();
				m_tmp_meshes.clear();
				m_tmp_terrains.clear();

				m_scene->getRenderableInfos(frustum, m_tmp_meshes, layer_mask);
				m_scene->getTerrainInfos(m_tmp_terrains, layer_mask, m_scene->getAppliedCamera().entity.getPosition(), m_frame_allocator);

				setLightUniforms(m_scene->getActiveGlobalLight());
				renderMeshes(m_tmp_meshes);
				renderTerrains(m_tmp_terrains);
				if (!is_shadowmap)
				{
					m_scene->getGrassInfos(frustum, m_tmp_grasses, layer_mask);
					renderGrasses(m_tmp_grasses);
				}
				finishInstances();
			}
		}


		virtual void toggleStats() override
		{
			m_debug_flags ^= BGFX_DEBUG_STATS;
			bgfx::setDebug(m_debug_flags);
		}


		virtual void renderModel(Model& model, const Matrix& mtx) override
		{
			RenderableMesh mesh;
			mesh.m_matrix = &mtx;
			mesh.m_model = &model;
			mesh.m_pose = nullptr;
			for (int i = 0; i < model.getMeshCount(); ++i)
			{
				mesh.m_mesh = &model.getMesh(i);
				renderRigidMesh(mesh);
			}
		}


		void setPoseUniform(const RenderableMesh& renderable_mesh) const
		{
			Matrix bone_mtx[64];

			const Pose& pose = *renderable_mesh.m_pose;
			const Model& model = *renderable_mesh.m_model;
			Vec3* poss = pose.getPositions();
			Quat* rots = pose.getRotations();

			ASSERT(pose.getCount() <= lengthOf(bone_mtx));
			for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
			{
				rots[bone_index].toMatrix(bone_mtx[bone_index]);
				bone_mtx[bone_index].translate(poss[bone_index]);
				bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
			}
			bgfx::setUniform(m_bone_matrices_uniform, bone_mtx, pose.getCount());
		}


		void renderSkinnedMesh(const RenderableMesh& info)
		{
			if (!info.m_model->isReady())
			{
				return;
			}
			const Mesh& mesh = *info.m_mesh;
			const Geometry& geometry = info.m_model->getGeometry();
			const Material* material = mesh.getMaterial();

			setPoseUniform(info);
			setMaterial(material);
			bgfx::setTransform(info.m_matrix);
			bgfx::setProgram(info.m_mesh->getMaterial()->getShaderInstance().m_program_handles[m_pass_idx]);
			bgfx::setVertexBuffer(geometry.getAttributesArrayID(), mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(), mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
			bgfx::setIndexBuffer(geometry.getIndicesArrayID(), mesh.getIndicesOffset(), mesh.getIndexCount());
			bgfx::setState(m_render_state | material->getRenderStates());
			bgfx::submit(m_view_idx);
		}


		void renderRigidMesh(const RenderableMesh& info)
		{
			if (!info.m_model->isReady())
			{
				return;
			}
			int instance_idx = info.m_mesh->getInstanceIdx();
			if (instance_idx == -1)
			{
				instance_idx = m_instance_data_idx;
				m_instance_data_idx = (m_instance_data_idx + 1) % lengthOf(m_instances_data);
				if (m_instances_data[instance_idx].m_buffer)
				{
					finishInstances(instance_idx);
				}
				info.m_mesh->setInstanceIdx(instance_idx);
			}
			InstanceData& data = m_instances_data[instance_idx];
			if (!data.m_buffer)
			{
				data.m_buffer = bgfx::allocInstanceDataBuffer(InstanceData::MAX_INSTANCE_COUNT, sizeof(Matrix));
				data.m_instance_count = 0;
				data.m_mesh = info;
			}
			Matrix* mtcs = (Matrix*)data.m_buffer->data;
			mtcs[data.m_instance_count] = *info.m_matrix;
			++data.m_instance_count;
			if (data.m_instance_count == InstanceData::MAX_INSTANCE_COUNT)
			{
				const Mesh& mesh = *info.m_mesh;
				const Geometry& geometry = info.m_model->getGeometry();
				const Material* material = mesh.getMaterial();

				setMaterial(material);
				bgfx::setProgram(info.m_mesh->getMaterial()->getShaderInstance().m_program_handles[m_pass_idx]);
				bgfx::setVertexBuffer(geometry.getAttributesArrayID(), mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(), mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
				bgfx::setIndexBuffer(geometry.getIndicesArrayID(), mesh.getIndicesOffset(), mesh.getIndexCount());
				bgfx::setState(m_render_state | material->getRenderStates());
				bgfx::setInstanceDataBuffer(data.m_buffer, data.m_instance_count);
				bgfx::submit(m_view_idx);
				data.m_mesh.m_mesh->setInstanceIdx(-1);
				data.m_buffer = nullptr;
				data.m_instance_count = 0;
			}
		}


		void setMaterial(const Material* material) const
		{
			bgfx::setProgram(material->getShaderInstance().m_program_handles[m_pass_idx]);

			for (int i = 0; i < material->getUniformCount(); ++i)
			{
				const Material::Uniform& uniform = material->getUniform(i);

				switch (uniform.m_type)
				{
					case Material::Uniform::FLOAT:
						{
							Vec4 v(uniform.m_float, 0, 0, 0);
							bgfx::setUniform(uniform.m_handle, &v);
						}
						break;
					case Material::Uniform::TIME:
						{
							Vec4 v(m_scene->getTime(), 0, 0, 0);
							bgfx::setUniform(uniform.m_handle, &v);
						}
						break;
					default:
						ASSERT(false);
						break;
				}
			}

			for (int i = 0; i < material->getTextureCount(); ++i)
			{
				Texture* texture = material->getTexture(i);
				if (texture)
				{
					bgfx::setTexture(i, material->getShader()->getTextureSlot(i).m_uniform_handle, texture->getTextureHandle());
				}
			}

			Vec4 specular_shininess(material->getSpecular(), material->getShininess());
			bgfx::setUniform(m_specular_shininess_uniform, &specular_shininess);

			int global_texture_offset = material->getTextureCount();
			for (int i = 0; i < m_global_textures.size(); ++i)
			{
				const GlobalTexture& t = m_global_textures[i];
				bgfx::setTexture(i + global_texture_offset, t.m_uniform, t.m_texture);
			}
		}


		void renderTerrain(const TerrainInfo& info)
		{
			Material* material = info.m_terrain->getMaterial();
			if (!material->isReady())
			{
				return;
			}
			Matrix inv_world_matrix;
			inv_world_matrix = info.m_world_matrix;
			inv_world_matrix.fastInverse();
			Vec3 camera_pos = m_active_camera.entity.getPosition();
			Vec3 rel_cam_pos = inv_world_matrix.multiplyPosition(camera_pos) / info.m_terrain->getXZScale();

			const Geometry& geometry = *info.m_terrain->getGeometry();
			const Mesh& mesh = *info.m_terrain->getMesh();

			Vec4 map_size(info.m_terrain->getRootSize(), 0, 0, 0);
			bgfx::setUniform(m_map_size_uniform, &map_size);

			Vec4 quad_min_and_size(info.m_min, info.m_size);
			bgfx::setUniform(m_quad_min_and_size_uniform, &quad_min_and_size);

			bgfx::setUniform(m_morph_const_uniform, &Vec4(info.m_morph_const, 0));
			bgfx::setUniform(m_rel_camera_pos_uniform, &Vec4(rel_cam_pos, 0));
			bgfx::setUniform(m_terrain_scale_uniform, &Vec4(info.m_terrain->getScale(), 0));

			setMaterial(material);

			bgfx::setTransform(&info.m_world_matrix.m11);
			bgfx::setProgram(material->getShaderInstance().m_program_handles[m_pass_idx]);
			bgfx::setVertexBuffer(geometry.getAttributesArrayID(), mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(), mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
			int mesh_part_indices_count = mesh.getIndexCount() / 4;
			bgfx::setIndexBuffer(geometry.getIndicesArrayID(), info.m_index * mesh_part_indices_count, mesh_part_indices_count);
			bgfx::setState(m_render_state | mesh.getMaterial()->getRenderStates());
			bgfx::submit(m_view_idx);
		}


		void renderGrass(const GrassInfo& grass)
		{
			const bgfx::InstanceDataBuffer* idb = bgfx::allocInstanceDataBuffer(grass.m_matrix_count, sizeof(Matrix));
			memcpy(idb->data, &grass.m_matrices[0], grass.m_matrix_count * sizeof(Matrix));
			const Mesh& mesh = grass.m_model->getMesh(0);
			const Geometry& geometry = grass.m_model->getGeometry();
			const Material* material = mesh.getMaterial();

			setMaterial(material);
			bgfx::setProgram(material->getShaderInstance().m_program_handles[m_pass_idx]);
			bgfx::setVertexBuffer(geometry.getAttributesArrayID(), mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(), mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
			bgfx::setIndexBuffer(geometry.getIndicesArrayID(), mesh.getIndicesOffset(), mesh.getIndexCount());
			bgfx::setState(m_render_state | material->getRenderStates());
			bgfx::setInstanceDataBuffer(idb, grass.m_matrix_count);
			bgfx::submit(m_view_idx);
		}


		void renderGrasses(const Array<GrassInfo>& grasses)
		{
			PROFILE_FUNCTION();
			for (const auto& grass : grasses)
			{
				renderGrass(grass);
			}
		}


		void renderTerrains(const Array<const TerrainInfo*>& terrains)
		{
			PROFILE_FUNCTION();
			for (auto* terrain : terrains)
			{
				renderTerrain(*terrain);
			}
		}


		void renderMeshes(const Array<const RenderableMesh*>& meshes)
		{
			PROFILE_FUNCTION();
			for (auto* mesh : meshes)
			{
				if (mesh->m_pose && mesh->m_pose->getCount() > 0)
				{
					renderSkinnedMesh(*mesh);
				}
				else
				{
					renderRigidMesh(*mesh);
				}
			}
		}


		virtual void resize(int w, int h) override
		{
			bgfx::reset(w, h);
			m_width = w;
			m_height = h;
		}


		virtual void render() override
		{
			PROFILE_FUNCTION();

			if (!m_source.isReady())
			{
				return;
			}

			m_render_state = BGFX_STATE_RGB_WRITE
				| BGFX_STATE_ALPHA_WRITE
				| BGFX_STATE_DEPTH_WRITE
				| BGFX_STATE_MSAA
				;
			m_view_idx = -1;
			m_pass_idx = -1;
			m_current_framebuffer = nullptr;
			m_global_textures.clear();
			memset(m_view2pass_map, 0xffffFFFF, sizeof(m_view2pass_map));
			m_instance_data_idx = 0;
			for (int i = 0; i < lengthOf(m_instances_data); ++i)
			{
				m_instances_data[i].m_buffer = nullptr;
				m_instances_data[i].m_instance_count = 0;
			}

			if (lua_getglobal(m_source.m_lua_state, "render") == LUA_TFUNCTION)
			{
				lua_pushlightuserdata(m_source.m_lua_state, this);
				if (lua_pcall(m_source.m_lua_state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("lua") << lua_tostring(m_source.m_lua_state, -1);
				}
			}
			finishInstances();

			m_frame_allocator.clear();
		}


		virtual void setScene(RenderScene* scene) override
		{
			m_scene = scene;
			m_active_camera = Component::INVALID;
		}


		virtual RenderScene* getScene() override
		{
			return m_scene;
		}


		virtual void setWireframe(bool wireframe) override
		{
			if (wireframe)
			{
				m_debug_flags = m_debug_flags | BGFX_DEBUG_WIREFRAME;
			}
			else
			{
				m_debug_flags = m_debug_flags & ~BGFX_DEBUG_WIREFRAME;
			}
			bgfx::setDebug(m_debug_flags);
		}


		class GlobalTexture
		{
			public:
				bgfx::TextureHandle m_texture;
				bgfx::UniformHandle m_uniform;
		};


		uint32_t m_debug_flags;
		uint8_t m_view_idx;
		int m_pass_idx;
		uint8_t m_view2pass_map[16];
		uint64_t m_render_state;
		IAllocator& m_allocator;
		Renderer& m_renderer;
		LIFOAllocator m_frame_allocator;
		PipelineImpl& m_source;
		RenderScene* m_scene;
		FrameBuffer* m_current_framebuffer;
		Array<FrameBuffer*> m_framebuffers;
		Array<GlobalTexture> m_global_textures;
		Array<bgfx::UniformHandle> m_uniforms;
		InstanceData m_instances_data[128];
		int m_instance_data_idx;

		Matrix m_shadow_modelviewprojection[4];
		Vec4 m_shadowmap_splits;
		int m_width;
		int m_height;
		int m_framebuffer_width;
		int m_framebuffer_height;
		AssociativeArray<uint32_t, CustomCommandHandler> m_custom_commands_handlers;
		Component m_active_camera;
		Array<const RenderableMesh*> m_tmp_meshes;
		Array<const TerrainInfo*> m_tmp_terrains;
		Array<GrassInfo> m_tmp_grasses;
		bgfx::UniformHandle m_specular_shininess_uniform;
		bgfx::UniformHandle m_bone_matrices_uniform;
		bgfx::UniformHandle m_terrain_scale_uniform;
		bgfx::UniformHandle m_morph_const_uniform;
		bgfx::UniformHandle m_rel_camera_pos_uniform;
		bgfx::UniformHandle m_map_size_uniform;
		bgfx::UniformHandle m_quad_min_and_size_uniform;
		bgfx::UniformHandle m_fog_color_density_uniform;
		bgfx::UniformHandle m_light_pos_radius_uniform;
		bgfx::UniformHandle m_light_color_uniform;
		bgfx::UniformHandle m_ambient_color_uniform;
		bgfx::UniformHandle m_light_dir_fov_uniform;
		bgfx::UniformHandle m_shadowmap_matrices_uniform;
		bgfx::UniformHandle m_shadowmap_splits_uniform;
		bgfx::UniformHandle m_light_specular_uniform;
		Material* m_screen_space_material;
		Material* m_debug_line_material;

	private:
		void operator=(const PipelineInstanceImpl&);
		PipelineInstanceImpl(const PipelineInstanceImpl&);
	};


	Pipeline::Pipeline(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
	{
		if (BaseVertex::s_vertex_decl.getStride() == 0)
		{
			BaseVertex::s_vertex_decl.begin();
				BaseVertex::s_vertex_decl.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
				BaseVertex::s_vertex_decl.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8);
				BaseVertex::s_vertex_decl.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
			BaseVertex::s_vertex_decl.end();
		}
	}


	PipelineInstance* PipelineInstance::create(Pipeline& pipeline, IAllocator& allocator)
	{
		return allocator.newObject<PipelineInstanceImpl>(pipeline, allocator);
	}


	void PipelineInstance::destroy(PipelineInstance* pipeline)
	{
		static_cast<PipelineInstanceImpl*>(pipeline)->m_allocator.deleteObject(pipeline);
	}


	Resource* PipelineManager::createResource(const Path& path)
	{
		return m_allocator.newObject<PipelineImpl>(path, getOwner(), m_allocator);
	}


	void PipelineManager::destroyResource(Resource& resource)
	{
		m_allocator.deleteObject(static_cast<PipelineImpl*>(&resource));
	}


	namespace LuaAPI
	{


		void setPass(PipelineInstanceImpl* pipeline, const char* pass)
		{
			pipeline->setPass(pass);
		}


		void setFramebuffer(PipelineInstanceImpl* pipeline, const char* framebuffer_name)
		{
			pipeline->setCurrentFramebuffer(framebuffer_name);
		}


		void enableBlending(PipelineInstanceImpl* pipeline)
		{
			pipeline->enableBlending();
		}


		void disableBlending(PipelineInstanceImpl* pipeline)
		{
			pipeline->disableBlending();
		}


		void applyCamera(PipelineInstanceImpl* pipeline, const char* slot)
		{
			Component cmp = pipeline->m_scene->getCameraInSlot(slot);
			pipeline->setActiveCamera(cmp);
			if (cmp.isValid())
			{
				if (pipeline->m_framebuffer_width > 0)
				{
					bgfx::setViewRect(pipeline->m_view_idx, 0, 0, (uint16_t)pipeline->m_framebuffer_width, (uint16_t)pipeline->m_framebuffer_height);
				}
				else
				{
					bgfx::setViewRect(pipeline->m_view_idx, 0, 0, (uint16_t)pipeline->m_width, (uint16_t)pipeline->m_height);
				}

				pipeline->m_scene->setCameraSize(cmp, pipeline->m_width, pipeline->m_height);
				pipeline->m_scene->applyCamera(cmp);
				
				Matrix view_matrix, projection_matrix;
				float fov = pipeline->getScene()->getCameraFOV(cmp);
				float near_plane = pipeline->getScene()->getCameraNearPlane(cmp);
				float far_plane = pipeline->getScene()->getCameraFarPlane(cmp);
				projection_matrix.setPerspective(Math::degreesToRadians(fov), (float)pipeline->m_width, (float)pipeline->m_height, near_plane, far_plane);

				Matrix mtx = cmp.entity.getMatrix();
				Vec3 pos = mtx.getTranslation();
				Vec3 center = pos - mtx.getZVector();
				Vec3 up = mtx.getYVector();
				view_matrix.lookAt(pos, center, up);

				bgfx::setViewTransform(pipeline->m_view_idx, &view_matrix.m11, &projection_matrix.m11);
			}
		}


		void clear(PipelineInstanceImpl* pipeline, const char* buffers)
		{
			uint16_t flags = 0;
			if (strcmp(buffers, "all") == 0)
			{
				flags = BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH;
			}
			else if (strcmp(buffers, "depth") == 0)
			{
				flags = BGFX_CLEAR_DEPTH;
			}
			bgfx::setViewClear(pipeline->m_view_idx, flags, 0x303030ff, 1.0f, 0);
			bgfx::submit(pipeline->m_view_idx);
		}


		void renderModels(PipelineInstanceImpl* pipeline, int64_t layer_mask, bool is_point_light_render)
		{
			if (is_point_light_render)
			{
				pipeline->renderPointLightInfluencedGeometry(pipeline->getScene()->getFrustum(), layer_mask);
			}
			else
			{
				pipeline->renderAll(pipeline->getScene()->getFrustum(), layer_mask, false);
			}
		}


		void bindFramebufferTexture(PipelineInstanceImpl* pipeline, const char* framebuffer_name, int renderbuffer_index, int uniform_idx)
		{
			pipeline->bindFramebufferTexture(framebuffer_name, renderbuffer_index, uniform_idx);
		}


		void executeCustomCommand(PipelineInstanceImpl* pipeline, const char* command)
		{
			pipeline->executeCustomCommand(crc32(command));
		}


		void renderDebugLines(PipelineInstanceImpl* pipeline)
		{
			pipeline->renderDebugLines();
		}


		void renderShadowmap(PipelineInstanceImpl* pipeline, int64_t layer_mask, const char* slot)
		{
			pipeline->renderShadowmap(pipeline->getScene()->getCameraInSlot(slot), layer_mask);
		}

		
		int createUniform(PipelineInstanceImpl* pipeline, const char* name)
		{
			bgfx::UniformHandle handle = bgfx::createUniform(name, bgfx::UniformType::Int1);
			pipeline->m_uniforms.push(handle);
			return pipeline->m_uniforms.size() - 1;
		}


		void drawQuad(PipelineInstanceImpl* pipeline, float x, float y, float w, float h)
		{
			pipeline->drawQuad(x, y, w, h);
		}


		void print(int x, int y, const char* text)
		{
			bgfx::dbgTextPrintf(x, y, 0x4f, text);
		}


	} // namespace LuaAPI


	namespace LuaWrapper
	{


		template <typename T> T toType(lua_State* L, int index) { return (T)lua_touserdata(L, index); }
		template <> int toType(lua_State* L, int index) { return (int)lua_tointeger(L, index); }
		template <> int64_t toType(lua_State* L, int index) { return (int64_t)lua_tointeger(L, index); }
		template <> bool toType(lua_State* L, int index) { return lua_toboolean(L, index) != 0; }
		template <> float toType(lua_State* L, int index) { return (float)lua_tonumber(L, index); }
		template <> const char* toType(lua_State* L, int index) { return lua_tostring(L, index); }
		template <> void* toType(lua_State* L, int index) { return lua_touserdata(L, index); }


		template <typename T> bool isType(lua_State* L, int index) { return lua_islightuserdata(L, index) != 0; }
		template <> bool isType<int>(lua_State* L, int index) { return lua_isinteger(L, index) != 0; }
		template <> bool isType<int64_t>(lua_State* L, int index) { return lua_isinteger(L, index) != 0; }
		template <> bool isType<bool>(lua_State* L, int index) { return lua_isboolean(L, index) != 0; }
		template <> bool isType<float>(lua_State* L, int index) { return lua_isnumber(L, index) != 0; }
		template <> bool isType<const char*>(lua_State* L, int index) { return lua_isstring(L, index) != 0; }
		template <> bool isType<void*>(lua_State* L, int index) { return lua_islightuserdata(L, index) != 0; }


		template <int N>
		struct FunctionCaller
		{
			template <typename R, typename... ArgsF, typename... Args>
			static LUMIX_FORCE_INLINE R callFunction(R (*f)(ArgsF...), lua_State* L, Args... args)
			{
				typedef std::tuple_element<sizeof...(ArgsF)-N, std::tuple<ArgsF...> >::type T;
				if (!isType<T>(L, sizeof...(ArgsF)-N + 1))
				{
					lua_Debug entry;
					int depth = 0;

					char tmp[2048];
					tmp[0] = 0;
					auto er = g_log_error.log("lua");
					er << "Wrong arguments in\n";
					while (lua_getstack(L, depth, &entry))
					{
						int status = lua_getinfo(L, "Sln", &entry);
						ASSERT(status);
						er << entry.short_src << "(" << entry.currentline << "): " << (entry.name ? entry.name : "?") << "\n";
						depth++;
					}
					return R();
				}
				T a = toType<T>(L, sizeof...(ArgsF)-N + 1);
				return FunctionCaller<N - 1>::callFunction(f, L, args..., a);
			}
		};


		template <>
		struct FunctionCaller<0>
		{
			template <typename R, typename... ArgsF, typename... Args>
			static LUMIX_FORCE_INLINE R callFunction(R (*f)(ArgsF...), lua_State*, Args... args)
			{
				return f(args...);
			}
		};


		template <typename R, typename... ArgsF>
		int LUMIX_FORCE_INLINE callFunction(R (*f)(ArgsF...), lua_State* L)
		{
			R v = FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
			lua_pushinteger(L, v);
			return 1;
		}

		template <typename... ArgsF>
		int LUMIX_FORCE_INLINE callFunction(void (*f)(ArgsF...), lua_State* L)
		{
			FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
			return 0;
		}

		template <typename T, T t>
		int wrap(lua_State* L)
		{
			return callFunction(t, L);
		}


	} // namespace LuaWrapper


	void PipelineImpl::registerCFunctions()
	{
		registerCFunction("drawQuad", LuaWrapper::wrap<decltype(&LuaAPI::drawQuad), LuaAPI::drawQuad>);
		registerCFunction("print", LuaWrapper::wrap<decltype(&LuaAPI::print), LuaAPI::print>);
		registerCFunction("createUniform", LuaWrapper::wrap<decltype(&LuaAPI::createUniform), LuaAPI::createUniform>);
		registerCFunction("setFramebuffer", LuaWrapper::wrap<decltype(&LuaAPI::setFramebuffer), LuaAPI::setFramebuffer>);
		registerCFunction("enableBlending", LuaWrapper::wrap<decltype(&LuaAPI::enableBlending), LuaAPI::enableBlending>);
		registerCFunction("disableBlending", LuaWrapper::wrap<decltype(&LuaAPI::disableBlending), LuaAPI::disableBlending>);
		registerCFunction("setPass", LuaWrapper::wrap<decltype(&LuaAPI::setPass), LuaAPI::setPass>);
		registerCFunction("applyCamera", LuaWrapper::wrap<decltype(&LuaAPI::applyCamera), LuaAPI::applyCamera>);
		registerCFunction("clear", LuaWrapper::wrap<decltype(&LuaAPI::clear), LuaAPI::clear>);
		registerCFunction("renderModels", LuaWrapper::wrap<decltype(&LuaAPI::renderModels), LuaAPI::renderModels>);
		registerCFunction("renderShadowmap", LuaWrapper::wrap<decltype(&LuaAPI::renderShadowmap), LuaAPI::renderShadowmap>);
		registerCFunction("bindFramebufferTexture", LuaWrapper::wrap<decltype(&LuaAPI::bindFramebufferTexture), LuaAPI::bindFramebufferTexture>);
		registerCFunction("executeCustomCommand", LuaWrapper::wrap<decltype(&LuaAPI::executeCustomCommand), LuaAPI::executeCustomCommand>);
		registerCFunction("renderDebugLines", LuaWrapper::wrap<decltype(&LuaAPI::renderDebugLines), LuaAPI::renderDebugLines>);
	}


} // ~namespace Lumix
