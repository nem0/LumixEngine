#include "render_scene.h"

#include "core/array.h"
#include "core/crc32.h"
#include "core/FS/file_system.h"
#include "core/FS/ifile.h"
#include "core/iserializer.h"
#include "core/log.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/timer.h"
#include "core/sphere.h"
#include "core/frustum.h"

#include "engine/engine.h"

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
	static const uint32_t LIGHT_HASH = crc32("light");
	static const uint32_t CAMERA_HASH = crc32("camera");
	static const uint32_t TERRAIN_HASH = crc32("terrain");


	struct Renderable
	{
		Renderable() {}

		bool m_is_free;
		int64_t m_layer_mask;
		ModelInstance m_model;
		Entity m_entity;
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
		bool m_is_free;
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
		public:
			RenderSceneImpl(Renderer& renderer, Engine& engine, Universe& universe)
				: m_engine(engine)
				, m_universe(universe)
				, m_renderer(renderer)
			{
				m_universe.entityMoved().bind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
				m_timer = Timer::create();
				m_culling_system = CullingSystem::create(m_engine.getMTJDManager());
			}

			~RenderSceneImpl()
			{
				m_universe.entityMoved().unbind<RenderSceneImpl, &RenderSceneImpl::onEntityMoved>(this);
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					LUMIX_DELETE(m_renderables[i]);
				}

				for (int i = 0; i < m_terrains.size(); ++i)
				{
					LUMIX_DELETE(m_terrains[i]);
				}

				Timer::destroy(m_timer);
				CullingSystem::destroy(*m_culling_system);
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
				//m_debug_lines.clear();

				m_applied_camera = cmp;
				Matrix mtx;
				cmp.entity.getMatrix(mtx);
				float fov = m_cameras[cmp.index].m_fov;
				float width = m_cameras[cmp.index].m_width;
				float height = m_cameras[cmp.index].m_height;
				float near_plane = m_cameras[cmp.index].m_near;
				float far_plane = m_cameras[cmp.index].m_far;
				m_renderer.setProjection(width, height, fov, near_plane, far_plane, mtx);

				m_camera_frustum.compute(
					mtx.getTranslation(),
					mtx.getZVector(),
					mtx.getYVector(),
					fov,
					width / height,
					near_plane,
					far_plane
					);

				//Vec3 corners[8];
				//Plane* planes = &m_camera_frustum.m_plane[0];

				//planes[(uint32_t)Frustum::Sides::Far].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Top], planes[(uint32_t)Frustum::Sides::Left], corners[0]);
				//planes[(uint32_t)Frustum::Sides::Far].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Top], planes[(uint32_t)Frustum::Sides::Right], corners[1]);
				//planes[(uint32_t)Frustum::Sides::Far].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Bottom], planes[(uint32_t)Frustum::Sides::Left], corners[2]);
				//planes[(uint32_t)Frustum::Sides::Far].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Bottom], planes[(uint32_t)Frustum::Sides::Right], corners[3]);
				//planes[(uint32_t)Frustum::Sides::Near].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Top], planes[(uint32_t)Frustum::Sides::Left], corners[4]);
				//planes[(uint32_t)Frustum::Sides::Near].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Top], planes[(uint32_t)Frustum::Sides::Right], corners[5]);
				//planes[(uint32_t)Frustum::Sides::Near].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Bottom], planes[(uint32_t)Frustum::Sides::Left], corners[6]);
				//planes[(uint32_t)Frustum::Sides::Near].getIntersectionWithPlanes(planes[(uint32_t)Frustum::Sides::Bottom], planes[(uint32_t)Frustum::Sides::Right], corners[7]);

				//Vec3 blue(0.f, 0.f, 1.f);

				//addDebugLine(corners[0], corners[1], blue, 0.001f);
				//addDebugLine(corners[0], corners[2], blue, 0.001f);
				//addDebugLine(corners[2], corners[3], blue, 0.001f);
				//addDebugLine(corners[3], corners[1], blue, 0.001f);

				//addDebugLine(corners[4], corners[5], blue, 0.001f);
				//addDebugLine(corners[4], corners[6], blue, 0.001f);
				//addDebugLine(corners[6], corners[7], blue, 0.001f);
				//addDebugLine(corners[7], corners[5], blue, 0.001f);

				//addDebugLine(corners[0], corners[4], blue, 0.001f);
				//addDebugLine(corners[1], corners[5], blue, 0.001f);
				//addDebugLine(corners[2], corners[6], blue, 0.001f);
				//addDebugLine(corners[3], corners[7], blue, 0.001f);

				//Lux::Array<Lux::Sphere> spheres;
				//for (float i = 0.f; i < 150.f; i += 15.f)
				//{
				// spheres.push(Lux::Sphere(i, 0.f, 50.f, 5.f));
				//}

				//for (int i = 0; i < spheres.size(); i++)
				//{
				// Vec3 pos = (Vec3)spheres[i].m_sphere;
				// Vec3 to = pos;
				// to.x += spheres[i].m_sphere.w;

				// Vec3 red(1.f, 0.f, 0.f);
				// Vec3 mag(1.f, 0.f, 1.f);

				// Vec3 color = cameraFrustum.sphereInFrustum(pos, spheres[i].m_sphere.w) ? mag : red;

				// addDebugLine(pos, Vec3(pos.x + 5, pos.y, pos.z), color, 0.0001f);
				// addDebugLine(pos, Vec3(pos.x - 5, pos.y, pos.z), color, 0.0001f);
				// addDebugLine(pos, Vec3(pos.x, pos.y + 5, pos.z), color, 0.0001f);
				// addDebugLine(pos, Vec3(pos.x, pos.y - 5, pos.z), color, 0.0001f);
				//}
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
					serializer.serializeArrayItem(m_cameras[i].m_is_free);
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
					serializer.serializeArrayItem(m_lights[i].m_is_free);
				}
				serializer.endArray();
			}

			void serializeRenderables(ISerializer& serializer)
			{
				serializer.serialize("renderable_count", m_renderables.size());
				serializer.beginArray("renderables");
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					serializer.serializeArrayItem(m_renderables[i]->m_is_free);
					if(!m_renderables[i]->m_is_free)
					{
						serializer.serializeArrayItem(m_renderables[i]->m_entity.index);
						serializer.serializeArrayItem(m_renderables[i]->m_layer_mask);
						if (m_renderables[i]->m_model.getModel())
						{
							serializer.serializeArrayItem(m_renderables[i]->m_model.getModel()->getPath().c_str());
						}
						else
						{
							serializer.serializeArrayItem("");
						}
						serializer.serializeArrayItem(m_renderables[i]->m_scale);
						Matrix mtx = m_renderables[i]->m_model.getMatrix();
						for (int j = 0; j < 16; ++j)
						{
							serializer.serializeArrayItem((&mtx.m11)[j]);
						}
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
					if(m_terrains[i])
					{
						serializer.serializeArrayItem(true);
						m_terrains[i]->serialize(serializer);
					}
					else
					{
						serializer.serializeArrayItem(false);
					}
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
					serializer.deserializeArrayItem(m_cameras[i].m_is_free);
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
					if(!m_cameras[i].m_is_free)
					{
						m_universe.addComponent(m_cameras[i].m_entity, CAMERA_HASH, this, i);
					}
				}
				serializer.deserializeArrayEnd();
			}

			void deserializeRenderables(ISerializer& serializer)
			{
				int32_t size = 0;
				serializer.deserialize("renderable_count", size);
				serializer.deserializeArrayBegin("renderables");
				for (int i = size; i < m_renderables.size(); ++i)
				{
					LUMIX_DELETE(m_renderables[i]);
				}
				int old_size = m_renderables.size();
				m_renderables.resize(size);
				for (int i = old_size; i < size; ++i)
				{
					m_renderables[i] = LUMIX_NEW(Renderable);
				}
				for (int i = 0; i < size; ++i)
				{
					serializer.deserializeArrayItem(m_renderables[i]->m_is_free);
					if(!m_renderables[i]->m_is_free)
					{
						serializer.deserializeArrayItem(m_renderables[i]->m_entity.index);
						m_renderables[i]->m_entity.universe = &m_universe;
						serializer.deserializeArrayItem(m_renderables[i]->m_layer_mask);
						char path[LUMIX_MAX_PATH];
						serializer.deserializeArrayItem(path, LUMIX_MAX_PATH);
						serializer.deserializeArrayItem(m_renderables[i]->m_scale);
						m_renderables[i]->m_model.setModel(static_cast<Model*>(m_engine.getResourceManager().get(ResourceManager::MODEL)->load(path)));
						for (int j = 0; j < 16; ++j)
						{
							serializer.deserializeArrayItem((&m_renderables[i]->m_model.getMatrix().m11)[j]);
						}
						m_universe.addComponent(m_renderables[i]->m_entity, RENDERABLE_HASH, this, i);
					}
				}
				serializer.deserializeArrayEnd();
			}

			void deserializeLights(ISerializer& serializer)
			{
				int32_t size = 0;
				serializer.deserialize("light_count", size);
				serializer.deserializeArrayBegin("lights");
				m_lights.resize(size);
				for (int i = 0; i < size; ++i)
				{
					serializer.deserializeArrayItem(m_lights[i].m_entity.index);
					m_lights[i].m_entity.universe = &m_universe;
					serializer.deserializeArrayItem((int32_t&)m_lights[i].m_type);
					serializer.deserializeArrayItem(m_lights[i].m_is_free);
					if(!m_lights[i].m_is_free)
					{
						m_universe.addComponent(m_lights[i].m_entity, LIGHT_HASH, this, i);
					}
				}
				serializer.deserializeArrayEnd();
			}

			void deserializeTerrains(ISerializer& serializer)
			{
				int32_t size = 0;
				serializer.deserialize("terrain_count", size);
				serializer.deserializeArrayBegin("terrains");
				for (int i = size; i < m_terrains.size(); ++i)
				{
					LUMIX_DELETE(m_terrains[i]);
					m_terrains[i] = NULL;
				}
				m_terrains.resize(size);
				for (int i = 0; i < size; ++i)
				{
					bool exists;
					serializer.deserializeArrayItem(exists);
					if(exists)
					{
						m_terrains[i] = LUMIX_NEW(Terrain)(Entity::INVALID, *this);
						Terrain* terrain = m_terrains[i];
						terrain->deserialize(serializer, m_universe, *this, i);
					}
					else
					{
						m_terrains[i] = NULL;
					}
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


			virtual void destroyComponent(const Component& component) override
			{
				if (component.type == RENDERABLE_HASH)
				{
					m_renderables[component.index]->m_model.setModel(NULL);
					m_renderables[component.index]->m_is_free = true;
					m_universe.destroyComponent(component);
					m_universe.componentDestroyed().invoke(component);
				}
				else if (component.type == LIGHT_HASH)
				{
					m_lights[component.index].m_is_free = true;
					m_universe.destroyComponent(component);
					m_universe.componentDestroyed().invoke(component);
				}
				else if (component.type == CAMERA_HASH)
				{
					m_cameras[component.index].m_is_free = true;
					m_universe.destroyComponent(component);
					m_universe.componentDestroyed().invoke(component);
				}
				else if(component.type == TERRAIN_HASH)
				{
					LUMIX_DELETE(m_terrains[component.index]);
					m_terrains[component.index] = NULL;
					m_universe.destroyComponent(component);
					m_universe.componentDestroyed().invoke(component);
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
					Terrain* terrain = LUMIX_NEW(Terrain)(entity, *this);
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
					Renderable* src = NULL;
					int index = -1;
					for (int i = 0, c = m_renderables.size(); i < c; ++i)
					{
						if (m_renderables[i]->m_is_free)
						{
							src = m_renderables[i];
							index = i;
							break;
						}
					}
					if (!src)
					{
						src = LUMIX_NEW(Renderable);
						m_renderables.push(src);
						index = m_renderables.size() - 1;
					}
					Renderable& r = *src;
					r.m_entity = entity;
					r.m_layer_mask = 1;
					r.m_scale = 1;
					r.m_is_free = false;
					r.m_model.setModel(NULL);
					Component cmp = m_universe.addComponent(entity, type, this, index);
					m_culling_system->addStatic(Sphere(entity.getPosition(), 1.0f), m_renderables.size() - 1);
					m_universe.componentCreated().invoke(cmp);
					return cmp;
				}
				else if (type == LIGHT_HASH)
				{
					Light& light = m_lights.pushEmpty();
					light.m_type = Light::Type::DIRECTIONAL;
					light.m_entity = entity;
					light.m_is_free = false;
					Component cmp = m_universe.addComponent(entity, type, this, m_lights.size() - 1);
					m_universe.componentCreated().invoke(cmp);
					return cmp;
				}
				return Component::INVALID;
			}

			void onEntityMoved(const Entity& entity)
			{
				const Entity::ComponentList& cmps = entity.getComponents();
				for (int i = 0; i < cmps.size(); ++i)
				{
					if (cmps[i].type == RENDERABLE_HASH)
					{
						m_renderables[cmps[i].index]->m_model.setMatrix(entity.getMatrix());
						m_culling_system->updateBoundingPosition(entity.getMatrix().getTranslation(), cmps[i].index);
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
				addDebugCross(position, 1, Vec3(1, 0, 0), 0);
			}


			virtual float getTerrainHeightAt(Component cmp, float x, float z) override
			{
				return m_terrains[cmp.index]->getHeight(x, z);
			}


			virtual void setTerrainMaterial(Component cmp, const string& path) override
			{
				Material* material = static_cast<Material*>(m_engine.getResourceManager().get(ResourceManager::MATERIAL)->load(path.c_str()));
				m_terrains[cmp.index]->setMaterial(material);
			}


			virtual Material* getTerrainMaterial(Component cmp) override
			{
				return m_terrains[cmp.index]->getMaterial();
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
				return m_renderables[cmp.index]->m_model.getPose();
			}

			virtual Model* getModel(Component cmp) override
			{
				return m_renderables[cmp.index]->m_model.getModel();
			}


			virtual void getRenderablePath(Component cmp, string& path) override
			{
					if (m_renderables[cmp.index]->m_model.getModel())
					{
						path = m_renderables[cmp.index]->m_model.getModel()->getPath().c_str();
					}
					else
					{
						path = "";
					}
			}


			virtual void setRenderableLayer(Component cmp, const int32_t& layer) override
			{
				m_renderables[cmp.index]->m_layer_mask = ((int64_t)1 << (int64_t)layer);
			}

			virtual void setRenderableScale(Component cmp, float scale) override
			{
				m_renderables[cmp.index]->m_scale = scale;
			}


			virtual void setRenderablePath(Component cmp, const string& path) override
			{
				Renderable& r = *m_renderables[cmp.index];

				Model* old_model = r.m_model.getModel();
				if (old_model)
				{
					old_model->getObserverCb().unbind<RenderSceneImpl, &RenderSceneImpl::modelUpdate>(this);
				}

				Model* model = static_cast<Model*>(m_engine.getResourceManager().get(ResourceManager::MODEL)->load(path));
				
				if (model)
				{
					model->getObserverCb().bind<RenderSceneImpl, &RenderSceneImpl::modelUpdate>(this);
				}

				r.m_model.setModel(model);
				r.m_model.setMatrix(r.m_entity.getMatrix());
			}


			virtual void getTerrainInfos(Array<TerrainInfo>& infos, int64_t layer_mask) override
			{
				PROFILE_FUNCTION();
				infos.reserve(m_terrains.size());
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					if (m_terrains[i] && (m_terrains[i]->getLayerMask() & layer_mask) != 0)
					{
						TerrainInfo& info = infos.pushEmpty();
						info.m_entity = m_terrains[i]->getEntity();
						info.m_material = m_terrains[i]->getMaterial();
						info.m_index = i;
						info.m_xz_scale = m_terrains[i]->getXZScale();
						info.m_y_scale = m_terrains[i]->getYScale();
					}
				}
			}


			virtual void getGrassInfos(Array<GrassInfo>& infos, int64_t layer_mask)
			{
				PROFILE_FUNCTION();
				for (int i = 0; i < m_terrains.size(); ++i)
				{
					if (m_terrains[i] && (m_terrains[i]->getLayerMask() & layer_mask) != 0)
					{
						m_terrains[i]->getGrassInfos(infos, m_applied_camera.entity.getPosition());
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
				m_terrains[cmp.index]->setGrassTypePath(index, path.c_str());
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


			virtual void getRenderableInfos(Array<RenderableInfo>& infos, int64_t layer_mask) override
			{
				PROFILE_FUNCTION();

				if (m_renderables.empty())
					return;

				m_culling_system->cullToFrustumAsync(m_camera_frustum);

				infos.reserve(m_renderables.size() * 2);
				for (int i = 0, c = m_renderables.size(); i < c; ++i)

				//const CullingSystem::InputSpheres& spheres = m_culling_system->getSpheres();

				////m_debug_lines.clear();
				//Vec3 mag(1.f, 0.f, 1.f);
				//for (int i = 0; i < spheres.size(); i++)
				//{
				//	Vec3 center = spheres[i].m_position;
				//	float radius = spheres[i].m_radius;

				//	//addDebugCircle(center, radius, mag, 0.001f);
				//	addDebugCross(center, radius, mag, 0.001f);
				//}

				const CullingSystem::Results& results = m_culling_system->getResultAsync();

				infos.reserve(m_renderables.size() * 2);
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					const Renderable* LUMIX_RESTRICT renderable = m_renderables[i];
					if (!renderable->m_is_free)
					{
						const ModelInstance& model_instance = renderable->m_model;
						const Model* model = model_instance.getModel();
						bool is_model_ready = model && model->isReady();
						if (is_model_ready && (renderable->m_layer_mask & layer_mask) != 0 && results[i] > -1)
						{
							for (int j = 0, c = renderable->m_model.getModel()->getMeshCount(); j < c; ++j)
							{
								RenderableInfo& info = infos.pushEmpty();
								info.m_scale = renderable->m_scale;
								info.m_geometry = model->getGeometry();
								info.m_mesh = &model->getMesh(j);
								info.m_pose = &model_instance.getPose();
								info.m_model = &model_instance;
								info.m_matrix = &model_instance.getMatrix();
							}
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

			virtual void addDebugCube(const Vec3& from, float size, const Vec3& color, float life) override
			{
				Vec3 a = from;
				Vec3 b = from;
				b.x += size;
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, b.z + size);
				addDebugLine(a, b, color, life);
				b.set(a.x - size, a.y, a.z);
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, b.z - size);
				addDebugLine(a, b, color, life);

				a = from;
				a.y += size;
				b = a;
				b.x += size;
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, b.z + size);
				addDebugLine(a, b, color, life);
				b.set(a.x - size, a.y, a.z);
				addDebugLine(a, b, color, life);
				a.set(b.x, b.y, b.z - size);
				addDebugLine(a, b, color, life);
			}

			virtual void addDebugCircle(const Vec3& center, float radius, const Vec3& color, float life) override
			{
				float prevx = radius;
				float prevz = 0;
				for (int i = 1; i < 64; ++i)
				{
					float a = i / 64.0f * 2 * Math::PI;
					float x = cosf(a) * radius;
					float z = sinf(a) * radius;
					addDebugLine(center + Vec3(x, 0, z), center + Vec3(prevx, 0, prevz), color, life);
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
				line.m_color = color;
				line.m_life = life;
			}

			virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, const Component& ignore) override
			{
				RayCastModelHit hit;
				hit.m_is_hit = false;
				Renderable* ignore_renderable = ignore.type == RENDERABLE_HASH ? m_renderables[ignore.index] : NULL;
				Terrain* ignore_terrain = ignore.type == TERRAIN_HASH ? m_terrains[ignore.index] : NULL;
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					if (ignore_renderable != m_renderables[i] && m_renderables[i]->m_model.getModel())
					{
						const Vec3& pos = m_renderables[i]->m_model.getMatrix().getTranslation();
						float radius = m_renderables[i]->m_model.getModel()->getBoundingRadius();
						float scale = m_renderables[i]->m_scale;
						Vec3 intersection;
						if (dotProduct(pos - origin, pos - origin) < radius * radius || Math::getRaySphereIntersection(origin, dir, pos, radius * scale, intersection))
						{
							RayCastModelHit new_hit = m_renderables[i]->m_model.getModel()->castRay(origin, dir, m_renderables[i]->m_model.getMatrix(), scale);
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

			virtual Component getLight(int index) override
			{
				if (index >= m_lights.size() || m_lights[index].m_is_free)
				{
					return Component::INVALID;
				}
				return Component(m_lights[index].m_entity, crc32("light"), this, index);
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

			virtual Timer* getTimer() const override
			{
				return m_timer;
			}

			virtual void renderTerrain(const TerrainInfo& info, Renderer& renderer, PipelineInstance& pipeline, const Vec3& camera_pos) override
			{
				int i = info.m_index;
				if (m_terrains[i]->getMaterial() && m_terrains[i]->getMaterial()->isReady())
				{
					m_terrains[i]->render(renderer, pipeline, camera_pos);
				}
			}

		private:
			// I know, but it's just a proof of concept
			void modelUpdate(Resource::State, Resource::State)
			{
				for (int i = 0; i < m_renderables.size(); ++i)
				{
					if (m_renderables[i]->m_model.getModel())
					{
						float bounding_radius = m_renderables[i]->m_model.getModel()->getBoundingRadius();
						m_culling_system->updateBoundingRadius(bounding_radius, i);
					}
				}
			}

			Array<Renderable*> m_renderables;
			Array<Light> m_lights;
			Array<Camera> m_cameras;
			Array<Terrain*> m_terrains;
			Universe& m_universe;
			Renderer& m_renderer;
			Engine& m_engine;
			Array<DebugLine> m_debug_lines;
			Timer* m_timer;
			Component m_applied_camera;
			CullingSystem* m_culling_system;
			Frustum m_camera_frustum;
	};


	RenderScene* RenderScene::createInstance(Renderer& renderer, Engine& engine, Universe& universe)
	{
		return LUMIX_NEW(RenderSceneImpl)(renderer, engine, universe);
	}


	void RenderScene::destroyInstance(RenderScene* scene)
	{
		LUMIX_DELETE(scene);
	}

}