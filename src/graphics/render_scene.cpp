#include "render_scene.h"

#include "core/array.h"
#include "core/blob.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/json_serializer.h"
#include "core/lifo_allocator.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/mtjd/generic_job.h"
#include "core/mtjd/job.h"
#include "core/mtjd/manager.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "core/sphere.h"
#include "core/frustum.h"

#include "engine/engine.h"

#include "graphics/bitmap_font.h"
#include "graphics/culling_system.h"
#include "graphics/geometry.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/pipeline.h"
#include "graphics/renderer.h"
#include "graphics/shader.h"
#include "graphics/terrain.h"
#include "graphics/texture.h"

#include "universe/universe.h"


namespace Lumix
{

	static const uint32_t RENDERABLE_HASH = crc32("renderable");
	static const uint32_t POINT_LIGHT_HASH = crc32("point_light");
	static const uint32_t GLOBAL_LIGHT_HASH = crc32("global_light");
	static const uint32_t CAMERA_HASH = crc32("camera");
	static const uint32_t TERRAIN_HASH = crc32("terrain");


	class DebugText
	{
		public:
			DebugText(IAllocator& allocator)
				: m_text(allocator)
				//, m_mesh(allocator)
			{}

		public:
			string m_text;
			int m_x;
			int m_y;
	};


	class DebugTextsData
	{
		public:
			DebugTextsData(Engine& engine, IAllocator& allocator)
				: m_texts(allocator)
				, m_allocator(allocator)
				, m_font(NULL)
				, m_engine(engine)
				, m_mesh(NULL)
			{
				setFont(Path("fonts/debug_font.fnt"));
			}


			~DebugTextsData()
			{
				if (m_font)
				{
					m_font->getResourceManager().get(ResourceManager::BITMAP_FONT)->unload(*m_font);
				}
				m_allocator.deleteObject(m_mesh);
			}

			int addText(const char* text, int x, int y)
			{
				int id = m_texts.size() == 0 ? 0 : m_texts.getKey(m_texts.size() - 1) + 1;
				DebugText debug_text(m_allocator);
				debug_text.m_x = x;
				debug_text.m_y = y;
				int index = m_texts.insert(id, debug_text);
				m_texts.at(index).m_text = text;
				return id;
			}


			void setText(int id, const char* text)
			{
				int index = m_texts.find(id);
				if (index < 0)
				{
					return;
				}
				if (m_texts.at(index).m_text != text)
				{
					m_texts.at(index).m_text = text;
				}
			}


			Geometry& getGeometry() { return m_geometry; }


			Mesh& getMesh() { return *m_mesh; }


			BitmapFont* getFont() const { return m_font; }


			void setFont(const Path& path)
			{
				m_font = static_cast<BitmapFont*>(m_engine.getResourceManager().get(ResourceManager::BITMAP_FONT)->load(path));
			}

			

			AssociativeArray<int, DebugText>& getTexts() { return m_texts; }

		private:
			IAllocator& m_allocator;
			Engine& m_engine;
			AssociativeArray<int, DebugText> m_texts;
			Geometry m_geometry;
			Mesh* m_mesh;
			BitmapFont* m_font;
	};


	struct Renderable
	{
		Renderable(IAllocator& allocator) : m_pose(allocator), m_meshes(allocator) {}

		Array<RenderableMesh> m_meshes;
		int32_t m_component_index;
		Pose m_pose;
		Model* m_model;
		Matrix m_matrix;
		Entity m_entity;
		float m_scale;
		bool m_is_always_visible;

		private:
			Renderable(const Renderable&);
	};

	
	struct PointLight
	{
		Vec4 m_color;
		float m_intensity;
		float m_range;
		Entity m_entity;
		int m_uid;
		float m_fov;
	};


	struct GlobalLight
	{
		int m_uid;
		Vec4 m_color;
		float m_intensity;
		Vec4 m_ambient_color;
		float m_ambient_intensity;
		Vec4 m_fog_color;
		float m_fog_density;
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
		bool m_is_free;
		char m_slot[MAX_SLOT_LENGTH + 1];
	};


	class RenderSceneImpl : public RenderScene
	{
		private:
			typedef HashMap<int32_t, int> DynamicRenderableCache;

			class ModelLoadedCallback
			{
				public:
					ModelLoadedCallback(RenderSceneImpl& scene, Model* model)
						: m_scene(scene)
						, m_ref_count(0)
						, m_model(model)
					{
						m_model->onLoaded<ModelLoadedCallback, &ModelLoadedCallback::callback>(this);
					}

					~ModelLoadedCallback()
					{
						m_model->getObserverCb().unbind<ModelLoadedCallback, &ModelLoadedCallback::callback>(this);
					}

					void callback(Resource::State, Resource::State new_state)
					{
						if(new_state == Resource::State::READY)
						{
							m_scene.modelLoaded(m_model);
						}
					}

					Model* m_model;
					int m_ref_count;
					RenderSceneImpl& m_scene;
			};

		public:
			RenderSceneImpl(Renderer& renderer, Engine& engine, Universe& universe, bool is_forward_rendered, IAllocator& allocator)
				: m_engine(engine)
				, m_universe(universe)
				, m_renderer(renderer)
				, m_allocator(allocator)
				, m_model_loaded_callbacks(m_allocator)
				, m_dynamic_renderable_cache(m_allocator)
				, m_renderables(m_allocator)
				, m_cameras(m_allocator)
				, m_terrains(m_allocator)
				, m_point_lights(m_allocator)
				, m_light_influenced_geometry(m_allocator)
				, m_global_lights(m_allocator)
				, m_debug_lines(m_allocator)
				, m_always_visible(m_allocator)
				, m_temporary_infos(m_allocator)
				, m_sync_point(true, m_allocator)
				, m_jobs(m_allocator)
				, m_debug_texts(engine, m_allocator)
				, m_active_global_light_uid(-1)
				, m_global_light_last_uid(-1)
				, m_point_light_last_uid(-1)
				, m_is_forward_rendered(is_forward_rendered)
				, m_applied_camera(Component::INVALID)
			{
				m_universe.entityMoved().bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
				m_culling_system = CullingSystem::create(m_engine.getMTJDManager(), m_allocator);
				m_time = 0;
			}


			~RenderSceneImpl()
			{
				m_universe.entityMoved().unbind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);

				for (int i = 0; i < m_model_loaded_callbacks.size(); ++i)
				{
					m_allocator.deleteObject(m_model_loaded_callbacks[i]);
				}

				for (int i = 0; i < m_terrains.size(); ++i)
				{
					m_allocator.deleteObject(m_terrains[i]);
				}

				for(int i = 0; i < m_renderables.size(); ++i)
				{
					if(m_renderables[i]->m_model)
					{
						m_renderables[i]->m_model->getResourceManager().get(ResourceManager::MODEL)->unload(*m_renderables[i]->m_model);
					}
					m_allocator.deleteObject(m_renderables[i]);
				}

				CullingSystem::destroy(*m_culling_system);
			}

			
			virtual bool ownComponentType(uint32_t type) const override
			{
				return type == RENDERABLE_HASH || type == POINT_LIGHT_HASH || type == GLOBAL_LIGHT_HASH || type == CAMERA_HASH || type == TERRAIN_HASH;
			}


			virtual IPlugin& getPlugin() const override
			{
				return m_renderer;
			}


			virtual void getRay(Component camera, float x, float y, Vec3& origin, Vec3& dir) override
			{
				Vec3 camera_pos = camera.entity.getPosition();
				float width = m_cameras[camera.index].m_width;
				float height = m_cameras[camera.index].m_height;
				float nx = 2 * (x / width) - 1;
				float ny = 2 * ((height - y) / height) - 1;

				float fov = m_cameras[camera.index].m_fov;
				float near_plane = m_cameras[camera.index].m_near;
				float far_plane = m_cameras[camera.index].m_far;

				Matrix projection_matrix;
				projection_matrix.setPerspective(Math::degreesToRadians(fov), width, height, near_plane, far_plane);
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

			virtual Component getAppliedCamera() override
			{
				return m_applied_camera;
			}
			
			virtual void applyCamera(Component cmp) override
			{
				m_applied_camera = cmp;
				Matrix mtx;
				cmp.entity.getMatrix(mtx);
				
				m_camera_frustum.computePerspective(
					mtx.getTranslation(),
					mtx.getZVector(),
					mtx.getYVector(),
					m_cameras[cmp.index].m_fov,
					m_cameras[cmp.index].m_width / m_cameras[cmp.index].m_height,
					m_cameras[cmp.index].m_near,
					m_cameras[cmp.index].m_far
					);
			}
			
			void update(float dt) override
			{
				m_time += dt;
				for (int i = m_debug_lines.size() - 1; i >= 0; --i)
				{
					float life = m_debug_lines[i].m_life;
					if (life < 0)
					{
						m_debug_lines.eraseFast(i);
					}
					else
					{
						life -= dt;
						m_debug_lines[i].m_life = life;
					}
				}
			}

			void serializeCameras(OutputBlob& serializer)
			{
				serializer.write((int32_t)m_cameras.size());
				for (int i = 0, c = m_cameras.size(); i < c; ++i)
				{
					Camera& camera = m_cameras[i];
					serializer.write(camera.m_entity.index);
					serializer.write(camera.m_far);
					serializer.write(camera.m_fov);
					serializer.write(camera.m_is_active);
					serializer.write(camera.m_is_free);
					serializer.write(camera.m_near);
					serializer.writeString(camera.m_slot);
				}
				
			}

			void serializeLights(OutputBlob& serializer)
			{
				serializer.write((int32_t)m_point_lights.size());
				for (int i = 0, c = m_point_lights.size(); i < c; ++i)
				{
					PointLight& point_light = m_point_lights[i];
					serializer.write(point_light.m_uid);
					serializer.write(point_light.m_color);
					serializer.write(point_light.m_intensity);
					serializer.write(point_light.m_entity.index);
					serializer.write(point_light.m_range);
					serializer.write(point_light.m_fov);
				}
				serializer.write(m_point_light_last_uid);

				serializer.write((int32_t)m_global_lights.size());
				for (int i = 0, c = m_global_lights.size(); i < c; ++i)
				{
					GlobalLight& global_light = m_global_lights[i];
					serializer.write(global_light.m_uid);
					serializer.write(global_light.m_color);
					serializer.write(global_light.m_intensity);
					serializer.write(global_light.m_entity.index);
					serializer.write(global_light.m_ambient_color);
					serializer.write(global_light.m_ambient_intensity);
					serializer.write(global_light.m_fog_color);
					serializer.write(global_light.m_fog_density);
				}
				serializer.write((int32_t)m_global_light_last_uid);
				serializer.write((int32_t)m_active_global_light_uid);
			}

			void serializeRenderables(OutputBlob& serializer)
			{
				serializer.write((int32_t)m_renderables.size());
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					serializer.write(m_renderables[i]->m_is_always_visible);
					serializer.write(m_renderables[i]->m_component_index);
					serializer.write(m_renderables[i]->m_entity.index);
					serializer.write(m_renderables[i]->m_scale);
					serializer.write(m_culling_system->getLayerMask(i));
					serializer.write(m_renderables[i]->m_model->getPath().getHash());
				}
			}

			void serializeTerrains(OutputBlob& serializer)
			{
				serializer.write((int32_t)m_terrains.size());
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					if(m_terrains[i])
					{
						serializer.write(true);
						m_terrains[i]->serialize(serializer);
					}
					else
					{
						serializer.write(false);
					}
				}
			}

			virtual void serialize(OutputBlob& serializer) override
			{
				serializeCameras(serializer);
				serializeRenderables(serializer);
				serializeLights(serializer);
				serializeTerrains(serializer);
			}

			void deserializeCameras(InputBlob& serializer)
			{
				int32_t size;
				serializer.read(size);
				m_cameras.resize(size);
				for (int i = 0; i < size; ++i)
				{
					Camera& camera = m_cameras[i];
					serializer.read(camera.m_entity.index);
					serializer.read(camera.m_far);
					serializer.read(camera.m_fov);
					serializer.read(camera.m_is_active);
					serializer.read(camera.m_is_free);
					serializer.read(camera.m_near);
					serializer.readString(camera.m_slot, sizeof(camera.m_slot));

					camera.m_entity.universe = &m_universe;
					if(!camera.m_is_free)
					{
						m_universe.addComponent(m_cameras[i].m_entity, CAMERA_HASH, this, i);
					}
				}
			}

			void deserializeRenderables(InputBlob& serializer)
			{
				int32_t size = 0;
				serializer.read(size);
				for(int i = size; i < m_renderables.size(); ++i)
				{
					setModel(i, NULL);
					m_allocator.deleteObject(m_renderables[i]);
				}
				m_culling_system->clear();
				m_renderables.clear();
				m_renderables.reserve(size);
				m_dynamic_renderable_cache = DynamicRenderableCache(m_allocator);
				m_always_visible.clear();
				for (int i = 0; i < size; ++i)
				{
					m_renderables.push(m_allocator.newObject<Renderable>(m_allocator));
					serializer.read(m_renderables[i]->m_is_always_visible);
					serializer.read(m_renderables[i]->m_component_index);
					if (m_renderables[i]->m_is_always_visible)
					{
						m_always_visible.push(m_renderables[i]->m_component_index);
					}
					serializer.read(m_renderables[i]->m_entity.index);
					serializer.read(m_renderables[i]->m_scale);
					int64_t layer_mask;
					serializer.read(layer_mask);
					m_renderables[i]->m_model = NULL;
					m_renderables[i]->m_entity.universe = &m_universe;
					m_renderables[i]->m_entity.getMatrix(m_renderables[i]->m_matrix);

					uint32_t path;
					serializer.read(path);
					m_culling_system->addStatic(Sphere(m_renderables[i]->m_entity.getPosition(), 1.0f));
					m_culling_system->setLayerMask(i, layer_mask);
					setModel(i, static_cast<Model*>(m_engine.getResourceManager().get(ResourceManager::MODEL)->load(Path(path))));
					m_universe.addComponent(m_renderables[i]->m_entity, RENDERABLE_HASH, this, i);
				}
			}

			void deserializeLights(InputBlob& serializer)
			{
				int32_t size = 0;
				serializer.read(size);
				m_point_lights.resize(size);
				m_light_influenced_geometry.clear();
				for (int i = 0; i < size; ++i)
				{
					m_light_influenced_geometry.push(Array<Renderable*>(m_allocator));
					PointLight& light = m_point_lights[i];
					serializer.read(light.m_uid);
					serializer.read(light.m_color);
					serializer.read(light.m_intensity);
					serializer.read(light.m_entity.index);
					serializer.read(light.m_range);
					serializer.read(light.m_fov);
					light.m_entity.universe = &m_universe;
					m_universe.addComponent(light.m_entity, POINT_LIGHT_HASH, this, light.m_uid);
				}
				serializer.read(m_point_light_last_uid);

				serializer.read(size);
				m_global_lights.resize(size);
				for (int i = 0; i < size; ++i)
				{
					GlobalLight& light = m_global_lights[i];
					serializer.read(light.m_uid);
					serializer.read(light.m_color);
					serializer.read(light.m_intensity);
					serializer.read(light.m_entity.index);
					serializer.read(light.m_ambient_color);
					serializer.read(light.m_ambient_intensity);
					serializer.read(light.m_fog_color);
					serializer.read(light.m_fog_density);
					light.m_entity.universe = &m_universe;
					m_universe.addComponent(light.m_entity, GLOBAL_LIGHT_HASH, this, light.m_uid);
				}
				serializer.read(m_global_light_last_uid);
				serializer.read(m_active_global_light_uid);
			}

			void deserializeTerrains(InputBlob& serializer)
			{
				int32_t size = 0;
				serializer.read(size);
				for (int i = size; i < m_terrains.size(); ++i)
				{
					m_allocator.deleteObject(m_terrains[i]);
					m_terrains[i] = NULL;
				}
				m_terrains.resize(size);
				for (int i = 0; i < size; ++i)
				{
					bool exists;
					serializer.read(exists);
					if(exists)
					{
						m_terrains[i] = m_allocator.newObject<Terrain>(m_renderer, Entity::INVALID, *this, m_allocator);
						Terrain* terrain = m_terrains[i];
						terrain->deserialize(serializer, m_universe, *this, i);
					}
					else
					{
						m_terrains[i] = NULL;
					}
				}
			}

			virtual void deserialize(InputBlob& serializer) override
			{
				deserializeCameras(serializer);
				deserializeRenderables(serializer);
				deserializeLights(serializer);
				deserializeTerrains(serializer);
			}


			void destroyRenderable(const Component& component)
			{
				int renderable_index = getRenderable(component.index);
				setModel(renderable_index, NULL);
				m_always_visible.eraseItemFast(component.index);
				m_allocator.deleteObject(m_renderables[renderable_index]);
				m_renderables.erase(renderable_index);
				m_culling_system->removeStatic(renderable_index);
				m_universe.destroyComponent(component);

				Lumix::HashMap<int32_t, int>::iterator iter = m_dynamic_renderable_cache.begin(), end = m_dynamic_renderable_cache.end();
				while (iter != end)
				{
					if (iter.value() > renderable_index)
					{
						--iter.value();
					}
					++iter;
				}
				m_dynamic_renderable_cache.erase(component.entity.index);
			}

			virtual void destroyComponent(const Component& component) override
			{
				if (component.type == RENDERABLE_HASH)
				{
					Renderable* renderable = m_renderables[getRenderable(component.index)];
					for (int i = 0; i < m_light_influenced_geometry.size(); ++i)
					{
						Array<Renderable*>& influenced_geometry = m_light_influenced_geometry[i];
						for (int j = 0; j < influenced_geometry.size(); ++j)
						{
							if (influenced_geometry[j] == renderable)
							{
								influenced_geometry.erase(j);
								i = m_light_influenced_geometry.size();
								break; 
							}
						}
					}
					destroyRenderable(component);
				}
				else if (component.type == GLOBAL_LIGHT_HASH)
				{
					m_universe.destroyComponent(component);
					if (component.index == m_active_global_light_uid)
					{
						m_active_global_light_uid = -1;
					}
					m_global_lights.eraseFast(getGlobalLightIndex(component));
				}
				else if (component.type == POINT_LIGHT_HASH)
				{
					int index = getPointLightIndex(component);
					m_point_lights.eraseFast(index);
					m_light_influenced_geometry.eraseFast(index);
					m_universe.destroyComponent(component);
				}
				else if (component.type == CAMERA_HASH)
				{
					m_cameras[component.index].m_is_free = true;
					m_universe.destroyComponent(component);
				}
				else if(component.type == TERRAIN_HASH)
				{
					m_allocator.deleteObject(m_terrains[component.index]);
					m_terrains[component.index] = NULL;
					m_universe.destroyComponent(component);
				}
				else
				{
					ASSERT(false);
				}
			}


			virtual Component createComponent(uint32_t type, const Entity& entity) override
			{
				if (type == TERRAIN_HASH)
				{
					Terrain* terrain = m_allocator.newObject<Terrain>(m_renderer, entity, *this, m_allocator);
					m_terrains.push(terrain);
					Component cmp = m_universe.addComponent(entity, type, this, m_terrains.size() - 1);
					m_universe.componentCreated().invoke(cmp);
					return cmp;
				}
				else if (type == CAMERA_HASH)
				{
					Camera& camera = m_cameras.pushEmpty();
					camera.m_is_free = false;
					camera.m_is_active = false;
					camera.m_entity = entity;
					camera.m_fov = 60;
					camera.m_width = 800;
					camera.m_height = 600;
					camera.m_aspect = 800.0f / 600.0f;
					camera.m_near = 0.1f;
					camera.m_far = 10000.0f;
					camera.m_slot[0] = '\0';
					Component cmp = m_universe.addComponent(entity, type, this, m_cameras.size() - 1);
					m_universe.componentCreated().invoke(cmp);
					return cmp;
				}
				else if (type == RENDERABLE_HASH)
				{
					return createRenderable(entity);
				}
				else if (type == GLOBAL_LIGHT_HASH)
				{
					GlobalLight& light = m_global_lights.pushEmpty();
					light.m_entity = entity;
					light.m_color.set(1, 1, 1, 1);
					light.m_intensity = 0;
					light.m_ambient_color.set(1, 1, 1, 1);
					light.m_ambient_intensity = 1;
					light.m_fog_color.set(1, 1, 1, 1);
					light.m_fog_density = 0;
					light.m_uid = ++m_global_light_last_uid;

					if (m_global_lights.size() == 1)
					{
						m_active_global_light_uid = light.m_uid;
					}

					Component cmp = m_universe.addComponent(entity, type, this, light.m_uid);
					m_universe.componentCreated().invoke(cmp);
					return cmp;
				}
				else if (type == POINT_LIGHT_HASH)
				{
					return createPointLight(entity);
				}
				return Component::INVALID;
			}


			virtual Component getRenderable(Entity entity) override
			{
				DynamicRenderableCache::iterator iter = m_dynamic_renderable_cache.find(entity.index);
				if (!iter.isValid())
				{
					for (int i = 0, c = m_renderables.size(); i < c; ++i)
					{
						if (m_renderables[i]->m_entity == entity)
						{
							m_dynamic_renderable_cache.insert(entity.index, i);
							return Component(entity, RENDERABLE_HASH, this, i);
						}
					}
				}
				else
				{
					return Component(entity, RENDERABLE_HASH, this, iter.value());
				}
				return Component::INVALID;
			}


			void onEntityMoved(const Entity& entity)
			{
				DynamicRenderableCache::iterator iter = m_dynamic_renderable_cache.find(entity.index);
				Renderable* renderable = NULL;
				if (!iter.isValid())
				{
					for (int i = 0, c = m_renderables.size(); i < c; ++i)
					{
						if (m_renderables[i]->m_entity == entity)
						{
							m_dynamic_renderable_cache.insert(entity.index, i);
							m_renderables[i]->m_matrix = entity.getMatrix();
							m_culling_system->updateBoundingPosition(entity.getMatrix().getTranslation(), i);
							renderable = m_renderables[i];
							break;
						}
					}
				}
				else
				{
					m_renderables[iter.value()]->m_matrix = entity.getMatrix();
					renderable = m_renderables[iter.value()];
					m_culling_system->updateBoundingPosition(entity.getMatrix().getTranslation(), iter.value());
				}

				for (int i = 0, c = m_point_lights.size(); i < c; ++i)
				{
					Frustum frustum;
					PointLight& light = m_point_lights[i];
					frustum.computeOrtho(light.m_entity.getPosition(), Vec3(1, 0, 0), Vec3(0, 1, 0), light.m_range, light.m_range, -light.m_range, light.m_range);

					if (renderable && m_is_forward_rendered)
					{
						for (int j = 0; j < m_light_influenced_geometry[i].size(); ++j)
						{
							if (m_light_influenced_geometry[i][j] == renderable)
							{
								m_light_influenced_geometry[i].eraseFast(j);
								break;
							}
						}
						if (frustum.isSphereInside(renderable->m_entity.getPosition(), renderable->m_model->getBoundingRadius()))
						{
							m_light_influenced_geometry[i].push(renderable);
						}
					}
					if (m_point_lights[i].m_entity == entity)
					{
						detectLightInfluencedGeometry(i);
						break;
					}
				}
			}

			virtual Engine& getEngine() const override
			{
				return m_engine;
			}


			virtual void setTerrainBrush(Component cmp, const Vec3& position, float size) override
			{
				m_terrains[cmp.index]->setBrush(position, size);
			}


			virtual float getTerrainHeightAt(Component cmp, float x, float z) override
			{
				return m_terrains[cmp.index]->getHeight(x, z);
			}


			virtual void getTerrainSize(Component cmp, float* width, float* height) override
			{
				m_terrains[cmp.index]->getSize(width, height);
			}


			virtual void setTerrainMaterial(Component cmp, const string& path) override
			{
				Material* material = static_cast<Material*>(m_engine.getResourceManager().get(ResourceManager::MATERIAL)->load(Path(path.c_str())));
				m_terrains[cmp.index]->setMaterial(material);
			}


			virtual void getTerrainMaterial(Component cmp, string& path) override
			{
				if (m_terrains[cmp.index]->getMaterial())
				{
					path = m_terrains[cmp.index]->getMaterial()->getPath().c_str();
				}
				else
				{
					path = "";
				}
			}


			virtual void setTerrainXZScale(Component cmp, float scale) override
			{
				m_terrains[cmp.index]->setXZScale(scale);
			}

			virtual float getTerrainXZScale(Component cmp) override
			{
				return m_terrains[cmp.index]->getXZScale();
			}


			virtual void setTerrainYScale(Component cmp, float scale) override
			{
				m_terrains[cmp.index]->setYScale(scale);
			}

			virtual float getTerrainYScale(Component cmp)
			{
				return m_terrains[cmp.index]->getYScale();
			}


			virtual Pose& getPose(const Component& cmp) override
			{
				return m_renderables[getRenderable(cmp.index)]->m_pose;
			}


			virtual Model* getRenderableModel(Component cmp) override
			{
				return m_renderables[getRenderable(cmp.index)]->m_model;
			}


			virtual void showRenderable(Component cmp) override
			{
				m_culling_system->enableStatic(getRenderable(cmp.index));
			}


			virtual void hideRenderable(Component cmp) override
			{
				int renderable_index = getRenderable(cmp.index);
				if (!m_renderables[renderable_index]->m_is_always_visible)
				{
					m_culling_system->disableStatic(renderable_index);
				}
			}


			virtual void setRenderableIsAlwaysVisible(Component cmp, bool value) override
			{
				int renderable_index = getRenderable(cmp.index);
				m_renderables[renderable_index]->m_is_always_visible = value;
				if(value)
				{
					m_culling_system->disableStatic(renderable_index);
					m_always_visible.push(cmp.index);
				}
				else
				{
					m_culling_system->enableStatic(renderable_index);
					m_always_visible.eraseItemFast(cmp.index);
				}
			}
			

			virtual bool isRenderableAlwaysVisible(Component cmp) override
			{
				return m_renderables[getRenderable(cmp.index)]->m_is_always_visible;
			}


			virtual void getRenderablePath(Component cmp, string& path) override
			{
					int index = getRenderable(cmp.index);
					if (index >= 0 && m_renderables[index]->m_model)
					{
						path = m_renderables[index]->m_model->getPath().c_str();
					}
					else
					{
						path = "";
					}
			}


			virtual void setRenderableLayer(Component cmp, const int32_t& layer) override
			{
				m_culling_system->setLayerMask(getRenderable(cmp.index), (int64_t)1 << (int64_t)layer);
			}

			virtual void setRenderableScale(Component cmp, float scale) override
			{
				Renderable& r = *m_renderables[getRenderable(cmp.index)];
				r.m_scale = scale;
			}


			virtual void setRenderablePath(Component cmp, const string& path) override
			{
				int renderable_index = getRenderable(cmp.index);
				Renderable& r = *m_renderables[renderable_index];

				Model* model = static_cast<Model*>(m_engine.getResourceManager().get(ResourceManager::MODEL)->load(Path(path.c_str())));
				setModel(renderable_index, model);
				r.m_matrix = r.m_entity.getMatrix();
			}


			virtual void getTerrainInfos(Array<const TerrainInfo*>& infos, int64_t layer_mask, const Vec3& camera_pos, LIFOAllocator& frame_allocator) override
			{
				PROFILE_FUNCTION();
				infos.reserve(m_terrains.size());
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					if (m_terrains[i] && (m_terrains[i]->getLayerMask() & layer_mask) != 0)
					{
						m_terrains[i]->getInfos(infos, camera_pos, frame_allocator);
					}
				}
			}


			virtual void getGrassInfos(const Frustum& frustum, Array<GrassInfo>& infos, int64_t layer_mask) override
			{
				PROFILE_FUNCTION();
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					if (m_terrains[i] && (m_terrains[i]->getLayerMask() & layer_mask) != 0)
					{
						m_terrains[i]->getGrassInfos(frustum, infos, m_applied_camera);
					}
				}
			}


			virtual void setGrassDensity(Component cmp, int index, int density) override
			{
				m_terrains[cmp.index]->setGrassTypeDensity(index, density);
			}
			
			
			virtual int getGrassDensity(Component cmp, int index) override
			{
				return m_terrains[cmp.index]->getGrassTypeDensity(index);
			}


			virtual void setGrassGround(Component cmp, int index, int ground) override
			{
				m_terrains[cmp.index]->setGrassTypeGround(index, ground);
			}
			

			virtual int getGrassGround(Component cmp, int index) override
			{
				return m_terrains[cmp.index]->getGrassTypeGround(index);
			}


			virtual void setGrass(Component cmp, int index, const string& path) override
			{
				m_terrains[cmp.index]->setGrassTypePath(index, Path(path.c_str()));
			}


			virtual void getGrass(Component cmp, int index, string& path) override
			{
				path = m_terrains[cmp.index]->getGrassTypePath(index).c_str();
			}


			virtual int getGrassCount(Component cmp) override
			{
				return m_terrains[cmp.index]->getGrassTypeCount();
			}


			virtual void addGrass(Component cmp, int index) override
			{
				m_terrains[cmp.index]->addGrassType(index);
			}


			virtual void removeGrass(Component cmp, int index) override
			{
				m_terrains[cmp.index]->removeGrassType(index);
			}


			virtual Frustum& getFrustum() override
			{
				return m_camera_frustum;
			}


			virtual Component getFirstRenderable() override
			{
				if(m_renderables.empty())
				{
					return Component::INVALID;
				}
				return Component(m_renderables[0]->m_entity, RENDERABLE_HASH, this, m_renderables[0]->m_component_index);
			}
			

			int getRenderable(int index)
			{
				int l = 0; 
				int h = m_renderables.size() - 1;
				while(l <= h)
				{
					int m = (l + h) >> 1;
					if(m_renderables[m]->m_component_index < index)
					{
						l = m + 1;
					}
					else if (m_renderables[m]->m_component_index > index)
					{
						h = m - 1;
					}
					else
					{
						return m;
					}
				}

				return -1;
			}

			
			virtual Component getNextRenderable(const Component& cmp) override
			{
				int i = getRenderable(cmp.index);
				if(i + 1 < m_renderables.size())
				{
					return Component(m_renderables[i + 1]->m_entity, RENDERABLE_HASH, this, m_renderables[i + 1]->m_component_index);
				}
				return Component::INVALID;
			}


			const CullingSystem::Results* cull(const Frustum& frustum, int64_t layer_mask)
			{
				PROFILE_FUNCTION();
				if (m_renderables.empty())
					return NULL;

				m_culling_system->cullToFrustumAsync(frustum, layer_mask);
				return &m_culling_system->getResult();
			}


			void mergeTemporaryInfos(Array<const RenderableMesh*>& all_infos)
			{
				PROFILE_FUNCTION();
				all_infos.reserve(m_renderables.size() * 2);
				for (int i = 0; i < m_temporary_infos.size(); ++i)
				{
					Array<const RenderableMesh*>& subinfos = m_temporary_infos[i];
					if (!subinfos.empty())
					{
						int size = all_infos.size();
						all_infos.resize(size + subinfos.size());
						memcpy(&all_infos[0] + size, &subinfos[0], sizeof(subinfos[0]) * subinfos.size());
					}
				}
			}


			void runJobs(Array<MTJD::Job*>& jobs, MTJD::Group& sync_point)
			{
				PROFILE_FUNCTION();
				for (int i = 0; i < jobs.size(); ++i)
				{
					m_engine.getMTJDManager().schedule(jobs[i]);
				}
				if (!jobs.empty())
				{
					sync_point.sync();
				}
			}


			void fillTemporaryInfos(const CullingSystem::Results& results, const Frustum& frustum, int64_t layer_mask)
			{
				PROFILE_FUNCTION();
				m_jobs.clear();

				while (m_temporary_infos.size() < results.size())
				{
					m_temporary_infos.emplace(m_allocator);
				}
				while (m_temporary_infos.size() > results.size())
				{
					m_temporary_infos.pop();
				}
				for (int subresult_index = 0; subresult_index < results.size(); ++subresult_index)
				{
					Array<const RenderableMesh*>& subinfos = m_temporary_infos[subresult_index];
					subinfos.clear();
					MTJD::Job* job = MTJD::makeJob(m_engine.getMTJDManager(), [&subinfos, layer_mask, this, &results, subresult_index, &frustum]()
						{
							Vec3 frustum_position = frustum.getPosition();
							const CullingSystem::Subresults& subresults = results[subresult_index];
							for (int i = 0, c = subresults.size(); i < c; ++i)
							{
								const Renderable* LUMIX_RESTRICT renderable = m_renderables[subresults[i]];
								const Model* LUMIX_RESTRICT model = renderable->m_model;
								float squared_distance = (renderable->m_matrix.getTranslation() - frustum_position).squaredLength();
								if (model && model->isReady())
								{
									LODMeshIndices lod = model->getLODMeshIndices(squared_distance);
									for (int j = lod.getFrom(), c = lod.getTo(); j <= c; ++j)
									{
										subinfos.push(&renderable->m_meshes[j]);
									}
								}
							}
						}
						, m_allocator
					);
					job->addDependency(&m_sync_point);
					m_jobs.push(job);
				}
				runJobs(m_jobs, m_sync_point);
			}


			virtual Entity getPointLightEntity(Component cmp) override
			{
				return m_point_lights[getPointLightIndex(cmp.index)].m_entity;
			}


			virtual void getPointLights(const Frustum& frustum, Array<Component>& lights) override
			{
				for (int i = 0, ci = m_point_lights.size(); i < ci; ++i)
				{
					PointLight& light = m_point_lights[i];
					
					if (frustum.isSphereInside(light.m_entity.getPosition(), light.m_range))
					{
						lights.push(Component(light.m_entity, POINT_LIGHT_HASH, this, light.m_uid));
					}
				}
			}


			virtual void getPointLightInfluencedGeometry(const Component& light_cmp, const Frustum& frustum, Array<const RenderableMesh*>& infos, int64_t layer_mask)
			{
				PROFILE_FUNCTION();

				int light_index = getPointLightIndex(light_cmp.index);
				for (int j = 0, cj = m_light_influenced_geometry[light_index].size(); j < cj; ++j)
				{
					Renderable* renderable = m_light_influenced_geometry[light_index][j];
					bool is_layer = (layer_mask & m_culling_system->getLayerMask(renderable->m_component_index)) != 0;
					const Sphere& sphere = m_culling_system->getSpheres()[renderable->m_component_index];
					if (is_layer && frustum.isSphereInside(sphere.m_position, sphere.m_radius))
					{
						for (int k = 0, kc = renderable->m_meshes.size(); k < kc; ++k)
						{
							infos.push(&renderable->m_meshes[k]);
						}
					}
				}
			}

			
			virtual void getRenderableInfos(const Frustum& frustum, Array<const RenderableMesh*>& meshes, int64_t layer_mask) override
			{
				PROFILE_FUNCTION();

				const CullingSystem::Results* results = cull(frustum, layer_mask);
				if (!results)
				{
					return;
				}

				fillTemporaryInfos(*results, frustum, layer_mask);
				mergeTemporaryInfos(meshes);

				for (int i = 0, c = m_always_visible.size(); i < c; ++i)
				{
					int renderable_index = getRenderable(m_always_visible[i]);
					const Renderable* LUMIX_RESTRICT renderable = m_renderables[renderable_index];
					if ((m_culling_system->getLayerMask(renderable_index) & layer_mask) != 0)
					{
						for (int j = 0, c = renderable->m_meshes.size(); j < c; ++j)
						{
							meshes.push(&renderable->m_meshes[j]);
						}
					}
				}
			}


			virtual void getRenderableMeshes(Array<RenderableMesh>& meshes, int64_t layer_mask) override
			{
				PROFILE_FUNCTION();

				if (m_renderables.empty())
					return;

				meshes.reserve(m_renderables.size() * 2);
				for (int i = 0, c = m_renderables.size(); i < c; ++i)
				{
					const Renderable* LUMIX_RESTRICT renderable = m_renderables[i];
					if ((m_culling_system->getLayerMask(i) & layer_mask) != 0)
					{
						for (int j = 0, c = renderable->m_meshes.size(); j < c; ++j)
						{
							meshes.push(renderable->m_meshes[j]);
						}
					}
				}
			}

			virtual void setCameraSlot(Component camera, const string& slot) override
			{
				copyString(m_cameras[camera.index].m_slot, Camera::MAX_SLOT_LENGTH, slot.c_str());
			}

			virtual void getCameraSlot(Component camera, string& slot) override
			{
				slot = m_cameras[camera.index].m_slot;
			}

			virtual float getCameraFOV(Component camera) override
			{
				return m_cameras[camera.index].m_fov;
			}

			virtual void setCameraFOV(Component camera, float fov) override
			{
				m_cameras[camera.index].m_fov = fov;
			}

			virtual void setCameraNearPlane(Component camera, float near_plane) override
			{
				m_cameras[camera.index].m_near = near_plane;
			}

			virtual float getCameraNearPlane(Component camera) override
			{
				return m_cameras[camera.index].m_near;
			}

			virtual void setCameraFarPlane(Component camera, float far_plane) override
			{
				m_cameras[camera.index].m_far = far_plane;
			}

			virtual float getCameraFarPlane(Component camera) override
			{
				return m_cameras[camera.index].m_far;
			}

			virtual float getCameraWidth(Component camera) override
			{
				return m_cameras[camera.index].m_width;
			}


			virtual float getCameraHeight(Component camera) override
			{
				return m_cameras[camera.index].m_height;
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


			virtual int addDebugText(const char* text, int x, int y) override
			{
				return m_debug_texts.addText(text, x, y);
			}


			virtual void setDebugText(int id, const char* text) override
			{
				return m_debug_texts.setText(id, text);
			}


			virtual Geometry& getDebugTextGeometry() override
			{
				return m_debug_texts.getGeometry();
			}


			virtual const char* getDebugText(int index) override
			{
				if (index < m_debug_texts.getTexts().size())
					return m_debug_texts.getTexts().at(index).m_text.c_str();
				return nullptr;
			}


			virtual Mesh& getDebugTextMesh() override
			{
				return m_debug_texts.getMesh();
			}


			virtual BitmapFont* getDebugTextFont() override
			{
				return m_debug_texts.getFont();
			}


			virtual void addDebugSphere(const Vec3& center, float radius, const Vec3& color, float life) override
			{
				static const int COLS = 36;
				static const int ROWS = COLS >> 1;
				static const float STEP = (Math::PI / 180.0f) * 360.0f / COLS;
				int p2 = COLS >> 1;
				int r2 = ROWS >> 1;
				float prev_ci = 1;
				float prev_si = 0;
				for (int y = -r2; y < r2; ++y) {
					float cy = cos(y * STEP);
					float cy1 = cos((y + 1) * STEP);
					float sy = sin(y * STEP);
					float sy1 = sin((y + 1) * STEP);

					for (int i = -p2; i < p2; ++i) {
						float ci = cos(i * STEP);
						float si = sin(i * STEP);
						addDebugLine(
							Vec3(center.x + radius * ci * cy, center.y + radius * sy, center.z + radius * si * cy),
							Vec3(center.x + radius * ci * cy1, center.y + radius * sy1, center.z + radius * si * cy1),
							color, life
						);
						addDebugLine(
							Vec3(center.x + radius * ci * cy, center.y + radius * sy, center.z + radius * si * cy),
							Vec3(center.x + radius * prev_ci * cy, center.y + radius * sy, center.z + radius * prev_si * cy),
							color, life
						);
						addDebugLine(
							Vec3(center.x + radius * prev_ci * cy1, center.y + radius * sy1, center.z + radius * prev_si * cy1),
							Vec3(center.x + radius * ci * cy1, center.y + radius * sy1, center.z + radius * si * cy1),
							color, life
						);
						prev_ci = ci;
						prev_si = si;
					}
				}
			}


			virtual void addDebugCylinder(const Vec3& position, const Vec3& up, float radius, const Vec3& color, float life) override
			{
				Vec3 z_vec(-up.y, up.x, 0);
				Vec3 x_vec = crossProduct(up, z_vec);
				float prevx = radius;
				float prevz = 0;
				z_vec.normalize();
				x_vec.normalize();
				Vec3 top = position + up;
				for (int i = 1; i <= 32; ++i)
				{
					float a = i / 32.0f * 2 * Math::PI;
					float x = cosf(a) * radius;
					float z = sinf(a) * radius;
					addDebugLine(position + x_vec * x + z_vec * z, position + x_vec * prevx + z_vec * prevz, color, life);
					addDebugLine(top + x_vec * x + z_vec * z, top + x_vec * prevx + z_vec * prevz, color, life);
					addDebugLine(position + x_vec * x + z_vec * z, top + x_vec * x + z_vec * z, color, life);
					prevx = x;
					prevz = z;
				}
			}


			virtual void addDebugCube(const Vec3& min, const Vec3& max, const Vec3& color, float life) override
			{
				Vec3 a = min;
				Vec3 b = min;
				b.x = max.x;
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, max.z);
				addDebugLine(a, b, color, life);
				b.set(min.x, a.y, a.z);
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, min.z);
				addDebugLine(a, b, color, life);

				a = min;
				a.y = max.y;
				b = a;
				b.x = max.x;
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, max.z);
				addDebugLine(a, b, color, life);
				b.set(min.x, a.y, a.z);
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, min.z);
				addDebugLine(a, b, color, life);

				a = min;
				b = a;
				b.y = max.y;
				addDebugLine(a, b, color, life);
				a.x = max.x;
				b.x = max.x;
				addDebugLine(a, b, color, life);
				a.z = max.z;
				b.z = max.z;
				addDebugLine(a, b, color, life);
				a.x = min.x;
				b.x = min.x;
				addDebugLine(a, b, color, life);
			}


			virtual void addDebugFrustum(const Frustum& frustum, const Vec3& color, float life) override
			{
				addDebugFrustum(frustum.getPosition(), frustum.getDirection(), frustum.getUp(), frustum.getFOV(), frustum.getRatio(), frustum.getNearDistance(), frustum.getFarDistance(), color, life);
			}


			virtual void addDebugFrustum(const Vec3& position, const Vec3& direction, const Vec3& up, float fov, float ratio, float near_distance, float far_distance, const Vec3& color, float life) override
			{
				Vec3 points[8];
				Vec3 near_center = position + direction * near_distance;
				Vec3 far_center = position + direction * far_distance;
				Vec3 right = crossProduct(direction, up);
				float scale = (float)tan(Math::PI / 180.0f * fov * 0.5f);
				Vec3 up_near = up * 0.5f * near_distance * scale;
				Vec3 right_near = right * (0.5f * near_distance * scale * ratio);

				points[0] = near_center + up_near + right_near;
				points[1] = near_center + up_near - right_near;
				points[2] = near_center - up_near - right_near;
				points[3] = near_center - up_near + right_near;

				Vec3 up_far = up * 0.5f * far_distance * scale;
				Vec3 right_far = right * (0.5f * far_distance * scale * ratio);

				points[4] = far_center + up_far + right_far;
				points[5] = far_center + up_far - right_far;
				points[6] = far_center - up_far - right_far;
				points[7] = far_center - up_far + right_far;

				addDebugLine(points[0], points[1], color, life);
				addDebugLine(points[1], points[2], color, life);
				addDebugLine(points[2], points[3], color, life);
				addDebugLine(points[3], points[0], color, life);

				addDebugLine(points[4], points[5], color, life);
				addDebugLine(points[5], points[6], color, life);
				addDebugLine(points[6], points[7], color, life);
				addDebugLine(points[7], points[4], color, life);

				addDebugLine(points[0], points[4], color, life);
				addDebugLine(points[1], points[5], color, life);
				addDebugLine(points[2], points[6], color, life);
				addDebugLine(points[3], points[7], color, life);

			}

			virtual void addDebugCircle(const Vec3& center, const Vec3& up, float radius, const Vec3& color, float life) override
			{
				Vec3 z_vec(-up.y, up.x, 0);
				Vec3 x_vec = crossProduct(up, z_vec);
				float prevx = radius;
				float prevz = 0;
				z_vec.normalize();
				x_vec.normalize();
				for (int i = 1; i <= 64; ++i)
				{
					float a = i / 64.0f * 2 * Math::PI;
					float x = cosf(a) * radius;
					float z = sinf(a) * radius;
					addDebugLine(center + x_vec * x + z_vec * z, center + x_vec * prevx + z_vec * prevz, color, life);
					prevx = x;
					prevz = z;
				}
			}

			virtual void addDebugCross(const Vec3& center, float size, const Vec3& color, float life) override
			{
				addDebugLine(center, Vec3(center.x - size, center.y, center.z), color, life);
				addDebugLine(center, Vec3(center.x + size, center.y, center.z), color, life);
				addDebugLine(center, Vec3(center.x, center.y - size, center.z), color, life);
				addDebugLine(center, Vec3(center.x, center.y + size, center.z), color, life);
				addDebugLine(center, Vec3(center.x, center.y, center.z - size), color, life);
				addDebugLine(center, Vec3(center.x, center.y, center.z + size), color, life);
			}


			virtual void addDebugLine(const Vec3& from, const Vec3& to, const Vec3& color, float life) override
			{
				DebugLine& line = m_debug_lines.pushEmpty();
				line.m_from = from;
				line.m_to = to;
				line.m_color = ((uint8_t)(color.x * 255) << 24)
					| ((uint8_t)(color.y * 255) << 16)
					| ((uint8_t)(color.z * 255) << 8) 
					| 255;
				line.m_life = life;
			}


			virtual void addDebugLine(const Vec3& from, const Vec3& to, uint32_t color, float life) override
			{
				DebugLine& line = m_debug_lines.pushEmpty();
				line.m_from = from;
				line.m_to = to;
				line.m_color = color;
				line.m_life = life;
			}

			virtual RayCastModelHit castRayTerrain(const Component& terrain, const Vec3& origin, const Vec3& dir) override
			{
				RayCastModelHit hit;
				hit.m_is_hit = false;
				if(m_terrains[terrain.index])
				{
					hit = m_terrains[terrain.index]->castRay(origin, dir);
					hit.m_component = terrain;
				}
				return hit;
			}


			virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Component& ignore) override
			{
				RayCastModelHit hit;
				hit.m_is_hit = false;
				int ignore_index = getRenderable(ignore.index);
				Terrain* ignore_terrain = ignore.type == TERRAIN_HASH ? m_terrains[ignore_index] : NULL;
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					if (ignore_index != i && m_renderables[i]->m_model)
					{
						const Vec3& pos = m_renderables[i]->m_matrix.getTranslation();
						float radius = m_renderables[i]->m_model->getBoundingRadius();
						float scale = m_renderables[i]->m_scale;
						Vec3 intersection;
						if (dotProduct(pos - origin, pos - origin) < radius * radius || Math::getRaySphereIntersection(origin, dir, pos, radius * scale, intersection))
						{
							RayCastModelHit new_hit = m_renderables[i]->m_model->castRay(origin, dir, m_renderables[i]->m_matrix, scale);
							if (new_hit.m_is_hit && (!hit.m_is_hit || new_hit.m_t < hit.m_t))
							{
								new_hit.m_component = Component(m_renderables[i]->m_entity, RENDERABLE_HASH, this, i);
								hit = new_hit;
								hit.m_is_hit = true;
							}
						}
					}
				}
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					if(m_terrains[i])
					{
						RayCastModelHit terrain_hit = m_terrains[i]->castRay(origin, dir);
						if (terrain_hit.m_is_hit && ignore_terrain != m_terrains[i] && (!hit.m_is_hit || terrain_hit.m_t < hit.m_t))
						{
							terrain_hit.m_component = Component(m_terrains[i]->getEntity(), TERRAIN_HASH, this, i);
							hit = terrain_hit;
						}
					}
				}
				return hit;
			}


			int getPointLightIndex(int uid) const
			{
				for (int i = 0; i < m_point_lights.size(); ++i)
				{
					if (m_point_lights[i].m_uid == uid)
					{
						return i;
					}
				}
				return -1;
			}


			int getPointLightIndex(const Component& cmp) const
			{
				return getPointLightIndex(cmp.index);
			}


			int getGlobalLightIndex(int uid) const
			{
				for (int i = 0; i < m_global_lights.size(); ++i)
				{
					if (m_global_lights[i].m_uid == uid)
					{
						return i;
					}
				}
				return -1;
			}


			int getGlobalLightIndex(const Component& cmp) const
			{
				return getGlobalLightIndex(cmp.index);
			}


			virtual void setFogDensity(Component cmp, float density) override
			{
				m_global_lights[getGlobalLightIndex(cmp)].m_fog_density = density;
			}

			virtual void setFogColor(Component cmp, const Vec4& color) override
			{
				m_global_lights[getGlobalLightIndex(cmp)].m_fog_color = color;
			}

			virtual float getFogDensity(Component cmp) override
			{
				return m_global_lights[getGlobalLightIndex(cmp)].m_fog_density;
			}
			
			virtual Vec4 getFogColor(Component cmp) override
			{
				return m_global_lights[getGlobalLightIndex(cmp)].m_fog_color;
			}

			virtual float getLightRange(Component cmp) override
			{
				return m_point_lights[getPointLightIndex(cmp.index)].m_range;
			}

			virtual void setLightRange(Component cmp, float range) override
			{
				int index = getPointLightIndex(cmp.index);
				m_point_lights[index].m_range = range;
				detectLightInfluencedGeometry(index);
			}
			
			virtual void setPointLightIntensity(Component cmp, float intensity) override
			{
				m_point_lights[getPointLightIndex(cmp.index)].m_intensity = intensity;
			}
			
			virtual void setGlobalLightIntensity(Component cmp, float intensity) override
			{
				m_global_lights[getGlobalLightIndex(cmp)].m_intensity = intensity;
			}

			virtual void setPointLightColor(Component cmp, const Vec4& color) override
			{
				m_point_lights[getPointLightIndex(cmp.index)].m_color = color;
			}

			virtual void setGlobalLightColor(Component cmp, const Vec4& color) override
			{
				m_global_lights[getGlobalLightIndex(cmp)].m_color = color;
			}

			virtual void setLightAmbientIntensity(Component cmp, float intensity) override
			{
				m_global_lights[getGlobalLightIndex(cmp)].m_ambient_intensity = intensity;
			}

			virtual void setLightAmbientColor(Component cmp, const Vec4& color) override
			{
				m_global_lights[getGlobalLightIndex(cmp)].m_ambient_color = color;
			}

			virtual float getPointLightIntensity(Component cmp) override
			{
				return m_point_lights[getPointLightIndex(cmp.index)].m_intensity;
			}
			
			virtual float getGlobalLightIntensity(Component cmp) override
			{
				return m_global_lights[getGlobalLightIndex(cmp)].m_intensity;
			}

			virtual Vec4 getPointLightColor(Component cmp) override
			{
				return m_point_lights[getPointLightIndex(cmp.index)].m_color;
			}
			
			virtual Vec4 getGlobalLightColor(Component cmp) override
			{
				return m_global_lights[getGlobalLightIndex(cmp)].m_color;
			}

			virtual float getLightAmbientIntensity(Component cmp) override
			{
				return m_global_lights[getGlobalLightIndex(cmp)].m_ambient_intensity;
			}

			virtual Vec4 getLightAmbientColor(Component cmp) override
			{
				return m_global_lights[getGlobalLightIndex(cmp)].m_ambient_color;
			}

			virtual void setActiveGlobalLight(const Component& cmp) override
			{
				ASSERT(cmp.type == GLOBAL_LIGHT_HASH);
				m_active_global_light_uid = cmp.index;
			}

			virtual Component getActiveGlobalLight() override
			{
				if (m_active_global_light_uid == -1)
				{
					return Component::INVALID;
				}
				return Component(m_global_lights[getGlobalLightIndex(m_active_global_light_uid)].m_entity, GLOBAL_LIGHT_HASH, this, m_active_global_light_uid);
			};

			virtual Component getCameraInSlot(const char* slot) override
			{
				for (int i = 0, c = m_cameras.size(); i < c; ++i)
				{
					if (!m_cameras[i].m_is_free && strcmp(m_cameras[i].m_slot, slot) == 0)
					{
						return Component(m_cameras[i].m_entity, CAMERA_HASH, this, i);
					}
				}
				return Component::INVALID;
			}

			virtual float getTime() const override
			{
				return m_time;
			}


		private:
			void modelLoaded(Model* model, int renderable_index)
			{
				float bounding_radius = m_renderables[renderable_index]->m_model->getBoundingRadius();
				m_culling_system->updateBoundingRadius(bounding_radius, renderable_index);
				m_renderables[renderable_index]->m_meshes.clear();
				m_renderables[renderable_index]->m_pose.resize(model->getBoneCount());
				model->getPose(m_renderables[renderable_index]->m_pose);
				for (int j = 0; j < model->getMeshCount(); ++j)
				{
					RenderableMesh& info = m_renderables[renderable_index]->m_meshes.pushEmpty();
					info.m_mesh = &model->getMesh(j);
					info.m_pose = &m_renderables[renderable_index]->m_pose;
					info.m_matrix = &m_renderables[renderable_index]->m_matrix;
					info.m_model = model;
				}
			}

			void modelLoaded(Model* model)
			{
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					if (m_renderables[i]->m_model == model)
					{
						modelLoaded(model, i);
					}
				}
			}


			ModelLoadedCallback* getModelLoadedCallback(Model* model)
			{
				for(int i = 0; i < m_model_loaded_callbacks.size(); ++i)
				{
					if(m_model_loaded_callbacks[i]->m_model == model)
					{
						return m_model_loaded_callbacks[i];
					}
				}
				ModelLoadedCallback* new_callback = m_allocator.newObject<ModelLoadedCallback>(*this, model);
				m_model_loaded_callbacks.push(new_callback);
				return new_callback;
			}


			void setModel(int renderable_index, Model* model)
			{
				Model* old_model = m_renderables[renderable_index]->m_model;
				if(model == old_model)
				{
					return;
				}
				if (old_model)
				{
					ModelLoadedCallback* callback = getModelLoadedCallback(old_model);
					--callback->m_ref_count;
					old_model->getResourceManager().get(ResourceManager::MODEL)->unload(*old_model);
				}
				m_renderables[renderable_index]->m_model = model;
				m_renderables[renderable_index]->m_meshes.clear();
				if (model)
				{
					ModelLoadedCallback* callback = getModelLoadedCallback(model);
					++callback->m_ref_count;

					if(model->isReady())
					{
						modelLoaded(model, renderable_index);
					}
				}
			}

			virtual IAllocator& getAllocator() override
			{
				return m_allocator;
			}


			void detectLightInfluencedGeometry(int light_index)
			{
				if (m_is_forward_rendered)
				{
					Frustum frustum;
					PointLight& light = m_point_lights[light_index];
					frustum.computeOrtho(light.m_entity.getPosition(), Vec3(1, 0, 0), Vec3(0, 1, 0), light.m_range, light.m_range, -light.m_range, light.m_range);
					m_culling_system->cullToFrustum(frustum, 0xffffFFFF);
					const CullingSystem::Results& results = m_culling_system->getResult();
					Array<Renderable*>& influenced_geometry = m_light_influenced_geometry[light_index];
					influenced_geometry.clear();
					for (int i = 0; i < results.size(); ++i)
					{
						const CullingSystem::Subresults& subresult = results[i];
						influenced_geometry.reserve(influenced_geometry.size() + subresult.size());
						for (int j = 0, c = subresult.size(); j < c; ++j)
						{
							influenced_geometry.push(m_renderables[subresult[j]]);
						}
					}
				}
			}


			virtual float getLightFOV(Component cmp) override
			{
				return m_point_lights[getPointLightIndex(cmp.index)].m_fov;
			}


			virtual void setLightFOV(Component cmp, float fov) override
			{
				m_point_lights[getPointLightIndex(cmp.index)].m_fov = fov;
			}
			

			Component createPointLight(const Entity& entity)
			{
				PointLight& light = m_point_lights.pushEmpty();
				m_light_influenced_geometry.push(Array<Renderable*>(m_allocator));
				light.m_entity = entity;
				light.m_color.set(1, 1, 1, 1);
				light.m_intensity = 1;
				light.m_range = 10;
				light.m_uid = ++m_point_light_last_uid;
				light.m_fov = 999;

				Component cmp = m_universe.addComponent(entity, POINT_LIGHT_HASH, this, light.m_uid);
				m_universe.componentCreated().invoke(cmp);

				detectLightInfluencedGeometry(m_point_lights.size() - 1);

				return cmp;
			}

		
			Component createRenderable(const Entity& entity)
			{
				int new_index = m_renderables.empty() ? 0 : m_renderables.back()->m_component_index + 1;
				Renderable& r = *m_allocator.newObject<Renderable>(m_allocator);
				m_renderables.push(&r);
				r.m_entity = entity;
				r.m_scale = 1;
				r.m_model = NULL;
				r.m_component_index = new_index;
				r.m_is_always_visible = false;
				r.m_matrix = entity.getMatrix();
				Component cmp = m_universe.addComponent(entity, RENDERABLE_HASH, this, r.m_component_index);
				m_culling_system->addStatic(Sphere(entity.getPosition(), 1.0f));
				m_universe.componentCreated().invoke(cmp);
				return cmp;
			}

		private:
			IAllocator& m_allocator;
			Array<ModelLoadedCallback*> m_model_loaded_callbacks;
			
			Array<Renderable*> m_renderables;
			Array<int> m_always_visible;
			
			int m_point_light_last_uid;
			Array<PointLight> m_point_lights;
			Array<Array<Renderable*> > m_light_influenced_geometry;
			int m_active_global_light_uid;
			int m_global_light_last_uid;
			Array<GlobalLight> m_global_lights;

			Array<Camera> m_cameras;
			Component m_applied_camera;
			Frustum m_camera_frustum;

			Array<Terrain*> m_terrains;
			Universe& m_universe;
			Renderer& m_renderer;
			Engine& m_engine;
			Array<DebugLine> m_debug_lines;
			DebugTextsData m_debug_texts;
			CullingSystem* m_culling_system;
			DynamicRenderableCache m_dynamic_renderable_cache;
			Array<Array<const RenderableMesh*> > m_temporary_infos;
			MTJD::Group m_sync_point;
			Array<MTJD::Job*> m_jobs;
			float m_time;
			bool m_is_forward_rendered;
	};


	RenderScene* RenderScene::createInstance(Renderer& renderer, Engine& engine, Universe& universe, bool is_forward_rendered, IAllocator& allocator)
	{
		return allocator.newObject<RenderSceneImpl>(renderer, engine, universe, is_forward_rendered, allocator);
	}


	void RenderScene::destroyInstance(RenderScene* scene)
	{
		scene->getAllocator().deleteObject(static_cast<RenderSceneImpl*>(scene));
	}

}