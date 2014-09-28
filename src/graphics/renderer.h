#pragma once

#include "core/lumix.h"
#include "core/array.h"
#include "core/string.h"
#include "engine/iplugin.h"
#include "graphics/ray_cast_model_hit.h"
#include "graphics/render_scene.h"


namespace Lumix
{


class Engine;
class IRenderDevice;
class Material;
class Model;
class ModelInstance;
class Pipeline;
class PipelineInstance;
class Pose;
class ResourceManager;
class Shader;
class Texture;
class Universe;



class LUMIX_ENGINE_API Renderer : public IPlugin 
{
	public:
		static Renderer* createInstance();
		static void destroyInstance(Renderer& renderer);

		static void getOrthoMatrix(float left, float right, float bottom, float top, float z_near, float z_far, Matrix* mtx);
		static void getLookAtMatrix(const Vec3& pos, const Vec3& center, const Vec3& up, Matrix* mtx);
		static void getProjectionMatrix(float fov, float width, float height, float near_plane, float far_plane, Matrix* mtx);

		virtual const Matrix& getCurrentViewMatrix() = 0;
		virtual const Matrix& getCurrentProjectionMatrix() = 0;

		virtual void render(IRenderDevice& device) = 0;
		virtual void renderGame() = 0;
		virtual void enableAlphaToCoverage(bool enable) = 0;
		virtual void enableZTest(bool enable) = 0;
		virtual void setRenderDevice(IRenderDevice& device) = 0;
		virtual void setEditorWireframe(bool is_wireframe) = 0;
		virtual bool isEditorWireframe() const = 0;
		virtual void cleanup() = 0;

		virtual void renderGeometry(Geometry& geometry, int start, int count, Shader& shader) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, int value) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Vec3& value) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, float value) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix& mtx) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix* matrices, int count) = 0;
		virtual void setFixedCachedUniform(const Shader& shader, int name, const Vec3& value) = 0;
		virtual void setFixedCachedUniform(const Shader& shader, int name, const Vec4& value) = 0;
		virtual void setFixedCachedUniform(const Shader& shader, int name, float value) = 0;
		virtual void setFixedCachedUniform(const Shader& shader, int name, const Matrix& mtx) = 0;
		virtual void setFixedCachedUniform(const Shader& shader, int name, const Matrix* matrices, int count) = 0;
		virtual void applyShader(const Shader& shader) = 0;
		virtual Shader& getDebugShader() = 0;

		virtual void setProjection(float width, float height, float fov, float near_plane, float far_plane, const Matrix& mtx) = 0;
		virtual void setViewMatrix(const Matrix& matrix) = 0;
		virtual void setProjectionMatrix(const Matrix& matrix) = 0;
		virtual Engine& getEngine() = 0;
		
		/// "immediate mode"
		virtual void renderModel(const Model& model, const Matrix& transform, PipelineInstance& pipeline) = 0;
}; 


} // !namespace Lumix

