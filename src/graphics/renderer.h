#pragma once

#include "core/lux.h"
#include "core/string.h"
#include "engine/iplugin.h"


namespace Lux
{


class LUX_ENGINE_API Renderer : public IPlugin 
{
	public:
		static Renderer* createInstance();
		static void destroyInstance(Renderer& renderer);

		virtual void render(class IRenderDevice& device) = 0;
		virtual void setCameraActive(Component cmp, const bool& active) = 0;
		virtual void getCameraActive(Component cmp, bool& active) = 0;
		virtual void setCameraPipeline(Component cmp, const string& pipeline) = 0;

/*		virtual void renderScene();
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
		bool getBonePosition(Component cmp, const char* bone_name, Vec3* out);

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
		bool isReady() const;

		void serialize(ISerializer& serializer);
		void deserialize(ISerializer& serializer);*/
};


} // !namespace Lux

