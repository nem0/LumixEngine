#pragma once

#include "core/lux.h"
#include "Horde3D.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "universe/universe.h"


namespace Lux
{


class Event;
class IFileSystem;
class ISerializer;
struct Vec3;


class LUX_ENGINE_API Renderer
{
	public:
		Renderer();
		
		bool create(IFileSystem* fs, int w, int h, const char* base_path);
		void destroy();

		void renderScene();
		void endFrame();
		void enableStage(const char* name, bool enable);
		int getWidth() const;
		int getHeight() const;
		void getRay(int x, int y, Vec3& origin, Vec3& dir);
		Component createRenderable(Entity entity);
		void destroyRenderable(Component cmp);
		Component createPointLight(Entity entity);
		void destroyPointLight(Component cmp);
		float getHalfFovTan();
		void setUniverse(Universe* universe);
		Component getRenderable(Universe& universe, H3DNode node);

		void getVisible(Component cmp, bool& visible);
		void setVisible(Component cmp, const bool& visible);
		void getMesh(Component cmp, string& str);
		void setMesh(Component cmp, const string& str);
		void getCastShadows(Component cmp, bool& cast_shadows);
		void setCastShadows(Component cmp, const bool& cast_shadows);

		H3DNode getMeshNode(Component cmp);
		void getLightFov(Component cmp, float& fov);
		void setLightFov(Component cmp, const float& fov);
		void getLightRadius(Component cmp, float& r);
		void setLightRadius(Component cmp, const float& r);
		H3DNode getRawCameraNode();
		void onResize(int w, int h);
		void getCameraMatrix(Matrix& mtx);
		void setCameraMatrix(const Matrix& mtx);
		const char* getBasePath() const;

		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);
	
	private:
		struct RendererImpl* m_impl;
};


} // !namespace Lux

