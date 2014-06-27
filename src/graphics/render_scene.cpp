#include "render_scene.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/event_manager.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/iserializer.h"
#include "core/log.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/texture.h"
#include "universe/component_event.h"
#include "universe/entity_moved_event.h"
#include "universe/universe.h"


namespace Lumix
{

	static const uint32_t RENDERABLE_HASH = crc32("renderable");
	static const uint32_t LIGHT_HASH = crc32("light");
	static const uint32_t CAMERA_HASH = crc32("camera");
	static const uint32_t TERRAIN_HASH = crc32("terrain");

	struct Terrain
	{
		Terrain()
			: m_mesh(NULL)
			, m_material(NULL)
		{}

		~Terrain()
		{
			LUMIX_DELETE(m_mesh);
			if (m_material)
			{
				m_material->getResourceManager().get(ResourceManager::MATERIAL)->unload(*m_material);
			}
			if (m_heightmap)
			{
				m_heightmap->getResourceManager().get(ResourceManager::TEXTURE)->unload(*m_heightmap);
				m_heightmap->getObserverCb().unbind<Terrain, &Terrain::heightmapLoaded>(this);
			}
		}

		void generateGeometry()
		{
			LUMIX_DELETE(m_mesh);
			m_mesh = NULL;
			struct Vertex
			{
				Vec3 pos;
				float u, v;
				float u2, v2;
				Vec3 normal;
			};
			m_width = m_heightmap->getWidth();
			m_height = m_heightmap->getHeight();
			Array<Vertex> points;
			points.resize(m_width * m_height * 4);
			Array<int32_t> indices;
			indices.resize((m_width - 1) * (m_height - 1) * 6);
			int indices_offset = 0;
			const uint8_t* data = m_heightmap->getData();
			int bytes_per_pixel = m_heightmap->getBytesPerPixel();
			for (int j = 0; j < m_height - 1; ++j)
			{
				for (int i = 0; i < m_width - 1; ++i)
				{
					int idx = 4 * (i + j * m_width);
					int data_idx = i + j * m_width;
					points[idx].pos.set((float)(i) / 5, data[data_idx * bytes_per_pixel] / 20.0f - 255 / 20.0f, (float)(j) / 5);
					points[idx].u2 = 0;
					points[idx].v2 = 0;
					points[idx].u = i / (float)m_width;
					points[idx].v = j / (float)m_height;
					points[idx+1].pos.set((float)(i+1) / 5, data[(data_idx + 1)* bytes_per_pixel] / 20.0f - 255 / 20.0f, (float)(j) / 5);
					points[idx+1].u2 = 1;
					points[idx+1].v2 = 0;
					points[idx+1].u = (i+1) / (float)m_width;
					points[idx+1].v = j / (float)m_height;
					points[idx+2].pos.set((float)(i+1) / 5, data[(data_idx + 1 + m_width)* bytes_per_pixel] / 20.0f - 255 / 20.0f, (float)(j+1) / 5);
					points[idx+2].u2 = 1;
					points[idx+2].v2 = 1;
					points[idx+2].u = (i+1) / (float)m_width;
					points[idx+2].v = (j+1) / (float)m_height;
					points[idx+3].pos.set((float)(i) / 5, data[(data_idx + m_width) * bytes_per_pixel] / 20.0f - 255 / 20.0f, (float)(j+1) / 5);
					points[idx+3].u2 = 0;
					points[idx+3].v2 = 1;
					points[idx+3].u = i / (float)m_width;
					points[idx+3].v = (j+1) / (float)m_height;
					
					indices[indices_offset] = idx;
					indices[indices_offset + 1] = idx + 3;
					indices[indices_offset + 2] = idx + 2;
					indices[indices_offset + 3] = idx;
					indices[indices_offset + 4] = idx + 2;
					indices[indices_offset + 5] = idx + 1;
					indices_offset += 6;
				}
			}
			/*for (int j = 1; j < m_height - 1; ++j)
			{
				for (int i = 1; i < m_width - 1; ++i)
				{
					int idx = i + j * m_width;
					Vec3 n = crossProduct(points[idx + 1].pos - points[idx - 1].pos, points[idx - m_width].pos - points[idx + m_width].pos);
					n.normalize();
					points[idx].normal = n;
				}
			}*/
			VertexDef vertex_def;
			vertex_def.parse("ptf2n", 5);
			m_geometry.copy((const uint8_t*)&points[0], sizeof(points[0]) * points.size(), indices, vertex_def);
			m_mesh = LUMIX_NEW(Mesh)(m_material, 0, indices.size(), "terrain");
		}

		void heightmapLoaded(Resource::State, Resource::State new_state)
		{
			if (new_state == Resource::State::READY)
			{
				generateGeometry();
			}
		}

		int32_t m_width;
		int32_t m_height;
		Geometry m_geometry;
		Material* m_material;
		Mesh* m_mesh;
		Matrix m_matrix;
		Entity m_entity;
		int64_t m_layer_mask;
		Texture* m_heightmap;
	};

	struct Renderable
	{
		Renderable() {}

		ModelInstance m_model;
		Entity m_entity;
		int64_t m_layer_mask;
		float m_scale;

		private:
			Renderable(const Renderable&) {}
			void operator =(const Renderable&) {}
	};

	struct Light
	{
		enum class Type : int32_t
		{
			DIRECTIONAL
		};

		Type m_type;
		Entity m_entity;
	};

	struct Camera
	{
		static const int MAX_SLOT_LENGTH = 30;

		Entity m_entity;
		float m_fov;
		float m_aspect;
		float m_near;
		float m_far;
		float m_width;
		float m_height;
		bool m_is_active;
		char m_slot[MAX_SLOT_LENGTH + 1];
	};

	class RenderSceneImpl : public RenderScene
	{
		public:
			RenderSceneImpl(Engine& engine, Universe& universe)
				: m_engine(engine)
				, m_universe(universe)
			{
				m_universe.getEventManager().addListener(EntityMovedEvent::type).bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
				m_timer = Timer::create();
			}

			~RenderSceneImpl()
			{
				EventManager::Listener cb;
				cb.bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
				m_universe.getEventManager().removeListener(EntityMovedEvent::type, cb);
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					LUMIX_DELETE(m_terrains[i]);
				}
				Timer::destroy(m_timer);
			}

			virtual void getRay(Component camera, float x, float y, Vec3& origin, Vec3& dir) override
			{
				Vec3 camera_pos = camera.entity.getPosition();
				float width = m_cameras[camera.index].m_width;
				float height = m_cameras[camera.index].m_height;
				float nx = 2 * (x / width) - 1;
				float ny = 2 * ((height - y) / height) - 1;
				Matrix projection_matrix = getProjectionMatrix(camera);
				Matrix view_matrix = camera.entity.getMatrix();
				view_matrix.inverse();
				Matrix inverted = (projection_matrix * view_matrix);
				inverted.inverse();
				Vec4 p0 = inverted * Vec4(nx, ny, -1, 1);
				Vec4 p1 = inverted * Vec4(nx, ny, 1, 1);
				p0.x /= p0.w; p0.y /= p0.w; p0.z /= p0.w;
				p1.x /= p1.w; p1.y /= p1.w; p1.z /= p1.w;
				origin = camera_pos;
				dir.x = p1.x - p0.x;
				dir.y = p1.y - p0.y;
				dir.z = p1.z - p0.z;
				dir.normalize();
			}

			virtual void applyCamera(Component cmp) override
			{
				Matrix mtx;
				cmp.entity.getMatrix(mtx);
				float fov = m_cameras[cmp.index].m_fov;
				float width = m_cameras[cmp.index].m_width;
				float height = m_cameras[cmp.index].m_height;
				float near_plane = m_cameras[cmp.index].m_near;
				float far_plane = m_cameras[cmp.index].m_far;
				m_engine.getRenderer().setProjection(width, height, fov, near_plane, far_plane, mtx);
			}

			Matrix getProjectionMatrix(Component cmp)
			{
				float fov = m_cameras[cmp.index].m_fov;
				float width = m_cameras[cmp.index].m_width;
				float height = m_cameras[cmp.index].m_height;
				float near_plane = m_cameras[cmp.index].m_near;
				float far_plane = m_cameras[cmp.index].m_far;
				
				Matrix mtx;
				mtx = Matrix::IDENTITY;
				float f = 1 / tanf(Math::degreesToRadians(fov) * 0.5f);
				mtx.m11 = f / (width / height);
				mtx.m22 = f;
				mtx.m33 = (far_plane + near_plane) / (near_plane - far_plane);
				mtx.m44 = 0;
				mtx.m43 = (2 * far_plane * near_plane) / (near_plane - far_plane);
				mtx.m34 = -1;

				return mtx;
			}

			void update(float dt) override
			{
				for (int i = m_debug_lines.size() - 1; i >= 0; --i)
				{
					float life = m_debug_lines[i].m_life;
					life -= dt;
					if (life < 0)
					{
						m_debug_lines.eraseFast(i);
					}
				}
			}

			void serializeCameras(ISerializer& serializer)
			{
				serializer.serialize("camera_count", m_cameras.size());
				serializer.beginArray("cameras");
				for (int i = 0; i < m_cameras.size(); ++i)
				{
					serializer.serializeArrayItem(m_cameras[i].m_far);
					serializer.serializeArrayItem(m_cameras[i].m_near);
					serializer.serializeArrayItem(m_cameras[i].m_fov);
					serializer.serializeArrayItem(m_cameras[i].m_is_active);
					serializer.serializeArrayItem(m_cameras[i].m_width);
					serializer.serializeArrayItem(m_cameras[i].m_height);
					serializer.serializeArrayItem(m_cameras[i].m_entity.index);
					serializer.serializeArrayItem(m_cameras[i].m_slot);
				}
				serializer.endArray();
			}

			void serializeLights(ISerializer& serializer)
			{
				serializer.serialize("light_count", m_lights.size());
				serializer.beginArray("lights");
				for (int i = 0; i < m_lights.size(); ++i)
				{
					serializer.serializeArrayItem(m_lights[i].m_entity.index);
					serializer.serializeArrayItem((int32_t)m_lights[i].m_type);
				}
				serializer.endArray();
			}

			void serializeRenderables(ISerializer& serializer)
			{
				serializer.serialize("renderable_count", m_renderables.size());
				serializer.beginArray("renderables");
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					serializer.serializeArrayItem(m_renderables[i].m_entity.index);
					if (m_renderables[i].m_model.getModel())
					{
						serializer.serializeArrayItem(m_renderables[i].m_model.getModel()->getPath().c_str());
					}
					else
					{
						serializer.serializeArrayItem("");
					}
					serializer.serializeArrayItem(m_renderables[i].m_scale);
					Matrix mtx = m_renderables[i].m_model.getMatrix();
					for (int j = 0; j < 16; ++j)
					{
						serializer.serializeArrayItem((&mtx.m11)[j]);
					}
				}
				serializer.endArray();
			}

			void serializeTerrains(ISerializer& serializer)
			{
				serializer.serialize("terrain_count", m_terrains.size());
				serializer.beginArray("terrains");
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					serializer.serializeArrayItem(m_terrains[i]->m_entity.index);
					serializer.serializeArrayItem(m_terrains[i]->m_layer_mask);
					serializer.serializeArrayItem(m_terrains[i]->m_heightmap->getPath().c_str());
					serializer.serializeArrayItem(m_terrains[i]->m_material->getPath().c_str());
				}
				serializer.endArray();
			}

			virtual void serialize(ISerializer& serializer) override
			{
				serializeCameras(serializer);
				serializeRenderables(serializer);
				serializeLights(serializer);
				serializeTerrains(serializer);
			}

			void deserializeCameras(ISerializer& serializer)
			{
				int32_t size;
				serializer.deserialize("camera_count", size);
				serializer.deserializeArrayBegin("cameras");
				m_cameras.resize(size);
				for (int i = 0; i < size; ++i)
				{
					serializer.deserializeArrayItem(m_cameras[i].m_far);
					serializer.deserializeArrayItem(m_cameras[i].m_near);
					serializer.deserializeArrayItem(m_cameras[i].m_fov);
					serializer.deserializeArrayItem(m_cameras[i].m_is_active);
					serializer.deserializeArrayItem(m_cameras[i].m_width);
					serializer.deserializeArrayItem(m_cameras[i].m_height);
					m_cameras[i].m_aspect = m_cameras[i].m_width / m_cameras[i].m_height;
					serializer.deserializeArrayItem(m_cameras[i].m_entity.index);
					m_cameras[i].m_entity.universe = &m_universe;
					serializer.deserializeArrayItem(m_cameras[i].m_slot, Camera::MAX_SLOT_LENGTH);
					m_universe.addComponent(m_cameras[i].m_entity, CAMERA_HASH, this, i);
				}
				serializer.deserializeArrayEnd();
			}

			void deserializeRenderables(ISerializer& serializer)
			{
				int32_t size;
				serializer.deserialize("renderable_count", size);
				serializer.deserializeArrayBegin("renderables");
				m_renderables.resize(size);
				for (int i = 0; i < size; ++i)
				{
					serializer.deserializeArrayItem(m_renderables[i].m_entity.index);
					m_renderables[i].m_entity.universe = &m_universe;
					char path[LUMIX_MAX_PATH];
					serializer.deserializeArrayItem(path, LUMIX_MAX_PATH);
					serializer.deserializeArrayItem(m_renderables[i].m_scale);
					m_renderables[i].m_model.setModel(static_cast<Model*>(m_engine.getResourceManager().get(ResourceManager::MODEL)->load(path)));
					for (int j = 0; j < 16; ++j)
					{
						serializer.deserializeArrayItem((&m_renderables[i].m_model.getMatrix().m11)[j]);
					}
					m_universe.addComponent(m_renderables[i].m_entity, RENDERABLE_HASH, this, i);
				}
				serializer.deserializeArrayEnd();
			}

			void deserializeLights(ISerializer& serializer)
			{
				int32_t size;
				serializer.deserialize("light_count", size);
				serializer.deserializeArrayBegin("lights");
				m_lights.resize(size);
				for (int i = 0; i < size; ++i)
				{
					serializer.deserializeArrayItem(m_lights[i].m_entity.index);
					m_lights[i].m_entity.universe = &m_universe;
					serializer.deserializeArrayItem((int32_t&)m_lights[i].m_type);
					m_universe.addComponent(m_lights[i].m_entity, LIGHT_HASH, this, i);
				}
				serializer.deserializeArrayEnd();
			}

			void deserializeTerrains(ISerializer& serializer)
			{
				int32_t size;
				serializer.deserialize("terrain_count", size);
				serializer.deserializeArrayBegin("terrains");
				for (int i = 0; i < size; ++i)
				{
					Entity e;
					serializer.deserializeArrayItem(e.index);
					e.universe = &m_universe;
					Component cmp = createComponent(TERRAIN_HASH, e);
					Terrain* terrain = m_terrains[cmp.index];
					serializer.deserializeArrayItem(terrain->m_layer_mask);
					char path[LUMIX_MAX_PATH];
					serializer.deserializeArrayItem(path, LUMIX_MAX_PATH);
					setTerrainHeightmap(cmp, string(path));
					serializer.deserializeArrayItem(path, LUMIX_MAX_PATH);
					setTerrainMaterial(cmp, string(path));
				}
				serializer.deserializeArrayEnd();
			}

			virtual void deserialize(ISerializer& serializer) override
			{
				deserializeCameras(serializer);
				deserializeRenderables(serializer);
				deserializeLights(serializer);
				deserializeTerrains(serializer);
			}

			virtual Component createComponent(uint32_t type, const Entity& entity) override
			{
				if (type == TERRAIN_HASH)
				{
					Terrain* terrain = LUMIX_NEW(Terrain);
					m_terrains.push(terrain);
					terrain->m_width = 0;
					terrain->m_height = 0;
					terrain->m_matrix = entity.getMatrix();
					terrain->m_entity = entity;
					terrain->m_layer_mask = 1;
					terrain->m_heightmap = NULL;
					Component cmp(entity, type, this, m_terrains.size() - 1);
					ComponentEvent evt(cmp);
					m_universe.getEventManager().emitEvent(evt);
					return cmp;

				}
				else if (type == CAMERA_HASH)
				{
					Camera& camera = m_cameras.pushEmpty();
					camera.m_is_active = false;
					camera.m_entity = entity;
					camera.m_fov = 60;
					camera.m_width = 800;
					camera.m_height = 600;
					camera.m_aspect = 800.0f / 600.0f;
					camera.m_near = 0.1f;
					camera.m_far = 1000.0f;
					camera.m_slot[0] = '\0';
					Component cmp(entity, type, this, m_cameras.size() - 1);
					ComponentEvent evt(cmp);
					m_universe.getEventManager().emitEvent(evt);
					return cmp;
				}
				else if (type == RENDERABLE_HASH)
				{
					Renderable& r = m_renderables.pushEmpty();
					r.m_entity = entity;
					r.m_layer_mask = 1;
					r.m_scale = 1;
					r.m_model.setModel(NULL);
					Component cmp(entity, type, this, m_renderables.size() - 1);
					ComponentEvent evt(cmp);
					m_universe.getEventManager().emitEvent(evt);
					return Component(entity, type, this, m_renderables.size() - 1);
				}
				else if (type == LIGHT_HASH)
				{
					Light& light = m_lights.pushEmpty();
					light.m_type = Light::Type::DIRECTIONAL;
					light.m_entity = entity;
					Component cmp(entity, type, this, m_lights.size() - 1);
					ComponentEvent evt(cmp);
					m_universe.getEventManager().emitEvent(evt);
					return Component(entity, type, this, m_lights.size() - 1);
				}
				ASSERT(false);
				return Component::INVALID;
			}

			void onEntityMoved(Event& evt)
			{
				EntityMovedEvent e = static_cast<EntityMovedEvent&>(evt);
				const Entity::ComponentList& cmps = e.entity.getComponents();
				for (int i = 0; i < cmps.size(); ++i)
				{
					if (cmps[i].type == RENDERABLE_HASH)
					{
						m_renderables[cmps[i].index].m_model.setMatrix(e.entity.getMatrix());
						break;
					}
					else if (cmps[i].type == TERRAIN_HASH)
					{
						m_terrains[cmps[i].index]->m_matrix = e.entity.getMatrix();
						break;
					}
				}
			}

			virtual void setTerrainMaterial(Component cmp, const string& path) override
			{
				if (m_terrains[cmp.index]->m_material)
				{
					m_engine.getResourceManager().get(ResourceManager::MATERIAL)->unload(*m_terrains[cmp.index]->m_material);
				}
				m_terrains[cmp.index]->m_material = static_cast<Material*>(m_engine.getResourceManager().get(ResourceManager::MATERIAL)->load(path.c_str()));
				if (m_terrains[cmp.index]->m_mesh)
				{
					m_terrains[cmp.index]->m_mesh->setMaterial(m_terrains[cmp.index]->m_material);
				}
			}

			virtual void getTerrainMaterial(Component cmp, string& path) override
			{
				if (m_terrains[cmp.index]->m_material)
				{
					path = m_terrains[cmp.index]->m_material->getPath().c_str();
				}
				else
				{
					path = "";
				}
			}

			virtual void setTerrainHeightmap(Component cmp, const string& path) override
			{
				if (m_terrains[cmp.index]->m_heightmap)
				{
					m_engine.getResourceManager().get(ResourceManager::TEXTURE)->unload(*m_terrains[cmp.index]->m_heightmap);
					m_terrains[cmp.index]->m_heightmap->getObserverCb().unbind<Terrain, &Terrain::heightmapLoaded>(m_terrains[cmp.index]);
				}
				Resource* res = m_engine.getResourceManager().get(ResourceManager::TEXTURE)->load(path.c_str());
				res->getObserverCb().bind<Terrain, &Terrain::heightmapLoaded>(m_terrains[cmp.index]);
				m_terrains[cmp.index]->m_heightmap = static_cast<Texture*>(res);
				m_terrains[cmp.index]->m_heightmap->setNonGL(true);
				if (m_terrains[cmp.index]->m_heightmap->isReady())
				{
					m_terrains[cmp.index]->heightmapLoaded(Resource::State::LOADING, Resource::State::READY);
				}
			}

			virtual void getTerrainHeightmap(Component cmp, string& path) override
			{
				path = m_terrains[cmp.index]->m_heightmap ? m_terrains[cmp.index]->m_heightmap->getPath().c_str() : "";
			}

			virtual Pose& getPose(const Component& cmp) override
			{
				return m_renderables[cmp.index].m_model.getPose();
			}

			virtual Model* getModel(Component cmp) override
			{
				return m_renderables[cmp.index].m_model.getModel();
			}

			virtual void getRenderablePath(Component cmp, string& path) override
			{
					if (m_renderables[cmp.index].m_model.getModel())
					{
						path = m_renderables[cmp.index].m_model.getModel()->getPath().c_str();
					}
					else
					{
						path = "";
					}
			}

			virtual void setRenderableLayer(Component cmp, const int32_t& layer) override
			{
				m_renderables[cmp.index].m_layer_mask = ((int64_t)1 << (int64_t)layer);
			}

			virtual void setRenderableScale(Component cmp, const float& scale) override
			{
				m_renderables[cmp.index].m_scale = scale;
			}

			virtual void setRenderablePath(Component cmp, const string& path) override
			{
				Renderable& r = m_renderables[cmp.index];
				Model* model = static_cast<Model*>(m_engine.getResourceManager().get(ResourceManager::MODEL)->load(path));
				r.m_model.setModel(model);
				r.m_model.setMatrix(r.m_entity.getMatrix());
			}

			virtual void getRenderableInfos(Array<RenderableInfo>& infos, int64_t layer_mask) override
			{
				infos.reserve(m_renderables.size() * 2);
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					if (m_renderables[i].m_model.getModel() && (m_renderables[i].m_layer_mask & layer_mask) != 0)
					{
						for (int j = 0, c = m_renderables[i].m_model.getModel()->getMeshCount(); j < c; ++j)
						{
							if (m_renderables[i].m_model.getModel()->getMesh(j).getMaterial()->isReady())
							{
								RenderableInfo& info = infos.pushEmpty();
								info.m_scale = m_renderables[i].m_scale;
								info.m_geometry = m_renderables[i].m_model.getModel()->getGeometry();
								info.m_mesh = &m_renderables[i].m_model.getModel()->getMesh(j);
								info.m_pose = &m_renderables[i].m_model.getPose();
								info.m_model = &m_renderables[i].m_model;
								info.m_matrix = &m_renderables[i].m_model.getMatrix();
							}
						}
					}
				}
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					bool is_in_wanted_layer = (m_terrains[i]->m_layer_mask & layer_mask) != 0;
					if (m_terrains[i]->m_mesh && m_terrains[i]->m_mesh->getMaterial() && m_terrains[i]->m_mesh->getMaterial()->isReady() && is_in_wanted_layer)
					{
						RenderableInfo& info = infos.pushEmpty();
						info.m_scale = 1;
						info.m_geometry = &m_terrains[i]->m_geometry;
						info.m_mesh = m_terrains[i]->m_mesh;
						info.m_pose = NULL;
						info.m_model = NULL;
						info.m_matrix = &m_terrains[i]->m_matrix;
					}
				}
			}

			virtual void setCameraSlot(Component camera, const string& slot) override
			{
				if (slot.length() > Camera::MAX_SLOT_LENGTH)
				{
					strncpy(m_cameras[camera.index].m_slot, slot.c_str(), Camera::MAX_SLOT_LENGTH);
				}
				else
				{
					strcpy(m_cameras[camera.index].m_slot, slot.c_str());
				}
			}

			virtual void getCameraSlot(Component camera, string& slot) override
			{
				slot = m_cameras[camera.index].m_slot;
			}

			virtual void getCameraFOV(Component camera, float& fov) override
			{
				fov = m_cameras[camera.index].m_fov;
			}

			virtual void setCameraFOV(Component camera, const float& fov) override
			{
				m_cameras[camera.index].m_fov = fov;
			}

			virtual void setCameraNearPlane(Component camera, const float& near_plane) override
			{
				m_cameras[camera.index].m_near = near_plane;
			}

			virtual void getCameraNearPlane(Component camera, float& near_plane) override
			{
				near_plane = m_cameras[camera.index].m_near;
			}

			virtual void setCameraFarPlane(Component camera, const float& far_plane) override
			{
				m_cameras[camera.index].m_far = far_plane;
			}

			virtual void getCameraFarPlane(Component camera, float& far_plane) override
			{
				far_plane = m_cameras[camera.index].m_far;
			}

			virtual void getCameraWidth(Component camera, float& width) override
			{
				width = m_cameras[camera.index].m_width;
			}

			virtual void getCameraHeight(Component camera, float& height) override
			{
				height = m_cameras[camera.index].m_height;
			}

			virtual void setCameraSize(Component camera, int w, int h) override
			{
				m_cameras[camera.index].m_width = (float)w;
				m_cameras[camera.index].m_height = (float)h;
				m_cameras[camera.index].m_aspect = w / (float)h;
			}

			virtual const Array<DebugLine>& getDebugLines() const override
			{
				return m_debug_lines;
			}

			virtual void addDebugLine(const Vec3& from, const Vec3& to, const Vec3& color, float life) override
			{
				DebugLine& line = m_debug_lines.pushEmpty();
				line.m_from = from;
				line.m_to = to;
				line.m_color = color;
				line.m_life = life;
			}

			virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir) override
			{
				RayCastModelHit hit;
				hit.m_is_hit = false;
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					if (m_renderables[i].m_model.getModel())
					{
						const Vec3& pos = m_renderables[i].m_model.getMatrix().getTranslation();
						float radius = m_renderables[i].m_model.getModel()->getBoundingRadius();
						float scale = m_renderables[i].m_scale;
						Vec3 intersection;
						if (dotProduct(pos - origin, pos - origin) < radius * radius || Math::getRaySphereIntersection(pos, radius * scale, origin, dir, intersection))
						{
							RayCastModelHit new_hit = m_renderables[i].m_model.getModel()->castRay(origin, dir, m_renderables[i].m_model.getMatrix(), scale);
							if (new_hit.m_is_hit && (!hit.m_is_hit || new_hit.m_t < hit.m_t))
							{
								new_hit.m_renderable = Component(m_renderables[i].m_entity, crc32("renderable"), this, i);
								hit = new_hit;
								hit.m_is_hit = true;
							}
						}
					}
				}
				return hit;
			}

			virtual Component getLight(int index) override
			{
				if (index >= m_lights.size())
				{
					return Component::INVALID;
				}
				return Component(m_lights[index].m_entity, crc32("light"), this, index);
			};

			virtual Component getCameraInSlot(const char* slot) override
			{
				for (int i = 0, c = m_cameras.size(); i < c; ++i)
				{
					if (strcmp(m_cameras[i].m_slot, slot) == 0)
					{
						return Component(m_cameras[i].m_entity, CAMERA_HASH, this, i);
					}
				}
				return Component::INVALID;
			}

			virtual Timer* getTimer() const override
			{
				return m_timer;
			}

		private:
			Array<Renderable> m_renderables;
			Array<Light> m_lights;
			Array<Camera> m_cameras;
			Array<Terrain*> m_terrains;
			Universe& m_universe;
			Engine& m_engine;
			Array<DebugLine> m_debug_lines;
			Timer* m_timer;
	};


	RenderScene* RenderScene::createInstance(Engine& engine, Universe& universe)
	{
		return LUMIX_NEW(RenderSceneImpl)(engine, universe);
	}


	void RenderScene::destroyInstance(RenderScene* scene)
	{
		LUMIX_DELETE(scene);
	}

}