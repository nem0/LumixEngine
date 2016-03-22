#pragma once

#include "lumix.h"
#include "iplugin.h"
#include "core/vec.h"
#include "core/array.h"
#include "core/matrix.h"


namespace Lumix
{

class Renderer;
class IAllocator;
class RayCastModelHit;
class Frustum;
class Pose;
class Model;


struct Renderable
{
	Pose* pose;//pointer because of permanent valid position in memory
	Model* model;
	Matrix matrix;
	Entity entity;
	int64 layer_mask;
};


struct DebugTriangle
{
	Vec3 p0;
	Vec3 p1;
	Vec3 p2;
	uint32 color;
	float life;
};


struct DebugLine
{
	Vec3 from;
	Vec3 to;
	uint32 color;
	float life;
};


struct DebugPoint
{
	Vec3 pos;
	uint32 color;
	float life;
};


class LUMIX_RENDERER_API RenderScene : public IScene
{
public:
	static RenderScene* createInstance(Renderer& renderer,
									   Engine& engine,
									   Universe& universe,
									   IAllocator& allocator);
	static void destroyInstance(RenderScene* scene);

	virtual RayCastModelHit castRay(const Vec3& origin, const Vec3& dir, ComponentIndex ignore) = 0;
	virtual void getRay(ComponentIndex camera, float x, float y, Vec3& origin, Vec3& dir) = 0;

	virtual Frustum getCameraFrustum(ComponentIndex camera) const = 0;
	virtual float getTime() const = 0;
	virtual Engine& getEngine() const = 0;
	virtual IAllocator& getAllocator() = 0;

	virtual ComponentIndex getActiveGlobalLight() = 0;
	virtual void setActiveGlobalLight(ComponentIndex cmp) = 0;

	virtual void addDebugTriangle(const Vec3& p0,
								  const Vec3& p1,
								  const Vec3& p2,
								  uint32 color,
								  float life) = 0;
	virtual void addDebugPoint(const Vec3& pos, uint32 color, float life) = 0;

	virtual void addDebugLine(const Vec3& from, const Vec3& to, uint32 color, float life) = 0;
	virtual void addDebugCross(const Vec3& center, float size, uint32 color, float life) = 0;
	virtual void addDebugCube(const Vec3& pos,
							  const Vec3& dir,
							  const Vec3& up,
							  const Vec3& right,
							  uint32 color,
							  float life) = 0;
	virtual void addDebugCube(const Vec3& from, const Vec3& max, uint32 color, float life) = 0;
	virtual void addDebugCubeSolid(const Vec3& from, const Vec3& max, uint32 color, float life) = 0;
	virtual void addDebugCircle(const Vec3& center,
								const Vec3& up,
								float radius,
								uint32 color,
								float life) = 0;
	virtual void addDebugSphere(const Vec3& center, float radius, uint32 color, float life) = 0;
	virtual void addDebugFrustum(const Vec3& position,
								 const Vec3& direction,
								 const Vec3& up,
								 float fov,
								 float ratio,
								 float near_distance,
								 float far_distance,
								 uint32 color,
								 float life) = 0;

	virtual void addDebugFrustum(const Frustum& frustum, uint32 color, float life) = 0;

	virtual void addDebugCapsule(const Vec3& position,
								 float height,
								 float radius,
								 uint32 color,
								 float life) = 0;

	virtual void addDebugCylinder(const Vec3& position,
								  const Vec3& up,
								  float radius,
								  uint32 color,
								  float life) = 0;

	virtual const Array<DebugTriangle>& getDebugTriangles() const = 0;
	virtual const Array<DebugLine>& getDebugLines() const = 0;
	virtual const Array<DebugPoint>& getDebugPoints() const = 0;

	virtual Entity getCameraEntity(ComponentIndex camera) const = 0;
	virtual ComponentIndex getCameraInSlot(const char* slot) = 0;
	virtual float getCameraFOV(ComponentIndex camera) = 0;
	virtual void setCameraFOV(ComponentIndex camera, float fov) = 0;
	virtual void setCameraFarPlane(ComponentIndex camera, float far) = 0;
	virtual void setCameraNearPlane(ComponentIndex camera, float near) = 0;
	virtual float getCameraFarPlane(ComponentIndex camera) = 0;
	virtual float getCameraNearPlane(ComponentIndex camera) = 0;
	virtual float getCameraWidth(ComponentIndex camera) = 0;
	virtual float getCameraHeight(ComponentIndex camera) = 0;
	virtual void setCameraSlot(ComponentIndex camera, const char* slot) = 0;
	virtual const char* getCameraSlot(ComponentIndex camera) = 0;
	virtual void setCameraSize(ComponentIndex camera, int w, int h) = 0;


	virtual Vec3 getLightAmbientColor(ComponentIndex cmp) = 0;
	virtual void setLightAmbientColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual Vec3 getGlobalLightColor(ComponentIndex cmp) = 0;
	virtual void setGlobalLightColor(ComponentIndex cmp, const Vec3& color) = 0;
	virtual Vec3 getGlobalLightSpecular(ComponentIndex cmp) = 0;
	virtual void setGlobalLightSpecular(ComponentIndex cmp, const Vec3& color) = 0;
	virtual float getLightAmbientIntensity(ComponentIndex cmp) = 0;
	virtual void setLightAmbientIntensity(ComponentIndex cmp, float intensity) = 0;
	virtual float getGlobalLightIntensity(ComponentIndex cmp) = 0;
	virtual void setGlobalLightIntensity(ComponentIndex cmp, float intensity) = 0;
	virtual float getGlobalLightSpecularIntensity(ComponentIndex cmp) = 0;
	virtual void setGlobalLightSpecularIntensity(ComponentIndex cmp, float intensity) = 0;
};


} // !namespace Lumix
