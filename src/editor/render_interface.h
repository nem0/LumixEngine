#pragma once


#include "engine/lumix.h"
#include "engine/geometry.h"
#include "engine/matrix.h"
#include "engine/path.h"
#include "engine/vec.h"
#include "editor/world_editor.h"
#include "imgui/imgui.h"


namespace Lumix
{


struct Pose;


class RenderInterface
{
public:
	typedef int ModelHandle;

	struct Vertex
	{
		Vec3 position;
		u32 color;
		float u, v;
	};

public:
	virtual ~RenderInterface() {}

	virtual AABB getEntityAABB(Universe& universe, Entity entity) = 0;
	virtual float getCameraFOV(ComponentHandle cmp) = 0;
	virtual bool isCameraOrtho(ComponentHandle cmp) = 0;
	virtual float getCameraOrthoSize(ComponentHandle cmp) = 0;
	virtual Vec2 getCameraScreenSize(ComponentHandle cmp) = 0;
	virtual ComponentHandle getCameraInSlot(const char* slot) = 0;
	virtual void setCameraSlot(ComponentHandle cmp, const char* slot) = 0;
	virtual Entity getCameraEntity(ComponentHandle cmp) = 0;
	virtual void getRay(ComponentHandle camera_index, float x, float y, Vec3& origin, Vec3& dir) = 0;
	virtual float castRay(ModelHandle model, const Vec3& origin, const Vec3& dir, const Matrix& mtx, const Pose* pose) = 0;
	virtual void renderModel(ModelHandle model, const Matrix& mtx) = 0;
	virtual ModelHandle loadModel(Path& path) = 0;
	virtual void unloadModel(ModelHandle handle) = 0;
	virtual Vec3 getModelCenter(Entity entity) = 0;
	virtual ImTextureID loadTexture(const Path& path) = 0;
	virtual void unloadTexture(ImTextureID handle) = 0;
	virtual void addDebugCube(const Vec3& minimum, const Vec3& maximum, u32 color, float life) = 0;
	virtual void addDebugCross(const Vec3& pos, float size, u32 color, float life) = 0;
	virtual void addDebugLine(const Vec3& from, const Vec3& to, u32 color, float life) = 0;
	virtual WorldEditor::RayHit castRay(const Vec3& origin, const Vec3& dir, ComponentHandle ignored) = 0;
	virtual Path getModelInstancePath(ComponentHandle cmp) = 0;
	virtual void render(const Matrix& mtx,
		u16* indices,
		int indices_count,
		Vertex* vertices,
		int vertices_count,
		bool lines) = 0;
	virtual void showEntity(Entity entity) = 0;
	virtual void hideEntity(Entity entity) = 0;
};


}