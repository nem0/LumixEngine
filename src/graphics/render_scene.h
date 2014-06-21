#pragma once

#include "core/lumix.h"
#include "core/array.h"
#include "core/string.h"
#include "graphics/ray_cast_model_hit.h"
#include "universe/component.h"

namespace Lumix
{

	class Engine;
	class Geometry;
	class IRenderDevice;
	class ISerializer;
	class Mesh;
	class Model;
	class ModelInstance;
	class Pose;
	class Timer;
	class Universe;

	struct RenderableInfo
	{
		Geometry* m_geometry;
		Mesh* m_mesh;
		const Pose* m_pose;
		const ModelInstance* m_model;
		const Matrix* m_matrix;
		float m_scale;
	};

	struct DebugLine
	{
		Vec3 m_from;
		Vec3 m_to;
		Vec3 m_color;
		float m_life;
	};

	class LUX_ENGINE_API RenderScene
	{
		public:
			static RenderScene* createInstance(Engine& engine, Universe& universe);
			static void destroyInstance(RenderScene* scene);

			virtual void serialize(ISerializer& serializer) = 0;
			virtual void deserialize(ISerializer& serializer) = 0;
			virtual Component createComponent(uint32_t type, const Entity& entity) = 0;
			virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir) = 0;
			virtual void getRay(Component camera, float x, float y, Vec3& origin, Vec3& dir) = 0;
			virtual void applyCamera(Component camera) = 0;
			virtual void update(float dt) = 0;
			virtual Timer* getTimer() const = 0;

			virtual Pose& getPose(const Component& cmp) = 0;
			virtual Component getLight(int index) = 0;

			virtual void addDebugLine(const Vec3& from, const Vec3& to, const Vec3& color, float life) = 0;
			virtual const Array<DebugLine>& getDebugLines() const = 0;
			virtual Component getCameraInSlot(const char* slot) = 0;
			virtual void getCameraFOV(Component camera, float& fov) = 0;
			virtual void setCameraFOV(Component camera, const float& fov) = 0;
			virtual void setCameraFarPlane(Component camera, const float& far) = 0;
			virtual void setCameraNearPlane(Component camera, const float& near) = 0;
			virtual void getCameraFarPlane(Component camera, float& far) = 0;
			virtual void getCameraNearPlane(Component camera, float& near) = 0;
			virtual void getCameraWidth(Component camera, float& width) = 0;
			virtual void getCameraHeight(Component camera, float& height) = 0;
			virtual void setCameraSlot(Component camera, const string& slot) = 0;
			virtual void getCameraSlot(Component camera, string& slot) = 0;
			virtual void setCameraSize(Component camera, int w, int h) = 0;
			virtual Model* getModel(Component cmp) = 0;
			virtual void getRenderablePath(Component cmp, string& path) = 0;
			virtual void setRenderableLayer(Component cmp, const int32_t& layer) = 0;
			virtual void setRenderablePath(Component cmp, const string& path) = 0;
			virtual void setRenderableScale(Component cmp, const float& scale) = 0;
			virtual void getRenderableInfos(Array<RenderableInfo>& infos, int64_t layer_mask) = 0;
			virtual void setTerrainHeightmap(Component cmp, const string& path) = 0;
			virtual void getTerrainHeightmap(Component cmp, string& path) = 0;
			virtual void setTerrainMaterial(Component cmp, const string& path) = 0;
			virtual void getTerrainMaterial(Component cmp, string& path) = 0;

		protected:
			virtual ~RenderScene() {}
	};


}