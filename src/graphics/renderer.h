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
class Geometry;
class IRenderDevice;
class Material;
class Model;
class Pipeline;
class PipelineInstance;
class Pose;
class ResourceManager;
class Shader;
class Texture;
class Universe;
struct VertexDef;



class LUMIX_ENGINE_API Renderer : public IPlugin 
{
	public:
		static Renderer* createInstance(Engine& engine);
		static void destroyInstance(Renderer& renderer);

		static void getOrthoMatrix(float left, float right, float bottom, float top, float z_near, float z_far, Matrix* mtx);
		static void getLookAtMatrix(const Vec3& pos, const Vec3& center, const Vec3& up, Matrix* mtx);
		static void getProjectionMatrix(float fov, float width, float height, float near_plane, float far_plane, Matrix* mtx);

		virtual const Matrix& getCurrentViewMatrix() = 0;
		virtual const Matrix& getCurrentProjectionMatrix() = 0;

		virtual void render(IRenderDevice& device) = 0;
		virtual void renderGame() = 0;
		virtual void makeScreenshot(const Path& filename, int width, int height) = 0;
		virtual void enableAlphaToCoverage(bool enable) = 0;
		virtual void enableZTest(bool enable) = 0;
		virtual void setRenderDevice(IRenderDevice& device) = 0;
		virtual void setEditorWireframe(bool is_wireframe) = 0;
		virtual bool isEditorWireframe() const = 0;
		virtual void cleanup() = 0;

		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, int value) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Vec3& value) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, float value) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix& mtx) = 0;
		virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix* matrices, int count) = 0;
		virtual void setPass(uint32_t pass_hash) = 0;
		virtual uint32_t getPass() = 0;
		virtual void applyShader(Shader& shader, uint32_t combination) = 0;
		virtual Shader& getDebugShader() = 0;
		virtual int getAttributeNameIndex(const char* name) = 0;

		virtual void setProjection(float width, float height, float fov, float near_plane, float far_plane, const Matrix& mtx) = 0;
		virtual void setViewMatrix(const Matrix& matrix) = 0;
		virtual void setProjectionMatrix(const Matrix& matrix) = 0;
		virtual Engine& getEngine() = 0;
		
		virtual int getGLSLVersion() const = 0;

		/// "immediate mode"
		virtual void renderModel(const Model& model, const Matrix& transform, PipelineInstance& pipeline) = 0;
}; 


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Vec3& value);
void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Vec4& value);
void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, float value);
void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Matrix& mtx);
void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Matrix* matrices, int count);
void renderInstancedGeometry(int indices_offset, int vertex_count, int instance_count, const Shader& shader);
void renderGeometry(int indices_start, int vertex_count);
void renderQuadGeometry(int start, int count);
void bindGeometry(Renderer& renderer, const Geometry& geometry, const Mesh& mesh);
int getUniformLocation(const Shader& shader, int name);
void setUniform(int location, const Matrix& mtx);
void setUniform(int location, const Matrix* matrices, int count);

} // !namespace Lumix

