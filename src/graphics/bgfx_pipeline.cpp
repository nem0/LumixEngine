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
			, m_terrain_infos(allocator)
			, m_framebuffers(allocator)
			, m_grass_infos(allocator)
			, m_renderable_infos(allocator)
			, m_frame_allocator(allocator, 1 * 1024 * 1024)
			, m_renderer(static_cast<PipelineImpl&>(pipeline).getRenderer())
		{
			m_light_pos_radius_uniform = bgfx::createUniform("u_lightPosRadius", bgfx::UniformType::Vec4);
			m_light_color_uniform = bgfx::createUniform("u_lightRgbInnerR", bgfx::UniformType::Vec4);
			m_light_dir_fov_uniform = bgfx::createUniform("u_lightDirFov", bgfx::UniformType::Vec4);
			m_ambient_color_uniform = bgfx::createUniform("u_ambientColor", bgfx::UniformType::Vec4);

			m_draw_calls_count = 0;
			m_vertices_count = 0;
			m_scene = NULL;
			m_width = m_height = -1;
			m_framebuffer_width = m_framebuffer_height = -1;
			m_shadowmap_framebuffer = NULL;
			pipeline.onLoaded<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
		}


		~PipelineInstanceImpl()
		{
			bgfx::destroyUniform(m_light_pos_radius_uniform);
			bgfx::destroyUniform(m_light_color_uniform);
			bgfx::destroyUniform(m_light_dir_fov_uniform);
			bgfx::destroyUniform(m_ambient_color_uniform);

			m_source.getObserverCb().unbind<PipelineInstanceImpl, &PipelineInstanceImpl::sourceLoaded>(this);
			m_source.getResourceManager().get(ResourceManager::PIPELINE)->unload(m_source);
			for (int i = 0; i < m_framebuffers.size(); ++i)
			{
				m_allocator.deleteObject(m_framebuffers[i]);
			}
		}


		void setPass(const char* name)
		{
			m_pass_idx = m_renderer.getPassIdx(name);
		}

		
		void setActiveCamera(const Component& cmp)
		{
			m_active_camera = cmp;
		}


		CustomCommandHandler& addCustomCommandHandler(const char* name) override
		{
			return m_custom_commands_handlers[crc32(name)];
		}


		FrameBuffer* getFrameBuffer(const char* name)
		{
			TODO("bgfx");
			/*
			for (int i = 0, c = m_framebuffers.size(); i < c; ++i)
			{
				if (strcmp(m_framebuffers[i]->getName(), name) == 0)
				{
					return m_framebuffers[i];
				}
			}*/
			return NULL;
		}


		virtual int getWidth() override
		{
			return m_width;
		}


		virtual int getHeight() override
		{
			return m_height;
		}


		virtual int getDrawCalls() const override
		{
			return m_draw_calls_count;
		}


		virtual int getRenderedTrianglesCount() const override
		{
			return m_vertices_count / 3;
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
					if (decl.m_name == "shadowmap")
					{
						m_shadowmap_framebuffer = m_framebuffers.back();
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
			TODO("bgfx");
			/*PROFILE_FUNCTION();
			ASSERT(m_renderer != NULL);
			Component light_cmp = m_scene->getActiveGlobalLight();
			if (!light_cmp.isValid() || !camera.isValid())
			{
				return;
			}
			m_shadowmap_framebuffer->bind();
			glClear(GL_DEPTH_BUFFER_BIT);

			Matrix light_mtx = light_cmp.entity.getMatrix();

			float shadowmap_height = (float)m_shadowmap_framebuffer->getHeight();
			float shadowmap_width = (float)m_shadowmap_framebuffer->getWidth();
			float viewports[] =
			{ 0, 0
			, 0.5f, 0
			, 0, 0.5f
			, 0.5f, 0.5f };
			float camera_fov = m_scene->getCameraFOV(camera);
			float camera_ratio = m_scene->getCameraWidth(camera) / m_scene->getCameraHeight(camera);
			for (int split_index = 0; split_index < 4; ++split_index)
			{
				float* viewport = viewports + split_index * 2;
				glViewport((int)(1 + shadowmap_width * viewport[0]), (int)(1 + shadowmap_height * viewport[1]),
					(int)(0.5f * shadowmap_width - 2), (int)(0.5f * shadowmap_height - 2));

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
				Renderer::getOrthoMatrix(-bb_size, bb_size, -bb_size, bb_size, SHADOW_CAM_NEAR, SHADOW_CAM_FAR, &projection_matrix);
				m_renderer->setProjectionMatrix(projection_matrix);

				Vec3 light_forward = light_mtx.getZVector();
				shadow_cam_pos -= light_forward * SHADOW_CAM_FAR * 0.5f;
				Matrix modelview_matrix;
				Renderer::getLookAtMatrix(shadow_cam_pos, shadow_cam_pos + light_forward, light_mtx.getYVector(), &modelview_matrix);
				m_renderer->setViewMatrix(modelview_matrix);
				static const Matrix biasMatrix(
					0.5, 0.0, 0.0, 0.0,
					0.0, 0.5, 0.0, 0.0,
					0.0, 0.0, 0.5, 0.0,
					0.5, 0.5, 0.5, 1.0
					);
				m_shadow_modelviewprojection[split_index] = biasMatrix * (projection_matrix * modelview_matrix);

				Frustum shadow_camera_frustum;
				shadow_camera_frustum.computeOrtho(shadow_cam_pos, -light_forward, light_mtx.getYVector(), bb_size * 2, bb_size * 2, SHADOW_CAM_NEAR, SHADOW_CAM_FAR);
				renderModels(shadow_camera_frustum, layer_mask, true);
			}
			FrameBuffer::unbind();
			glCullFace(GL_BACK);*/
		}


		void renderScreenGeometry(Geometry* geometry, Mesh* mesh)
		{
			TODO("bgfx");
			/*if (mesh->getMaterial()->isReady())
			{
				ASSERT(m_renderer != NULL);
				Shader* shader = mesh->getMaterial()->getShader();
				Matrix mtx;
				Renderer::getOrthoMatrix(-1, 1, -1, 1, 0, 30, &mtx);
				m_renderer->setProjectionMatrix(mtx);
				m_renderer->setViewMatrix(Matrix::IDENTITY);
				applyMaterial(*mesh->getMaterial());
				Component light_cmp = m_scene->getActiveGlobalLight();
				if (light_cmp.isValid())
				{
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX0, m_shadow_modelviewprojection[0]);
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX1, m_shadow_modelviewprojection[1]);
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX2, m_shadow_modelviewprojection[2]);
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX3, m_shadow_modelviewprojection[3]);
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::AMBIENT_COLOR, m_scene->getLightAmbientColor(light_cmp));
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::AMBIENT_INTENSITY, m_scene->getLightAmbientIntensity(light_cmp));
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_COLOR, m_scene->getGlobalLightColor(light_cmp));
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::DIFFUSE_INTENSITY, m_scene->getGlobalLightIntensity(light_cmp));
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::LIGHT_DIR, light_cmp.entity.getRotation() * Vec3(0, 0, 1));
				}
				m_renderer->setUniform(*shader, "camera_pos", CAMERA_POS_HASH, m_active_camera.entity.getPosition());

				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX, Matrix::IDENTITY);
				setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::PROJECTION_MATRIX, mtx);
				bindGeometry(*m_renderer, *geometry, *mesh);
				renderGeometry(0, 6);
			}*/
		}


		void renderDebugTexts()
		{
			/*BitmapFont* font = m_scene->getDebugTextFont();
			if (!font || !font->isReady())
			{
				return;
			}
			m_renderer->cleanup();
			Matrix projection_matrix;
			Renderer::getOrthoMatrix(0, (float)m_width, 0, (float)m_height, 0, 10, &projection_matrix);
			m_renderer->setProjectionMatrix(projection_matrix);
			m_renderer->setViewMatrix(Matrix::IDENTITY);

			Geometry& geometry = m_scene->getDebugTextGeometry();
			Mesh& mesh = m_scene->getDebugTextMesh();

			applyMaterial(*font->getMaterial());
			bindGeometry(*m_renderer, geometry, mesh);
			glEnable(GL_BLEND);
			glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			glDisable(GL_DEPTH_TEST);
			renderGeometry(0, mesh.getIndexCount());
			glDisable(GL_BLEND);
			glEnable(GL_DEPTH_TEST);*/
			
			bgfx::dbgTextClear();
			int i = 0;
			while (const char* text = m_scene->getDebugText(i))
			{
				bgfx::dbgTextPrintf(m_pass_idx, 1, 0x4f, text);
				++i;
			}
		}


		void renderDebugLines()
		{
			TODO("bgfx");
			/*
			m_renderer->cleanup();

			const Array<DebugLine>& lines = m_scene->getDebugLines();
			Shader& shader = m_renderer->getDebugShader();
			m_renderer->applyShader(shader, 0);

			for (int j = 0; j <= lines.size() / 256; ++j)
			{
				Vec3 positions[512];
				Vec3 colors[512];
				int indices[512];
				int offset = j * 256;
				for (int i = 0, c = Math::minValue(lines.size() - offset, 256); i < c; ++i)
				{
					positions[i * 2] = lines[offset + i].m_from;
					positions[i * 2 + 1] = lines[offset + i].m_to;
					colors[i * 2] = lines[offset + i].m_color;
					colors[i * 2 + 1] = lines[offset + i].m_color;
					indices[i * 2] = i * 2;
					indices[i * 2 + 1] = i * 2 + 1;
				}

				glEnableVertexAttribArray(shader.getAttribId(0));
				glVertexAttribPointer(shader.getAttribId(0), 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), positions);
				glEnableVertexAttribArray(shader.getAttribId(1));
				glVertexAttribPointer(shader.getAttribId(1), 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), colors);
				glDrawElements(GL_LINES, Math::minValue(lines.size() - offset, 256) * 2, GL_UNSIGNED_INT, indices);
			}*/
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
		}


		void setDirectionalLightUniforms(const Component& light_cmp)
		{
			Vec4 diffuse_light_color(m_scene->getGlobalLightColor(light_cmp) * m_scene->getGlobalLightIntensity(light_cmp), 1);
			bgfx::setUniform(m_light_color_uniform, &diffuse_light_color);

			Vec4 ambient_light_color(m_scene->getLightAmbientColor(light_cmp) * m_scene->getLightAmbientIntensity(light_cmp), 1);
			bgfx::setUniform(m_ambient_color_uniform, &ambient_light_color);

			Vec4 light_dir_fov(light_cmp.entity.getRotation() * Vec3(0, 0, 1), 0);
			bgfx::setUniform(m_light_dir_fov_uniform, &light_dir_fov);

			TODO("bgfx");
			/*setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX0, m_shadow_modelviewprojection[0]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX1, m_shadow_modelviewprojection[1]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX2, m_shadow_modelviewprojection[2]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOW_MATRIX3, m_shadow_modelviewprojection[3]);
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_COLOR, m_scene->getFogColor(light_cmp));
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::FOG_DENSITY, m_scene->getFogDensity(light_cmp));
			setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::SHADOWMAP_SPLITS, m_shadowmap_splits);
			bgfx::setUniform(light_color, &m_scene->getLightAmbientColor(light_cmp));
			float pos_r[] = { 0, 0, 0, 4 };
			bgfx::setUniform(light_pos, pos_r);*/
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


		void deferredPointLightLoop(Material* material)
		{
			TODO("bgfx");
			/*
			Array<Component> lights(m_allocator);
			m_scene->getPointLights(m_scene->getFrustum(), lights);
			if (!lights.empty() && material->isReady())
			{
				Component camera = m_scene->getCameraInSlot("editor");
				applyMaterial(*material);
				GLint attrib_id = material->getShader()->getAttribId(m_renderer->getAttributeNameIndex("in_position"));

				glEnable(GL_BLEND);
				glBlendFunc(GL_SRC_ALPHA, GL_ONE);

				GLubyte indices[] = {
					0, 1, 2, 2, 3, 0,
					3, 2, 6, 6, 7, 3,
					7, 6, 5, 5, 4, 7,
					4, 5, 1, 1, 0, 4,
					4, 0, 3, 3, 7, 4,
					1, 5, 6, 6, 2, 1
				};
				for (int i = 0; i < lights.size(); ++i)
				{
					float light_range = m_scene->getLightRange(lights[i]);
					Vec3 light_pos = m_scene->getPointLightEntity(lights[i]).getPosition();
					Matrix camera_matrix = camera.entity.getMatrix();
					Vec3 forward(0, 0, light_range);
					Vec3 up(0, light_range, 0);
					Vec3 side(light_range, 0, 0);

					Vec3 vertices[] =
					{
						light_pos + forward - up - side,
						light_pos + forward - up + side,
						light_pos + forward + up + side,
						light_pos + forward + up - side,
						light_pos - forward - up - side,
						light_pos - forward - up + side,
						light_pos - forward + up + side,
						light_pos - forward + up - side
					};

					setLightUniforms(lights[i], material->getShader());

					glEnableVertexAttribArray(attrib_id);
					glBindBuffer(GL_ARRAY_BUFFER, 0);
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
					glVertexAttribPointer(attrib_id, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), vertices);
					glCullFace(GL_FRONT);
					glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, indices);
				}

				glDisable(GL_BLEND);
			}*/
		}


		bool beginTerrainRenderLoop(const RenderableInfo* info, const Component& light_cmp)
		{
			TODO("bgfx");
			return false;
			/*
			TerrainInfo* data = (TerrainInfo*)info->m_data;

			Material* material = data->m_terrain->getMesh()->getMaterial();
			if (material->isReady())
			{
				Shader* shader = material->getShader();
				if (shader->isReady())
				{
					applyMaterial(*data->m_terrain->getMesh()->getMaterial());
					setLightUniforms(light_cmp, shader);
					m_renderer->setUniform(*shader, "terrain_scale", TERRAIN_SCALE_HASH, data->m_terrain->getScale());
					return true;
				}
			}
			return false;*/
		}


		bool beginGrassRenderLoop(const RenderableInfo* info, const Component& light_cmp)
		{
			TODO("bgfx");
			/*
			const Terrain::GrassPatch* patch = static_cast<const Terrain::GrassPatch*>(info->m_data);
			const Mesh& mesh = *patch->m_type->m_grass_mesh;
			const Material& material = *mesh.getMaterial();
			Shader* shader = material.getShader();
			uint32_t pass_hash = getRenderer().getPass();
			if (!shader->hasPass(pass_hash) || !material.isReady())
			{
				return false;
			}
			applyMaterial(material);
			m_renderer->setUniform(*shader, "camera_pos", CAMERA_POS_HASH, m_active_camera.entity.getPosition());
			setLightUniforms(light_cmp, shader);

			bindGeometry(*m_renderer, *patch->m_type->m_grass_geometry, mesh);
			return true;*/
			return false;
		}


		void setPoseUniform(const RenderableMesh* LUMIX_RESTRICT renderable_mesh, Shader* shader)
		{
			TODO("bgfx");
			/*
			Matrix bone_mtx[64];

			const Pose& pose = *renderable_mesh->m_pose;
			const Model& model = *renderable_mesh->m_model;
			Vec3* poss = pose.getPositions();
			Quat* rots = pose.getRotations();

			ASSERT(pose.getCount() <= sizeof(bone_mtx) / sizeof(bone_mtx[0]));
			for (int bone_index = 0, bone_count = pose.getCount(); bone_index < bone_count; ++bone_index)
			{
				rots[bone_index].toMatrix(bone_mtx[bone_index]);
				bone_mtx[bone_index].translate(poss[bone_index]);
				bone_mtx[bone_index] = bone_mtx[bone_index] * model.getBone(bone_index).inv_bind_matrix;
			}
			m_renderer->setUniform(*shader, "bone_matrices", BONE_MATRICES_HASH, bone_mtx, pose.getCount());*/
		}


		void enableBlending()
		{
			m_render_state |= BGFX_STATE_BLEND_ADD;
		}


#error gizmo z test
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
				Component light = lights[i];
				m_renderable_infos.clear();
				m_scene->getPointLightInfluencedGeometry(light, frustum, m_renderable_infos, layer_mask);
				m_scene->getGrassInfos(frustum, m_renderable_infos, layer_mask);
				m_scene->getTerrainInfos(m_renderable_infos, layer_mask, m_scene->getAppliedCamera().entity.getPosition(), m_frame_allocator);
				render(&m_renderable_infos, light);
			}
		}


		void renderModels(const Frustum& frustum, int64_t layer_mask, bool is_shadowmap)
		{
			PROFILE_FUNCTION();

			if (m_scene->getAppliedCamera().isValid())
			{
				m_renderable_infos.clear();
				m_scene->getRenderableInfos(frustum, m_renderable_infos, layer_mask);
				m_scene->getTerrainInfos(m_renderable_infos, layer_mask, m_scene->getAppliedCamera().entity.getPosition(), m_frame_allocator);
				if (!is_shadowmap)
				{
					m_scene->getGrassInfos(frustum, m_renderable_infos, layer_mask);
				}
				render(&m_renderable_infos, m_scene->getActiveGlobalLight());
			}
		}


		inline const RenderableInfo* renderLoopSkinned(const RenderableInfo* info)
		{
			TODO("bgfx");
			return nullptr;
			/*
			const RenderableMesh* LUMIX_RESTRICT renderable_mesh = static_cast<const RenderableMesh*>(info->m_data);
			Shader* shader = renderable_mesh->m_mesh->getMaterial()->getShader();
			GLint world_matrix_uniform_location = shader->getFixedCachedUniformLocation(Shader::FixedCachedUniforms::WORLD_MATRIX);
			bindGeometry(*m_renderer, renderable_mesh->m_model->getGeometry(), *renderable_mesh->m_mesh);
			int64_t last_key = info->m_key;
			int indices_offset = renderable_mesh->m_mesh->getIndicesOffset();
			int indices_count = renderable_mesh->m_mesh->getIndexCount();
			while (last_key == info->m_key)
			{
				const RenderableMesh* LUMIX_RESTRICT renderable_mesh = static_cast<const RenderableMesh*>(info->m_data);
				const Matrix& world_matrix = *renderable_mesh->m_matrix;
				setUniform(world_matrix_uniform_location, world_matrix);
				setPoseUniform(renderable_mesh, shader);
				++m_draw_calls_count;
				m_vertices_count += indices_count;
				renderGeometry(indices_offset, indices_count);
				++info;
			}
			return info;*/
		}


		const RenderableInfo* renderLoopTerrain(const RenderableInfo* info)
		{
			TODO("bgfx");
			return nullptr;

			/*PROFILE_FUNCTION();
			const TerrainInfo* data = static_cast<const TerrainInfo*>(info->m_data);

			Matrix inv_world_matrix;
			inv_world_matrix = data->m_world_matrix;
			inv_world_matrix.fastInverse();
			Vec3 camera_pos = m_active_camera.entity.getPosition();
			Vec3 rel_cam_pos = inv_world_matrix.multiplyPosition(camera_pos) / data->m_terrain->getXZScale();
			Shader& shader = *data->m_terrain->getMesh()->getMaterial()->getShader();
			m_renderer->setUniform(shader, "brush_position", BRUSH_POSITION_HASH, data->m_terrain->getBrushPosition());
			m_renderer->setUniform(shader, "brush_size", BRUSH_SIZE_HASH, data->m_terrain->getBrushSize());
			m_renderer->setUniform(shader, "map_size", MAP_SIZE_HASH, data->m_terrain->getRootSize());
			m_renderer->setUniform(shader, "camera_pos", CAMERA_POS_HASH, rel_cam_pos);

			int64_t last_key = info->m_key;
			bindGeometry(*m_renderer, *data->m_terrain->getGeometry(), *data->m_terrain->getMesh());
			int world_matrix_location = getUniformLocation(shader, (int)Shader::FixedCachedUniforms::WORLD_MATRIX);
			int morph_const_location = getUniformLocation(shader, (int)Shader::FixedCachedUniforms::MORPH_CONST);
			int quad_size_location = getUniformLocation(shader, (int)Shader::FixedCachedUniforms::QUAD_SIZE);
			int quad_min_location = getUniformLocation(shader, (int)Shader::FixedCachedUniforms::QUAD_MIN);
			int mesh_part_indices_count = data->m_terrain->getMesh()->getIndexCount() / 4;
			while (info->m_key == last_key)
			{
				const TerrainInfo* data = static_cast<const TerrainInfo*>(info->m_data);
				setUniform(world_matrix_location, data->m_world_matrix);
				setUniform(morph_const_location, data->m_morph_const);
				setUniform(quad_size_location, data->m_size);
				setUniform(quad_min_location, data->m_min);
				++m_draw_calls_count;
				m_vertices_count += mesh_part_indices_count;
				renderGeometry(mesh_part_indices_count * data->m_index, mesh_part_indices_count);
				++info;
			}
			return info;*/
		}


		const RenderableInfo* renderLoopGrass(const RenderableInfo* info)
		{
			TODO("bgfx");
			return nullptr;
			/*const int COPY_COUNT = 50;
			int64_t last_key = info->m_key;
			while (last_key == info->m_key)
			{
				const Terrain::GrassPatch* patch = static_cast<const Terrain::GrassPatch*>(info->m_data);
				const Mesh& mesh = *patch->m_type->m_grass_mesh;
				Shader* shader = mesh.getMaterial()->getShader();

				for (int j = 0; j < patch->m_matrices.size() / COPY_COUNT; ++j)
				{
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::GRASS_MATRICES, &patch->m_matrices[j * COPY_COUNT], COPY_COUNT);
					++m_draw_calls_count;
					m_vertices_count += mesh.getIndexCount();
					renderGeometry(mesh.getIndicesOffset(), mesh.getIndexCount());
				}
				if (patch->m_matrices.size() % 50 != 0)
				{
					setFixedCachedUniform(*m_renderer, *shader, (int)Shader::FixedCachedUniforms::GRASS_MATRICES, &patch->m_matrices[(patch->m_matrices.size() / COPY_COUNT) * COPY_COUNT], patch->m_matrices.size() % 50);
					++m_draw_calls_count;
					int vertices_count = mesh.getIndexCount() / COPY_COUNT * (patch->m_matrices.size() % 50);
					m_vertices_count += vertices_count;
					renderGeometry(mesh.getIndicesOffset(), vertices_count);
				}
				++info;
			}
			return info;*/
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


		void renderSkinnedMesh(const RenderableMesh& info)
		{
			TODO("bgfx");
			ASSERT(false);
		}

		void renderRigidMesh(const RenderableMesh& info)
		{
			if (!info.m_model->isReady())
			{
				return;
			}

			const Mesh& mesh = *info.m_mesh;
			const Model& model = *info.m_model;
			bgfx::setTransform(&info.m_matrix->m11);
			bgfx::setProgram(mesh.getMaterial()->getShaderInstance().m_program_handles[m_pass_idx]);
			for (int i = 0; i < mesh.getMaterial()->getTextureCount(); ++i)
			{
				Texture* texture = mesh.getMaterial()->getTexture(i);
				if (texture)
				{
					bgfx::setTexture(i, mesh.getMaterial()->getShader()->getTextureSlot(i).m_uniform_handle, texture->getTextureHandle());
				}
			}
			bgfx::setVertexBuffer(model.getGeometry().getAttributesArrayID(), mesh.getAttributeArrayOffset() / mesh.getVertexDefinition().getStride(), mesh.getAttributeArraySize() / mesh.getVertexDefinition().getStride());
			bgfx::setIndexBuffer(model.getGeometry().getIndicesArrayID(), mesh.getIndicesOffset(), mesh.getIndexCount());
			bgfx::setState(m_render_state | mesh.getMaterial()->getRenderStates());
			bgfx::submit(m_pass_idx);
		}


		void render(Array<RenderableInfo>* renderable_infos, const Component& light)
		{
			PROFILE_FUNCTION();
			if (renderable_infos->empty() || !m_active_camera.isValid())
			{
				return;
			}
			RenderableInfo& sentinel = renderable_infos->pushEmpty();
			sentinel.m_key = 0;
			const RenderableInfo* LUMIX_RESTRICT info = &(*renderable_infos)[0];
			const RenderableInfo* LUMIX_RESTRICT end = &(*renderable_infos)[0] + renderable_infos->size() - 1;
			setLightUniforms(light);
			while (info != end)
			{
				switch (info->m_type)
				{
					case (int32_t)RenderableType::RIGID_MESH:
						{
							const RenderableMesh* mesh = static_cast<const RenderableMesh*>(info->m_data);
							renderRigidMesh(*static_cast<const RenderableMesh*>(info->m_data));
						}
						break;
					case (int32_t)RenderableType::SKINNED_MESH:
						{
							const RenderableMesh* mesh = static_cast<const RenderableMesh*>(info->m_data);
							renderSkinnedMesh(*static_cast<const RenderableMesh*>(info->m_data));
						}
						break;
						TODO("bgfx");
					default:
						ASSERT(false);
						break;
				}
				++info;
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
			m_pass_idx = -1;
			m_draw_calls_count = 0;
			m_vertices_count = 0;

			if (lua_getglobal(m_source.m_lua_state, "render") == LUA_TFUNCTION)
			{
				lua_pushlightuserdata(m_source.m_lua_state, this);
				if (lua_pcall(m_source.m_lua_state, 1, 0, 0) != LUA_OK)
				{
					g_log_error.log("lua") << lua_tostring(m_source.m_lua_state, -1);
				}
			}

			m_frame_allocator.clear();
		}


		virtual FrameBuffer* getShadowmapFramebuffer() override
		{
			return m_shadowmap_framebuffer;
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
			bgfx::setDebug(wireframe ? BGFX_DEBUG_TEXT : BGFX_DEBUG_WIREFRAME | BGFX_DEBUG_TEXT);
		}


		int m_pass_idx;
		uint64_t m_render_state;
		IAllocator& m_allocator;
		Renderer& m_renderer;
		LIFOAllocator m_frame_allocator;
		PipelineImpl& m_source;
		RenderScene* m_scene;
		Array<FrameBuffer*> m_framebuffers;
		FrameBuffer* m_shadowmap_framebuffer;
		Matrix m_shadow_modelviewprojection[4];
		Vec4 m_shadowmap_splits;
		int m_width;
		int m_height;
		int m_framebuffer_width;
		int m_framebuffer_height;
		AssociativeArray<uint32_t, CustomCommandHandler> m_custom_commands_handlers;
		Component m_active_camera;
		Array<TerrainInfo> m_terrain_infos;
		Array<GrassInfo> m_grass_infos;
		Array<RenderableInfo> m_renderable_infos;
		bgfx::UniformHandle m_light_pos_radius_uniform;
		bgfx::UniformHandle m_light_color_uniform;
		bgfx::UniformHandle m_ambient_color_uniform;
		bgfx::UniformHandle m_light_dir_fov_uniform;
		int m_draw_calls_count;
		int m_vertices_count;

	private:
		void operator=(const PipelineInstanceImpl&);
		PipelineInstanceImpl(const PipelineInstanceImpl&);
	};


	Pipeline::Pipeline(const Path& path, ResourceManager& resource_manager, IAllocator& allocator)
		: Resource(path, resource_manager, allocator)
	{
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
					bgfx::setViewRect(pipeline->m_pass_idx, 0, 0, (uint16_t)pipeline->m_framebuffer_width, (uint16_t)pipeline->m_framebuffer_height);
				}
				else
				{
					bgfx::setViewRect(pipeline->m_pass_idx, 0, 0, (uint16_t)pipeline->m_width, (uint16_t)pipeline->m_height);
				}

				pipeline->m_scene->setCameraSize(cmp, pipeline->m_width, pipeline->m_height);
				pipeline->m_scene->applyCamera(cmp);
				
				Matrix view_matrix, projection_matrix;
				float fov = pipeline->getScene()->getCameraFOV(cmp);
				float near_plane = pipeline->getScene()->getCameraNearPlane(cmp);
				float far_plane = pipeline->getScene()->getCameraFarPlane(cmp);
				projection_matrix.setPerspective(Math::degreesToRadians(fov), pipeline->m_width, pipeline->m_height, near_plane, far_plane);

				Matrix mtx = cmp.entity.getMatrix();
				Vec3 pos = mtx.getTranslation();
				Vec3 center = pos - mtx.getZVector();
				Vec3 up = mtx.getYVector();
				view_matrix.lookAt(pos, center, up);

				bgfx::setViewTransform(pipeline->m_pass_idx, &view_matrix.m11, &projection_matrix.m11);
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
			bgfx::setViewClear(pipeline->m_pass_idx, flags, 0x303030ff, 1.0f, 0);
			bgfx::submit(pipeline->m_pass_idx);
		}


		void renderModels(PipelineInstanceImpl* pipeline, int64_t layer_mask, bool is_point_light_render)
		{
			if (is_point_light_render)
			{
				TODO("bgfx");
				/*
				glEnable(GL_BLEND);
				glBlendFunc(GL_ONE, GL_ONE);
				*/

				pipeline->renderPointLightInfluencedGeometry(pipeline->getScene()->getFrustum(), layer_mask);

				//glDisable(GL_BLEND);
			}
			else
			{
				pipeline->renderModels(pipeline->getScene()->getFrustum(), layer_mask, false);
			}
		}


		void bindFramebufferTexture(PipelineInstanceImpl* pipeline, const char* framebuffer_name, int renderbuffer_index, const char* uniform)
		{
			TODO("bgfx");
			/*
			FrameBuffer* fb = pipeline->getFrameBuffer(framebuffer_name);
			if (fb)
			{
				pipeline->addGlobalTexture(fb->getTexture(renderbuffer_index), uniform);
			}
			*/
		}


		void executeCustomCommand(PipelineInstanceImpl* pipeline, const char* command)
		{
			pipeline->executeCustomCommand(crc32(command));
		}


		void renderDebugLines(PipelineInstanceImpl* pipeline)
		{
			pipeline->renderDebugLines();
		}


		void renderDebugTexts(PipelineInstanceImpl* pipeline)
		{
			pipeline->renderDebugTexts();
		}


		void renderShadowmap(PipelineInstanceImpl* pipeline, int64_t layer_mask, const char* slot)
		{
			pipeline->renderShadowmap(pipeline->getScene()->getCameraInSlot(slot), layer_mask);
		}


		void bindFramebuffer(PipelineInstanceImpl* pipeline, const char* buffer_name)
		{
			TODO("bgfx");
			/*FrameBuffer* fb = pipeline->getFrameBuffer(buffer_name);
			if (fb)
			{
				fb->bind();
				pipeline->m_framebuffer_width = fb->getWidth();
				pipeline->m_framebuffer_height = fb->getHeight();
			}*/
		}


		void unbindFramebuffer(PipelineInstanceImpl* pipeline)
		{
			TODO("bgfx");
			/*
			FrameBuffer::unbind();
			pipeline->m_framebuffer_width = pipeline->m_framebuffer_height = -1;
			*/
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
			template <typename... ArgsF, typename... Args>
			static LUMIX_FORCE_INLINE void callFunction(void(*f)(ArgsF...), lua_State* L, Args... args)
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
					return;
				}
				T a = toType<T>(L, sizeof...(ArgsF)-N + 1);
				FunctionCaller<N - 1>::callFunction(f, L, args..., a);
			}
		};


		template <>
		struct FunctionCaller<0>
		{
			template < typename... ArgsF, typename... Args >
			static LUMIX_FORCE_INLINE void callFunction(void(*f)(ArgsF...), lua_State*, Args... args)
			{
				f(args...);
			}
		};


		template <typename... ArgsF>
		void LUMIX_FORCE_INLINE callFunction(void(*f)(ArgsF...), lua_State* L)
		{
			FunctionCaller<sizeof...(ArgsF)>::callFunction(f, L);
		}


		template <typename T, T t>
		int wrap(lua_State* L)
		{
			callFunction(t, L);
			return 0;
		}


	} // namespace LuaWrapper


	void PipelineImpl::registerCFunctions()
	{
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
		registerCFunction("renderDebugTexts", LuaWrapper::wrap<decltype(&LuaAPI::renderDebugTexts), LuaAPI::renderDebugTexts>);
		registerCFunction("bindFramebuffer", LuaWrapper::wrap<decltype(&LuaAPI::bindFramebuffer), LuaAPI::bindFramebuffer>);
		registerCFunction("unbindFramebuffer", LuaWrapper::wrap<decltype(&LuaAPI::unbindFramebuffer), LuaAPI::unbindFramebuffer>);
	}


} // ~namespace Lumix
