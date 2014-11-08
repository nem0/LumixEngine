#include "graphics/renderer.h"
#include "core/array.h"
#include "core/crc32.h"
#include "core/fs/file_system.h"
#include "core/json_serializer.h"
#include "core/math_utils.h"
#include "core/profiler.h"
#include "core/resource_manager.h"
#include "core/resource_manager_base.h"
#include "core/vec4.h"
#include "editor/world_editor.h"
#include "engine/engine.h"
#include "graphics/geometry.h"
#include "graphics/gl_ext.h"
#include "graphics/irender_device.h"
#include "graphics/material.h"
#include "graphics/material_manager.h"
#include "graphics/model.h"
#include "graphics/model_instance.h"
#include "graphics/model_manager.h"
#include "graphics/pipeline.h"
#include "graphics/render_scene.h"
#include "graphics/shader.h"
#include "graphics/shader_manager.h"
#include "graphics/texture.h"
#include "graphics/texture_manager.h"
#include "universe/universe.h"


namespace Lumix
{


static const uint32_t LIGHT_HASH = crc32("light");
static const uint32_t RENDERABLE_HASH = crc32("renderable");
static const uint32_t CAMERA_HASH = crc32("camera");


struct RendererImpl : public Renderer
{
	RendererImpl(Engine& engine)
		: m_engine(engine)
		, m_allocator(engine.getAllocator())
		, m_texture_manager(m_allocator)
		, m_model_manager(m_allocator)
		, m_material_manager(m_allocator)
		, m_shader_manager(m_allocator)
		, m_pipeline_manager(m_allocator)
	{
		m_texture_manager.create(ResourceManager::TEXTURE, engine.getResourceManager());
		m_model_manager.create(ResourceManager::MODEL, engine.getResourceManager());
		m_material_manager.create(ResourceManager::MATERIAL, engine.getResourceManager());
		m_shader_manager.create(ResourceManager::SHADER, engine.getResourceManager());
		m_pipeline_manager.create(ResourceManager::PIPELINE, engine.getResourceManager());

		m_current_pass_hash = crc32("MAIN");
		m_last_bind_geometry = NULL;
		m_last_program_id = 0xffffFFFF;
		m_is_editor_wireframe = false;
	}

	~RendererImpl()
	{
		m_texture_manager.destroy();
		m_model_manager.destroy();
		m_material_manager.destroy();
		m_shader_manager.destroy();
		m_pipeline_manager.destroy();
	}

	virtual int getGLSLVersion() const override
	{
		int version = 0;
		const GLubyte* version_str = glGetString(GL_SHADING_LANGUAGE_VERSION);
		if(version_str)
		{
			for(int i = 0; i < 2; ++i)
			{
				while(*version_str >= '0' && *version_str <= '9')
				{
					version *= 10;
					version += *version_str - '0';
					++version_str;
				}
				if(*version_str == '.')
				{
					++version_str;
				}
			}
		}
		return version;
	}

	virtual IScene* createScene(Universe& universe) override
	{
		return RenderScene::createInstance(*this, m_engine, universe, m_allocator);
	}


	virtual void destroyScene(IScene* scene) override
	{
		RenderScene::destroyInstance(static_cast<RenderScene*>(scene));
	}


	virtual void setViewMatrix(const Matrix& matrix) override
	{
		m_view_matrix = matrix;
	}

	virtual void setProjectionMatrix(const Matrix& matrix) override
	{
		m_projection_matrix = matrix;
	}

	virtual void setProjection(float width, float height, float fov, float near_plane, float far_plane, const Matrix& mtx) override
	{
		glViewport(0, 0, (GLsizei)width, (GLsizei)height);
		getProjectionMatrix(fov, width, height, near_plane, far_plane, &m_projection_matrix);

		Vec3 pos = mtx.getTranslation();
		Vec3 center = pos - mtx.getZVector();
		Vec3 up = mtx.getYVector();
		getLookAtMatrix(pos, center, up, &m_view_matrix);
	}

	virtual void setRenderDevice(IRenderDevice& device) override
	{
		m_render_device = &device;
	}

	virtual void renderGame() override
	{
		PROFILE_FUNCTION();
		if (m_render_device)
		{
			m_render_device->beginFrame();
			render(*m_render_device);
			m_render_device->endFrame();
		}
	}

	virtual void render(IRenderDevice& device) override
	{
		PROFILE_FUNCTION();
		glEnable(GL_DEPTH_TEST);
		glDisable(GL_BLEND);

		device.getPipeline().render();

		cleanup();
	}

	virtual const Matrix& getCurrentViewMatrix() override
	{
		return m_view_matrix;
	}

	virtual const Matrix& getCurrentProjectionMatrix() override
	{
		return m_projection_matrix;
	}

	virtual void cleanup() override
	{
		if(m_last_bind_geometry)
		{
			m_last_bind_geometry->getVertexDefinition().end(*m_last_bind_geometry_shader);
		}
		m_last_bind_geometry = NULL;
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
		glUseProgram(0);
		for(int i = 0; i < 16; ++i)
		{
			glActiveTexture(GL_TEXTURE0 + i);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
		glActiveTexture(GL_TEXTURE0); 
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, int value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform1i(loc, value);
		}
	}
	
	
	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Vec3& value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform3f(loc, value.x, value.y, value.z);
		}
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, float value) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniform1f(loc, value);
		}
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix& mtx) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			//glProgramUniformMatrix4fv(shader.getProgramId(), loc, 1, false, &mtx.m11);
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniformMatrix4fv(loc, 1, false, &mtx.m11);
		}
	}


	virtual void setUniform(Shader& shader, const char* name, const uint32_t name_hash, const Matrix* matrices, int count) override
	{
		PROFILE_FUNCTION();
		GLint loc = shader.getUniformLocation(name, name_hash);
		if (loc >= 0)
		{
			if (m_last_program_id != shader.getProgramId())
			{
				glUseProgram(shader.getProgramId());
				m_last_program_id = shader.getProgramId();
			}
			glUniformMatrix4fv(loc, count, false, (float*)matrices);
		}
	}


	virtual uint32_t getPass() override
	{
		return m_current_pass_hash;
	}


	virtual void setPass(uint32_t pass_hash) override
	{
		m_current_pass_hash = pass_hash;
	}


	virtual Shader& getDebugShader() override
	{
		ASSERT(m_debug_shader);
		return *m_debug_shader;
	}


	virtual void applyShader(Shader& shader, uint32_t combination) override
	{
		shader.setCurrentCombination(combination, m_current_pass_hash);
		GLuint id = shader.getProgramId();
		m_last_program_id = id;
		glUseProgram(id);
		setFixedCachedUniform(*this, shader, (int)Shader::FixedCachedUniforms::VIEW_MATRIX, m_view_matrix);
		setFixedCachedUniform(*this, shader, (int)Shader::FixedCachedUniforms::PROJECTION_MATRIX, m_projection_matrix);
	}


	void registerPropertyDescriptors(Engine& engine)
	{
		if(engine.getWorldEditor())
		{
			WorldEditor& editor = *engine.getWorldEditor();
			IAllocator& allocator = engine.getAllocator();
		
			editor.registerProperty("camera", allocator.newObject<StringPropertyDescriptor<RenderScene> >("slot", &RenderScene::getCameraSlot, &RenderScene::setCameraSlot));
			editor.registerProperty("camera", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("fov", &RenderScene::getCameraFOV, &RenderScene::setCameraFOV));
			editor.registerProperty("camera", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("near", &RenderScene::getCameraNearPlane, &RenderScene::setCameraNearPlane));
			editor.registerProperty("camera", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("far", &RenderScene::getCameraFarPlane, &RenderScene::setCameraFarPlane));
		
			editor.registerProperty("renderable", allocator.newObject<ResourcePropertyDescriptor<RenderScene> >("source", &RenderScene::getRenderablePath, &RenderScene::setRenderablePath, "Mesh (*.msh)"));
			editor.registerProperty("renderable", allocator.newObject<BoolPropertyDescriptor<RenderScene> >("is_always_visible", &RenderScene::isRenderableAlwaysVisible, &RenderScene::setRenderableIsAlwaysVisible));
		
			editor.registerProperty("light", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("ambient_intensity", &RenderScene::getLightAmbientIntensity, &RenderScene::setLightAmbientIntensity));
			editor.registerProperty("light", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("diffuse_intensity", &RenderScene::getLightDiffuseIntensity, &RenderScene::setLightDiffuseIntensity));
			editor.registerProperty("light", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("fog_density", &RenderScene::getFogDensity, &RenderScene::setFogDensity));
			editor.registerProperty("light", allocator.newObject<ColorPropertyDescriptor<RenderScene> >("ambient_color", &RenderScene::getLightAmbientColor, &RenderScene::setLightAmbientColor));
			editor.registerProperty("light", allocator.newObject<ColorPropertyDescriptor<RenderScene> >("diffuse_color", &RenderScene::getLightDiffuseColor, &RenderScene::setLightDiffuseColor));
			editor.registerProperty("light", allocator.newObject<ColorPropertyDescriptor<RenderScene> >("fog_color", &RenderScene::getFogColor, &RenderScene::setFogColor));

			editor.registerProperty("terrain", allocator.newObject<ResourcePropertyDescriptor<RenderScene> >("material", &RenderScene::getTerrainMaterial, &RenderScene::setTerrainMaterial, "Material (*.mat)"));
			editor.registerProperty("terrain", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("xz_scale", &RenderScene::getTerrainXZScale, &RenderScene::setTerrainXZScale));
			editor.registerProperty("terrain", allocator.newObject<DecimalPropertyDescriptor<RenderScene> >("y_scale", &RenderScene::getTerrainYScale, &RenderScene::setTerrainYScale));

			auto grass = allocator.newObject<ArrayDescriptor<RenderScene> >("grass", &RenderScene::getGrassCount, &RenderScene::addGrass, &RenderScene::removeGrass);
			grass->addChild(allocator.newObject<ResourceArrayObjectDescriptor<RenderScene> >("mesh", &RenderScene::getGrass, &RenderScene::setGrass, "Mesh (*.msh)"));
			auto ground = allocator.newObject<IntArrayObjectDescriptor<RenderScene> >("ground", &RenderScene::getGrassGround, &RenderScene::setGrassGround);
			ground->setLimit(0, 4);
			grass->addChild(ground);
			grass->addChild(allocator.newObject<IntArrayObjectDescriptor<RenderScene> >("density", &RenderScene::getGrassDensity, &RenderScene::setGrassDensity));
			editor.registerProperty("terrain", grass);
		}
	}


	virtual bool create() override
	{
		m_shader_manager.setRenderer(*this);
		registerPropertyDescriptors(m_engine);

		glewExperimental = GL_TRUE;
		GLenum err = glewInit();
		m_debug_shader = static_cast<Shader*>(m_engine.getResourceManager().get(ResourceManager::SHADER)->load("shaders/debug.shd"));
		return err == GLEW_OK;
	}


	virtual void destroy() override
	{
	}


	virtual const char* getName() const override
	{
		return "renderer";
	}


	virtual void enableAlphaToCoverage(bool enable) override
	{
		if (enable)
		{
			glEnable(GL_MULTISAMPLE);
			glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		}
		else
		{
			glDisable(GL_MULTISAMPLE);
			glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		}
	}


	virtual void enableZTest(bool enable) override
	{
		if (enable)
		{
			glEnable(GL_DEPTH_TEST);
		}
		else
		{
			glDisable(GL_DEPTH_TEST);
		}
	}

	


	virtual Engine& getEngine() override
	{
		return m_engine;
	}


	virtual void renderModel(const Model& model, const Matrix& transform, PipelineInstance& pipeline) override
	{
		for (int i = 0, c = model.getMeshCount(); i < c;  ++i)
		{
			const Mesh& mesh = model.getMesh(i);
			mesh.getMaterial()->apply(*this, pipeline);
			setFixedCachedUniform(*this, *mesh.getMaterial()->getShader(), (int)Shader::FixedCachedUniforms::WORLD_MATRIX, transform);
			renderGeometry(*this, *model.getGeometry(), mesh.getStart(), mesh.getCount(), *mesh.getMaterial()->getShader());
		}
	}

	virtual void serialize(ISerializer&) override
	{
	}

	virtual void deserialize(ISerializer&) override
	{
	}

	virtual void setEditorWireframe(bool is_wireframe)
	{
		m_is_editor_wireframe = is_wireframe;
	}

	virtual bool isEditorWireframe() const
	{
		return m_is_editor_wireframe;
	}

	Engine& m_engine;
	BaseProxyAllocator m_allocator;
	TextureManager m_texture_manager;
	MaterialManager m_material_manager;
	ShaderManager m_shader_manager;
	ModelManager m_model_manager;
	PipelineManager m_pipeline_manager;
	IRenderDevice* m_render_device;
	bool m_is_editor_wireframe;
	Geometry* m_last_bind_geometry;
	Shader* m_last_bind_geometry_shader;
	GLuint m_last_program_id;
	uint32_t m_current_pass_hash;
	Matrix m_view_matrix;
	Matrix m_projection_matrix;
	Shader* m_debug_shader;
};


Renderer* Renderer::createInstance(Engine& engine)
{
	return engine.getAllocator().newObject<RendererImpl>(engine);
}


void Renderer::destroyInstance(Renderer& renderer)
{
	renderer.getEngine().getAllocator().deleteObject(&renderer);
}


void Renderer::getProjectionMatrix(float fov, float width, float height, float near_plane, float far_plane, Matrix* mtx)
{
	*mtx = Matrix::IDENTITY;
	float f = 1 / tanf(Math::degreesToRadians(fov) * 0.5f);
	mtx->m11 = f / (width / height);
	mtx->m22 = f;
	mtx->m33 = (far_plane + near_plane) / (near_plane - far_plane);
	mtx->m44 = 0;
	mtx->m43 = (2 * far_plane * near_plane) / (near_plane - far_plane);
	mtx->m34 = -1;
}


void Renderer::getOrthoMatrix(float left, float right, float bottom, float top, float z_near, float z_far, Matrix* mtx)
{
	*mtx = Matrix::IDENTITY;
	mtx->m11 = 2 / (right - left);
	mtx->m22 = 2 / (top - bottom);
	mtx->m33 = -2 / (z_far - z_near);
	mtx->m41 = -(right + left) / (right - left);
	mtx->m42 = -(top + bottom) / (top - bottom);
	mtx->m43 = -(z_far + z_near) / (z_far - z_near);
	/*		glOrtho(left, right, bottom, top, z_near, z_far);
	glGetFloatv(GL_PROJECTION_MATRIX, &mtx->m11);
	*/
}


void Renderer::getLookAtMatrix(const Vec3& pos, const Vec3& center, const Vec3& up, Matrix* mtx)
{
	*mtx = Matrix::IDENTITY;
	Vec3 f = center - pos;
	f.normalize();
	Vec3 r = crossProduct(f, up);
	r.normalize();
	Vec3 u = crossProduct(r, f);
	mtx->setXVector(r);
	mtx->setYVector(u);
	mtx->setZVector(-f);
	mtx->transpose();
	mtx->setTranslation(Vec3(-dotProduct(r, pos), -dotProduct(u, pos), dotProduct(f, pos)));
	/*glPushMatrix();
	float m[16];
	gluLookAt(pos.x, pos.y, pos.z, center.x, center.y, center.z, up.x, up.y, up.z);
	glGetFloatv(GL_MODELVIEW_MATRIX, m);
	glPopMatrix();*/
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Vec3& value)
{
	PROFILE_FUNCTION();
	RendererImpl& renderer_impl = static_cast<RendererImpl&>(renderer);
	GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
	if (loc >= 0)
	{
		if (renderer_impl.m_last_program_id != shader.getProgramId())
		{
			glUseProgram(shader.getProgramId());
			renderer_impl.m_last_program_id = shader.getProgramId();
		}
		glUniform3f(loc, value.x, value.y, value.z);
	}
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Vec4& value)
{
	PROFILE_FUNCTION();
	RendererImpl& renderer_impl = static_cast<RendererImpl&>(renderer);
	GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
	if (loc >= 0)
	{
		if (renderer_impl.m_last_program_id != shader.getProgramId())
		{
			glUseProgram(shader.getProgramId());
			renderer_impl.m_last_program_id = shader.getProgramId();
		}
		glUniform4f(loc, value.x, value.y, value.z, value.w);
	}
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, float value)
{
	PROFILE_FUNCTION();
	RendererImpl& renderer_impl = static_cast<RendererImpl&>(renderer);
	GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
	if (loc >= 0)
	{
		if (renderer_impl.m_last_program_id != shader.getProgramId())
		{
			glUseProgram(shader.getProgramId());
			renderer_impl.m_last_program_id = shader.getProgramId();
		}
		glUniform1f(loc, value);
	}
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Matrix& mtx)
{
	PROFILE_FUNCTION();
	RendererImpl& renderer_impl = static_cast<RendererImpl&>(renderer);
	GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
	if (loc >= 0)
	{
		if (renderer_impl.m_last_program_id != shader.getProgramId())
		{
			glUseProgram(shader.getProgramId());
			renderer_impl.m_last_program_id = shader.getProgramId();
		}
		glUniformMatrix4fv(loc, 1, false, &mtx.m11);
	}
}


void setFixedCachedUniform(Renderer& renderer, const Shader& shader, int name, const Matrix* matrices, int count)
{
	PROFILE_FUNCTION();
	RendererImpl& renderer_impl = static_cast<RendererImpl&>(renderer);
	GLint loc = shader.getFixedCachedUniformLocation((Shader::FixedCachedUniforms)name);
	if (loc >= 0)
	{
		if (renderer_impl.m_last_program_id != shader.getProgramId())
		{
			glUseProgram(shader.getProgramId());
			renderer_impl.m_last_program_id = shader.getProgramId();
		}
		glUniformMatrix4fv(loc, count, false, (float*)matrices);
	}
}


LUMIX_FORCE_INLINE void renderGeometry(Renderer& renderer, Geometry& geometry, int start, int count, Shader& shader)
{
	PROFILE_FUNCTION();
	RendererImpl& renderer_impl = static_cast<RendererImpl&>(renderer);
	if (renderer_impl.m_last_bind_geometry != &geometry)
	{
		if (renderer_impl.m_last_bind_geometry)
		{
			renderer_impl.m_last_bind_geometry->getVertexDefinition().end(shader);
		}
		glBindBuffer(GL_ARRAY_BUFFER, geometry.getID());
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.getIndicesID());
		renderer_impl.m_last_bind_geometry = &geometry;
		renderer_impl.m_last_bind_geometry_shader = &shader;
		geometry.getVertexDefinition().begin(shader);
	}
	glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, (void*)(start * sizeof(GLint)));
}


} // ~namespace Lumix
